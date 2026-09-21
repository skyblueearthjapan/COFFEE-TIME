#pragma once

#include <lvgl.h>

/**
 * ゲーム一覧の画面。
 * 人狼の本体（人数決め〜結果）は WerewolfGame.h の createGameScreen()。
 */
namespace werewolf {

lv_obj_t *createEntryScreen();    // ゲーム一覧（メニューの「ゲーム」から開く）

}  // namespace werewolf
