#include "DuelStore.h"

#include <Arduino.h>
#include <Preferences.h>

#include <array>

namespace duel {
namespace {

namespace core = coffee::duel;

constexpr const char *kNvsNamespace = "ct_duel";
constexpr const char *kMetaKey = "meta";
constexpr uint8_t kMetaVersion = 1;
constexpr size_t kMetaBytes = 8;        // 版 1 + 予約 1 + 通し番号 2 + CRC 4

core::Stats s_profiles[kProfileSlots];
core::Stats s_scratch;                  // 範囲外のときの捨て場所（どこにも保存しない）
uint16_t s_match_seq = 0;
bool s_loaded = false;

// キーは "p0" 〜 "p7"。NVS のキーは 15 文字までなので短くしてある
const char *profileKey(size_t slot)
{
    static const char *const kKeys[kProfileSlots] = {"p0", "p1", "p2", "p3",
                                                     "p4", "p5", "p6", "p7"};
    return kKeys[slot];
}

void encodeMeta(std::array<uint8_t, kMetaBytes> &out)
{
    out.fill(0);
    out[0] = kMetaVersion;
    out[2] = (uint8_t)(s_match_seq & 0xFFu);
    out[3] = (uint8_t)((s_match_seq >> 8) & 0xFFu);
    const uint32_t crc = core::detail::crc32(out.data(), kMetaBytes - 4);
    core::detail::put32(out.data() + kMetaBytes - 4, crc);
}

bool decodeMeta(const std::array<uint8_t, kMetaBytes> &in)
{
    if (in[0] != kMetaVersion) {
        return false;
    }
    if (core::detail::get32(in.data() + kMetaBytes - 4) !=
        core::detail::crc32(in.data(), kMetaBytes - 4)) {
        return false;
    }
    s_match_seq = (uint16_t)((uint16_t)in[2] | ((uint16_t)in[3] << 8));
    return true;
}

bool saveMeta()
{
    std::array<uint8_t, kMetaBytes> bytes{};
    encodeMeta(bytes);
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) {
        Serial.println("[DUEL] cannot open NVS for writing (meta)");
        return false;
    }
    const size_t written = prefs.putBytes(kMetaKey, bytes.data(), bytes.size());
    prefs.end();
    return written == bytes.size();
}

// 1 人ぶんを書いて読み戻す。中身までそろっていなければ false
bool writeProfile(size_t slot)
{
    static uint8_t blob[core::kBlobBytes];
    static uint8_t back[core::kBlobBytes];
    core::encodeStats(s_profiles[slot], blob);

    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) {
        Serial.println("[DUEL] cannot open NVS for writing");
        return false;
    }
    const size_t written = prefs.putBytes(profileKey(slot), blob, core::kBlobBytes);
    const size_t read = prefs.getBytes(profileKey(slot), back, core::kBlobBytes);
    prefs.end();
    const bool ok = written == core::kBlobBytes && read == core::kBlobBytes &&
                    memcmp(blob, back, core::kBlobBytes) == 0;
    if (!ok) {
        Serial.printf("[DUEL] p%u write-back mismatch\n", (unsigned)slot);
    }
    return ok;
}

}  // namespace

bool load()
{
    for (size_t i = 0; i < kProfileSlots; ++i) {
        s_profiles[i] = core::Stats{};
    }
    s_match_seq = 0;
    s_loaded = true;

    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, true)) {
        // 名前空間がまだ無い＝一度も遊んでいない。異常ではない
        return true;
    }
    bool all_ok = true;
    static uint8_t blob[core::kBlobBytes];
    for (size_t i = 0; i < kProfileSlots; ++i) {
        const size_t length = prefs.getBytesLength(profileKey(i));
        if (length == 0) {
            continue;       // その席はまだ誰も使っていない
        }
        if (length != core::kBlobBytes ||
            prefs.getBytes(profileKey(i), blob, core::kBlobBytes) != core::kBlobBytes ||
            !core::decodeStats(blob, s_profiles[i])) {
            // 版数違い・長さ違い・CRC 違い。その人だけ空の統計として始める
            // （消しはしない。次の保存で新しい形式に置き換わる）
            s_profiles[i] = core::Stats{};
            Serial.printf("[DUEL] p%u stats blob is broken or old; starting over\n", (unsigned)i);
            all_ok = false;
        }
    }
    const size_t meta_length = prefs.getBytesLength(kMetaKey);
    if (meta_length == kMetaBytes) {
        std::array<uint8_t, kMetaBytes> meta{};
        if (prefs.getBytes(kMetaKey, meta.data(), meta.size()) != meta.size() ||
            !decodeMeta(meta)) {
            s_match_seq = 0;
            Serial.println("[DUEL] meta is broken; the match number starts over");
            all_ok = false;
        }
    } else if (meta_length != 0) {
        all_ok = false;
    }
    prefs.end();
    return all_ok;
}

const coffee::duel::Stats &stats(size_t slot)
{
    static const core::Stats kEmpty{};
    if (slot >= kProfileSlots) {
        return kEmpty;
    }
    if (!s_loaded) {
        load();
    }
    return s_profiles[slot];
}

coffee::duel::Stats &mutableStats(size_t slot)
{
    if (slot >= kProfileSlots) {
        return s_scratch;
    }
    if (!s_loaded) {
        load();
    }
    return s_profiles[slot];
}

bool saveProfile(size_t slot)
{
    if (slot >= kProfileSlots) {
        return true;        // ゲストは保存しない（計画 §3）
    }
    if (!s_loaded) {
        load();
    }
    if (writeProfile(slot)) {
        return true;
    }
    // 1 回だけやり直す。それでもだめならフラッシュを読み直して、
    // メモリー上だけ進んだ状態を残さない（探偵・エスパーと同じ考え方）
    if (writeProfile(slot)) {
        return true;
    }
    Serial.println("[DUEL] stats save failed, reloading from flash");
    load();
    return false;
}

bool resetProfile(size_t slot)
{
    if (slot >= kProfileSlots) {
        return false;
    }
    if (!s_loaded) {
        load();
    }
    s_profiles[slot] = core::Stats{};
    return saveProfile(slot);
}

uint16_t newMatchId()
{
    if (!s_loaded) {
        load();
    }
    ++s_match_seq;
    if (s_match_seq == 0) {
        s_match_seq = 1;    // 0 は「対戦していない」印に使うので飛ばす
    }
    if (!saveMeta()) {
        // 番号は RAM 上では進むが保存できていない。次の起動で同じ番号が出る可能性がある
        // （遷移の数え方は match_id が同じかどうかで決まるので、古い記録と混ざりうる）
        Serial.printf("[DUEL] meta save failed; the match number %u may repeat after a restart\n",
                      (unsigned)s_match_seq);
    }
    return s_match_seq;
}

uint16_t lastMatchId()
{
    if (!s_loaded) {
        load();
    }
    return s_match_seq;
}

}  // namespace duel
