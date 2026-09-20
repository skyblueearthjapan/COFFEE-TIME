#pragma once

#include <lvgl.h>

/**
 * 丸型 480x480 画面の共通部品。
 * 表示は半径 228px の円内に収める（角は物理的に見えない）。
 */
namespace ui {

// カフェ風パレット（HOME 画面と共通）
#define CT_COLOR_BG        lv_color_hex(0x14100D)
#define CT_COLOR_PANEL     lv_color_hex(0x241A13)
#define CT_COLOR_TEXT      lv_color_hex(0xF5EDE3)
#define CT_COLOR_SUBTEXT   lv_color_hex(0xB9A58F)
#define CT_COLOR_DIM       lv_color_hex(0x5C4E42)
#define CT_COLOR_ACCENT    lv_color_hex(0x7A4A2A)
#define CT_COLOR_ACCENT_HI lv_color_hex(0xC08A5B)
#define CT_COLOR_WARN      lv_color_hex(0xE0A040)
#define CT_COLOR_ALERT     lv_color_hex(0xD9534F)

constexpr int kScreenSize = 480;
constexpr int kSafeRadius = 228;        // この円の内側に収める

lv_obj_t *makeScreen();                                  // 背景色つきの空の画面
lv_obj_t *makeTitle(lv_obj_t *parent, const char *text);  // 上部見出し
lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text);

// 画面下部に戻るボタン（押すと 1 つ前の画面へ）
lv_obj_t *makeBackButton(lv_obj_t *parent, const char *text = "もどる");

// 一時メッセージ（同じ画面に重複して出さない。2 秒で消える）
void showToast(lv_obj_t *parent, const char *text);

// 横長のメニュー項目。cb が nullptr なら「準備中」の見た目（押せない）
lv_obj_t *makeMenuItem(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data,
                       int y_offset, bool enabled = true);

}  // namespace ui
