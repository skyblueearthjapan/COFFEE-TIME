#include "CoffeeCounterUI.h"

#include <Arduino.h>
#include <lvgl.h>

// カラーパレット（カフェ風：深いブラウン系）
#define COLOR_BG        lv_color_hex(0x14100D)
#define COLOR_TEXT      lv_color_hex(0xF5EDE3)
#define COLOR_SUBTEXT   lv_color_hex(0xB9A58F)
#define COLOR_BTN       lv_color_hex(0x7A4A2A)
#define COLOR_BTN_PRESS lv_color_hex(0x5A3620)
#define COLOR_BTN_RING  lv_color_hex(0xC08A5B)

static uint32_t s_count = 0;
static lv_obj_t *s_count_label = nullptr;

static void update_count_label(void)
{
    lv_label_set_text_fmt(s_count_label, "%lu", (unsigned long)s_count);
}

static void count_pulse_anim_cb(void *obj, int32_t v)
{
    lv_obj_set_style_transform_zoom((lv_obj_t *)obj, v, 0);
}

// 押して離したとき（LV_EVENT_CLICKED）だけ加算する。押下中の連続加算はしない。
static void plus_one_clicked_cb(lv_event_t *e)
{
    (void)e;
    s_count++;
    update_count_label();
    Serial.printf("[COUNT] +1 -> %lu\n", (unsigned long)s_count);

    // 数字を一瞬大きくして反応を示す（イベント内で待機はしない）
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_count_label);
    lv_anim_set_exec_cb(&a, count_pulse_anim_cb);
    lv_anim_set_values(&a, 330, 256);
    lv_anim_set_time(&a, 250);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

bool coffee_counter_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    if (scr == nullptr) {
        return false;
    }
    lv_obj_clean(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // タイトル（丸画面の上部、円の内側に収める）
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "COFFEE TIME");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, COLOR_SUBTEXT, 0);
    lv_obj_set_style_text_letter_space(title, 4, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 70);

    // 杯数
    s_count_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_count_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_count_label, COLOR_TEXT, 0);
    lv_obj_set_style_transform_pivot_x(s_count_label, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(s_count_label, LV_PCT(50), 0);
    lv_obj_align(s_count_label, LV_ALIGN_CENTER, 0, -90);
    update_count_label();

    lv_obj_t *caption = lv_label_create(scr);
    lv_label_set_text(caption, "CUPS TAKEN");
    lv_obj_set_style_text_font(caption, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(caption, COLOR_SUBTEXT, 0);
    lv_obj_set_style_text_letter_space(caption, 3, 0);
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, -45);

    // +1 ボタン（丸）
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 170, 170);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 85);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, COLOR_BTN, 0);
    lv_obj_set_style_bg_color(btn, COLOR_BTN_PRESS, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, COLOR_BTN_RING, 0);
    lv_obj_set_style_border_width(btn, 4, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, plus_one_clicked_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "+1");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(btn_label, COLOR_TEXT, 0);
    lv_obj_center(btn_label);

    return true;
}

uint32_t coffee_counter_get(void)
{
    return s_count;
}
