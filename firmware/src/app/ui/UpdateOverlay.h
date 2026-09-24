#pragma once

/**
 * ソフトの更新中（Wi-Fi 越し。RemoteConsole）に画面いっぱいに出す板。
 * 最前面（lv_layer_top）に置き、タッチも受け止める（更新中に ＋1 などを押させない）。
 * **どれも LVGL のロックを取ってから呼ぶ**。
 */
namespace ui {

void showUpdateOverlay();
void setUpdateProgress(int percent);
void setUpdateFailed();         // 失敗の知らせに切り替える（元のソフトのまま動く）
void hideUpdateOverlay();

}  // namespace ui
