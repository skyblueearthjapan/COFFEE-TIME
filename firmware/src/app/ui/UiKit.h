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

// 座標で置く部品の枠。ゲーム画面と同じ考え方（丸い画面なので中央そろえでは足りない）
struct Rect {
    int16_t x, y, w, h;
};

lv_obj_t *makeScreen();                                  // 背景色つきの空の画面
lv_obj_t *makeTitle(lv_obj_t *parent, const char *text);  // 上部見出し（大きい字）
lv_obj_t *makeHeading(lv_obj_t *parent, const char *text);   // 中身の詰まった画面用の見出し
lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text);

// --- 座標で置く部品（メニューの中身の画面で使う）---------------------------
// 日本語は自動折り返しされないので、改行は文言側に入れ、幅ではみ出しを切る
lv_obj_t *makeRectLabel(lv_obj_t *parent, const Rect &r, const lv_font_t *font, lv_color_t color,
                        const char *text, lv_text_align_t align = LV_TEXT_ALIGN_CENTER);

// 角丸の板（設定の 1 行・グラフの下敷きなど。押せない）
lv_obj_t *makePanel(lv_obj_t *parent, const Rect &r, lv_color_t color, int radius = 14);

// 座標で置くボタン。**設定がオンなら操作音を鳴らす**（ゲーム画面では使わないこと）
lv_obj_t *makeRectButton(lv_obj_t *parent, const Rect &r, const char *text, lv_event_cb_t cb,
                         void *user_data, bool enabled = true, bool primary = false);

// 「もどる」。makeBackButton と同じ位置・見た目で、操作音だけ足したもの
lv_obj_t *makeMenuBackButton(lv_obj_t *parent, const char *text = "もどる");

// 棒グラフの 1 本。cx を中心に、baseline を下端として高さ h の棒を立てる
lv_obj_t *makeBar(lv_obj_t *parent, int16_t cx, int16_t baseline, int16_t w, int16_t h,
                  lv_color_t color);

// マーク（アイコンフォント）のラベル。枠の中央に置く
lv_obj_t *makeIconLabel(lv_obj_t *parent, const Rect &r, const lv_font_t *font, lv_color_t color,
                        const char *text);

// 画面下部に戻るボタン（押すと 1 つ前の画面へ）
lv_obj_t *makeBackButton(lv_obj_t *parent, const char *text = "もどる");

// 一時メッセージ（同じ画面に重複して出さない。2 秒で消える）
void showToast(lv_obj_t *parent, const char *text);

// 横長のメニュー項目。cb が nullptr なら「準備中」の見た目（押せない）
lv_obj_t *makeMenuItem(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data,
                       int y_offset, bool enabled = true);

}  // namespace ui
