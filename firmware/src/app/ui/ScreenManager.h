#pragma once

#include <lvgl.h>

/**
 * 画面の切り替えを一元管理する。
 * すべて LVGL のロックを取った状態（LVGL タスク内のイベント、または lvgl_port_lock 中）で呼ぶこと。
 *
 * 各画面は「開くときに自分の lv_obj を作り、閉じるときに破棄する」方式。
 * 丸型 480x480 のため、画面の中身は半径 228px の円内に収める。
 */
namespace ui {

// 画面を作る関数。呼ばれたら新しい screen オブジェクトを返す
using ScreenFactory = lv_obj_t *(*)();

void begin(lv_obj_t *home_screen);      // HOME を最初の画面として登録する
void push(ScreenFactory factory);       // 画面を開く（戻り先として現在の画面を覚える）
void pop();                             // 1 つ前の画面へ戻る
void goHome();                          // HOME まで一気に戻る（ゲーム中の「カフェへ」導線）
bool isHome();
int depth();        // 重なっている画面の数（1 = HOME だけ、2 = メニューやゲーム一覧など 1 枚上）。開発用の状態表示に使う

}  // namespace ui
