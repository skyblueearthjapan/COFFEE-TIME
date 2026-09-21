#pragma once

#include <lvgl.h>

/**
 * 閉店後の人狼会（CAFE WEREWOLF v2.0）の本体。
 *
 * 人数決め → 必読 → 配役 → 夜（役職確認・対象選択・結果）→ 昼（議論）→ 投票 →
 * 決選 → 結果 までを、LVGL の画面 1 枚の中身を作り替えて進める。
 * 進行の判断はすべて core/werewolf_core.hpp の Engine が持ち、ここは表示と入力だけを担う。
 *
 * 秘密（役職・占い結果・投票先）は RAM にしか存在しない。
 * NVS・SD・シリアル・通信のどこへも出さない。
 */
namespace werewolf {

// ゲーム画面を作る（ui::push に渡す）。人数決めから結果まで、この 1 枚で進む
lv_obj_t *createGameScreen();

// 進行中の局があるか。画面を離れると局は無効になる
bool gameActive();

// 秘密（役職・占い結果・投票先）が今この瞬間パネルに映っているか。
// 開発用のシリアルコマンドは、これが true の間は画面を送ったり切り替えたりしない
bool secretOnScreen();

// 開発用：今の場面を確かめるためにシリアルへ 1 行出す。
// 出すのは公開情報（場面・手番・人数・残り時間）だけ。役職・占い結果・投票先は決して出さない
void debugPrintPublicState();

}  // namespace werewolf
