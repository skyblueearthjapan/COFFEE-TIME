#pragma once

#include <cstddef>
#include <cstdint>

#include "core/duel_core.hpp"

/**
 * AI DUEL の「この端末だけ」の個人記録（計画 §4）。
 *
 * 設計書はスプレッドシートを正本にしているが、この端末は持ち運びで Wi-Fi が
 * 無い日が多いので**端末 (NVS) を正本**にした。オンラインのときは同じ内容を
 * GAS 経由でシートへ 1 行ずつ写すだけ（写せなくても遊びは成立する）。
 *
 * 保存先は NVS 名前空間 `ct_duel`:
 *   p0 〜 p7 … アイコン 1 つぶんの統計（core/duel_core.hpp の 488 バイトの塊）
 *   meta     … 対戦通し番号など（8 バイト）
 *
 * **個人情報は入らない。** アイコンの席番号と手・勝敗の回数だけで、名前も PIN も
 * 時刻も持たない。ゲストは RAM だけで遊ぶのでここには一切書かない。
 *
 * 保存は 1 ラウンド確定ごと（電源が切れても確定済みは残る）。書き込みは必ず
 * 読み戻して照合し、失敗したらもう一度だけ試してからフラッシュを読み直す
 * （探偵の進み具合・エスパーのきろくと同じ作法）。
 * 読み込み失敗・版数違いは「記録なし」として扱う。消しはしないので、
 * 次の保存で新しい形式に置き換わる。
 *
 * **NVS への書き込みは画面を一瞬止める**ので、1 ラウンドに 1 回だけにすること。
 */
namespace duel {

constexpr size_t kProfileSlots = 8;     // アイコン 8 つ（計画 §3）

// NVS から全員ぶんを読む。まだ無い場合は全部空にして true を返す（異常ではない）。
// どれかが壊れていた場合、その人だけ空にして false を返す
bool load();

const coffee::duel::Stats &stats(size_t slot);

// 1 ラウンド反映するために書き換える実体。範囲外のときは捨て場所を返す
coffee::duel::Stats &mutableStats(size_t slot);

// その人ぶんを NVS へ書いて読み戻す。一致しなければ false
bool saveProfile(size_t slot);

// その人の記録だけ消して保存する（プロフィール画面の「記録を消す」）
bool resetProfile(size_t slot);

// 対戦通し番号を 1 つ払い出す（設計書の 32 桁 ID の代わり。0 は使わない）。
// meta も一緒に保存するので、電源を切っても番号は戻らない
uint16_t newMatchId();

uint16_t lastMatchId();

}  // namespace duel
