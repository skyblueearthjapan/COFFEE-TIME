#include "HomeScreen.h"

#include "lvgl_v8_port.h"

#include <Arduino.h>
#include <lvgl.h>
#include <time.h>

#include "Battery.h"
#include "ui/MainMenu.h"
#include "ui/ScreenManager.h"
#include "CupState.h"
#include "SdLog.h"

LV_FONT_DECLARE(ct_font_time_104);
LV_FONT_DECLARE(ct_font_30);
LV_FONT_DECLARE(ct_font_22);
LV_FONT_DECLARE(ct_font_weather_44);
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
static lv_obj_t *s_weather_icon = nullptr;
static lv_obj_t *s_taken = nullptr;
static lv_obj_t *s_left = nullptr;
static lv_obj_t *s_left_box = nullptr;
static lv_obj_t *s_wifi = nullptr;
static lv_obj_t *s_battery = nullptr;
static lv_obj_t *s_sd = nullptr;
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

// 天気をマークで出すか（true）、日本語の文字で出すか（false）
static constexpr bool kWeatherAsIcon = true;

// WMO 天気コード → 天気マーク（fonts/ct_font_weather_44。収録は tools/gen_fonts.sh の 11 個だけ）。
// 夜（18〜5 時）の晴れ・晴れ時々くもりは月のマークにする。hour が負なら昼扱い
static const char *weatherIcon(int code, int hour)
{
    const bool night = hour >= 0 && (hour >= 18 || hour < 5);
    if (code <= 1) return night ? "\xEF\x80\xAE" : "\xEF\x80\x8D";     // U+F02E 月 / U+F00D 太陽
    if (code == 2) return night ? "\xEF\x82\x86" : "\xEF\x80\x82";     // U+F086 月と雲 / U+F002 太陽と雲
    if (code == 3) return "\xEF\x80\x93";                              // U+F013 くもり
    if (code == 45 || code == 48) return "\xEF\x80\x94";               // U+F014 霧
    if (code >= 51 && code <= 57) return "\xEF\x80\x9C";               // U+F01C 霧雨
    if (code >= 61 && code <= 67) return "\xEF\x80\x99";               // U+F019 雨
    if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) return "\xEF\x80\x9B";   // U+F01B 雪
    if (code >= 80 && code <= 82) return "\xEF\x80\x9A";               // U+F01A にわか雨
    if (code >= 95) return "\xEF\x80\x9E";                             // U+F01E 雷雨
    return "";
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

static lv_timer_t *s_toast_timer = nullptr;

static void toastHideCb(lv_timer_t *t)
{
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_del(t);
    s_toast_timer = nullptr;
}

// 続けて出したときに前のタイマーで新しいトーストが早く消えないよう、2 秒を数え直す
static void showToast(const char *text)
{
    lv_label_set_text(s_toast, text);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    if (s_toast_timer) {
        lv_timer_reset(s_toast_timer);
    } else {
        s_toast_timer = lv_timer_create(toastHideCb, 2000, nullptr);
    }
}

// 杯数が変わった出来事を、GAS（Wi-Fi）と SD の操作ログの両方へ記録する
static void recordEvent(const char *event, uint32_t prev)
{
    net::reportEvent(event, cup::taken(), cup::remaining(), prev);
    sdlog::event(event, cup::taken(), cup::remaining(), prev);
}

// コーヒーを 1 杯記録する。HOME の「+1」のほか、ゲーム中の一時停止メニューからも呼ばれる
void addOneCup()
{
    // 開発用の偽装タップ（シリアル P）からは絶対に記録しない。自動操作の誤タップで本物の「＋1」が
    // 2 度入ったため（2026-09-22 / 23）。本物の記録を試験したいときはシリアル T を使う
    if (lvgl_port_debug_tap_recent()) {
        Serial.println("[DEV] ignored: +1 from a debug tap (use T for a real record)");
        return;
    }
    const uint32_t prev = cup::remaining();
    cup::takeOne();
    recordEvent("take", prev);
    // HOME 以外の画面が出ていても値は更新しておく（部品は HOME 画面に残っている）
    refreshCups();
    if (ui::isHome()) {
        pulse(s_taken);
        // 残り 0 でも「今日」は数える（設定 10 杯でも実際は 11〜12 杯出ることがある。ユーザー決定 9/23）。
        // 「残り」は 0 のままなので、補充の操作を思い出してもらう
        if (prev == 0) {
            showToast("残りは 0 のままです。\n補充したら『残り』を長押し");
        }
    }
}

// 押して離したとき（LV_EVENT_CLICKED）だけ加算する。押下中の連続加算はしない。
static void plusOneClickedCb(lv_event_t *e)
{
    (void)e;
    addOneCup();
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
            const uint32_t prev = cup::remaining();
            cup::refill();
            recordEvent("refill", prev);
            refreshCups();
            pulse(s_left);
            // 杯数は設定で 5〜15 に変えられるので、文言も設定値に合わせる
            // 日本語は 1 文字 3 バイトなので余裕を持たせる
            char toast[48];
            snprintf(toast, sizeof(toast), "補充しました：%lu 杯", (unsigned long)cup::maxCups());
            showToast(toast);
        }
    }
}

static void openMenuCb(lv_event_t *e)
{
    (void)e;
    ui::push(ui::createMainMenu);
}

static void clockTimerCb(lv_timer_t *t)
{
    (void)t;
    // 日付の切り替わり判定は常に行い、画面の書き換えは HOME 表示中だけにする
    const bool visible = ui::isHome();
    const time_t now = time(nullptr);
    if (net::timeSynced()) {
        struct tm tm;
        localtime_r(&now, &tm);
        if (visible) {
            lv_label_set_text_fmt(s_time, "%02d:%02d", tm.tm_hour, tm.tm_min);
            updateBackground(tm.tm_hour);
        }
        if (visible) {
        static const char *const kWeekdays[] = {"日", "月", "火", "水", "木", "金", "土"};
        lv_label_set_text_fmt(s_date, "%d月%d日（%s）", tm.tm_mon + 1, tm.tm_mday, kWeekdays[tm.tm_wday]);
        }

        const uint32_t ymd = (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
        const uint32_t prev_left = cup::remaining();
        // 日付が変わったときだけ記録する。イベント名は "newday" のままなので、
        // 「朝いちばんの残り」を満杯にしても GAS の通知条件（take かつ 3/0 杯）には掛からない
        if (cup::checkNewDay(ymd)) {
            recordEvent("newday", prev_left);
            refreshCups();
        }
    }
    if (!visible) {
        return;
    }
    lv_obj_set_style_text_color(s_wifi, net::wifiConnected() ? COLOR_SUBTEXT : COLOR_DIM, 0);
    lv_obj_set_style_text_color(s_sd, sdlog::mounted() ? COLOR_SUBTEXT : COLOR_DIM, 0);

    // 電池電圧（充電確認のため当面は電圧を表示する）
    const uint32_t mv = battery::millivolts();
    const int pct = battery::percent();
    const char *icon = pct >= 80 ? LV_SYMBOL_BATTERY_FULL
                     : pct >= 55 ? LV_SYMBOL_BATTERY_3
                     : pct >= 30 ? LV_SYMBOL_BATTERY_2
                     : pct >= 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
    lv_label_set_text_fmt(s_battery, "%s %lu.%02luV", icon, (unsigned long)(mv / 1000), (unsigned long)(mv % 1000 / 10));
    lv_obj_set_style_text_color(s_battery, pct < 10 ? COLOR_EMPTY : COLOR_SUBTEXT, 0);
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

    // 見出しは日本語（「今日」「残り」）。HOME 用の日本語フォント ct_font_22 を使う
    lv_obj_t *cap = makeLabel(box, &ct_font_22, COLOR_SUBTEXT, caption);
    lv_obj_set_style_text_letter_space(cap, 4, 0);
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

    // 天気の行：マーク + 気温を横に並べて中央寄せ（幅は中身に合わせる）。取得前は「接続中…」だけを出す
    lv_obj_t *weather_row = lv_obj_create(scr);
    lv_obj_remove_style_all(weather_row);
    lv_obj_set_size(weather_row, LV_SIZE_CONTENT, 48);
    lv_obj_set_flex_flow(weather_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(weather_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(weather_row, 12, 0);
    lv_obj_clear_flag(weather_row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(weather_row, LV_ALIGN_TOP_MID, 0, 178);

    s_weather_icon = makeLabel(weather_row, &ct_font_weather_44, COLOR_TEXT, "");
    lv_obj_add_flag(s_weather_icon, LV_OBJ_FLAG_HIDDEN);
    s_weather = makeLabel(weather_row, &ct_font_30, COLOR_SUBTEXT, "接続中…");

    // 中段：本日杯数 / +1 / 残り杯数
    lv_obj_t *taken_box = makeStatBox(scr, "今日", &s_taken);
    lv_obj_align(taken_box, LV_ALIGN_CENTER, -148, 82);

    s_left_box = makeStatBox(scr, "残り", &s_left);
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

    // メニューボタン（右斜め下。LEFT の長押し領域の下端 y=372 とは重ならない。丸画面の内側に収める）
    lv_obj_t *menu_btn = lv_btn_create(scr);
    lv_obj_set_size(menu_btn, 52, 52);
    lv_obj_align(menu_btn, LV_ALIGN_CENTER, 132, 170);
    lv_obj_set_style_radius(menu_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(menu_btn, lv_color_hex(0x241A13), 0);
    lv_obj_set_style_bg_opa(menu_btn, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(menu_btn, COLOR_BTN, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(menu_btn, COLOR_SUBTEXT, 0);
    lv_obj_set_style_border_width(menu_btn, 1, 0);
    lv_obj_set_style_shadow_width(menu_btn, 0, 0);
    lv_obj_add_event_cb(menu_btn, openMenuCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *menu_icon = makeLabel(menu_btn, &lv_font_montserrat_16, COLOR_SUBTEXT, LV_SYMBOL_LIST);
    lv_obj_center(menu_icon);

    // 下部：ロゴと Wi-Fi 状態
    lv_obj_t *logo = makeLabel(scr, &lv_font_montserrat_16, COLOR_SUBTEXT, "COFFEE TIME");
    lv_obj_set_style_text_letter_space(logo, 4, 0);
    lv_obj_align(logo, LV_ALIGN_BOTTOM_MID, 0, -42);

    s_wifi = makeLabel(scr, &lv_font_montserrat_16, COLOR_DIM, LV_SYMBOL_WIFI);
    lv_obj_align(s_wifi, LV_ALIGN_BOTTOM_MID, -34, -18);

    // microSD が使えるとき明るく、無いとき暗く表示する
    s_sd = makeLabel(scr, &lv_font_montserrat_16, COLOR_DIM, LV_SYMBOL_SD_CARD);
    lv_obj_align(s_sd, LV_ALIGN_BOTTOM_MID, -62, -18);

    s_battery = makeLabel(scr, &lv_font_montserrat_16, COLOR_SUBTEXT, "");
    lv_obj_align(s_battery, LV_ALIGN_BOTTOM_MID, 24, -18);

    // 補充したときなどの一時メッセージ
    s_toast = makeLabel(scr, &ct_font_22, COLOR_BG, "");
    lv_obj_set_style_bg_color(s_toast, COLOR_BTN_RING, 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_toast, 18, 0);
    lv_obj_set_style_pad_hor(s_toast, 18, 0);
    lv_obj_set_style_pad_ver(s_toast, 8, 0);
    lv_obj_set_style_text_align(s_toast, LV_TEXT_ALIGN_CENTER, 0);
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
    const int temp = (int)lroundf(w.temperature);
    int hour = -1;
    if (net::timeSynced()) {
        const time_t now = time(nullptr);
        struct tm tm;
        localtime_r(&now, &tm);
        hour = tm.tm_hour;
    }
    const char *icon = weatherIcon(w.code, hour);
    if (kWeatherAsIcon && icon[0] != '\0') {
        lv_label_set_text(s_weather_icon, icon);
        lv_obj_clear_flag(s_weather_icon, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_weather, "%d\xC2\xB0" "C", temp);
    } else {
        lv_obj_add_flag(s_weather_icon, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_weather, "%s  %d\xC2\xB0" "C", weatherText(w.code), temp);
    }
}

}  // namespace home

namespace home {

// 開発用：画面操作と同じ処理をシリアルから呼ぶ
void debugTake()
{
    plusOneClickedCb(nullptr);
}

void debugRefill()
{
    const uint32_t prev = cup::remaining();
    cup::refill();
    recordEvent("refill", prev);
    refreshCups();
}

// 自動操作の誤タップで入ってしまった本物の「+1」を取り消す（2026-09-22 / 23 の後始末）。
// recordEvent は使わない＝GAS へは送らない。SD の操作ログにだけ "undo" を 1 行残す
bool debugUndoCup()
{
    int bucket = -2;
    if (!cup::undoOne(bucket)) {
        return false;
    }
    char note[48];
    if (bucket >= 0) {
        snprintf(note, sizeof(note), "serial hour=%02d", bucket);
    } else {
        snprintf(note, sizeof(note), "serial bucket=%s", bucket == -1 ? "unknown" : "none");
    }
    sdlog::event("undo", cup::taken(), cup::remaining(), cup::remaining(), note);
    refreshCups();
    return true;
}

void debugForceHour(int hour)
{
    s_forced_hour = hour;
    updateBackground(hour);
}

}  // namespace home
