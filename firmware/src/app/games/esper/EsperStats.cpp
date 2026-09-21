#include "EsperStats.h"

#include <Arduino.h>
#include <Preferences.h>

#include <array>

namespace esper {
namespace {

constexpr const char *kNvsNamespace = "ct_esp";
constexpr const char *kNvsKey = "stat";
constexpr uint8_t kFormatVersion = 1;
constexpr size_t kBlobBytes = 32;
constexpr size_t kHeaderBytes = 4;
constexpr size_t kRecordBytes = 8;

ModeStats s_records[kModeSlots] = {};
bool s_loaded = false;

// CRC-32/ISO-HDLC。表を持たない実装（32 バイトしか通さないので速度は問題にならない）
uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

void clearAll()
{
    for (size_t i = 0; i < kModeSlots; ++i) {
        s_records[i] = ModeStats{0, 0, 0};
    }
}

void encode(std::array<uint8_t, kBlobBytes> &out)
{
    out.fill(0);
    out[0] = kFormatVersion;
    out[1] = (uint8_t)kModeSlots;
    for (size_t i = 0; i < kModeSlots; ++i) {
        const size_t at = kHeaderBytes + kRecordBytes * i;
        out[at + 0] = (uint8_t)(s_records[i].ai_win & 0xFFu);
        out[at + 1] = (uint8_t)((s_records[i].ai_win >> 8) & 0xFFu);
        out[at + 2] = (uint8_t)(s_records[i].human_win & 0xFFu);
        out[at + 3] = (uint8_t)((s_records[i].human_win >> 8) & 0xFFu);
        out[at + 4] = s_records[i].best_questions;
    }
    const uint32_t crc = crc32(out.data(), kBlobBytes - 4);
    out[28] = (uint8_t)(crc & 0xFFu);
    out[29] = (uint8_t)((crc >> 8) & 0xFFu);
    out[30] = (uint8_t)((crc >> 16) & 0xFFu);
    out[31] = (uint8_t)((crc >> 24) & 0xFFu);
}

bool decode(const std::array<uint8_t, kBlobBytes> &in)
{
    if (in[0] != kFormatVersion || in[1] != (uint8_t)kModeSlots) {
        return false;   // 版が違う。移行は管理者の明示的な作業で行う
    }
    const uint32_t want = (uint32_t)in[28] | ((uint32_t)in[29] << 8) |
                          ((uint32_t)in[30] << 16) | ((uint32_t)in[31] << 24);
    if (crc32(in.data(), kBlobBytes - 4) != want) {
        return false;
    }
    for (size_t i = 0; i < kModeSlots; ++i) {
        const size_t at = kHeaderBytes + kRecordBytes * i;
        s_records[i].ai_win = (uint16_t)((uint16_t)in[at + 0] | ((uint16_t)in[at + 1] << 8));
        s_records[i].human_win = (uint16_t)((uint16_t)in[at + 2] | ((uint16_t)in[at + 3] << 8));
        // 最大問数は 10 なので、それより大きい値は壊れているとみなして落とす
        s_records[i].best_questions = in[at + 4] > 10 ? 0 : in[at + 4];
    }
    return true;
}

}  // namespace

bool loadStats()
{
    clearAll();
    s_loaded = true;

    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, true)) {
        // 名前空間がまだ無い＝一度も遊んでいない。異常ではない
        return true;
    }
    const size_t length = prefs.getBytesLength(kNvsKey);
    if (length == 0) {
        prefs.end();
        return true;
    }
    std::array<uint8_t, kBlobBytes> bytes{};
    if (length != bytes.size()) {
        prefs.end();
        Serial.println("[ESP] stats blob has an unexpected length; starting over");
        return false;
    }
    const size_t read = prefs.getBytes(kNvsKey, bytes.data(), bytes.size());
    prefs.end();
    if (read != bytes.size() || !decode(bytes)) {
        clearAll();
        Serial.println("[ESP] stats blob is broken; starting over");
        return false;
    }
    return true;
}

bool saveStats()
{
    std::array<uint8_t, kBlobBytes> bytes{};
    encode(bytes);

    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) {
        Serial.println("[ESP] cannot open NVS for writing");
        return false;
    }
    const size_t written = prefs.putBytes(kNvsKey, bytes.data(), bytes.size());
    std::array<uint8_t, kBlobBytes> back{};
    const size_t read = prefs.getBytes(kNvsKey, back.data(), back.size());
    prefs.end();
    const bool ok = written == bytes.size() && read == bytes.size() && back == bytes;
    if (!ok) {
        Serial.println("[ESP] stats write-back mismatch");
    }
    return ok;
}

const ModeStats &stats(size_t mode)
{
    static const ModeStats kEmpty{0, 0, 0};
    if (mode >= kModeSlots) {
        return kEmpty;
    }
    if (!s_loaded) {
        loadStats();
    }
    return s_records[mode];
}

uint32_t totalPlays()
{
    if (!s_loaded) {
        loadStats();
    }
    uint32_t n = 0;
    for (size_t i = 0; i < kModeSlots; ++i) {
        n += (uint32_t)s_records[i].ai_win + (uint32_t)s_records[i].human_win;
    }
    return n;
}

void recordResult(size_t mode, bool ai_win, uint8_t questions)
{
    if (mode >= kModeSlots) {
        return;
    }
    if (!s_loaded) {
        loadStats();
    }
    ModeStats &r = s_records[mode];
    if (ai_win) {
        if (r.ai_win < 0xFFFFu) {
            ++r.ai_win;
        }
        // 「最少問数」は当てられた回だけを数える（外れた回の問数は記録に残さない）
        if (questions > 0 && (r.best_questions == 0 || questions < r.best_questions)) {
            r.best_questions = questions;
        }
    } else if (r.human_win < 0xFFFFu) {
        ++r.human_win;
    }
}

bool resetStats()
{
    clearAll();
    s_loaded = true;
    return saveStats();
}

}  // namespace esper
