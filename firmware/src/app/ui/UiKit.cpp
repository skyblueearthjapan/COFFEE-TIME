#include "UiKit.h"

#include "ScreenManager.h"

LV_FONT_DECLARE(ct_font_jp_20);
LV_FONT_DECLARE(ct_font_jp_22);
LV_FONT_DECLARE(ct_font_jp_40);

namespace ui {

lv_obj_t *makeScreen()
{
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, CT_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    return scr;
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

lv_obj_t *makeTitle(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = makeLabel(parent, &ct_font_jp_40, CT_COLOR_TEXT, text);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 56);
    return l;
}

static void backCb(lv_event_t *e)
{
    (void)e;
    pop();
}

lv_obj_t *makeBackButton(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 150, 52);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -26);
    lv_obj_set_style_radius(btn, 26, 0);
    lv_obj_set_style_bg_color(btn, CT_COLOR_PANEL, 0);
    lv_obj_set_style_bg_color(btn, CT_COLOR_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, CT_COLOR_DIM, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, backCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *l = makeLabel(btn, &ct_font_jp_22, CT_COLOR_SUBTEXT, text);
    lv_obj_center(l);
    return btn;
}

// 表示中のトースト（無ければ nullptr）。消えたら必ず nullptr に戻す
static lv_obj_t *s_toast = nullptr;

static void toastHideCb(lv_timer_t *t)
{
    // タイマーの削除は toastDeletedCb に任せる（ここでも消すと二重解放になる）
    lv_obj_t *toast = (lv_obj_t *)t->user_data;
    if (toast != nullptr) {
        lv_obj_del(toast);
    }
}

static void toastDeletedCb(lv_event_t *e)
{
    // 時間切れ・連打による差し替え・画面ごとの破棄のどの経路でも、ここでタイマーを止めて参照を消す
    lv_timer_t *timer = (lv_timer_t *)lv_event_get_user_data(e);
    if (timer != nullptr) {
        lv_timer_del(timer);
    }
    if (lv_event_get_target(e) == s_toast) {
        s_toast = nullptr;
    }
}

void showToast(lv_obj_t *parent, const char *text)
{
    if (s_toast != nullptr) {      // 連打しても 1 つだけにする
        lv_obj_del(s_toast);       // toastDeletedCb が s_toast を nullptr に戻す
    }
    lv_obj_t *toast = makeLabel(parent, &ct_font_jp_22, CT_COLOR_BG, text);
    lv_obj_set_style_bg_color(toast, CT_COLOR_ACCENT_HI, 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(toast, 18, 0);
    lv_obj_set_style_pad_hor(toast, 18, 0);
    lv_obj_set_style_pad_ver(toast, 8, 0);
    lv_obj_align(toast, LV_ALIGN_CENTER, 0, 150);
    lv_timer_t *timer = lv_timer_create(toastHideCb, 2000, toast);
    lv_obj_add_event_cb(toast, toastDeletedCb, LV_EVENT_DELETE, timer);
    s_toast = toast;
}

lv_obj_t *makeMenuItem(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data,
                       int y_offset, bool enabled)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 300, 62);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, y_offset);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_bg_color(btn, CT_COLOR_PANEL, 0);
    lv_obj_set_style_bg_color(btn, CT_COLOR_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, enabled ? CT_COLOR_ACCENT_HI : CT_COLOR_DIM, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    // 既定の内側余白が大きく配置指定が効かなくなるため 0 にして座標で置く
    lv_obj_set_style_pad_all(btn, 0, 0);

    lv_obj_t *l = makeLabel(btn, &ct_font_jp_20, enabled ? CT_COLOR_TEXT : CT_COLOR_DIM, text);
    // 日本語は空白が無く自動折返しが効かないので、幅を決めてはみ出しを切る。
    // 右端は「準備中」(60px) か「>」(16px) のどちらかなので、遊べる項目では広く取れる
    // （「喫茶「余白」の事件簿」は 10 文字 = 200px あり、196px だと末尾が欠ける）
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(l, (enabled && cb != nullptr) ? 250 : 196);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 14, 0);

    if (enabled && cb != nullptr) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
        lv_obj_t *arrow = makeLabel(btn, &ct_font_jp_20, CT_COLOR_SUBTEXT, ">");
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -16, 0);
    } else {
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *soon = makeLabel(btn, &ct_font_jp_20, CT_COLOR_DIM, "準備中");
        lv_obj_align(soon, LV_ALIGN_RIGHT_MID, -16, 0);
    }
    return btn;
}

}  // namespace ui
