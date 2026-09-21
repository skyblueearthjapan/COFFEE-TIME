#pragma once

#include <stdint.h>

#include <lvgl.h>

/**
 * 「設定」（docs/MENU_DESIGN.md §2 の 8 項目・2 ページ）と、その子画面。
 *
 * 保存（NVS）は「決定」を押したときだけ行う。書き込み中は画面が一瞬止まるので、
 * タイマーやスライダーを動かしている最中には書かない。
 */
namespace ui {

enum class SettingsSub : uint8_t {
    Brightness,     // 画面の明るさ
    DimTimeout,     // 画面を暗くする
    TimeSet,        // 時刻を合わせる
    Sound,          // 操作音
    MaxCups,        // 1 回に作る杯数
    Morning,        // 朝いちばんの残り
    Wifi,           // Wi-Fi（状態の表示のみ）
    SystemInfo,     // システム情報
};

lv_obj_t *createSettingsScreen();       // 一覧（2 ページ）
lv_obj_t *createSettingsSubScreen();    // 子画面（pushSettingsSub が種類を決める）

// 子画面を開く。開発用のシリアルコマンドからも呼ぶ
void pushSettingsSub(SettingsSub which);

}  // namespace ui
