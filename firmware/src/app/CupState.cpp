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

// NVS に置く「今日の状況」。末尾の crc は手前のバイト列に対して計算する
struct __attribute__((packed)) TodayBlob {
    uint16_t version;
    uint16_t unknown;
    uint32_t ymd;
    uint8_t hour[24];
    uint16_t refills;
    int16_t last_take_min;
    int16_t last_refill_min;
    uint8_t games[kGameCount];
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
    uint8_t games[kGameCount];
    uint8_t reserved1[4];
};
static_assert(sizeof(DayBlob) == 16, "history record layout changed");

struct __attribute__((packed)) HistBlob {
    uint16_t version;
    uint16_t count;
    uint16_t head;
    uint16_t reserved;
    uint32_t total_games[kGameCount];   // 累計（35 日の輪から外れても残す）
    DayBlob days[kHistoryDays];
    uint32_t crc;
};
static_assert(sizeof(HistBlob) == 588, "history blob layout changed");

uint32_t s_ymd = 0;          // 記録中の日付 (YYYYMMDD)。時刻未取得なら 0
uint32_t s_taken = 0;
uint32_t s_remaining = 0;
volatile bool s_dirty = false;

Today s_today = {0, {0}, 0, 0, -1, -1, {0}};

HistBlob s_hist = {};
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
    memcpy(s_today.games, b.games, sizeof(s_today.games));
    // 杯数側の日付とずれていたら、時間帯のグラフだけ作り直す
    if (s_today.ymd != s_ymd) {
        clearToday(s_ymd);
    }
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
    memcpy(rec.games, games, kGameCount);

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
    memcpy(out.games, b.games, kGameCount);
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
    if (s_hist.total_games[i] < UINT32_MAX) {
        ++s_hist.total_games[i];
    }
    // 今日の分は次の保存でまとめて書く。累計は履歴 blob にあるので、
    // 遊び終わりのここで 1 回だけ書く（タイマーからは書かない）
    s_dirty = true;
    saveHistory();
    // SD の操作ログにも 1 行。**回数だけ**で、役職・投票・勝敗・答えは残さない
    sdlog::event("game", s_taken, s_remaining, s_remaining, note);
    Serial.printf("[CUP] game %u played (today=%u total=%lu)\n", (unsigned)i,
                  (unsigned)s_today.games[i], (unsigned long)s_hist.total_games[i]);
}

uint8_t todayGames(GameId id)
{
    const size_t i = (size_t)id;
    return i < kGameCount ? s_today.games[i] : 0;
}

uint32_t totalGames(GameId id)
{
    const size_t i = (size_t)id;
    return i < kGameCount ? s_hist.total_games[i] : 0;
}

}  // namespace stats

}  // namespace cup
