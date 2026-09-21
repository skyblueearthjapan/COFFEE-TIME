#pragma once

#include <cstddef>
#include <cstdint>

/**
 * エスパー対決の「この端末だけ」の記録（設計書 6.3・8.2・11）。
 *
 * 段階 1 は通信しないので、中央集計へは何も送らない。端末の中だけで
 * モード別に AI の勝ち／人の勝ち／当てたときの最少問数を数える。
 *
 * 保存先は NVS の名前空間 `ct_esp`・キー `stat` に置く 32 バイトの塊 1 つだけ。
 * 個人の名前・時刻・頭の中の答え・回答の中身は**一切入れない**（設計書 10）。
 * 通信もしないし SD にも書かない（STORAGE_POLICY.md の「設定・少量・頻繁 → フラッシュ」）。
 *
 * 塊の並び（合計 32 バイト。数値はリトルエンディアン）:
 *
 *   0      : format_version = 1
 *   1      : mode_slots = 3
 *   2..3   : 予約（0）
 *   4+8*m  : ai_win        （uint16。m = モード 0..2）
 *   6+8*m  : human_win     （uint16）
 *   8+8*m  : best_questions（当てたときの最少問数。0 = まだ無い）
 *   9..11+8*m : 予約（0）
 *   28..31 : crc32（0..27 バイトに対する CRC-32/ISO-HDLC）
 *
 * 版番号・長さ・CRC のどれかが合わなければ「記録なし」として作り直す。
 * CRC は破損検出用で、署名や改ざん対策ではない（設計書 8.2 の明記事項）。
 * 書き込みは必ず読み戻して照合する（探偵ゲームと同じ考え方）。
 *
 * **NVS への書き込みは画面を止める**ので、遊び終わって結果が確定した 1 回だけ書く。
 */
namespace esper {

constexpr size_t kModeSlots = 3;

struct ModeStats {
    uint16_t ai_win;
    uint16_t human_win;
    uint8_t best_questions;   // 当てたときの最少問数。0 = まだ記録なし
};

// NVS から読む。まだ無い場合は全部 0 にして true を返す（異常ではない）。
// 壊れていた場合も全部 0 にするが false を返す
bool loadStats();

// NVS へ書いて読み戻す。一致しなければ false
bool saveStats();

const ModeStats &stats(size_t mode);

uint32_t totalPlays();      // 全モードの ai_win + human_win

// 1 局が確定したときに呼ぶ（RAM 上の数だけ更新する。保存は saveStats）
void recordResult(size_t mode, bool ai_win, uint8_t questions);

// 開発・試験用：記録を全部消して保存する（シリアルコマンド 'Y'）
bool resetStats();

}  // namespace esper
