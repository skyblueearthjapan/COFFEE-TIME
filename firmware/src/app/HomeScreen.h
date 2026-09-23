#pragma once

#include "NetService.h"

/**
 * HOME 画面（時計・日付・天気・+1・本日杯数・残り杯数）。
 * すべて LVGL のロックを取った状態で呼ぶこと。
 */
namespace home {

bool create();                              // 1 回だけ呼ぶ
void setWeather(const net::Weather &w);

// コーヒーを 1 杯記録する（HOME の「+1」を押したときと同じ。記録も通知も同じ経路）。
// ゲーム中の一時停止メニューなど、HOME 以外の画面からも使う
void addOneCup();

void debugTake();                          // 開発用：+1 と同じ処理
void debugRefill();                        // 開発用：LEFT 長押しと同じ処理
// 開発用：誤って入った今日の 1 杯を取り消す（シリアルの "Y!undo" からだけ呼ぶ）。
// SD の操作ログには残すが、**GAS（シート）へは何も送らない**（シートの行は手で消す）
bool debugUndoCup();
void debugForceHour(int hour);            // 開発用：背景の時間帯を固定（-1 で解除）

}  // namespace home
