#include "UpdateOverlay.h"

#include <stdio.h>

#include "UiKit.h"

LV_FONT_DECLARE(ct_font_jp_20);
LV_FONT_DECLARE(ct_font_jp_22);
LV_FONT_DECLARE(ct_font_time_64);

namespace ui {

namespace {
lv_obj_t *s_panel = nullptr;
lv_obj_t *s_title = nullptr;
lv_obj_t *s_percent = nullptr;
lv_obj_t *s_note = nullptr;
}  // namespace

void showUpdateOverlay()
{
    if (s_panel != nullptr) {
        return;
    }
    s_panel = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, kScreenSize, kScreenSize);
    lv_obj_set_style_bg_color(s_panel, CT_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_CLICKABLE);      // 下の画面へタッチを通さない
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_title = makeRectLabel(s_panel, Rect{60, 140, 360, 34}, &ct_font_jp_22, CT_COLOR_TEXT,
                            "ソフトを更新しています");
    s_percent = makeRectLabel(s_panel, Rect{120, 190, 240, 76}, &ct_font_time_64, CT_COLOR_ACCENT_HI, "0%");
    s_note = makeRectLabel(s_panel, Rect{60, 290, 360, 60}, &ct_font_jp_20, CT_COLOR_SUBTEXT,
                           "電源を切らないでください\n終わると自動で再起動します");
}

void setUpdateProgress(int percent)
{
    if (s_percent == nullptr) {
        return;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", percent);
    lv_label_set_text(s_percent, buf);
}

void setUpdateFailed()
{
    if (s_panel == nullptr) {
        return;
    }
    lv_label_set_text(s_title, "更新に失敗しました");
    lv_obj_set_style_text_color(s_title, CT_COLOR_ALERT, 0);
    lv_label_set_text(s_note, "元のソフトのまま動きます");
}

void hideUpdateOverlay()
{
    if (s_panel == nullptr) {
        return;
    }
    lv_obj_del(s_panel);
    s_panel = s_title = s_percent = s_note = nullptr;
}

}  // namespace ui
