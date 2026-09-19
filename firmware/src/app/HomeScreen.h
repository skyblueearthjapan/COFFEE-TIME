#pragma once

#include "NetService.h"

/**
 * HOME 画面（時計・日付・天気・+1・本日杯数・残り杯数）。
 * すべて LVGL のロックを取った状態で呼ぶこと。
 */
namespace home {

bool create();                              // 1 回だけ呼ぶ
void setWeather(const net::Weather &w);
void debugTake();                          // 開発用：+1 と同じ処理
void debugRefill();                        // 開発用：LEFT 長押しと同じ処理
void debugForceHour(int hour);            // 開発用：背景の時間帯を固定（-1 で解除）

}  // namespace home
