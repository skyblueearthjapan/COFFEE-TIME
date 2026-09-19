#pragma once

#include <stdint.h>

/**
 * コーヒーの杯数状態。
 * 変更は LVGL のロックを取った状態（LVGL タスク内、または lvgl_port_lock 中）でのみ行う。
 */
namespace cup {

constexpr uint32_t kMaxCups = 10;   // 1 回に作る最大杯数（残り杯数の上限）

void load();                        // NVS から読み込む（起動時に 1 回）
void saveIfDirty();                 // 変更があれば NVS に保存（loop から呼ぶ）

void takeOne();                     // +1：飲んだ杯数を増やし、残りを減らす
void refill();                      // 作った：残りを上限に戻す
void checkNewDay(uint32_t ymd);     // 日付が変わっていれば本日分をリセット

uint32_t taken();                   // 本日飲まれた杯数
uint32_t remaining();               // 残り杯数

}  // namespace cup
