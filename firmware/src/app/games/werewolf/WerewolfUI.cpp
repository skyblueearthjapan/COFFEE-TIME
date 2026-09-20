#include "WerewolfUI.h"

#include <Arduino.h>

#include "../../ui/ScreenManager.h"
#include "../../ui/UiKit.h"
// Arduino.h の bit(b) マクロが coffee::wolf::bit() と衝突するため、コアを読む前に無効化する
#undef bit
#include "core/werewolf_core.hpp"

LV_FONT_DECLARE(ct_font_jp_20);
LV_FONT_DECLARE(ct_font_jp_22);
LV_FONT_DECLARE(ct_font_jp_40);
LV_FONT_DECLARE(ct_font_time_104);

namespace werewolf {

using namespace ui;

static uint8_t s_players = coffee::wolf::MIN_PLAYERS;   // 初期人数は 3（rules.json の default_players）
static lv_obj_t *s_players_label = nullptr;
static lv_obj_t *s_hint_label = nullptr;

uint8_t selectedPlayers()
{
    return s_players;
}

// ---- ゲーム一覧 -------------------------------------------------------------

static void openWerewolfCb(lv_event_t *e)
{
    (void)e;
    push(createLobbyScreen);
}

lv_obj_t *createEntryScreen()
{
    lv_obj_t *scr = makeScreen();
    makeTitle(scr, "ゲーム");

    makeMenuItem(scr, "閉店後の人狼会", openWerewolfCb, nullptr, -60, true);
    makeMenuItem(scr, "喫茶「余白」の事件簿", nullptr, nullptr, 10, false);
    makeMenuItem(scr, "エスパー対決", nullptr, nullptr, 80, false);

    makeBackButton(scr);
    return scr;
}

// ---- 人数を決める -----------------------------------------------------------

static void lobbyDeletedCb(lv_event_t *e)
{
    (void)e;
    s_players_label = nullptr;
    s_hint_label = nullptr;
}

static void refreshPlayers()
{
    if (s_players_label == nullptr || s_hint_label == nullptr) {
        return;
    }
    lv_label_set_text_fmt(s_players_label, "%u", (unsigned)s_players);
    // 人数に応じて役職の枚数と議論時間が変わる（N+2 枚、うち 2 枚は伏せ札）
    const uint16_t seconds = coffee::wolf::defaultDiscussion(s_players);
    lv_label_set_text_fmt(s_hint_label, "役職 %u 枚 ／ 議論 %u 分",
                          (unsigned)(s_players + 2), (unsigned)(seconds / 60));
}

static void stepCb(lv_event_t *e)
{
    const int delta = (int)(intptr_t)lv_event_get_user_data(e);
    const int next = (int)s_players + delta;
    if (next < coffee::wolf::MIN_PLAYERS || next > coffee::wolf::MAX_PLAYERS) {
        return;
    }
    s_players = (uint8_t)next;
    refreshPlayers();
}

static void startCb(lv_event_t *e)
{
    (void)e;
    Serial.printf("[WOLF] start requested players=%u\n", (unsigned)s_players);
    // W2 でここから配役・夜の受け渡しへ進む
    showToast(lv_scr_act(), "ただいま準備中です");
}

static lv_obj_t *makeStepButton(lv_obj_t *parent, const char *text, int delta, int x)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 72, 72);
    lv_obj_align(btn, LV_ALIGN_CENTER, x, -10);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, CT_COLOR_PANEL, 0);
    lv_obj_set_style_bg_color(btn, CT_COLOR_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, CT_COLOR_ACCENT_HI, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, stepCb, LV_EVENT_CLICKED, (void *)(intptr_t)delta);
    lv_obj_t *l = makeLabel(btn, &ct_font_jp_40, CT_COLOR_TEXT, text);
    lv_obj_center(l);
    return btn;
}

lv_obj_t *createLobbyScreen()
{
    lv_obj_t *scr = makeScreen();
    makeTitle(scr, "なん人で あそぶ？");

    s_players_label = makeLabel(scr, &ct_font_time_104, CT_COLOR_TEXT, "3");
    lv_obj_align(s_players_label, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *unit = makeLabel(scr, &ct_font_jp_40, CT_COLOR_SUBTEXT, "人");
    lv_obj_align(unit, LV_ALIGN_CENTER, 72, 8);

    makeStepButton(scr, "-", -1, -130);
    makeStepButton(scr, "+", +1, 130);

    s_hint_label = makeLabel(scr, &ct_font_jp_22, CT_COLOR_SUBTEXT, "");
    lv_obj_align(s_hint_label, LV_ALIGN_CENTER, 0, 66);

    lv_obj_t *start = lv_btn_create(scr);
    lv_obj_set_size(start, 220, 62);
    lv_obj_align(start, LV_ALIGN_CENTER, 0, 128);
    lv_obj_set_style_radius(start, 31, 0);
    lv_obj_set_style_bg_color(start, CT_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_color(start, CT_COLOR_ACCENT_HI, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(start, CT_COLOR_ACCENT_HI, 0);
    lv_obj_set_style_border_width(start, 2, 0);
    lv_obj_set_style_shadow_width(start, 0, 0);
    lv_obj_add_event_cb(start, startCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *sl = makeLabel(start, &ct_font_jp_40, CT_COLOR_TEXT, "はじめる");
    lv_obj_center(sl);

    lv_obj_t *back = makeBackButton(scr);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -26);

    // 画面が破棄されたら静的ポインタを無効化する（解放済みオブジェクトを触らないため）
    lv_obj_add_event_cb(scr, lobbyDeletedCb, LV_EVENT_DELETE, nullptr);

    refreshPlayers();
    return scr;
}

}  // namespace werewolf
