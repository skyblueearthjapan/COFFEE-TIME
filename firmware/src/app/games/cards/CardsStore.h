#pragma once

#include <cstddef>
#include <cstdint>

/**
 * POKER TABLE（CAFE CARDS）の「この端末だけ」のきろく（計画 §4）。
 *
 * 保存先は NVS 名前空間 `ct_cards`、キーは `stats` **だけ**:
 *   アイコン 8 人 × ゲーム 4 種 ×（勝・負・分・中止）＋ バカラの的中数
 *
 * **試合の途中経過（山札・手札・イベント）は NVS にも SD にもシリアルにも書かない。**
 * 手札は共有端末の私的情報で、この端末には本人確認の仕組みが無いため（計画 §2）。
 * 途中の試合は CardsGame が PSRAM に持ち、電源が切れたら消える。
 *
 * ゲスト（スロット 8）は RAM だけ。電源を切ると消えるし、NVS にも書かない。
 * 書き込みは DuelStore・ReversiStore と同じ作法で「書いて読み戻して照合」し、
 * 2 回失敗したらフラッシュを読み直して RAM だけ進んだ状態を残さない。
 */
namespace cards {
namespace store {

constexpr size_t kSlots = 8;        // アイコン 8 人（NVS に残る）
constexpr size_t kGuestSlot = 8;    // ゲスト（RAM だけ）
constexpr size_t kGames = 4;        // POKER / GOPS / THIRTY-ONE / BACCARAT
constexpr size_t kOutcomes = 4;     // 勝 / 負 / 分 / 中止

enum class Outcome : uint8_t { Win = 0, Loss = 1, Draw = 2, Aborted = 3 };

struct Record {
    uint16_t counts[kSlots + 1][kGames][kOutcomes];
    uint32_t bac_hits[kSlots + 1];      // バカラの的中の累計（本人ぶんだけ）
};

// NVS から読む（無ければ全部 0 で true。壊れていたら全部 0 で false）
bool loadStats();

// --- はじめての説明を見たか（キー `seen`。8 バイト・版と CRC つき）---------
//
// **`stats` の塊とは別のキー**にしてある（勝敗の形は 1 バイトも変えない）。
// 4 ゲームぶんの 1 ビットずつだけを持つ: bit0 POKER / bit1 GOPS /
// bit2 THIRTY-ONE / bit3 BACCARAT。ゲストと本人は区別しない（端末の設定と同じ扱い）

bool loadSeen();                    // 画面を開いたときに 1 回読む
uint8_t seenFlags();                // 4 ビットぶん
bool seenFlag(size_t game);         // その卓の説明をもう見たか
bool markSeen(size_t game);         // 「次回から表示しない」で立てる
bool clearSeen();                   // 入口の「説明をもう一度」で全部おろす

const Record &stats();

// 1 試合ぶん数えて保存する。slot が kGuestSlot 以上なら RAM だけ（NVS は触らない）。
// bac_hits は BACCARAT のときだけ足す的中数（ほかのゲームは 0）
bool noteResult(size_t slot, size_t game, Outcome outcome, uint32_t bac_hits);

// 画面の「きろく」用
void totals(size_t slot, size_t game, uint32_t &win, uint32_t &loss, uint32_t &draw,
            uint32_t &aborted);

}  // namespace store
}  // namespace cards
