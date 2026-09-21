#pragma once

#include <Arduino.h>
#include <esp_display_panel.hpp>

// microSD への操作ログ（docs/STORAGE_POLICY.md）。
// SD が無い・抜けた・壊れている場合は黙って捨て、本体の動作は止めない。
namespace sdlog {

// board->begin() の後に呼ぶこと（GPIO1・2 を LCD の初期化と共用しているため）
void begin(esp_panel::board::Board *board);

// 記録をキューに積むだけで、SD には触らない（どのタスクから呼んでもよい・待たない）
void event(const char *name, uint32_t taken, uint32_t left, uint32_t prev, const char *note = "");

// loop() から呼ぶ。キューの中身を SD に書く（LVGL のロックの外で呼ぶこと）
void poll();

bool mounted();

// 「システム情報」画面用。カードが無ければ 0（SD の読み取りは loop 側から呼ぶこと）
uint64_t freeBytes();

// 開発用：今月のログの末尾をシリアルに出す
void dumpTail(Stream &out, size_t max_bytes = 2048);

}  // namespace sdlog
