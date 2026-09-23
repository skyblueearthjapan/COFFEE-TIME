#include "CardsStore.h"

#include <Arduino.h>
#include <Preferences.h>

#include <cstring>

namespace cards {
namespace store {
namespace {

constexpr const char *kNvsNamespace = "ct_cards";
constexpr const char *kStatsKey = "stats";
constexpr const char *kSeenKey = "seen";

// きろくの塊: 版 1 + 予備 3 + 勝敗 (8×4×4)×2 + 的中 8×4 + CRC 4
constexpr uint8_t kStatsVersion = 1;
constexpr size_t kCountBytes = kSlots * kGames * kOutcomes * 2;
constexpr size_t kHitBytes = kSlots * 4;
constexpr size_t kStatsBytes = 4 + kCountBytes + kHitBytes + 4;
static_assert(kStatsBytes == 296, "stats blob layout changed");

Record s_stats = {};
bool s_loaded = false;

// はじめての説明を見たか。版 1 + フラグ 1 + 予備 2 + CRC 4
constexpr uint8_t kSeenVersion = 1;
constexpr size_t kSeenBytes = 8;
uint8_t s_seen = 0;
bool s_seen_loaded = false;

uint32_t crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

void encodeStats(uint8_t *out)
{
    std::memset(out, 0, kStatsBytes);
    out[0] = kStatsVersion;
    size_t at = 4;
    for (size_t s = 0; s < kSlots; ++s) {
        for (size_t g = 0; g < kGames; ++g) {
            for (size_t o = 0; o < kOutcomes; ++o) {
                const uint16_t v = s_stats.counts[s][g][o];
                out[at++] = (uint8_t)(v & 0xFFu);
                out[at++] = (uint8_t)(v >> 8);
            }
        }
    }
    for (size_t s = 0; s < kSlots; ++s) {
        put32(out + at, s_stats.bac_hits[s]);
        at += 4;
    }
    put32(out + kStatsBytes - 4, crc32(out, kStatsBytes - 4));
}

bool decodeStats(const uint8_t *in)
{
    if (in[0] != kStatsVersion) {
        return false;
    }
    if (get32(in + kStatsBytes - 4) != crc32(in, kStatsBytes - 4)) {
        return false;
    }
    size_t at = 4;
    for (size_t s = 0; s < kSlots; ++s) {
        for (size_t g = 0; g < kGames; ++g) {
            for (size_t o = 0; o < kOutcomes; ++o) {
                s_stats.counts[s][g][o] =
                    (uint16_t)((uint16_t)in[at] | ((uint16_t)in[at + 1] << 8));
                at += 2;
            }
        }
    }
    for (size_t s = 0; s < kSlots; ++s) {
        s_stats.bac_hits[s] = get32(in + at);
        at += 4;
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
        Serial.println("[CARDS] cannot open NVS for writing");
        return false;
    }
    const size_t written = prefs.putBytes(kStatsKey, blob, kStatsBytes);
    const size_t read = prefs.getBytes(kStatsKey, back, kStatsBytes);
    prefs.end();
    return written == kStatsBytes && read == kStatsBytes &&
           std::memcmp(blob, back, kStatsBytes) == 0;
}

bool writeSeen()
{
    uint8_t blob[kSeenBytes] = {};
    uint8_t back[kSeenBytes] = {};
    blob[0] = kSeenVersion;
    blob[1] = (uint8_t)(s_seen & 0x0Fu);
    put32(blob + kSeenBytes - 4, crc32(blob, kSeenBytes - 4));

    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) {
        Serial.println("[CARDS] cannot open NVS for writing (seen)");
        return false;
    }
    const size_t written = prefs.putBytes(kSeenKey, blob, kSeenBytes);
    const size_t read = prefs.getBytes(kSeenKey, back, kSeenBytes);
    prefs.end();
    return written == kSeenBytes && read == kSeenBytes &&
           std::memcmp(blob, back, kSeenBytes) == 0;
}

bool saveSeen()
{
    if (writeSeen() || writeSeen()) {
        return true;
    }
    Serial.println("[CARDS] seen save failed, reloading from flash");
    loadSeen();
    return false;
}

}  // namespace

bool loadStats()
{
    // **ここで全部 0 に戻すので、ゲスト（スロット 8）の記録は画面を開くたびに白紙になる。**
    // NVS に入っているのは 0〜7 番だけなので、ゲストぶんは二度と戻らない
    std::memset(&s_stats, 0, sizeof(s_stats));
    s_loaded = true;

    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, true)) {
        return true;        // 名前空間がまだ無い＝一度も遊んでいない
    }
    const size_t length = prefs.getBytesLength(kStatsKey);
    bool ok = true;
    if (length == kStatsBytes) {
        uint8_t blob[kStatsBytes];
        if (prefs.getBytes(kStatsKey, blob, kStatsBytes) != kStatsBytes || !decodeStats(blob)) {
            std::memset(&s_stats, 0, sizeof(s_stats));
            Serial.println("[CARDS] stats are broken; starting over");
            ok = false;
        }
    } else if (length != 0) {
        Serial.printf("[CARDS] stats blob has an unknown length (%u); starting over\n",
                      (unsigned)length);
        ok = false;
    }
    prefs.end();
    return ok;
}

const Record &stats()
{
    if (!s_loaded) {
        loadStats();
    }
    return s_stats;
}

bool noteResult(size_t slot, size_t game, Outcome outcome, uint32_t bac_hits)
{
    if (!s_loaded) {
        loadStats();
    }
    if (game >= kGames || (size_t)outcome >= kOutcomes || slot > kGuestSlot) {
        return false;
    }
    if (s_stats.counts[slot][game][(size_t)outcome] < 65535) {
        ++s_stats.counts[slot][game][(size_t)outcome];
    }
    s_stats.bac_hits[slot] += bac_hits;

    if (slot >= kGuestSlot) {
        return true;        // ゲストは RAM だけ。フラッシュには 1 バイトも書かない
    }
    if (writeStats() || writeStats()) {
        return true;
    }
    // 保存できなかった。RAM だけ進んだ状態を残さずフラッシュを読み直す
    Serial.println("[CARDS] stats save failed, reloading from flash");
    loadStats();
    return false;
}

void totals(size_t slot, size_t game, uint32_t &win, uint32_t &loss, uint32_t &draw,
            uint32_t &aborted)
{
    const Record &r = stats();
    win = loss = draw = aborted = 0;
    if (slot > kGuestSlot || game >= kGames) {
        return;
    }
    win = r.counts[slot][game][(size_t)Outcome::Win];
    loss = r.counts[slot][game][(size_t)Outcome::Loss];
    draw = r.counts[slot][game][(size_t)Outcome::Draw];
    aborted = r.counts[slot][game][(size_t)Outcome::Aborted];
}

bool loadSeen()
{
    s_seen = 0;
    s_seen_loaded = true;

    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, true)) {
        return true;        // まだ一度も遊んでいない
    }
    const size_t length = prefs.getBytesLength(kSeenKey);
    bool ok = true;
    if (length == kSeenBytes) {
        uint8_t blob[kSeenBytes];
        if (prefs.getBytes(kSeenKey, blob, kSeenBytes) != kSeenBytes ||
            blob[0] != kSeenVersion ||
            get32(blob + kSeenBytes - 4) != crc32(blob, kSeenBytes - 4)) {
            // 壊れていたら「まだ見ていない」に倒す（説明が余分に出るだけで害がない）
            Serial.println("[CARDS] the seen flags are broken; showing the walkthrough again");
            s_seen = 0;
            ok = false;
        } else {
            s_seen = (uint8_t)(blob[1] & 0x0Fu);
        }
    } else if (length != 0) {
        Serial.printf("[CARDS] the seen blob has an unknown length (%u)\n", (unsigned)length);
        ok = false;
    }
    prefs.end();
    return ok;
}

uint8_t seenFlags()
{
    if (!s_seen_loaded) {
        loadSeen();
    }
    return s_seen;
}

bool seenFlag(size_t game)
{
    return game < kGames && (seenFlags() & (uint8_t)(1u << game)) != 0;
}

bool markSeen(size_t game)
{
    if (game >= kGames) {
        return false;
    }
    if (!s_seen_loaded) {
        loadSeen();
    }
    const uint8_t next = (uint8_t)(s_seen | (1u << game));
    if (next == s_seen) {
        return true;        // すでに立っている。フラッシュは触らない
    }
    s_seen = next;
    return saveSeen();
}

bool clearSeen()
{
    if (!s_seen_loaded) {
        loadSeen();
    }
    if (s_seen == 0) {
        return true;
    }
    s_seen = 0;
    return saveSeen();
}

}  // namespace store
}  // namespace cards
