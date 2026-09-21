#pragma once

#include <esp_display_panel.hpp>

/**
 * 画面の明るさ（バックライト）の持ち主。**バックライトを触ってよいのはここだけ**。
 *
 * - 設定の明るさ（10〜100%）を保持し、その場で反映する
 * - 触らない時間が続いたら暗くする（設定で 1/5/10/30 分・しない）。
 *   **暗い間の最初のタッチは「明るさを戻すだけ」**で、下のボタンは押されない
 *   （最前面に透明な板を置いて受け止める）
 * - **ゲーム画面が出ている間は自動で暗くしない**（読み物の途中や秘密の受け渡しのため）
 * - 人狼の覗き見防止（`WerewolfPort` の cutBacklight/restoreBacklight）はここを通す。
 *   戻すときは 100% ではなく**設定の明るさ**に戻す
 *
 * 明るさの実際の書き込みは 1 本のミューテックスで直列化する。
 * 人狼の見張りタスク（コア 0）と loop()（コア 1）の両方から呼ばれるため。
 */
namespace display {

void begin(esp_panel::board::Board *board);   // board->begin() の後・設定の読み込みの後に 1 回

// loop() から LVGL のロックを取った状態で呼ぶ（透明な板の出し入れに LVGL を触る）
void poll();

void applySettings();       // 設定の明るさ・暗くするまでの時間を読み直して反映する
void noteActivity();        // 操作があった（暗転までの時間を数え直す）
bool dimmed();              // 今、自動で暗くなっているか

void setGameActive(bool active);   // ゲーム画面の生成／破棄で呼ぶ

// --- 人狼の覗き見防止 -------------------------------------------------------
void privacyCut();          // 明かりを完全に消す（設定より優先）
void privacyRestore();      // 設定の明るさに戻す（100% にはしない）
bool privacyCutActive();

}  // namespace display
