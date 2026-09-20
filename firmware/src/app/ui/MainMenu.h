#pragma once

#include <lvgl.h>

/**
 * HOME から開くメニュー。項目の並びは設計書に合わせる（ゲームは 4 番目）。
 */
namespace ui {

lv_obj_t *createMainMenu();     // ScreenManager::push に渡す

}  // namespace ui
