#pragma once

#include <esp_display_panel.hpp>

/**
 * 操作音（ブザー）。
 *
 * ブザーは IO エキスパンダー (TCA9554) の **EXIO8**（ライブラリ上のピン番号 7）につながっている。
 * I2C 越しなので音程を変える PWM はできない。「短く鳴らす」だけを行う。
 *
 * 鳴らす場所はメニュー・今日の状況・履歴・設定の各ボタンだけ。**ゲーム中は絶対に鳴らさない**
 * （人狼の秘密の受け渡しで音が手掛かりになるため）。既定はオフ（`settings::sound()`）。
 *
 * LVGL のタスクを止めないよう、鳴らし始めるだけで戻り、止めるのは一発タイマーに任せる。
 */
namespace buzzer {

void begin(esp_panel::board::Board *board);   // board->begin() の後に 1 回

// 短いクリック音。設定がオフなら何もしない。どの画面からでも呼べるが、
// 呼ぶのは公開画面（メニュー系）のボタンだけにすること
void click();

}  // namespace buzzer
