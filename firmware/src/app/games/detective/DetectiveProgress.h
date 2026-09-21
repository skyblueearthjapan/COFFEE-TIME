#pragma once

#include <cstddef>
#include <cstdint>

/**
 * 探偵「喫茶『余白』の事件簿」のローカル進み具合（この端末だけの記録）。
 *
 * 保存先は NVS の名前空間 `ct_det`・キー `prog` に置く 20 バイトの塊 1 つだけ。
 * 個人の名前・時刻・読んだ順番・迷った時間は一切入れない（設計書 12.2）。
 * 通信もしないし SD にも書かない（STORAGE_POLICY.md の「設定・少量・頻繁 → フラッシュ」）。
 *
 * 塊の並び（合計 20 バイト。数値はリトルエンディアン）:
 *
 *   0      : format_version = 1
 *   1      : episode_slots = 3
 *   2..3   : 予約（0）
 *   4+4*i  : first_result   （FirstResult。i = 話の番号 0..2）
 *   5+4*i  : max_hint_level （これまでに見た最大のヒント段階 0..2）
 *   6+4*i  : flags          （bit0: 結末まで読んだ / bit1: 記念の品を持っている）
 *   7+4*i  : 予約（0）
 *   16..19 : crc32（0..15 バイトに対する CRC-32/ISO-HDLC）
 *
 * 版番号・長さ・CRC のどれかが合わなければ「記録なし」として作り直す。
 * 書き込みは必ず読み戻して照合する（人狼の writePublicMetaAndReadBack と同じ考え方）。
 */
namespace detective {

// 1 話の「初めての結果」。復習では決して上書きしない（設計書 3.4）
enum class FirstResult : uint8_t {
    None = 0,             // まだ答えを出していない
    Correct = 1,          // ヒントなしで正解（2 点）
    Incorrect = 2,        // 不正解（0 点）
    AssistedCorrect = 3,  // ヒントを読んでから正解（1 点）
    GaveUp = 4,           // 「答えとつづきを読む」（0 点）
};

struct EpisodeProgress {
    FirstResult first_result;
    uint8_t max_hint_level;    // 0..2。戻る操作でも下がらない
    bool story_completed;      // 結末の最後まで読んだ
    bool collectible_owned;    // 記念の品を手帳に入れた（重複しない）
};

constexpr size_t kEpisodeSlots = 3;

// NVS から読む。まだ無い場合は全部 None にして true を返す（異常ではない）。
// 壊れていた場合も全部 None にするが false を返す
bool load();

// NVS へ書いて読み戻す。一致しなければ false
bool save();

const EpisodeProgress &get(size_t index);

// 初回の結果だけを記録する（すでに None 以外なら何もしない）。変化したら true
bool recordFirstResult(size_t index, FirstResult result);
bool raiseHintLevel(size_t index, uint8_t level);
bool markStoryCompleted(size_t index);
bool markCollectible(size_t index);

uint8_t points(size_t index);     // 初回の結果だけを 2 / 1 / 0 で数える
uint8_t totalPoints();            // 章の最大は 6。公開順位は作らない
bool allCollectiblesOwned();

// 開発・試験用：全話を「初見・未読」に戻して保存する（シリアルコマンド 'X'）。
// 試験で遊ぶたびに利用者の初回記録が消費されてしまうのを避けるためのもの
bool resetAll();

}  // namespace detective
