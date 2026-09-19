#pragma once

#include "NetService.h"

/**
 * HOME 画面（時計・日付・天気・+1・本日杯数・残り杯数）。
 * すべて LVGL のロックを取った状態で呼ぶこと。
 */
namespace home {

bool create();                              // 1 回だけ呼ぶ
void setWeather(const net::Weather &w);

}  // namespace home
