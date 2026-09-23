#include "Thinking.h"

#include <cstring>

LV_FONT_DECLARE(ct_font_jp_20);

namespace ui {

namespace {

// **見張りは 0.5 秒ごと**に回し、一言は 3 回に 1 度だけ入れ替える。
// 1.5 秒ごとにしか見ないと、5 秒の注記が実際には 6 秒まで出なかった
constexpr uint32_t kTickMs  = 500;
constexpr uint8_t  kTicksPerStep = 3;   // 500ms × 3 = 一言は 1.5 秒ごと（従来どおり）
constexpr uint32_t kHintMs  = 5000;   // ここを過ぎたら「時間がかかっています」も出す
constexpr int16_t  kArcSize = 20;     // 回る弧の大きさ
constexpr int16_t  kArcGap  = 8;      // 弧と一言のすきま
constexpr int16_t  kRowGap  = 4;      // 一言と注記のすきま

// 5 秒を過ぎたときの注記。枠が狭ければ短いほうを使う（はみ出すと尻切れになる）。
// 全角 1 文字 = 20px なので、広いほうは 300px・狭いほうは 200px 要る
const char *const kHintWide   = "通信に少し時間がかかっています";
const char *const kHintNarrow = "時間がかかっています";

}  // namespace

struct Thinking {
    lv_timer_t *timer;
    lv_obj_t *caption;
    lv_obj_t *hint;              // 1 行しか置けない枠では nullptr
    const char *const *phrases;
    const char *hint_text;
    uint32_t start_ms;
    uint16_t count;
    uint16_t index;
    uint8_t tick;                // 0.5 秒の刻み（kTicksPerStep で一言を入れ替える）
    bool late;                   // 5 秒を過ぎたか
};

namespace {

void showCaption(Thinking *th)
{
    const bool is_hint = th->index >= th->count;
    lv_label_set_text(th->caption, is_hint ? th->hint_text : th->phrases[th->index]);
    lv_obj_set_style_text_color(th->caption, is_hint ? CT_COLOR_SUBTEXT : CT_COLOR_TEXT, 0);
}

// 0.5 秒ごと。字を入れ替えるだけで、通信も保存もここではしない（LVGL を待たせない）
void stepCb(lv_timer_t *t)
{
    Thinking *th = (Thinking *)t->user_data;
    // 5 秒の見張りは毎回（0.5 秒ごと）。一言の入れ替えを待たないので 5.0〜5.5 秒で出る
    if (!th->late && lv_tick_elaps(th->start_ms) >= kHintMs) {
        th->late = true;
        if (th->hint != nullptr) {
            lv_obj_clear_flag(th->hint, LV_OBJ_FLAG_HIDDEN);   // 下の行に出しっぱなしにする
        } else {
            th->index = th->count;      // 1 行しかない枠では、まず注記そのものを出す
            th->tick = 0;
            showCaption(th);
            return;
        }
    }
    if (++th->tick < kTicksPerStep) {
        return;
    }
    th->tick = 0;
    // 注記を別の行に置けない枠では、5 秒を過ぎてから一言の順番に混ぜる
    const uint16_t total = (th->hint == nullptr && th->late) ? (uint16_t)(th->count + 1)
                                                             : th->count;
    th->index = (uint16_t)((th->index + 1) % total);
    showCaption(th);
}

// 根っこが消えるとき（画面の作り直し・ゲームの終了のどちらでも）ここで片付ける。
// 弧の動きは LVGL が弧といっしょに消してくれる
void deletedCb(lv_event_t *e)
{
    Thinking *th = (Thinking *)lv_event_get_user_data(e);
    if (th == nullptr) {
        return;
    }
    if (th->timer != nullptr) {
        lv_timer_del(th->timer);
    }
    lv_mem_free(th);
}

lv_obj_t *makeLine(lv_obj_t *parent, int16_t x, int16_t y, int16_t w, lv_color_t color,
                   const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, &ct_font_jp_20, 0);
    lv_obj_set_style_text_color(l, color, 0);
    // 日本語は自動折返しが効かない。1 行で収まる長さだけを渡し、はみ出しは切る
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, w);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, x, y);
    return l;
}

}  // namespace

Thinking *thinkingCreate(lv_obj_t *parent, const Rect &area, const char *const *phrases,
                         size_t count)
{
    if (parent == nullptr || phrases == nullptr || count == 0) {
        return nullptr;
    }
    Thinking *th = (Thinking *)lv_mem_alloc(sizeof(Thinking));
    if (th == nullptr) {
        return nullptr;
    }
    std::memset(th, 0, sizeof(*th));
    th->phrases = phrases;
    th->count = (uint16_t)count;
    th->start_ms = lv_tick_get();

    const lv_font_t *font = &ct_font_jp_20;
    const int16_t line = (int16_t)font->line_height;
    const int16_t row_h = line > kArcSize ? line : kArcSize;      // 弧と一言の行
    const int16_t caption_w = (int16_t)(area.w - kArcSize - kArcGap);
    const bool two_rows = area.h >= (int16_t)(row_h + kRowGap + line);
    const int16_t body_h = two_rows ? (int16_t)(row_h + kRowGap + line) : row_h;
    const int16_t top = (int16_t)(area.h > body_h ? (area.h - body_h) / 2 : 0);

    // 注記が入る幅かどうかで長さを選ぶ（全角 1 文字 = 20px）
    const int16_t room = two_rows ? area.w : caption_w;
    th->hint_text = lv_txt_get_width(kHintWide, (uint32_t)std::strlen(kHintWide), font, 0,
                                     LV_TEXT_FLAG_NONE) <= room
                        ? kHintWide
                        : kHintNarrow;

    // 根っこ。透明で押せない（下に置いてあるものやタップ位置の邪魔をしない）
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_pos(root, area.x, area.y);
    lv_obj_set_size(root, area.w, area.h);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE);

    // 回る弧。動きは lv_anim（見張りのタイマーは持たない）で、弧を消せば止まる
    lv_obj_t *arc = lv_spinner_create(root, 1100, 70);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);      // つまみは出さない
    lv_obj_set_size(arc, kArcSize, kArcSize);
    lv_obj_set_pos(arc, 0, (int16_t)(top + (row_h - kArcSize) / 2));
    lv_obj_set_style_pad_all(arc, 0, 0);
    lv_obj_set_style_arc_width(arc, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, CT_COLOR_DIM, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, CT_COLOR_ACCENT_HI, LV_PART_INDICATOR);

    th->caption = makeLine(root, (int16_t)(kArcSize + kArcGap),
                           (int16_t)(top + (row_h - line) / 2), caption_w, CT_COLOR_TEXT,
                           phrases[0]);
    if (two_rows) {
        th->hint = makeLine(root, 0, (int16_t)(top + row_h + kRowGap), area.w, CT_COLOR_DIM,
                            th->hint_text);
        lv_obj_add_flag(th->hint, LV_OBJ_FLAG_HIDDEN);   // 5 秒を過ぎたら出す
    }

    th->timer = lv_timer_create(stepCb, kTickMs, th);
    lv_obj_add_event_cb(root, deletedCb, LV_EVENT_DELETE, th);
    return th;
}

}  // namespace ui
