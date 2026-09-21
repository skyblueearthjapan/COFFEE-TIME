#pragma once

#include <lvgl.h>

/**
 * 「履歴」（docs/MENU_DESIGN.md §2）。
 *   グラフ: この 1 週間 / 30 日（切り替え）と 1 日の平均
 *   一覧  : 日付・杯数・補充回数を 5 件ずつページ送り
 * 記録の元は NVS の `cup_hist/days`（直近 35 日）。SD も Wi-Fi も使わない。
 */
namespace ui {

lv_obj_t *createHistoryScreen();        // グラフ
lv_obj_t *createHistoryListScreen();    // 日ごとの一覧

}  // namespace ui
