#pragma once

#include <lvgl.h>

/**
 * POKER TABLE（原本の CAFE CARDS）。トランプ 4 種を 1 つの入口から遊ぶ。
 * 入口 → テーブル選び → 本人 → 種類 → 相手 → 対戦 → 結果までを 1 枚の画面で進める
 * （JEV REVERSI・AI DUEL と同じ作り）。
 */
namespace cards {

lv_obj_t *createGameScreen();

// いま画面に私的な札（ポーカー・31 の手札と、そこから作った役・得点の文字）が出ているか。
// 出ている間は開発用のスクリーンショットと画面切替を断る（人狼の秘密と同じ扱い）
bool privateOnScreen();

// 開発用（シリアル 'K'）。**手札と、公開前の相手の選択は絶対に出さない**
void debugPrintPublicState();

}  // namespace cards
