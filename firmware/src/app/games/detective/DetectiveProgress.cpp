#include "DetectiveProgress.h"

#include <Arduino.h>
#include <Preferences.h>

#include <array>

#include "DetectiveContent.h"

namespace detective {
namespace {

constexpr const char *kNvsNamespace = "ct_det";
constexpr const char *kNvsKey = "prog";
constexpr uint8_t kFormatVersion = 1;
constexpr size_t kBlobBytes = 20;
constexpr size_t kRecordBytes = 4;
constexpr size_t kHeaderBytes = 4;

constexpr uint8_t kFlagStoryCompleted = 0x01;
constexpr uint8_t kFlagCollectible = 0x02;

EpisodeProgress s_records[kEpisodeSlots] = {};
bool s_loaded = false;

// CRC-32/ISO-HDLC。表を持たない実装（20 バイトしか通さないので速度は問題にならない）
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
    for (size_t i = 0; i < kEpisodeSlots; ++i) {
        s_records[i] = EpisodeProgress{FirstResult::None, 0, false, false};
    }
}

void encode(std::array<uint8_t, kBlobBytes> &out)
{
    out.fill(0);
    out[0] = kFormatVersion;
    out[1] = (uint8_t)kEpisodeSlots;
    for (size_t i = 0; i < kEpisodeSlots; ++i) {
        const size_t at = kHeaderBytes + kRecordBytes * i;
        out[at + 0] = (uint8_t)s_records[i].first_result;
        out[at + 1] = s_records[i].max_hint_level;
        out[at + 2] = (uint8_t)((s_records[i].story_completed ? kFlagStoryCompleted : 0) |
                                (s_records[i].collectible_owned ? kFlagCollectible : 0));
        out[at + 3] = 0;
    }
    const uint32_t crc = crc32(out.data(), kBlobBytes - 4);
    out[16] = (uint8_t)(crc & 0xFFu);
    out[17] = (uint8_t)((crc >> 8) & 0xFFu);
    out[18] = (uint8_t)((crc >> 16) & 0xFFu);
    out[19] = (uint8_t)((crc >> 24) & 0xFFu);
}

bool decode(const std::array<uint8_t, kBlobBytes> &in)
{
    if (in[0] != kFormatVersion || in[1] != (uint8_t)kEpisodeSlots) {
        return false;   // 版が違う。移行は管理者の明示的な作業で行う（設計書 14.3）
    }
    const uint32_t want = (uint32_t)in[16] | ((uint32_t)in[17] << 8) |
                          ((uint32_t)in[18] << 16) | ((uint32_t)in[19] << 24);
    if (crc32(in.data(), kBlobBytes - 4) != want) {
        return false;
    }
    for (size_t i = 0; i < kEpisodeSlots; ++i) {
        const size_t at = kHeaderBytes + kRecordBytes * i;
        const uint8_t result = in[at + 0];
        if (result > (uint8_t)FirstResult::GaveUp) {
            return false;   // 知らない値。壊れているとみなす
        }
        s_records[i].first_result = (FirstResult)result;
        s_records[i].max_hint_level = in[at + 1] > 2 ? 2 : in[at + 1];
        s_records[i].story_completed = (in[at + 2] & kFlagStoryCompleted) != 0;
        s_records[i].collectible_owned = (in[at + 2] & kFlagCollectible) != 0;
    }
    return true;
}

}  // namespace

bool load()
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
        Serial.println("[DET] progress blob has an unexpected length; starting over");
        return false;
    }
    const size_t read = prefs.getBytes(kNvsKey, bytes.data(), bytes.size());
    prefs.end();
    if (read != bytes.size() || !decode(bytes)) {
        clearAll();
        Serial.println("[DET] progress blob is broken; starting over");
        return false;
    }
    return true;
}

bool save()
{
    std::array<uint8_t, kBlobBytes> bytes{};
    encode(bytes);

    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) {
        Serial.println("[DET] cannot open NVS for writing");
        return false;
    }
    const size_t written = prefs.putBytes(kNvsKey, bytes.data(), bytes.size());
    std::array<uint8_t, kBlobBytes> back{};
    const size_t read = prefs.getBytes(kNvsKey, back.data(), back.size());
    prefs.end();
    // 初回の結果は二度と書き直さないので、確かに残ったことを読み戻して確かめる
    const bool ok = written == bytes.size() && read == bytes.size() && back == bytes;
    if (!ok) {
        Serial.println("[DET] progress write-back mismatch");
    }
    return ok;
}

const EpisodeProgress &get(size_t index)
{
    static const EpisodeProgress kEmpty{FirstResult::None, 0, false, false};
    if (index >= kEpisodeSlots) {
        return kEmpty;
    }
    if (!s_loaded) {
        load();
    }
    return s_records[index];
}

bool recordFirstResult(size_t index, FirstResult result)
{
    if (index >= kEpisodeSlots || result == FirstResult::None) {
        return false;
    }
    if (s_records[index].first_result != FirstResult::None) {
        return false;   // 復習。初回の記録は上書きしない
    }
    s_records[index].first_result = result;
    return true;
}

bool raiseHintLevel(size_t index, uint8_t level)
{
    if (index >= kEpisodeSlots || level > 2 || s_records[index].max_hint_level >= level) {
        return false;
    }
    s_records[index].max_hint_level = level;
    return true;
}

bool markStoryCompleted(size_t index)
{
    if (index >= kEpisodeSlots || s_records[index].story_completed) {
        return false;
    }
    s_records[index].story_completed = true;
    return true;
}

bool markCollectible(size_t index)
{
    if (index >= kEpisodeSlots || s_records[index].collectible_owned) {
        return false;   // 何度遊んでも重複しない（設計書 3.4）
    }
    s_records[index].collectible_owned = true;
    return true;
}

uint8_t points(size_t index)
{
    switch (get(index).first_result) {
    case FirstResult::Correct:          return coffee::det::content::kPointsUnassistedCorrect;
    case FirstResult::AssistedCorrect:  return coffee::det::content::kPointsAssistedCorrect;
    default:                            return coffee::det::content::kPointsIncorrect;
    }
}

uint8_t totalPoints()
{
    uint8_t total = 0;
    for (size_t i = 0; i < kEpisodeSlots; ++i) {
        total = (uint8_t)(total + points(i));
    }
    return total;
}

bool resetAll()
{
    clearAll();
    s_loaded = true;
    return save();
}

bool allCollectiblesOwned()
{
    for (size_t i = 0; i < kEpisodeSlots; ++i) {
        if (!get(i).collectible_owned) {
            return false;
        }
    }
    return true;
}

}  // namespace detective
