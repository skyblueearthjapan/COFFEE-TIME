#include "ReversiStore.h"

#include <Arduino.h>
#include <Preferences.h>

#include <array>
#include <cstring>

namespace reversi {
namespace store {
namespace {

namespace core = ct_rev;

constexpr const char *kNvsNamespace = "ct_rev";
constexpr const char *kGameKey = "game";
constexpr const char *kStatsKey = "stats";

// きろくの塊: 版 2 + 予備 2 + 勝敗 (2×5×3)×2 = 60 + CRC 4
constexpr uint8_t kStatsVersion = 1;
constexpr size_t kStatsCounts = kSizeCount * rev::kOpponentCount * rev::kOutcomeCount;
constexpr size_t kStatsBytes = 4 + 2 * kStatsCounts + 4;
static_assert(kStatsBytes == 68, "stats blob layout changed");

Record s_stats = {};
bool s_stats_loaded = false;

void encodeStats(uint8_t *out)
{
    std::memset(out, 0, kStatsBytes);
    out[0] = kStatsVersion;
    size_t at = 4;
    for (size_t n = 0; n < kSizeCount; ++n) {
        for (size_t o = 0; o < rev::kOpponentCount; ++o) {
            for (size_t r = 0; r < rev::kOutcomeCount; ++r) {
                const uint16_t v = s_stats.counts[n][o][r];
                out[at++] = (uint8_t)(v & 0xFFu);
                out[at++] = (uint8_t)(v >> 8);
            }
        }
    }
    core::put32(out + kStatsBytes - 4, core::crc32(out, kStatsBytes - 4));
}

bool decodeStats(const uint8_t *in)
{
    if (in[0] != kStatsVersion) {
        return false;
    }
    if (core::get32(in + kStatsBytes - 4) != core::crc32(in, kStatsBytes - 4)) {
        return false;
    }
    size_t at = 4;
    for (size_t n = 0; n < kSizeCount; ++n) {
        for (size_t o = 0; o < rev::kOpponentCount; ++o) {
            for (size_t r = 0; r < rev::kOutcomeCount; ++r) {
                s_stats.counts[n][o][r] = (uint16_t)((uint16_t)in[at] | ((uint16_t)in[at + 1] << 8));
                at += 2;
            }
        }
    }
    return true;
}

bool writeStats()
{
    uint8_t blob[kStatsBytes];
    uint8_t back[kStatsBytes];
    encodeStats(blob);

    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) {
        Serial.println("[REV] cannot open NVS for writing (stats)");
        return false;
    }
    const size_t written = prefs.putBytes(kStatsKey, blob, kStatsBytes);
    const size_t read = prefs.getBytes(kStatsKey, back, kStatsBytes);
    prefs.end();
    return written == kStatsBytes && read == kStatsBytes &&
           std::memcmp(blob, back, kStatsBytes) == 0;
}

// 1 局ぶんを書いて読み戻し、**同じバイト列が返ってきたときだけ** true（設計書 11.2）。
//
// 書いた塊は encode がコア自身の作った Session から組み立てたもので、CRC も encode が
// 付けている。読み戻して 1 バイトも違わなければ、その塊は decode できる塊である。
// ここで decode まで走らせると初手から全部を再生することになり、手数の 2 乗の重さが
// 1 手ごとに LVGL タスクへ乗る（8×8 の終盤で効いてくる）。**復元の検査は読み込み側だけ**
bool writeGame(const ct_rev::Session &s)
{
    static std::array<uint8_t, core::SNAPSHOT_MAX> blob;
    static std::array<uint8_t, core::SNAPSHOT_MAX> back;
    const size_t bytes = core::encode(s, blob);
    if (bytes == 0) {
        Serial.println("[REV] snapshot could not be encoded");
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) {
        Serial.println("[REV] cannot open NVS for writing (game)");
        return false;
    }
    const size_t written = prefs.putBytes(kGameKey, blob.data(), bytes);
    const size_t read = prefs.getBytes(kGameKey, back.data(), back.size());
    prefs.end();
    if (written != bytes || read != bytes || std::memcmp(blob.data(), back.data(), bytes) != 0) {
        Serial.println("[REV] game write-back mismatch");
        return false;
    }
    return true;
}

}  // namespace

bool loadGame(ct_rev::Session &out)
{
    static std::array<uint8_t, core::SNAPSHOT_MAX> blob;
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, true)) {
        return false;       // 名前空間がまだ無い＝一度も遊んでいない
    }
    const size_t length = prefs.getBytesLength(kGameKey);
    bool ok = false;
    if (length >= 38 && length <= blob.size() &&
        prefs.getBytes(kGameKey, blob.data(), length) == length) {
        ok = core::decode(blob.data(), length, out);
    }
    prefs.end();
    if (length != 0 && !ok) {
        // 壊れている。古い盤面へ黙って戻らない（設計書 11.3）。消すのは呼び出し側の判断
        Serial.printf("[REV] saved game is unreadable (%u bytes)\n", (unsigned)length);
    }
    return ok;
}

bool hasResumableGame()
{
    ct_rev::Session s;
    return loadGame(s) && s.closure == ct_rev::Closure::Active;
}

bool saveGame(const ct_rev::Session &s)
{
    if (writeGame(s)) {
        return true;
    }
    // 1 回だけやり直す（DuelStore・DetectiveProgress と同じ作法）
    if (writeGame(s)) {
        return true;
    }
    Serial.println("[REV] game save failed twice");
    return false;
}

void clearGame()
{
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) {
        return;
    }
    prefs.remove(kGameKey);
    prefs.end();
}

bool loadStats()
{
    s_stats = Record{};
    s_stats_loaded = true;

    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, true)) {
        return true;
    }
    const size_t length = prefs.getBytesLength(kStatsKey);
    bool ok = true;
    if (length == kStatsBytes) {
        uint8_t blob[kStatsBytes];
        if (prefs.getBytes(kStatsKey, blob, kStatsBytes) != kStatsBytes || !decodeStats(blob)) {
            s_stats = Record{};
            Serial.println("[REV] stats are broken; starting over");
            ok = false;
        }
    } else if (length != 0) {
        Serial.printf("[REV] stats blob has an unknown length (%u); starting over\n",
                      (unsigned)length);
        ok = false;
    }
    prefs.end();
    return ok;
}

const Record &stats()
{
    if (!s_stats_loaded) {
        loadStats();
    }
    return s_stats;
}

bool noteResult(uint8_t n, rev::Opponent opponent, rev::Outcome outcome)
{
    if (!s_stats_loaded) {
        loadStats();
    }
    const size_t size = (n == 8) ? 1 : 0;
    const size_t o = (size_t)opponent;
    const size_t r = (size_t)outcome;
    if (o >= rev::kOpponentCount || r >= rev::kOutcomeCount) {
        return false;
    }
    if (s_stats.counts[size][o][r] < 65535) {
        ++s_stats.counts[size][o][r];
    }
    if (writeStats()) {
        return true;
    }
    if (writeStats()) {
        return true;
    }
    // 保存できなかった。RAM だけ進んだ状態を残さずフラッシュを読み直す
    Serial.println("[REV] stats save failed, reloading from flash");
    loadStats();
    return false;
}

void totals(uint8_t n, uint32_t &win, uint32_t &loss, uint32_t &draw)
{
    const Record &r = stats();
    const size_t size = (n == 8) ? 1 : 0;
    win = loss = draw = 0;
    for (size_t o = 0; o < rev::kOpponentCount; ++o) {
        win += r.counts[size][o][(size_t)rev::Outcome::Win];
        loss += r.counts[size][o][(size_t)rev::Outcome::Loss];
        draw += r.counts[size][o][(size_t)rev::Outcome::Draw];
    }
}

}  // namespace store
}  // namespace reversi
