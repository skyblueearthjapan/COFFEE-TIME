#pragma once

#include <lvgl.h>

/**
 * 「今日の状況」（docs/MENU_DESIGN.md §2）。
 *   1 枚目: 大きな杯数・カップのゲージ・進み具合・残り杯数・最後の 1 杯／補充の時刻
 *   2 枚目: 今日の時間帯別の棒グラフ
 * どちらも ui::push() で開く。SD カードも Wi-Fi も無い状態で動くこと。
 */
namespace ui {

lv_obj_t *createTodayScreen();          // 1 枚目
lv_obj_t *createTodayHourlyScreen();    // 2 枚目（時間ごと）

}  // namespace ui
