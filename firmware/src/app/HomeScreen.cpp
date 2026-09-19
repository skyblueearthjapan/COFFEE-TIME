#include "HomeScreen.h"

#include <Arduino.h>
#include <lvgl.h>
#include <time.h>

#include "CupState.h"

LV_FONT_DECLARE(ct_font_time_104);
LV_FONT_DECLARE(ct_font_30);
LV_FONT_DECLARE(ct_font_22);
LV_IMG_DECLARE(bg_morning);
LV_IMG_DECLARE(bg_noon);
LV_IMG_DECLARE(bg_evening);

namespace home {

// カラーパレット（カフェ風：深いブラウン系）
#define COLOR_BG        lv_color_hex(0x14100D)
#define COLOR_TEXT      lv_color_hex(0xF5EDE3)
#define COLOR_SUBTEXT   lv_color_hex(0xB9A58F)
#define COLOR_DIM       lv_color_hex(0x5C4E42)
#define COLOR_BTN       lv_color_hex(0x7A4A2A)
#define COLOR_BTN_PRESS lv_color_hex(0x5A3620)
#define COLOR_BTN_RING  lv_color_hex(0xC08A5B)
#define COLOR_WARN      lv_color_hex(0xE0A040)
#define COLOR_EMPTY     lv_color_hex(0xD9534F)

static constexpr uint32_t kRefillHoldMs = 1500;  // 残り杯数を長押しして補充するまでの時間

static lv_obj_t *s_bg = nullptr;
static const lv_img_dsc_t *s_bg_src = nullptr;
static lv_obj_t *s_date = nullptr;
static lv_obj_t *s_time = nullptr;
static lv_obj_t *s_weather = nullptr;
static lv_obj_t *s_taken = nullptr;
static lv_obj_t *s_left = nullptr;
static lv_obj_t *s_left_box = nullptr;
static lv_obj_t *s_wifi = nullptr;
static lv_obj_t *s_toast = nullptr;

static uint32_t s_left_press_ms = 0;
static bool s_refill_fired = false;

// WMO 天気コード → 日本語。使う文字は fonts/ct_font_jp_symbols.txt に含めること
static const char *weatherText(int code)
{
    if (code <= 1) return "晴れ";
    if (code == 2) return "晴れ時々くもり";
    if (code == 3) return "くもり";
    if (code == 45 || code == 48) return "霧";
    if (code >= 51 && code <= 57) return "霧雨";
    if (code >= 61 && code <= 67) return "雨";
    if (code >= 71 && code <= 77) return "雪";
    if (code >= 80 && code <= 82) return "にわか雨";
    if (code >= 85 && code <= 86) return "にわか雪";
    if (code >= 95) return "雷雨";
    return "--";
}

// 時間帯で背景を切り替える：5-11 時 朝 / 11-16 時 昼 / それ以外 夕方〜夜
static int s_forced_hour = -1;   // 開発用：背景確認のため時間帯を固定する

static void updateBackground(int hour)
{
    if (s_forced_hour >= 0) {
        hour = s_forced_hour;
    }
    const lv_img_dsc_t *src = &bg_evening;
    if (hour >= 5 && hour < 11) {
        src = &bg_morning;
    } else if (hour >= 11 && hour < 16) {
        src = &bg_noon;
    }
    if (src != s_bg_src) {
        s_bg_src = src;
        lv_img_set_src(s_bg, src);
    }
}

static void refreshCups()
{
    lv_label_set_text_fmt(s_taken, "%lu", (unsigned long)cup::taken());
    const uint32_t left = cup::remaining();
    lv_label_set_text_fmt(s_left, "%lu", (unsigned long)left);
    lv_color_t c = COLOR_TEXT;
    if (left == 0) {
        c = COLOR_EMPTY;
    } else if (left <= 3) {
        c = COLOR_WARN;
    }
    lv_obj_set_style_text_color(s_left, c, 0);
}

static void zoomAnimCb(void *obj, int32_t v)
{
    lv_obj_set_style_transform_zoom((lv_obj_t *)obj, v, 0);
}

static void pulse(lv_obj_t *obj)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, zoomAnimCb);
    lv_anim_set_values(&a, 340, 256);
    lv_anim_set_time(&a, 250);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void toastHideCb(lv_timer_t *t)
{
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_del(t);
}

static void showToast(const char *text)
{
    lv_label_set_text(s_toast, text);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(toastHideCb, 2000, nullptr);
}

// 押して離したとき（LV_EVENT_CLICKED）だけ加算する。押下中の連続加算はしない。
static void plusOneClickedCb(lv_event_t *e)
{
    (void)e;
    cup::takeOne();
    refreshCups();
    pulse(s_taken);
}

// 残り杯数を長押し（1.5 秒）すると「コーヒーを作った」として上限まで補充する
static void leftBoxEventCb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_left_press_ms = lv_tick_get();
        s_refill_fired = false;
    } else if (code == LV_EVENT_PRESSING) {
        if (!s_refill_fired && lv_tick_elaps(s_left_press_ms) >= kRefillHoldMs) {
            s_refill_fired = true;
            cup::refill();
            refreshCups();
            pulse(s_left);
            showToast("REFILLED: 10 CUPS");
        }
    }
}

static void clockTimerCb(lv_timer_t *t)
{
    (void)t;
    const time_t now = time(nullptr);
    if (net::timeSynced()) {
        struct tm tm;
        localtime_r(&now, &tm);
        lv_label_set_text_fmt(s_time, "%02d:%02d", tm.tm_hour, tm.tm_min);
        updateBackground(tm.tm_hour);
        static const char *const kWeekdays[] = {"日", "月", "火", "水", "木", "金", "土"};
        lv_label_set_text_fmt(s_date, "%d月%d日（%s）", tm.tm_mon + 1, tm.tm_mday, kWeekdays[tm.tm_wday]);

        const uint32_t ymd = (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
        const uint32_t before = cup::taken() + cup::remaining() * 1000;
        cup::checkNewDay(ymd);
        if (before != cup::taken() + cup::remaining() * 1000) {
            refreshCups();
        }
    }
    lv_obj_set_style_text_color(s_wifi, net::wifiConnected() ? COLOR_SUBTEXT : COLOR_DIM, 0);
}

static lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

// 本日杯数・残り杯数の表示ブロック（キャプション + 数字）
static lv_obj_t *makeStatBox(lv_obj_t *parent, const char *caption, lv_obj_t **value_out)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 110, 100);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cap = makeLabel(box, &lv_font_montserrat_16, COLOR_SUBTEXT, caption);
    lv_obj_set_style_text_letter_space(cap, 2, 0);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *val = makeLabel(box, &lv_font_montserrat_48, COLOR_TEXT, "0");
    lv_obj_set_style_transform_pivot_x(val, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(val, LV_PCT(50), 0);
    lv_obj_align(val, LV_ALIGN_TOP_MID, 0, 34);
    *value_out = val;
    return box;
}

bool create()
{
    lv_obj_t *scr = lv_scr_act();
    if (scr == nullptr) {
        return false;
    }
    lv_obj_clean(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // 背景写真（時刻取得前は夕方〜夜の画像）
    s_bg = lv_img_create(scr);
    lv_obj_center(s_bg);
    updateBackground(-1);

    // 上部：日付・時刻・天気（丸画面の円の内側に収める）
    s_date = makeLabel(scr, &ct_font_22, COLOR_SUBTEXT, "--月--日");
    lv_obj_set_style_text_letter_space(s_date, 1, 0);
    lv_obj_align(s_date, LV_ALIGN_TOP_MID, 0, 40);

    s_time = makeLabel(scr, &ct_font_time_104, COLOR_TEXT, "--:--");
    lv_obj_align(s_time, LV_ALIGN_TOP_MID, 0, 72);

    s_weather = makeLabel(scr, &ct_font_30, COLOR_SUBTEXT, "接続中…");
    lv_obj_align(s_weather, LV_ALIGN_TOP_MID, 0, 186);

    // 中段：本日杯数 / +1 / 残り杯数
    lv_obj_t *taken_box = makeStatBox(scr, "TODAY", &s_taken);
    lv_obj_align(taken_box, LV_ALIGN_CENTER, -148, 82);

    s_left_box = makeStatBox(scr, "LEFT", &s_left);
    lv_obj_align(s_left_box, LV_ALIGN_CENTER, 148, 82);
    lv_obj_add_flag(s_left_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_left_box, leftBoxEventCb, LV_EVENT_ALL, nullptr);

    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 150, 150);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 92);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, COLOR_BTN, 0);
    lv_obj_set_style_bg_color(btn, COLOR_BTN_PRESS, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, COLOR_BTN_RING, 0);
    lv_obj_set_style_border_width(btn, 4, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, plusOneClickedCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *btn_label = makeLabel(btn, &lv_font_montserrat_48, COLOR_TEXT, "+1");
    lv_obj_center(btn_label);

    // 下部：ロゴと Wi-Fi 状態
    lv_obj_t *logo = makeLabel(scr, &lv_font_montserrat_16, COLOR_SUBTEXT, "COFFEE TIME");
    lv_obj_set_style_text_letter_space(logo, 4, 0);
    lv_obj_align(logo, LV_ALIGN_BOTTOM_MID, 0, -42);

    s_wifi = makeLabel(scr, &lv_font_montserrat_16, COLOR_DIM, LV_SYMBOL_WIFI);
    lv_obj_align(s_wifi, LV_ALIGN_BOTTOM_MID, 0, -18);

    // 補充したときなどの一時メッセージ
    s_toast = makeLabel(scr, &ct_font_22, COLOR_BG, "");
    lv_obj_set_style_bg_color(s_toast, COLOR_BTN_RING, 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_toast, 18, 0);
    lv_obj_set_style_pad_hor(s_toast, 18, 0);
    lv_obj_set_style_pad_ver(s_toast, 8, 0);
    lv_obj_align(s_toast, LV_ALIGN_CENTER, 0, -10);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);

    refreshCups();
    lv_timer_create(clockTimerCb, 1000, nullptr);
    clockTimerCb(nullptr);
    return true;
}

void setWeather(const net::Weather &w)
{
    if (!w.valid) {
        return;
    }
    lv_obj_set_style_text_color(s_weather, COLOR_TEXT, 0);
    lv_label_set_text_fmt(s_weather, "%s  %d\xC2\xB0" "C", weatherText(w.code), (int)lroundf(w.temperature));
}

}  // namespace home

namespace home {

void debugForceHour(int hour)
{
    s_forced_hour = hour;
    updateBackground(hour);
}

}  // namespace home
