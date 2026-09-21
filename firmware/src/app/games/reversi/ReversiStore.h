#pragma once

#include <cstddef>
#include <cstdint>

#include "core/reversi_extra.hpp"

/**
 * JEV REVERSI の「この端末だけ」の記録（計画 §4）。
 *
 * 保存先は NVS 名前空間 `ct_rev`:
 *   game  … 進行中・直前の 1 局（設計一式の `REV1` 形式・最大 294 バイト）
 *   stats … 盤サイズ 2 × 相手区分 5（jev / jev_pro / casual / 端末AI / 混在）× 勝・負・分
 *
 * **個人情報は入らない。** 盤面と棋譜は公開情報なので NVS に置いてよい
 * （秘密の役職を RAM だけに置く人狼とは用途が違う。設計書 11.1）。
 *
 * 一手ごとの保存は「書いて → 読み戻して → 復元して照合」まで成功して初めて
 * 保存できたことにする（設計書 5.4 / 11.2）。**保存に成功してから画面を確定表示**に
 * するので、電源が切れても「画面に出たのに残っていない手」は生まれない。
 * 失敗したら 1 回だけやり直し、それでもだめならフラッシュを読み直す
 * （DuelStore・DetectiveProgress と同じ作法）。
 *
 * 設計書 11.2 は slot0 / slot1 の二世代を勧めているが、この端末では
 *   - 再開できる局は 1 つだけ（計画 §4）
 *   - 書き込みのたびに読み戻して復元まで確かめる
 *   - 復元できない塊は「保存なし」として捨て、古い局へ黙って戻らない
 * という方針にして 1 スロットにした（計画 §2 の「作らないもの」と同じ考え方）。
 *
 * **NVS への書き込みは画面を一瞬止める**ので、1 手につき 1 回だけにすること。
 */
namespace reversi {
namespace store {

namespace rev = coffee::rev;

// --- 進行中の 1 局 ----------------------------------------------------------

// 保存されている局を読む。塊が無い・壊れている・復元できないときは false
// （その場合 out は触らない）
bool loadGame(ct_rev::Session &out);

// 読める局があり、まだ終わっていない（closure == Active）か
bool hasResumableGame();

// 1 局を保存して読み戻す。復元して棋譜まで一致したときだけ true
bool saveGame(const ct_rev::Session &s);

// 保存中の局を消す（新しい局を始める前・壊れていたとき）
void clearGame();

// --- 勝敗のきろく -----------------------------------------------------------

constexpr size_t kSizeCount = 2;        // 0 = 6×6, 1 = 8×8

struct Record {
    uint16_t counts[kSizeCount][rev::kOpponentCount][rev::kOutcomeCount];
};

// NVS から読む（無ければ全部 0 で true。壊れていたら全部 0 で false）
bool loadStats();

const Record &stats();

// 1 局ぶん数えて保存する（completed と resigned だけ。aborted は数えない）
bool noteResult(uint8_t n, rev::Opponent opponent, rev::Outcome outcome);

// 盤サイズごとの通算（相手区分をまとめた合計）。画面の「きろく」用
void totals(uint8_t n, uint32_t &win, uint32_t &loss, uint32_t &draw);

}  // namespace store
}  // namespace reversi
