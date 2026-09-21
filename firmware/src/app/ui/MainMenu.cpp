#include "MainMenu.h"

#include "../games/werewolf/WerewolfUI.h"
#include "HistoryScreen.h"
#include "ScreenManager.h"
#include "SettingsScreen.h"
#include "TodayScreen.h"
#include "UiKit.h"

LV_FONT_DECLARE(ct_font_jp_20);
// メニューのマーク（Material Icons Round）。日本語フォントとは別物
LV_FONT_DECLARE(ct_font_icons_54);

namespace ui {

namespace {

// マークは tools/gen_fonts.sh の ICONS_54 に入っている 4 つ
constexpr const char *kIconToday = "\xEE\x95\x81";      // U+E541 local_cafe
constexpr const char *kIconHistory = "\xEE\x89\xAB";    // U+E26B bar_chart
constexpr const char *kIconSettings = "\xEE\xA2\xB8";   // U+E8B8 settings
constexpr const char *kIconGames = "\xEE\xA8\xA8";      // U+EA28 sports_esports

// 2×2 のます目。中心 (240,240) から半径 228px の円の内側に収まることを確認済み
constexpr Rect kTile[4] = {
    {106, 118, 126, 126},   // 今日の状況
    {248, 118, 126, 126},   // 履歴
    {106, 260, 126, 126},   // 設定
    {248, 260, 126, 126},   // ゲーム
};

void openTodayCb(lv_event_t *e)
{
    (void)e;
    push(createTodayScreen);
}

void openHistoryCb(lv_event_t *e)
{
    (void)e;
    push(createHistoryScreen);
}

void openSettingsCb(lv_event_t *e)
{
    (void)e;
    push(createSettingsScreen);
}

void openGamesCb(lv_event_t *e)
{
    (void)e;
    push(werewolf::createEntryScreen);
}

// マーク（上）と名前（下）を載せた四角いボタン
void makeTile(lv_obj_t *scr, const Rect &r, const char *icon, const char *name, lv_event_cb_t cb)
{
    lv_obj_t *btn = makeRectButton(scr, r, "", cb, nullptr, true, false);

    lv_obj_t *ic = lv_label_create(btn);
    lv_obj_set_style_text_font(ic, &ct_font_icons_54, 0);
    lv_obj_set_style_text_color(ic, CT_COLOR_ACCENT_HI, 0);
    lv_label_set_long_mode(ic, LV_LABEL_LONG_CLIP);
    lv_label_set_text(ic, icon);
    lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *l = lv_label_create(btn);
    lv_obj_set_style_text_font(l, &ct_font_jp_20, 0);
    lv_obj_set_style_text_color(l, CT_COLOR_TEXT, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, r.w);
    lv_label_set_text(l, name);
    lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -14);
}

}  // namespace

lv_obj_t *createMainMenu()
{
    lv_obj_t *scr = makeScreen();
    makeHeading(scr, "メニュー");

    makeTile(scr, kTile[0], kIconToday, "今日の状況", openTodayCb);
    makeTile(scr, kTile[1], kIconHistory, "履歴", openHistoryCb);
    makeTile(scr, kTile[2], kIconSettings, "設定", openSettingsCb);
    makeTile(scr, kTile[3], kIconGames, "ゲーム", openGamesCb);

    makeMenuBackButton(scr, "カフェへ");
    return scr;
}

}  // namespace ui
