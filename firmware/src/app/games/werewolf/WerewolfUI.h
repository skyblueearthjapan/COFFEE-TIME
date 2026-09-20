#pragma once

#include <lvgl.h>

/**
 * 閉店後の人狼会（CAFE WEREWOLF v2.0）の画面。
 * ロジックは core/werewolf_core.hpp（ホスト検証済み・無改変）を使う。
 */
namespace werewolf {

lv_obj_t *createEntryScreen();    // ゲーム一覧（メニューの「ゲーム」から開く）
lv_obj_t *createLobbyScreen();    // 人数を決める

uint8_t selectedPlayers();        // 直近に選ばれた人数（3〜10）

}  // namespace werewolf
