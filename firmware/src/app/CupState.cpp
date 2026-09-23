#include "CupState.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_rom_crc.h>
#include <time.h>

#include <string.h>

#include "SdLog.h"
#include "Settings.h"

namespace cup {

namespace {

constexpr const char *kNs = "cup";
constexpr const char *kNsHist = "cup_hist";
constexpr const char *kKeyToday = "today";
constexpr const char *kKeyDays = "days";
// 版 2 でゲームを遊んだ回数を足した（版が違えば既定値で始める。杯数には影響させない）
constexpr uint16_t kTodayVersion = 2;
constexpr uint16_t kHistVersion = 2;

// NVS に置く「今日の状況」。末尾の crc は手前のバイト列に対して計算する。
// **ゲームの回数は 0〜3 番だけ**（5 つめ以降は下の cup_ext へ）。
// 杯数と同じ塊なので、ここの形は 1 バイトも変えない
struct __attribute__((packed)) TodayBlob {
    uint16_t version;
    uint16_t unknown;
    uint32_t ymd;
    uint8_t hour[24];
    uint16_t refills;
    int16_t last_take_min;
    int16_t last_refill_min;
    uint8_t games[kLegacyGameCount];
    uint16_t reserved;
    uint32_t crc;
};
static_assert(sizeof(TodayBlob) == 48, "today blob layout changed");

// NVS に置く履歴の輪。head は「次に書く位置」、count は有効な件数。
// 1 日ぶんは 16 バイトのきりの良い大きさにし、あとから項目を足せるよう予備を残す
struct __attribute__((packed)) DayBlob {
    uint32_t ymd;
    uint16_t cups;
    uint8_t refills;
    uint8_t reserved0;
    uint8_t games[kLegacyGameCount];
    uint8_t reserved1[4];
};
static_assert(sizeof(DayBlob) == 16, "history record layout changed");

struct __attribute__((packed)) HistBlob {
    uint16_t version;
    uint16_t count;
    uint16_t head;
    uint16_t reserved;
    uint32_t total_games[kLegacyGameCount];   // 累計（35 日の輪から外れても残す）
    DayBlob days[kHistoryDays];
    uint32_t crc;
};
static_assert(sizeof(HistBlob) == 588, "history blob layout changed");

// --- 5 つめ以降のゲームの回数（名前空間 cup_ext）-----------------------------
// 杯数の塊（上の 2 つ）を守るため、ゲーム id 4 番から先の回数だけを別の名前空間に置く。
// 考え方は同じ（版 + CRC + 35 日の輪 + 累計）。**読み書きの入口は cup::stats:: のまま**
constexpr const char *kNsExt = "cup_ext";
constexpr const char *kKeyExtToday = "today";
constexpr const char *kKeyExtDays = "days";
constexpr uint16_t kExtVersion = 1;
constexpr size_t kExtSlots = 4;     // ゲーム id 4〜7 のぶん（いま使うのは 1 つ）
static_assert(kGameCount - kLegacyGameCount <= kExtSlots, "cup_ext is too small");

struct __attribute__((packed)) ExtTodayBlob {
    uint16_t version;
    uint16_t reserved;
    uint32_t ymd;
    uint8_t games[kExtSlots];
    uint32_t crc;
};
static_assert(sizeof(ExtTodayBlob) == 16, "cup_ext today blob layout changed");

struct __attribute__((packed)) ExtDayBlob {
    uint32_t ymd;
    uint8_t games[kExtSlots];
};
static_assert(sizeof(ExtDayBlob) == 8, "cup_ext day record layout changed");

struct __attribute__((packed)) ExtHistBlob {
    uint16_t version;
    uint16_t count;
    uint16_t head;
    uint16_t reserved;
    uint32_t total_games[kExtSlots];
    ExtDayBlob days[kHistoryDays];
    uint32_t crc;
};
static_assert(sizeof(ExtHistBlob) == 308, "cup_ext history blob layout changed");

uint32_t s_ymd = 0;          // 記録中の日付 (YYYYMMDD)。時刻未取得なら 0
uint32_t s_taken = 0;
uint32_t s_remaining = 0;
volatile bool s_dirty = false;

Today s_today = {0, {0}, 0, 0, -1, -1, {0}};

HistBlob s_hist = {};
ExtHistBlob s_ext_hist = {};
bool s_ext_dirty = false;       // cup_ext の「今日」を書き直す必要があるか
const DayRecord kEmptyDay = {0, 0, 0, {0}};

// 0 時からの分。時刻がまだ分からなければ -1
int16_t nowMinutes()
{
    const time_t now = time(nullptr);
    if (now < 1700000000) {     // 2023 年より前なら未設定とみなす（net::timeSynced と同じ判定）
        return -1;
    }
    struct tm tm;
    localtime_r(&now, &tm);
    return (int16_t)(tm.tm_hour * 60 + tm.tm_min);
}

uint32_t crcOfToday(const TodayBlob &b)
{
    return esp_rom_crc32_le(0, (const uint8_t *)&b, sizeof(TodayBlob) - sizeof(uint32_t));
}

uint32_t crcOfHist(const HistBlob &b)
{
    return esp_rom_crc32_le(0, (const uint8_t *)&b, sizeof(HistBlob) - sizeof(uint32_t));
}

uint32_t crcOfExtToday(const ExtTodayBlob &b)
{
    return esp_rom_crc32_le(0, (const uint8_t *)&b, sizeof(ExtTodayBlob) - sizeof(uint32_t));
}

uint32_t crcOfExtHist(const ExtHistBlob &b)
{
    return esp_rom_crc32_le(0, (const uint8_t *)&b, sizeof(ExtHistBlob) - sizeof(uint32_t));
}

void clearToday(uint32_t ymd)
{
    s_today = Today{ymd, {0}, 0, 0, -1, -1, {0}};
}

void loadToday(Preferences &prefs)
{
    TodayBlob b = {};
    if (prefs.getBytesLength(kKeyToday) != sizeof(TodayBlob) ||
        prefs.getBytes(kKeyToday, &b, sizeof(TodayBlob)) != sizeof(TodayBlob) ||
        b.version != kTodayVersion || b.crc != crcOfToday(b)) {
        // 壊れている・まだ無い。グラフだけ空にして始める（杯数 taken/left には触らない）
        clearToday(s_ymd);
        return;
    }
    s_today.ymd = b.ymd;
    memcpy(s_today.hour, b.hour, sizeof(s_today.hour));
    s_today.unknown = b.unknown;
    s_today.refills = b.refills;
    s_today.last_take_min = b.last_take_min;
    s_today.last_refill_min = b.last_refill_min;
    // 塊に入っているのは 0〜3 番だけ。**塊の側の大きさで写す**
    // （s_today.games は 5 つあるので、そちらの大きさを使うと塊を 1 バイト読み越す）。
    // 4 番から先はこのあと loadExtToday が入れる
    memcpy(s_today.games, b.games, sizeof(b.games));
    // 杯数側の日付とずれていたら、時間帯のグラフだけ作り直す
    if (s_today.ymd != s_ymd) {
        clearToday(s_ymd);
    }
}

// --- cup_ext（5 つめ以降のゲームの回数）-------------------------------------
// 失敗しても杯数側には一切触らない。読めなければ「記録なし」として 0 から始める

void loadExtToday()
{
    for (size_t i = kLegacyGameCount; i < kGameCount; ++i) {
        s_today.games[i] = 0;
    }
    ExtTodayBlob b = {};
    Preferences prefs;
    if (!prefs.begin(kNsExt, true)) {
        return;     // 名前空間がまだ無い＝一度も遊んでいない
    }
    const bool ok = prefs.getBytesLength(kKeyExtToday) == sizeof(ExtTodayBlob) &&
                    prefs.getBytes(kKeyExtToday, &b, sizeof(b)) == sizeof(b) &&
                    b.version == kExtVersion && b.crc == crcOfExtToday(b);
    prefs.end();
    if (!ok || b.ymd != s_today.ymd) {
        return;     // 別の日の記録は引き継がない（杯数側の clearToday と同じ扱い）
    }
    for (size_t i = kLegacyGameCount; i < kGameCount; ++i) {
        s_today.games[i] = b.games[i - kLegacyGameCount];
    }
}

void saveExtToday()
{
    if (!s_ext_dirty) {
        return;
    }
    ExtTodayBlob b = {};
    b.version = kExtVersion;
    b.ymd = s_today.ymd;
    for (size_t i = kLegacyGameCount; i < kGameCount; ++i) {
        b.games[i - kLegacyGameCount] = s_today.games[i];
    }
    b.crc = crcOfExtToday(b);

    Preferences prefs;
    if (!prefs.begin(kNsExt, false)) {
        Serial.println("[CUP] cup_ext today save failed (open)");
        return;
    }
    prefs.putBytes(kKeyExtToday, &b, sizeof(b));
    prefs.end();
    s_ext_dirty = false;
}

void loadExtHistory()
{
    memset(&s_ext_hist, 0, sizeof(s_ext_hist));
    s_ext_hist.version = kExtVersion;

    ExtHistBlob b = {};
    Preferences prefs;
    if (!prefs.begin(kNsExt, true)) {
        return;
    }
    const bool ok = prefs.getBytesLength(kKeyExtDays) == sizeof(ExtHistBlob) &&
                    prefs.getBytes(kKeyExtDays, &b, sizeof(b)) == sizeof(b) &&
                    b.version == kExtVersion && b.crc == crcOfExtHist(b) &&
                    b.count <= kHistoryDays && b.head < kHistoryDays;
    prefs.end();
    if (!ok) {
        return;
    }
    s_ext_hist = b;
}

bool saveExtHistory()
{
    s_ext_hist.version = kExtVersion;
    s_ext_hist.reserved = 0;
    s_ext_hist.crc = crcOfExtHist(s_ext_hist);

    Preferences prefs;
    if (!prefs.begin(kNsExt, false)) {
        Serial.println("[CUP] cup_ext history save failed (open)");
        return false;
    }
    const size_t written = prefs.putBytes(kKeyExtDays, &s_ext_hist, sizeof(ExtHistBlob));
    ExtHistBlob back = {};
    const size_t read = prefs.getBytes(kKeyExtDays, &back, sizeof(ExtHistBlob));
    prefs.end();
    if (written != sizeof(ExtHistBlob) || read != sizeof(ExtHistBlob) ||
        memcmp(&back, &s_ext_hist, sizeof(ExtHistBlob)) != 0) {
        Serial.println("[CUP] cup_ext history save verify FAILED");
        return false;
    }
    return true;
}

// その日の 4 番以降の回数を輪から拾う（無ければ 0 のまま）
void fillExtGames(uint32_t ymd, uint8_t *games)
{
    for (size_t i = 0; i < s_ext_hist.count; ++i) {
        const size_t pos = (s_ext_hist.head + kHistoryDays - s_ext_hist.count + i) % kHistoryDays;
        if (s_ext_hist.days[pos].ymd != ymd) {
            continue;
        }
        for (size_t g = kLegacyGameCount; g < kGameCount; ++g) {
            games[g] = s_ext_hist.days[pos].games[g - kLegacyGameCount];
        }
        return;
    }
}

// 前日ぶんを cup_ext の輪にも 1 件足す（杯数側の appendHistory と同じ時機）
void appendExtHistory(uint32_t ymd, const uint8_t *games)
{
    ExtDayBlob rec = {};
    rec.ymd = ymd;
    for (size_t g = kLegacyGameCount; g < kGameCount; ++g) {
        rec.games[g - kLegacyGameCount] = games[g];
    }
    const uint16_t last = (uint16_t)((s_ext_hist.head + kHistoryDays - 1) % kHistoryDays);
    if (s_ext_hist.count > 0 && s_ext_hist.days[last].ymd == ymd) {
        s_ext_hist.days[last] = rec;
    } else {
        s_ext_hist.days[s_ext_hist.head] = rec;
        s_ext_hist.head = (uint16_t)((s_ext_hist.head + 1) % kHistoryDays);
        if (s_ext_hist.count < kHistoryDays) {
            ++s_ext_hist.count;
        }
    }
    saveExtHistory();
}

void saveToday(Preferences &prefs)
{
    TodayBlob b = {};
    b.version = kTodayVersion;
    b.unknown = s_today.unknown;
    b.ymd = s_today.ymd;
    memcpy(b.hour, s_today.hour, sizeof(b.hour));
    b.refills = s_today.refills;
    b.last_take_min = s_today.last_take_min;
    b.last_refill_min = s_today.last_refill_min;
    memcpy(b.games, s_today.games, sizeof(b.games));
    b.crc = crcOfToday(b);
    prefs.putBytes(kKeyToday, &b, sizeof(TodayBlob));
}

void loadHistory()
{
    memset(&s_hist, 0, sizeof(s_hist));
    s_hist.version = kHistVersion;

    HistBlob b = {};
    Preferences prefs;
    if (!prefs.begin(kNsHist, true)) {
        return;     // 名前空間がまだ無い＝一度も保存していない
    }
    const bool ok = prefs.getBytesLength(kKeyDays) == sizeof(HistBlob) &&
                    prefs.getBytes(kKeyDays, &b, sizeof(HistBlob)) == sizeof(HistBlob) &&
                    b.version == kHistVersion && b.crc == crcOfHist(b) &&
                    b.count <= kHistoryDays && b.head < kHistoryDays;
    prefs.end();
    if (!ok) {
        Serial.println("[CUP] history empty or broken -> start empty");
        return;
    }
    s_hist = b;
    Serial.printf("[CUP] history %u day(s)\n", (unsigned)s_hist.count);
}

// 履歴 blob を書き、読み返して照合する（日付が変わったときと、ゲームが 1 回終わったとき）
bool saveHistory()
{
    s_hist.version = kHistVersion;
    s_hist.reserved = 0;
    s_hist.crc = crcOfHist(s_hist);

    Preferences prefs;
    if (!prefs.begin(kNsHist, false)) {
        Serial.println("[CUP] history save failed (open)");
        return false;
    }
    const size_t written = prefs.putBytes(kKeyDays, &s_hist, sizeof(HistBlob));
    HistBlob back = {};
    const size_t read = prefs.getBytes(kKeyDays, &back, sizeof(HistBlob));
    prefs.end();
    if (written != sizeof(HistBlob) || read != sizeof(HistBlob) ||
        memcmp(&back, &s_hist, sizeof(HistBlob)) != 0) {
        Serial.println("[CUP] history save verify FAILED");
        return false;
    }
    return true;
}

// 前日の確定値を輪に 1 件足して保存する
void appendHistory(uint32_t ymd, uint32_t cups, uint16_t refills, const uint8_t *games)
{
    if (ymd == 0) {
        return;
    }
    DayBlob rec = {};
    rec.ymd = ymd;
    rec.cups = (uint16_t)(cups > 65535 ? 65535 : cups);
    rec.refills = (uint8_t)(refills > 255 ? 255 : refills);
    memcpy(rec.games, games, kLegacyGameCount);     // 4 番から先は cup_ext 側へ

    const uint16_t last = (uint16_t)((s_hist.head + kHistoryDays - 1) % kHistoryDays);
    if (s_hist.count > 0 && s_hist.days[last].ymd == ymd) {
        // 同じ日付が末尾にあるなら上書きする（時刻を直すと同じ日が 2 回終わることがある）
        s_hist.days[last] = rec;
    } else {
        s_hist.days[s_hist.head] = rec;
        s_hist.head = (uint16_t)((s_hist.head + 1) % kHistoryDays);
        if (s_hist.count < kHistoryDays) {
            ++s_hist.count;
        }
    }
    if (saveHistory()) {
        Serial.printf("[CUP] history += %lu (%lu cups)\n", (unsigned long)ymd, (unsigned long)cups);
    }
    appendExtHistory(ymd, games);
}

}  // namespace

void load()
{
    Preferences prefs;
    prefs.begin(kNs, true);
    s_ymd = prefs.getUInt("ymd", 0);
    s_taken = prefs.getUInt("taken", 0);
    s_remaining = prefs.getUInt("left", 0);
    loadToday(prefs);
    prefs.end();
    if (s_remaining > maxCups()) {
        s_remaining = maxCups();    // 「1 回に作る杯数」を減らした直後はここで丸まる
    }
    loadHistory();
    // 5 つめ以降のゲームの回数（杯数の塊とは別の名前空間。壊れていても杯数に影響しない）
    loadExtHistory();
    loadExtToday();
    Serial.printf("[CUP] loaded ymd=%lu taken=%lu left=%lu (max %lu)\n",
                  (unsigned long)s_ymd, (unsigned long)s_taken, (unsigned long)s_remaining,
                  (unsigned long)maxCups());
}

void saveIfDirty()
{
    if (!s_dirty) {
        return;
    }
    s_dirty = false;
    Preferences prefs;
    prefs.begin(kNs, false);
    prefs.putUInt("ymd", s_ymd);
    prefs.putUInt("taken", s_taken);
    prefs.putUInt("left", s_remaining);
    // 「今日の状況」も同じ時機にまとめて書く（NVS への書き込みは画面が一瞬止まるため）
    saveToday(prefs);
    prefs.end();
    // 5 つめ以降のゲームの回数は別の名前空間。変わったときだけ書く
    saveExtToday();
}

void takeOne()
{
    s_taken++;
    if (s_remaining > 0) {
        s_remaining--;
    }
    const int16_t minutes = nowMinutes();
    if (minutes < 0) {
        // 時刻が分からない間の 1 杯は杯数にだけ数え、時間帯のグラフには入れない
        if (s_today.unknown < 65535) {
            ++s_today.unknown;
        }
    } else {
        const uint8_t h = (uint8_t)(minutes / 60);
        if (s_today.hour[h] < 255) {
            ++s_today.hour[h];
        }
        s_today.last_take_min = minutes;
    }
    s_dirty = true;
    Serial.printf("[CUP] +1 taken=%lu left=%lu\n", (unsigned long)s_taken, (unsigned long)s_remaining);
}

void refill()
{
    s_remaining = maxCups();
    if (s_today.refills < 65535) {
        ++s_today.refills;
    }
    const int16_t minutes = nowMinutes();
    if (minutes >= 0) {
        s_today.last_refill_min = minutes;
    }
    s_dirty = true;
    Serial.printf("[CUP] refill left=%lu\n", (unsigned long)s_remaining);
}

bool checkNewDay(uint32_t ymd)
{
    if (ymd == 0 || ymd == s_ymd) {
        return false;
    }
    bool rolled = false;
    // 時刻取得前 (s_ymd==0) に数えた分は、その日の分として引き継ぐ
    if (s_ymd != 0) {
        // 前日の確定値を履歴へ。NVS への書き込みはここだけ（1 日 1 回）
        appendHistory(s_ymd, s_taken, s_today.refills, s_today.games);
        s_taken = 0;
        // 朝いちばんの残り: 既定は 0 杯（まだ作っていない扱い）。設定で満杯にもできる
        s_remaining = settings::morningFull() ? maxCups() : 0;
        clearToday(ymd);
        rolled = true;
        Serial.printf("[CUP] new day %lu: reset (left=%lu)\n",
                      (unsigned long)ymd, (unsigned long)s_remaining);
    }
    s_ymd = ymd;
    s_today.ymd = ymd;
    s_dirty = true;
    // 日付が入るのは日またぎだけではない（起動直後に時刻が合った瞬間 0 -> 実日付 も通る）。
    // cup_ext の「今日」にも同じ日付を書き直さないと、次の起動で日付違いとして捨てられる
    s_ext_dirty = true;
    return rolled;
}

uint32_t taken()
{
    return s_taken;
}

uint32_t remaining()
{
    return s_remaining;
}

uint32_t maxCups()
{
    return settings::maxCups();
}

const Today &today()
{
    return s_today;
}

size_t historyCount()
{
    return s_hist.count;
}

namespace {

DayRecord toRecord(const DayBlob &b)
{
    DayRecord out = {};
    out.ymd = b.ymd;
    out.cups = b.cups;
    out.refills = b.refills;
    memcpy(out.games, b.games, kLegacyGameCount);
    fillExtGames(b.ymd, out.games);     // 4 番から先は cup_ext の輪から
    return out;
}

}  // namespace

DayRecord historyAt(size_t index)
{
    if (index >= s_hist.count) {
        return kEmptyDay;
    }
    return toRecord(s_hist.days[(s_hist.head + kHistoryDays - s_hist.count + index) % kHistoryDays]);
}

bool historyFor(uint32_t ymd, DayRecord &out)
{
    for (size_t i = 0; i < s_hist.count; ++i) {
        const size_t pos = (s_hist.head + kHistoryDays - s_hist.count + i) % kHistoryDays;
        if (s_hist.days[pos].ymd == ymd) {
            out = toRecord(s_hist.days[pos]);
            return true;
        }
    }
    return false;
}

bool undoOne(int &out_bucket)
{
    out_bucket = -2;
    if (s_taken == 0) {
        Serial.println("[CUP] undo: nothing to undo (taken=0)");
        return false;
    }
    const uint32_t before = s_taken;
    --s_taken;
    // 残り杯数 left は動かさない。朝いちばんは 0 杯から始まるので、
    // 誤って入った 1 杯でも left は減っていない（takeOne は left>0 のときだけ減らす）

    // どの時間帯の 1 杯だったかは記録に残っていないので、
    // **いまの時刻に近い順に、数の入っている時間帯を 1 つだけ探して減らす**。
    // 順番は 今の時 →(1 時間前 → 1 時間後)→(2 時間前 → 2 時間後)→ … で、0 時より前・23 時より後へは回らない。
    // 時刻が分からない、または時間帯のグラフが空なら unknown（時刻不明で数えた分）を減らす
    int hour_hit = -1;
    const int16_t minutes = nowMinutes();
    if (minutes >= 0) {
        const int now_h = minutes / 60;
        for (int d = 0; d <= 23 && hour_hit < 0; ++d) {
            const int cand[2] = {now_h - d, now_h + d};
            for (int k = 0; k < (d == 0 ? 1 : 2); ++k) {
                const int h = cand[k];
                if (h < 0 || h > 23 || s_today.hour[h] == 0) {
                    continue;
                }
                hour_hit = h;
                break;
            }
        }
    }
    if (hour_hit >= 0) {
        --s_today.hour[hour_hit];
        out_bucket = hour_hit;
    } else if (s_today.unknown > 0) {
        --s_today.unknown;
        out_bucket = -1;
    }
    // last_take_min はそのまま（「最後の 1 杯の時刻」を作り直す手立てが無いため）
    s_dirty = true;
    Serial.printf("[CUP] undo: taken %lu->%lu\n", (unsigned long)before, (unsigned long)s_taken);
    if (out_bucket >= 0) {
        Serial.printf("[CUP] undo: hour %02d -> %u (left=%lu, last_take_min kept)\n",
                      out_bucket, (unsigned)s_today.hour[out_bucket], (unsigned long)s_remaining);
    } else if (out_bucket == -1) {
        Serial.printf("[CUP] undo: unknown -> %u (left=%lu, last_take_min kept)\n",
                      (unsigned)s_today.unknown, (unsigned long)s_remaining);
    } else {
        Serial.printf("[CUP] undo: no bucket to decrement (left=%lu)\n", (unsigned long)s_remaining);
    }
    return true;
}

bool setHistoryCups(uint32_t ymd, uint16_t cups)
{
    for (size_t i = 0; i < s_hist.count; ++i) {
        const size_t pos = (s_hist.head + kHistoryDays - s_hist.count + i) % kHistoryDays;
        if (s_hist.days[pos].ymd != ymd) {
            continue;
        }
        const uint16_t before = s_hist.days[pos].cups;
        s_hist.days[pos].cups = cups;       // 補充回数とゲームの回数には触らない
        if (!saveHistory()) {               // 書いて読み返す通常の経路。駄目なら元に戻す
            s_hist.days[pos].cups = before;
            return false;
        }
        return true;
    }
    return false;       // その日が輪に無い。記録を新しく作ることはしない
}

namespace stats {

void gamePlayed(GameId id, const char *note)
{
    const size_t i = (size_t)id;
    if (i >= kGameCount) {
        return;
    }
    if (s_today.games[i] < 255) {
        ++s_today.games[i];
    }
    // 今日の分は次の保存でまとめて書く。累計は履歴 blob にあるので、
    // 遊び終わりのここで 1 回だけ書く（タイマーからは書かない）
    s_dirty = true;
    uint32_t total = 0;
    if (i < kLegacyGameCount) {
        if (s_hist.total_games[i] < UINT32_MAX) {
            ++s_hist.total_games[i];
        }
        total = s_hist.total_games[i];
        saveHistory();
    } else {
        const size_t j = i - kLegacyGameCount;
        if (s_ext_hist.total_games[j] < UINT32_MAX) {
            ++s_ext_hist.total_games[j];
        }
        total = s_ext_hist.total_games[j];
        s_ext_dirty = true;
        saveExtHistory();
    }
    // SD の操作ログにも 1 行。**回数だけ**で、役職・投票・勝敗・答えは残さない
    sdlog::event("game", s_taken, s_remaining, s_remaining, note);
    Serial.printf("[CUP] game %u played (today=%u total=%lu)\n", (unsigned)i,
                  (unsigned)s_today.games[i], (unsigned long)total);
}

uint8_t todayGames(GameId id)
{
    const size_t i = (size_t)id;
    return i < kGameCount ? s_today.games[i] : 0;
}

uint32_t totalGames(GameId id)
{
    const size_t i = (size_t)id;
    if (i < kLegacyGameCount) {
        return s_hist.total_games[i];
    }
    return i < kGameCount ? s_ext_hist.total_games[i - kLegacyGameCount] : 0;
}

}  // namespace stats

}  // namespace cup
