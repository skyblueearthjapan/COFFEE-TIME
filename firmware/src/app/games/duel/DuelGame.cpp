#include "DuelGame.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_random.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../../CupState.h"
#include "../../Display.h"
#include "../../HomeScreen.h"
#include "../../NetService.h"
#include "../../ui/ScreenManager.h"
#include "../../ui/UiKit.h"
#include "DuelStore.h"
#include "core/duel_core.hpp"

LV_FONT_DECLARE(ct_font_jp_20);
LV_FONT_DECLARE(ct_font_jp_22);
LV_FONT_DECLARE(ct_font_jp_40);
LV_FONT_DECLARE(ct_font_icons_54);
LV_FONT_DECLARE(ct_font_hands_54);

namespace duel {
namespace {

namespace core = coffee::duel;

using core::Hand;
using core::HabitCard;
using core::HabitId;
using core::Probs;
using core::Provider;
using core::Result;
using ui::Rect;

// ---------------------------------------------------------------------------
// 画面の配置
//
// 丸型なので中心 (240,240)・半径 224px（設計書 13.1）の円の内側に**四隅すべて**が
// 入ることを座標ごとに確かめてある。円は上下ほど狭いので、y が 420 を超える行には
// 幅 240px 以上のものを置けない。
//
// じゃんけんの 3 ボタンは設計書 13.1 の中心 (120,264)/(240,264)/(360,264)・半径 54
// をそのまま使う（相互の間隔 12px も設計書どおり）。手の名前はボタンの中に入れず
// 下の行に置いた。図形フォントの行の高さが 54px より大きく、円の中で窮屈だったため。
// ---------------------------------------------------------------------------
namespace layout {

// 見出しは全画面共通。エスパーと同じ y=34（実機で確認済みの位置）に置き、
// 文言は全角 10 文字までにして円からはみ出させない
constexpr Rect kTitle      {120,  34, 240, 26};

// --- entry ------------------------------------------------------------------
constexpr Rect kEntryLogo  { 90, 118, 300, 56};
constexpr Rect kEntrySub   { 80, 184, 320, 30};
constexpr Rect kEntryStart {130, 238, 220, 56};
constexpr Rect kEntryHow   {130, 304, 220, 48};
constexpr Rect kEntryBack  {140, 364, 200, 46};

// --- howto ------------------------------------------------------------------
constexpr Rect kHowBody    { 84,  70, 312, 296};
constexpr Rect kHowBack    {140, 378, 200, 48};

// --- players ----------------------------------------------------------------
constexpr Rect kPlayerNote { 84,  64, 312, 24};
constexpr Rect kAvatar[8]  = {{ 73, 102, 78, 88}, {158, 102, 78, 88},
                              {244, 102, 78, 88}, {329, 102, 78, 88},
                              { 73, 198, 78, 88}, {158, 198, 78, 88},
                              {244, 198, 78, 88}, {329, 198, 78, 88}};
constexpr Rect kGuestBtn   {140, 296, 200, 46};
constexpr Rect kPlayersBack{150, 350, 180, 44};

// --- profile ----------------------------------------------------------------
constexpr Rect kProfIcon   {200,  62, 80, 62};
constexpr Rect kProfLine1  { 84, 126, 312, 26};
constexpr Rect kProfLine2  { 84, 154, 312, 26};
constexpr Rect kProfLevel  { 84, 182, 312, 26};
constexpr Rect kProfPlay   {130, 216, 220, 54};
constexpr Rect kProfHabits {130, 276, 220, 46};
constexpr Rect kProfErase  {130, 328, 220, 44};
constexpr Rect kProfBack   {160, 380, 160, 42};

// --- intro ------------------------------------------------------------------
constexpr Rect kIntroBody  { 84,  84, 312, 176};
constexpr Rect kIntroStart {130, 272, 220, 56};
constexpr Rect kIntroBack  {150, 342, 180, 46};

// --- choose（設計書 13.1） ---------------------------------------------------
constexpr Rect kChooseScore{ 84,  66, 312, 26};
constexpr Rect kChooseRound{130,  96, 220, 46};
constexpr Rect kChooseState{ 84, 148, 312, 52};
constexpr Rect kHandBtn[3] = {{66, 210, 108, 108}, {186, 210, 108, 108}, {306, 210, 108, 108}};
constexpr Rect kHandName[3]= {{66, 322, 108,  26}, {186, 322, 108,  26}, {306, 322, 108,  26}};
constexpr Rect kChooseCafe {126, 356, 104, 42};
constexpr Rect kChooseQuit {250, 356, 104, 42};

// --- result -----------------------------------------------------------------
constexpr Rect kResYouTag  { 90,  64, 120, 24};
constexpr Rect kResAiTag   {270,  64, 120, 24};
constexpr Rect kResYouIcon { 90,  86, 120, 66};
constexpr Rect kResAiIcon  {270,  86, 120, 66};
constexpr Rect kResYouName { 90, 154, 120, 26};
constexpr Rect kResAiName  {270, 154, 120, 26};
constexpr Rect kResVerdict { 84, 184, 312, 48};
constexpr Rect kResPredict { 84, 234, 312, 26};
constexpr Rect kResSource  { 84, 260, 312, 26};
constexpr Rect kResScore   { 84, 286, 312, 26};
constexpr Rect kResNext    {130, 318, 220, 54};
constexpr Rect kResCafe    {128, 380, 102, 44};
constexpr Rect kResQuit    {250, 380, 102, 44};

// --- summary ----------------------------------------------------------------
constexpr Rect kSumVerdict { 84,  66, 312, 50};
constexpr Rect kSumTally   { 84, 124, 312, 30};
constexpr Rect kSumBreak   { 84, 158, 312, 48};
constexpr Rect kSumNote    { 84, 208, 312, 46};
constexpr Rect kSumAgain   {130, 258, 220, 50};
constexpr Rect kSumHabits  {130, 314, 220, 46};
constexpr Rect kSumEnd     {150, 366, 180, 46};

// --- habits -----------------------------------------------------------------
// カードは下敷きの色が見えるので、上の 2 枚は幅を 300 に詰めて円の内側へ入れた
constexpr Rect kHabitCard[3] = {{90, 76, 300, 84}, {90, 168, 300, 84}, {90, 260, 300, 84}};
constexpr Rect kHabitNote  { 84, 350, 312, 26};
constexpr Rect kHabitBack  {150, 382, 180, 44};

// --- 途中終了の確認 ----------------------------------------------------------
constexpr Rect kAskBody    { 84, 118, 312, 116};
constexpr Rect kAskPrimary {110, 258, 260, 52};
constexpr Rect kAskSecond  {110, 322, 260, 48};

// --- カフェ / ひと休み（ほかのゲームと同じ並び）------------------------------
constexpr Rect kPanelBody  { 94, 138, 292, 58};
constexpr Rect kPanel[3]   = {{110, 214, 260, 54}, {110, 278, 260, 54}, {110, 342, 260, 54}};

}  // namespace layout

// ---------------------------------------------------------------------------
// 画面の種類（計画 §3）
// ---------------------------------------------------------------------------
enum class View : uint8_t {
    Entry,      // 「AI DUEL」「あなたの次の一手を読めるか」
    HowTo,      // あそびかた
    Players,    // 8 つのアイコン ＋ ゲスト
    Profile,    // その人の記録
    Intro,      // 10 回勝負のまえおき
    Choose,     // n/10・スコア・グー / チョキ / パー
    RoundEnd,   // 両者の手・勝敗・予測
    Summary,    // 10 回の結果
    Habits,     // 癖のカード（最大 3 枚）
    Confirm,    // 途中終了の確認
    Cafe,       // 共通カフェパネル
    Paused,     // 無操作でひと休み
};

enum class Act : int {
    EntryStart = 1, EntryHow, EntryBack, HowBack,
    GuestPick, PlayersBack,
    ProfPlay, ProfHabits, ProfBack,
    IntroStart, IntroBack,
    HandRock, HandScissors, HandPaper,
    RoundNext,
    SumAgain, SumHabits, SumEnd,
    HabitsBack,
    QuitAsk, QuitYes, QuitNo,
    Cafe, CafeCoffee, CafeBack, CafeHome,
    PausedResume, PausedQuit,
};

// ---------------------------------------------------------------------------
// アイコン 8 種（Material Icons Round。ct_font_icons_54 に入っている）
// ---------------------------------------------------------------------------
struct Avatar {
    const char *glyph;
    const char *name;
};
constexpr Avatar kAvatars[duel::kProfileSlots] = {
    {"\xEE\xBF\xAF", "カップ"},     // U+EFEF coffee
    {"\xEE\x8F\xAA", "まめ"},        // U+E3EA grain
    {"\xEE\xA0\xB8", "ほし"},        // U+E838 star
    {"\xEE\x94\x9C", "つき"},        // U+E51C dark_mode
    {"\xEE\x90\xB0", "たいよう"},    // U+E430 wb_sunny
    {"\xEE\xA8\xB5", "はっぱ"},      // U+EA35 eco
    {"\xEE\x8A\xBD", "くも"},        // U+E2BD cloud
    {"\xEE\xA2\xB8", "はぐるま"},    // U+E8B8 settings
};

// じゃんけんの手（Font Awesome Free Solid。ct_font_hands_54 に入っている）
constexpr const char *kHandGlyph[3] = {
    "\xEF\x89\x95",     // U+F255 hand-back-fist（グー）
    "\xEF\x89\x97",     // U+F257 hand-scissors（チョキ）
    "\xEF\x89\x96",     // U+F256 hand（パー）
};
constexpr const char *kHandName[3] = {"グー", "チョキ", "パー"};

// ゲストのマーク。8 人のだれでもない印として「人」を出す
// （カップを暗くして出すと「カップの人」に見えてしまうため）
constexpr const char *kGuestGlyph = "\xEE\x9F\xBD";     // U+E7FD person

// 記録量レベルの呼び名（設計書 5.3 / duel_config.json の profile_levels）
constexpr const char *kLevelName[4] = {
    "はじめまして", "記録が増えています", "履歴を活用中", "長期履歴あり",
};

constexpr size_t kGuestSlot = duel::kProfileSlots;   // 8 = ゲスト
constexpr uint32_t kIdlePauseMs = 180 * 1000;        // 3 分放置でひと休み
// 計画 §5：これを超えたら統計 AI。実機（RSSI -91）で GAS の往復が 3.0〜5.5 秒だったので、
// 9 秒まで待つ（NetService 側の上限は つなぐまで 3 + POST 6.5 + GET 5 = 14.5 秒なので、
// 先にこちらが見切る）。待っている間ボタンは無効なので、これ以上は伸ばさない
constexpr uint32_t kJevWaitMs = 9000;
constexpr uint32_t kInputLatchMs = 250;              // 画面が変わった直後の誤タップよけ
constexpr uint32_t kEraseHoldMs = 1500;              // 「記録を消す」の長押し

// ---------------------------------------------------------------------------
// 状態
// ---------------------------------------------------------------------------
lv_obj_t *s_screen = nullptr;
lv_obj_t *s_content = nullptr;
lv_timer_t *s_tick = nullptr;

View s_view = View::Entry;
View s_cafe_return = View::Entry;
View s_paused_return = View::Entry;
View s_habits_return = View::Profile;
View s_confirm_return = View::Choose;
bool s_dirty = true;
uint32_t s_view_ms = 0;

size_t s_slot = kGuestSlot;             // 選んだアイコン（8 = ゲスト）
core::Stats s_guest;                    // ゲストの統計。RAM だけ・対戦ごとに白紙
uint16_t s_guest_seq = 0;

bool s_in_match = false;
uint16_t s_match_id = 0;
uint8_t s_round = 1;                    // これから遊ぶ回（1..10）
uint8_t s_resolved = 0;                 // この対戦で確定したラウンド数（途中終了の判定用）
uint8_t s_score[3] = {0, 0, 0};         // human_win / ai_win / draw
uint8_t s_provider_rounds[2] = {0, 0};  // jev / stats で戦った回数
uint8_t s_provider_hits[2] = {0, 0};    // そのうち予測が当たった回数
bool s_counted = false;                 // gamePlayed をもう数えたか

// この回の予測と AI の手
Probs s_used;                           // 採用した確率（Jev か 統計 AI）
Provider s_provider = Provider::Stats;
bool s_jev_requested = false;           // Jev に頼んだか（採用したかは s_provider）
bool s_ai_fixed = false;                // AI の手が確定したか
Hand s_ai_hand = Hand::Rock;
bool s_round_done = false;              // この回の手を確定したか（二度押しよけ）

// 通信（依頼は 1 件だけ。net:: の依頼箱を使う）
uint32_t s_req_no = 0;
bool s_req_pending = false;     // 予測の返事を待っている（ボタンが無効なのはこの間だけ）
uint32_t s_req_ms = 0;
// 予測を頼まなかった / 使えなかった理由。ログに 1 行で出すだけ（画面には出さない）
char s_req_reason[32] = {0};

// 確定したラウンドの控え（シートへ送るぶん）。
// **1 ラウンドごとには送らない。** GAS は Jev とシート書き込みを終えてから答えるので、
// 記録を予測の往復に相乗りさせると 1 回の往復が 5 秒級になり、予測が毎回間に合わなかった
// （2026-09-22 の実機試験）。10 回ぶんを RAM にためて、10 回のけっか画面へ進んだとき
// （と、1 回以上確定した対戦を途中でやめたとき）に 1 往復だけで送る。
// 送れなければ黙って捨てる（再送もしないし、遊びを待たせもしない）
struct LogEntry {
    uint8_t round_no;
    Hand you;
    Hand ai;
    Result result;
    Provider provider;
};
LogEntry s_logs[core::kRoundsPerMatch];
uint8_t s_log_count = 0;

// 結果画面に出すぶんだけ控える（次の回の準備で s_used などは上書きされるため）
struct RoundView {
    Hand player = Hand::Rock;
    Hand ai = Hand::Rock;
    Result result = Result::Draw;
    Provider provider = Provider::Stats;
    bool fell_back = false;             // Jev に頼んだのに使えなかった
    Hand predicted = Hand::Rock;
    uint8_t percent = 33;
    uint8_t round_no = 1;
};
RoundView s_last;

// 「記録を消す」の長押し
uint32_t s_erase_ms = 0;
bool s_erase_fired = false;

core::Stats &activeStats()
{
    return s_slot >= kGuestSlot ? s_guest : duel::mutableStats(s_slot);
}

bool isGuest()
{
    return s_slot >= kGuestSlot;
}

// ---------------------------------------------------------------------------
// 部品づくり（エスパー・探偵と同じ考え方）
// ---------------------------------------------------------------------------
uint16_t lineUnits(const char *begin, const char *end)
{
    uint16_t cells = 0;
    for (const char *p = begin; p < end; ++p) {
        if (((unsigned char)*p & 0xC0) == 0x80) {
            continue;
        }
        cells = (uint16_t)(cells + (((unsigned char)*p < 0x80) ? 1 : 2));
    }
    return cells;
}

uint16_t widestUnits(const char *t)
{
    uint16_t widest = 0;
    const char *line = t;
    for (const char *p = t;; ++p) {
        if (*p == '\n' || *p == '\0') {
            const uint16_t u = lineUnits(line, p);
            if (u > widest) {
                widest = u;
            }
            line = p + 1;
            if (*p == '\0') {
                break;
            }
        }
    }
    return widest;
}

uint16_t lineCount(const char *t)
{
    uint16_t n = 1;
    for (const char *p = t; *p != '\0'; ++p) {
        if (*p == '\n') {
            ++n;
        }
    }
    return n;
}

// 一番長い行が枠に収まるかで字の大きさを決める（1 全角 ≒ 11px 半角 2 つ分）
const lv_font_t *fitFont(const char *t, int16_t width)
{
    return (int16_t)(widestUnits(t) * 11) <= width ? &ct_font_jp_22 : &ct_font_jp_20;
}

// 大きく出したい文字。枠に入るなら 40px、入らなければ 22px
const lv_font_t *bigFont(const char *t, int16_t width)
{
    return (int16_t)(widestUnits(t) * 20) <= width ? &ct_font_jp_40 : &ct_font_jp_22;
}

lv_obj_t *rectLabel(const Rect &r, const lv_font_t *font, lv_color_t color, const char *t)
{
    lv_obj_t *l = lv_label_create(s_content);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    // 日本語は自動折返しが効かない。改行は文言に入れてあるので幅で切る
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, r.w);
    lv_label_set_text(l, t);
    const uint16_t lines = lineCount(t);
    const int16_t h = (int16_t)(font->line_height * lines);
    if (h > r.h && lines > 1) {
        lv_obj_set_height(l, r.h);
        lv_obj_set_pos(l, r.x, r.y);
    } else {
        lv_obj_set_pos(l, r.x, (int16_t)(r.h > h ? r.y + (r.h - h) / 2 : r.y));
    }
    return l;
}

void actionCb(lv_event_t *e);
void avatarCb(lv_event_t *e);
void eraseCb(lv_event_t *e);

lv_obj_t *makeButton(const Rect &r, const char *t, lv_event_cb_t cb, void *user_data,
                     bool enabled, bool primary, const lv_font_t *font = nullptr)
{
    lv_obj_t *btn = lv_btn_create(s_content);
    lv_obj_set_pos(btn, r.x, r.y);
    lv_obj_set_size(btn, r.w, r.h);
    lv_obj_set_style_radius(btn, r.h / 2 > 24 ? 24 : r.h / 2, 0);
    lv_obj_set_style_bg_color(btn, primary ? CT_COLOR_ACCENT : CT_COLOR_PANEL, 0);
    lv_obj_set_style_bg_color(btn, CT_COLOR_ACCENT_HI, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, enabled ? CT_COLOR_ACCENT_HI : CT_COLOR_DIM, 0);
    lv_obj_set_style_border_width(btn, r.h < 48 ? 1 : 2, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    // 既定の内側余白が大きく、座標での配置が効かなくなるので 0 にする
    lv_obj_set_style_pad_all(btn, 0, 0);

    if (t != nullptr) {
        lv_obj_t *l = lv_label_create(btn);
        lv_obj_set_style_text_font(l, font != nullptr ? font : fitFont(t, (int16_t)(r.w - 8)), 0);
        lv_obj_set_style_text_color(l, enabled ? CT_COLOR_TEXT : CT_COLOR_DIM, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(l, (int16_t)(r.w - 8));
        lv_label_set_text(l, t);
        lv_obj_center(l);
    }
    if (enabled) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    } else {
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    }
    return btn;
}

lv_obj_t *rectButton(const Rect &r, const char *t, Act act, bool enabled = true,
                     bool primary = false)
{
    return makeButton(r, t, actionCb, (void *)(intptr_t)act, enabled, primary);
}

void makeTitle(const char *t)
{
    rectLabel(layout::kTitle, &ct_font_jp_20, CT_COLOR_SUBTEXT, t);
}

void makePanelBox(const Rect &r)
{
    lv_obj_t *o = lv_obj_create(s_content);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, r.x, r.y);
    lv_obj_set_size(o, r.w, r.h);
    lv_obj_set_style_bg_color(o, CT_COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, 12, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

// マーク 1 文字を枠の中央に置く（アイコンフォントは行の高さが枠より大きいことがある）
void iconLabel(const Rect &r, const lv_font_t *font, lv_color_t color, const char *glyph)
{
    lv_obj_t *l = lv_label_create(s_content);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, r.w);
    lv_label_set_text(l, glyph);
    const int16_t h = (int16_t)font->line_height;
    lv_obj_set_pos(l, r.x, (int16_t)(r.y + (r.h - h) / 2));
}

// ---------------------------------------------------------------------------
// 文言づくり
// ---------------------------------------------------------------------------
const char *verdictText(Result result, Provider provider)
{
    // 設計書 13.3 の固定文言
    if (result == Result::HumanWin) {
        return "あなたの勝ち！";
    }
    if (result == Result::Draw) {
        return "あいこ";
    }
    return provider == Provider::Jev ? "Jevの勝ち！" : "統計AIの勝ち！";
}

// 画面に出す呼び名（設計書 13.3）
const char *providerName(Provider p)
{
    return p == Provider::Jev ? "Jev" : "統計AI";
}

// 通信で使う名前（計画 §6 の契約。GAS 側は "jev" 以外を "stats" として扱う）
const char *providerId(Provider p)
{
    return p == Provider::Jev ? "jev" : "stats";
}

// 癖のカード 1 枚を 2 行の文字にする（設計書 4.5 のテンプレートだけを使う）
void habitText(const HabitCard &card, char *head, size_t head_size, char *body, size_t body_size)
{
    const char *subject = kHandName[(size_t)card.subject];
    const char *object = kHandName[(size_t)card.object];
    switch (card.id) {
    case HabitId::RecentFavorite:
        std::snprintf(head, head_size, "直近20回は%s", subject);
        break;
    case HabitId::ChangeAfterLoss:
        std::snprintf(head, head_size, "負けた次は手を変更");
        break;
    case HabitId::RepeatAfterWin:
        std::snprintf(head, head_size, "勝った次は同じ手");
        break;
    case HabitId::FirstHandFavorite:
        std::snprintf(head, head_size, "最初は%sが多め", subject);
        break;
    case HabitId::TransitionFavorite:
        std::snprintf(head, head_size, "%sの次は%s", subject, object);
        break;
    case HabitId::RepeatAfterDraw:
        std::snprintf(head, head_size, "あいこの次も同じ手");
        break;
    case HabitId::OverallFavorite:
        std::snprintf(head, head_size, "これまで%sが多め", subject);
        break;
    default:
        std::snprintf(head, head_size, "まだはっきりした");
        std::snprintf(body, body_size, "偏りは見つかっていません");
        return;
    }
    // 「73%で変える」のような断定はしない。件数だけを出す（設計書 4.5）
    const char *unit = card.id == HabitId::FirstHandFavorite ? "対戦" : "回";
    std::snprintf(body, body_size, "%lu/%lu %s", (unsigned long)card.hits,
                  (unsigned long)card.total, unit);
}

// ---------------------------------------------------------------------------
// 通信（計画 §5・§6）
//
// **LVGL のコールバックの中で通信しない。** net:: の依頼箱に預けるだけで、
// 実際の送受信はメインループ（net::poll）が行う。
// 送ってよいもの: 集計だけ。アイコン名・端末名・杯数・今回の手・未公開の AI の手は
// 絶対に入れない（設計書 5.2）。
// ---------------------------------------------------------------------------
const char *handId(Hand h)
{
    static const char *const kIds[3] = {"ROCK", "SCISSORS", "PAPER"};
    return kIds[(size_t)h];
}

const char *resultId(Result r)
{
    static const char *const kIds[3] = {"human_win", "ai_win", "draw"};
    return kIds[(size_t)r];
}

void addCounts(JsonObject obj, const uint32_t counts[3])
{
    for (size_t i = 0; i < 3; ++i) {
        obj[handId((Hand)i)] = counts[i];
    }
}

// プレイヤーの呼び名（GAS 側は ^(p[0-7]|guest)$ しか受け付けない）
void playerId(char *out, size_t size)
{
    if (isGuest()) {
        std::snprintf(out, size, "guest");
    } else {
        std::snprintf(out, size, "p%u", (unsigned)s_slot);
    }
}

// 組み上げた JSON を依頼箱へ預ける。入り切らない・箱がふさがっているときは false。
// detached = true は「返事を取りに来ない」依頼（NetService.h の 2 つの預け方を参照）
bool enqueue(JsonDocument &doc, uint32_t req, bool detached = false)
{
    static char body[net::kGasRequestMax];
    const size_t n = serializeJson(doc, body, sizeof(body));
    if (n == 0 || n >= sizeof(body) - 1) {
        // 入り切らなかった（切り詰められた）。壊れた JSON は送らない
        Serial.printf("[DUEL] request too large (%u bytes), dropped\n", (unsigned)n);
        return false;
    }
    return net::gasRequest(req, body, detached);
}

// 次の回の予測だけを頼む（記録は相乗りさせない）。
// reason には、頼めなかったときの短い理由を入れる
bool sendState(uint8_t next_round, const core::Probs &baseline, char *reason, size_t reason_size)
{
    if (!net::gasReady()) {
        std::snprintf(reason, reason_size, "not sent: offline");
        return false;
    }
    const uint32_t req = ++s_req_no;

    JsonDocument doc;
    doc["event"] = "duel";
    doc["req"] = req;

    {
        core::JevState st;
        core::buildJevState(activeStats(), s_match_id, next_round, baseline, st);

        JsonObject state = doc["state"].to<JsonObject>();
        state["game"] = "rock_paper_scissors";
        state["round_no"] = st.round_no;
        state["history_rounds"] = st.history_rounds;
        addCounts(state["overall_counts"].to<JsonObject>(), st.overall_counts);
        addCounts(state["recent20_counts"].to<JsonObject>(), st.recent20_counts);
        addCounts(state["first_hand_counts"].to<JsonObject>(), st.first_hand_counts);
        if (st.has_previous) {
            JsonObject prev = state["previous_round"].to<JsonObject>();
            prev["player_hand"] = handId(st.previous_player_hand);
            prev["ai_hand"] = handId(st.previous_ai_hand);
            prev["player_result"] = resultId(st.previous_player_result);
        } else {
            state["previous_round"] = nullptr;
        }
        JsonObject cond = state["conditional_next_counts"].to<JsonObject>();
        JsonObject by_hand = cond["by_hand"].to<JsonObject>();
        by_hand["sample_n"] = st.by_hand_sample_n;
        addCounts(by_hand, st.by_hand);
        JsonObject by_hr = cond["by_hand_result"].to<JsonObject>();
        by_hr["sample_n"] = st.by_hand_result_sample_n;
        addCounts(by_hr, st.by_hand_result);

        JsonArray seq = state["recent_sequence"].to<JsonArray>();
        for (uint8_t i = 0; i < st.sequence_count; ++i) {
            JsonObject item = seq.add<JsonObject>();
            item["new_match"] = st.sequence[i].new_match;
            item["history_truncated"] = st.sequence[i].history_truncated;
            item["round_no"] = st.sequence[i].round_no;
            item["player_hand"] = handId(st.sequence[i].player_hand);
            item["ai_hand"] = handId(st.sequence[i].ai_hand);
            item["player_result"] = resultId(st.sequence[i].player_result);
        }
        state["same_hand_streak"] = st.same_hand_streak;
        JsonObject base = state["stats_baseline"].to<JsonObject>();
        for (size_t i = 0; i < 3; ++i) {
            base[handId((Hand)i)] = st.stats_baseline.p[i];
        }
        state["notice"] =
            "Counts are observations, not certainties. The current hand is not present.";
    }

    if (!enqueue(doc, req)) {
        // 直前の対戦の記録を送っている最中など。待たずに統計 AI で進める
        std::snprintf(reason, reason_size, "not sent: busy");
        return false;
    }
    s_req_ms = millis();
    return true;
}

// ためておいた 1 対戦ぶんの記録をまとめて送る（計画 §6 の `logs`）。
// **返事は待たない**ので「投げっぱなし」で預ける（NetService.h）。画面を閉じる途中で
// 預けても、返事を取りに来る者がいないまま箱がふさがることがない。
// 送れなければ黙って捨てる（再送しない・遊びを止めない）。
// 依頼箱がふさがっていても、次の対戦の第 1 回の予測が奪われることはない:
// 予測を頼めなければ sendState が false を返し、その場で端末内の統計 AI に決まるので
// 手のボタンが無効のままになることはない
void flushLogs()
{
    if (s_log_count == 0) {
        return;
    }
    const uint8_t n = s_log_count;
    s_log_count = 0;            // 送れても送れなくても、同じ記録は二度と送らない
    if (!net::gasReady()) {
        Serial.printf("[DUEL] logs=%u dropped (offline)\n", (unsigned)n);
        return;
    }
    char player[8];
    playerId(player, sizeof(player));

    const uint32_t req = ++s_req_no;
    JsonDocument doc;
    doc["event"] = "duel";
    doc["req"] = req;
    JsonArray logs = doc["logs"].to<JsonArray>();
    for (uint8_t i = 0; i < n; ++i) {
        JsonObject row = logs.add<JsonObject>();
        row["player"] = player;
        row["match"] = s_match_id;
        row["round"] = s_logs[i].round_no;
        row["you"] = handId(s_logs[i].you);
        row["ai"] = handId(s_logs[i].ai);
        row["result"] = resultId(s_logs[i].result);
        row["provider"] = providerId(s_logs[i].provider);
    }
    if (!enqueue(doc, req, true)) {
        Serial.printf("[DUEL] logs=%u dropped (busy)\n", (unsigned)n);
        return;
    }
    Serial.printf("[DUEL] logs=%u sent req=%lu\n", (unsigned)n, (unsigned long)req);
}

// 返事を読む。Jev の確率を受け取れたら true（計画 §6 の契約）。
// 使えなかったときは reason に短い理由を入れる（**本文そのものはログに出さない**）
bool parseJevResult(const net::GasResult &r, Probs &out, char *reason, size_t reason_size)
{
    if (r.req != s_req_no) {
        std::snprintf(reason, reason_size, "stale reply");
        return false;
    }
    if (!r.ok) {
        // HTTPClient の戻り値。負の値は接続・読み取りの失敗（-11 = 読み取り待ちの時間切れ）
        std::snprintf(reason, reason_size, "http %d", r.status);
        return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, r.body) != DeserializationError::Ok ||
        doc["ok"].as<bool>() != true || doc["req"].as<uint32_t>() != s_req_no) {
        std::snprintf(reason, reason_size, "bad reply");
        return false;
    }
    const char *provider = doc["provider"] | "none";
    if (std::strcmp(provider, "jev") != 0) {
        // provider:"none"。GAS が付ける理由（NO_KEY / HTTP_xxx / DAILY_LIMIT …）をそのまま出す
        const char *why = doc["reason"] | "none";
        std::snprintf(reason, reason_size, "none:%.16s", why);
        return false;
    }
    JsonArrayConst p = doc["p"].as<JsonArrayConst>();
    if (p.isNull() || p.size() != 3) {
        std::snprintf(reason, reason_size, "bad reply");
        return false;
    }
    Probs raw;
    for (size_t i = 0; i < 3; ++i) {
        raw.p[i] = p[i].as<double>();
    }
    // 端末側でももう一度検証する（GAS を信用しきらない／設計書 6.2）
    if (!core::normalizeProbabilities(raw, out)) {
        std::snprintf(reason, reason_size, "bad reply");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 1 ラウンドの流れ（計画 §5）
// ---------------------------------------------------------------------------
// AI の手を決める。**いちど決めたら選び直さない**
void fixAiHand()
{
    const uint8_t mask = core::optimalActions(s_used);
    s_ai_hand = core::pickFromActions(mask, esp_random());
    s_ai_fixed = true;
    // 作り直すのは手をえらぶ画面のときだけ。結果画面を見ている最中に先読みの返事が
    // 届いて画面を作り直すと、ちらつくうえに「次へ」のタップを食べてしまう
    if (s_view == View::Choose) {
        s_dirty = true;
    }
}

// 次の回の準備。統計 AI の確率は必ずここで計算し、条件がそろえば Jev にも頼む。
// **どの道を通っても、ここを出たときには必ず「戦える状態」になっている**
// （頼めなければその場で AI の手を決めるので、手のボタンが無効のままにならない）
// 対戦の通し番号は「本当に始まる瞬間」に取る。
// 「対戦する」を押すたびに取ると、遊ばずに戻っただけで番号が飛び、NVS の meta も毎回書き換わる
void ensureMatchId()
{
    if (s_match_id != 0) {
        return;
    }
    if (isGuest()) {
        ++s_guest_seq;
        s_match_id = s_guest_seq == 0 ? 1 : s_guest_seq;   // ゲストは RAM の連番だけ
    } else {
        s_match_id = duel::newMatchId();
    }
}

void prepareRound(uint8_t round_no)
{
    if (round_no == 1) {
        ensureMatchId();    // 統計の遷移は match_id で判定するので、予測の前に決めておく
    }
    const core::Stats &st = activeStats();
    Probs baseline;
    if (!core::statsPrediction(st, s_match_id, round_no, baseline)) {
        baseline = Probs::uniform();
    }
    s_used = baseline;
    s_provider = Provider::Stats;
    s_ai_fixed = false;
    s_round_done = false;
    s_jev_requested = false;
    s_req_pending = false;
    s_req_reason[0] = '\0';

    // 設計書 5.3：記録が 0 のうちは Jev を呼ばない（根拠が無いため）
    if (st.rounds == 0) {
        std::snprintf(s_req_reason, sizeof(s_req_reason), "not sent: no history");
        Serial.printf("[DUEL] req=- -> stats (%s)\n", s_req_reason);
        fixAiHand();
        return;
    }
    if (sendState(round_no, baseline, s_req_reason, sizeof(s_req_reason))) {
        s_jev_requested = true;
        s_req_pending = true;
        return;
    }
    Serial.printf("[DUEL] req=- -> stats (%s)\n", s_req_reason);
    fixAiHand();
}

void startMatch()
{
    if (isGuest()) {
        // ゲストは「その対戦の中だけ」統計を使う（計画 §3）
        s_guest = core::Stats{};
    }
    s_match_id = 0;         // 通し番号は第 1 回の準備で取る（ensureMatchId）
    s_in_match = true;
    s_round = 1;
    s_resolved = 0;
    s_log_count = 0;
    // 前の対戦の「もう決まっている」状態を引きずらない（はじめる の二度押しよけが効くように）
    s_ai_fixed = false;
    s_round_done = false;
    s_req_pending = false;
    s_score[0] = s_score[1] = s_score[2] = 0;
    s_provider_rounds[0] = s_provider_rounds[1] = 0;
    s_provider_hits[0] = s_provider_hits[1] = 0;
    s_counted = false;
    s_last = RoundView{};
}

// 途中終了・放置終了（計画 §4）。確定済みのラウンドは残す
void abortMatch()
{
    if (s_in_match) {
        s_in_match = false;
        // 待っている予測はもう要らない。記録の送信はこのあと（箱を空けてから）。
        // 錠が取れなかったときは箱の状態が分からないので、そのことをログに残す
        // （このあとの flushLogs が「busy」で捨てても理由が追える）
        if (!net::gasCancel()) {
            Serial.println("[DUEL] mailbox was locked; the pending request was left alone");
        }
        s_req_pending = false;
        // 1 ラウンドも確定していなければ「対戦をやめた」ことにもしない
        if (s_resolved > 0) {
            core::noteMatchAborted(activeStats());
            if (!isGuest()) {
                duel::saveProfile(s_slot);
            }
        }
        s_resolved = 0;
    }
    // ここまでの確定ラウンドを 1 往復で送る（送れなければ捨てる）。
    // 10 回そろって s_in_match が false になったあと、けっか画面を見ずに
    // 画面を閉じた場合もここを通るので、記録が取り残されない
    flushLogs();
}

void setView(View v);

// プレイヤーが手を押した。ここで勝敗を出し、統計を更新して保存する
void playHand(Hand h)
{
    if (!s_ai_fixed || s_round_done || !s_in_match) {
        return;
    }
    if (millis() - s_view_ms < kInputLatchMs) {
        return;     // 画面が変わった直後の指の残り
    }

    const Result result = core::resultOf(h, s_ai_hand);

    // **先に統計へ反映し、コアが受け取ったときだけ先へ進む。**
    // 重複・勝敗の食い違い・確率の不正はコアが弾くので、その判定をこの画面の
    // 「1 回として数えてよいか」の唯一の入口にする（数だけ進んで記録が無い状態を作らない）
    core::ResolvedRound r;
    r.match_id = s_match_id;
    r.round_no = s_round;
    r.player_hand = h;
    r.ai_hand = s_ai_hand;
    r.player_result = result;
    r.provider_used = s_provider;
    r.probabilities = s_used;
    if (!core::appendResolved(activeStats(), r)) {
        // 起こらないはずの状態（同じ回の二重反映など）。このタップは無かったことにする
        Serial.printf("[DUEL] round %u was rejected by the core; the tap is ignored\n",
                      (unsigned)s_round);
        return;
    }

    s_round_done = true;
    ++s_resolved;
    ++s_score[(size_t)result];

    // 表示用の控え（次の回の準備で s_used は上書きされる）
    s_last.player = h;
    s_last.ai = s_ai_hand;
    s_last.result = result;
    s_last.provider = s_provider;
    s_last.fell_back = s_jev_requested && s_provider != Provider::Jev;
    s_last.round_no = s_round;
    size_t top = 0;
    for (size_t i = 1; i < 3; ++i) {
        if (s_used.p[i] > s_used.p[top]) {
            top = i;
        }
    }
    s_last.predicted = (Hand)top;
    s_last.percent = (uint8_t)(s_used.p[top] * 100.0 + 0.5);
    ++s_provider_rounds[(size_t)s_provider];
    if ((Hand)top == h) {
        ++s_provider_hits[(size_t)s_provider];
    }

    // シートへ送るぶんは RAM にためるだけ。送るのは 10 回のけっか／途中終了のとき 1 回
    if (s_log_count < core::kRoundsPerMatch) {
        s_logs[s_log_count++] = LogEntry{s_round, h, s_ai_hand, result, s_provider};
    }

    const bool last_round = s_round >= core::kRoundsPerMatch;
    if (last_round) {
        const Result winner = core::matchWinner(s_score[(size_t)Result::HumanWin],
                                                s_score[(size_t)Result::AiWin]);
        core::noteMatchCompleted(activeStats(), winner);
    }
    // NVS へ書くのは 1 ラウンドにつき 1 回だけ（計画 §4）。
    // 第 10 回は**対戦の勝敗を数えたあと**に書くので、「10 ラウンドは残ったのに
    // completed が増えていない」という中途半端な状態が電源断で残らない
    if (!isGuest()) {
        duel::saveProfile(s_slot);
    }

    if (last_round) {
        if (!s_counted) {
            s_counted = true;
            // 数えるのは回数だけ。手も勝敗も SD には残さない
            char note[40];
            std::snprintf(note, sizeof(note), "duel %s r%u",
                          isGuest() ? "guest" : "player", (unsigned)core::kRoundsPerMatch);
            cup::stats::gamePlayed(cup::GameId::Duel, note);
        }
        s_in_match = false;
        // 記録は「10回のけっか」へ進んだときに 1 往復で送る（結果画面の表示を待たせない）
    } else {
        // 結果を見ている間に次の回を先読みする（計画 §2・§5）
        prepareRound((uint8_t)(s_round + 1));
    }
    setView(View::RoundEnd);
}

// ---------------------------------------------------------------------------
// 画面づくり
// ---------------------------------------------------------------------------
void buildEntry()
{
    makeTitle("ゲーム");
    rectLabel(layout::kEntryLogo, &ct_font_jp_40, CT_COLOR_ACCENT_HI, "AI DUEL");
    rectLabel(layout::kEntrySub, &ct_font_jp_20, CT_COLOR_TEXT, "あなたの次の一手を読めるか");
    rectButton(layout::kEntryStart, "はじめる", Act::EntryStart, true, true);
    rectButton(layout::kEntryHow, "あそびかた", Act::EntryHow);
    rectButton(layout::kEntryBack, "ゲーム一覧へ", Act::EntryBack);
}

void buildHowTo()
{
    makeTitle("あそびかた");
    rectLabel(layout::kHowBody, &ct_font_jp_22, CT_COLOR_TEXT,
              "じゃんけんを 10 回します。\n"
              "相手は あなたの次の手を\n"
              "これまでの記録から予想し、\n"
              "先に自分の手を決めます。\n"
              "\n"
              "つながっていれば Jev、\n"
              "つながらなければ端末の\n"
              "統計AI が相手になります。\n"
              "\n"
              "記録はアイコンごとに\n"
              "この端末だけに残ります。");
    rectButton(layout::kHowBack, "もどる", Act::HowBack, true, true);
}

void buildPlayers()
{
    makeTitle("だれが遊びますか");
    rectLabel(layout::kPlayerNote, &ct_font_jp_20, CT_COLOR_SUBTEXT,
              "下の数字は ラウンド数");

    for (size_t i = 0; i < duel::kProfileSlots; ++i) {
        const Rect &r = layout::kAvatar[i];
        lv_obj_t *btn = makeButton(r, nullptr, avatarCb, (void *)(intptr_t)i, true,
                                   i == s_slot);
        lv_obj_set_style_radius(btn, 16, 0);

        lv_obj_t *icon = lv_label_create(btn);
        lv_obj_set_style_text_font(icon, &ct_font_icons_54, 0);
        lv_obj_set_style_text_color(icon, CT_COLOR_TEXT, 0);
        lv_label_set_text(icon, kAvatars[i].glyph);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 0);

        char count[16];
        std::snprintf(count, sizeof(count), "%lu",
                      (unsigned long)duel::stats(i).rounds);
        lv_obj_t *label = lv_label_create(btn);
        lv_obj_set_style_text_font(label, &ct_font_jp_20, 0);
        lv_obj_set_style_text_color(label, CT_COLOR_SUBTEXT, 0);
        lv_label_set_text(label, count);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -2);
    }

    rectButton(layout::kGuestBtn, "ゲスト（記録なし）", Act::GuestPick);
    rectButton(layout::kPlayersBack, "もどる", Act::PlayersBack);
}

void buildProfile()
{
    const core::Stats &st = activeStats();
    if (isGuest()) {
        makeTitle("ゲスト");
        iconLabel(layout::kProfIcon, &ct_font_icons_54, CT_COLOR_SUBTEXT, kGuestGlyph);
    } else {
        makeTitle(kAvatars[s_slot].name);
        iconLabel(layout::kProfIcon, &ct_font_icons_54, CT_COLOR_ACCENT_HI,
                  kAvatars[s_slot].glyph);
    }

    char line1[80];
    std::snprintf(line1, sizeof(line1), "対戦 %lu 回（勝 %lu ・ 負 %lu ・ 分 %lu）",
                  (unsigned long)st.match_counts.completed,
                  (unsigned long)st.match_counts.human_win,
                  (unsigned long)st.match_counts.ai_win,
                  (unsigned long)st.match_counts.draw);
    rectLabel(layout::kProfLine1, &ct_font_jp_20, CT_COLOR_TEXT, line1);

    char line2[64];
    std::snprintf(line2, sizeof(line2), "ラウンド %lu 回 ・ 途中終了 %lu 回",
                  (unsigned long)st.rounds, (unsigned long)st.match_counts.aborted);
    rectLabel(layout::kProfLine2, &ct_font_jp_20, CT_COLOR_SUBTEXT, line2);

    char level[64];
    std::snprintf(level, sizeof(level), "記録：%s", kLevelName[core::recordLevel(st.rounds)]);
    rectLabel(layout::kProfLevel, &ct_font_jp_20, CT_COLOR_ACCENT_HI, level);

    rectButton(layout::kProfPlay, "対戦する", Act::ProfPlay, true, true);
    rectButton(layout::kProfHabits, "癖を見る", Act::ProfHabits);
    if (isGuest()) {
        rectLabel(layout::kProfErase, &ct_font_jp_20, CT_COLOR_DIM,
                  "今回だけの記録です。\n次回には引き継ぎません");
    } else {
        // 長押し（1.5 秒）だけで消す。押し間違いで記録が飛ばないようにする。
        // eraseCb は PRESSED / PRESSING だけを見るので、CLICKED では何も起きない
        lv_obj_t *btn = makeButton(layout::kProfErase, "記録を消す（長押し）", eraseCb,
                                   nullptr, true, false);
        lv_obj_add_event_cb(btn, eraseCb, LV_EVENT_PRESSED, nullptr);
        lv_obj_add_event_cb(btn, eraseCb, LV_EVENT_PRESSING, nullptr);
    }
    rectButton(layout::kProfBack, "もどる", Act::ProfBack);
}

void buildIntro()
{
    makeTitle("AI DUEL");
    const core::Stats &st = activeStats();
    char body[280];
    std::snprintf(body, sizeof(body),
                  "10回勝負。\n相手は先に手を決めます。\n\n%s\n%s",
                  st.rounds < 10 ? "まだ記録が少ないため、\n予測は不確かです"
                                 : "あなたの記録から\n次の手を予想します",
                  isGuest() ? "今回だけの記録です。\n次回には引き継ぎません"
                            : "1 回ごとに記録します");
    rectLabel(layout::kIntroBody, &ct_font_jp_22, CT_COLOR_TEXT, body);
    rectButton(layout::kIntroStart, "はじめる", Act::IntroStart, true, true);
    rectButton(layout::kIntroBack, "もどる", Act::IntroBack);
}

void buildChoose()
{
    makeTitle("AI DUEL");

    char score[64];
    std::snprintf(score, sizeof(score), "あなた %u ・ 相手 %u ・ あいこ %u",
                  (unsigned)s_score[0], (unsigned)s_score[1], (unsigned)s_score[2]);
    rectLabel(layout::kChooseScore, &ct_font_jp_20, CT_COLOR_SUBTEXT, score);

    char round[24];
    std::snprintf(round, sizeof(round), "%u / %u", (unsigned)s_round,
                  (unsigned)core::kRoundsPerMatch);
    rectLabel(layout::kChooseRound, &ct_font_jp_40, CT_COLOR_TEXT, round);

    char state[96];
    if (!s_ai_fixed) {
        std::snprintf(state, sizeof(state), "相手が考え中…");
    } else {
        std::snprintf(state, sizeof(state), "相手の手は確定済み（%s）\n手をえらんでください",
                      providerName(s_provider));
    }
    rectLabel(layout::kChooseState, &ct_font_jp_20,
              s_ai_fixed ? CT_COLOR_TEXT : CT_COLOR_SUBTEXT, state);

    // AI の手が確定するまで押せない（設計書 1.2）。手は LV_EVENT_CLICKED で 1 回だけ確定する
    static const Act kHandAct[3] = {Act::HandRock, Act::HandScissors, Act::HandPaper};
    const bool enabled = s_ai_fixed && !s_round_done;
    for (size_t i = 0; i < 3; ++i) {
        lv_obj_t *btn = makeButton(layout::kHandBtn[i], nullptr, actionCb,
                                   (void *)(intptr_t)kHandAct[i], enabled, false);
        lv_obj_set_style_radius(btn, 54, 0);
        lv_obj_t *icon = lv_label_create(btn);
        lv_obj_set_style_text_font(icon, &ct_font_hands_54, 0);
        lv_obj_set_style_text_color(icon, enabled ? CT_COLOR_TEXT : CT_COLOR_DIM, 0);
        lv_label_set_text(icon, kHandGlyph[i]);
        lv_obj_center(icon);
        rectLabel(layout::kHandName[i], &ct_font_jp_20,
                  enabled ? CT_COLOR_TEXT : CT_COLOR_DIM, kHandName[i]);
    }

    rectButton(layout::kChooseCafe, "カフェ", Act::Cafe);
    rectButton(layout::kChooseQuit, "やめる", Act::QuitAsk);
}

void buildRoundEnd()
{
    char title[40];
    std::snprintf(title, sizeof(title), "%u 回め / %u", (unsigned)s_last.round_no,
                  (unsigned)core::kRoundsPerMatch);
    makeTitle(title);

    rectLabel(layout::kResYouTag, &ct_font_jp_20, CT_COLOR_SUBTEXT, "あなた");
    rectLabel(layout::kResAiTag, &ct_font_jp_20, CT_COLOR_SUBTEXT, "あいて");
    iconLabel(layout::kResYouIcon, &ct_font_hands_54, CT_COLOR_TEXT,
              kHandGlyph[(size_t)s_last.player]);
    iconLabel(layout::kResAiIcon, &ct_font_hands_54, CT_COLOR_ACCENT_HI,
              kHandGlyph[(size_t)s_last.ai]);
    rectLabel(layout::kResYouName, &ct_font_jp_20, CT_COLOR_TEXT,
              kHandName[(size_t)s_last.player]);
    rectLabel(layout::kResAiName, &ct_font_jp_20, CT_COLOR_ACCENT_HI,
              kHandName[(size_t)s_last.ai]);

    const char *verdict = verdictText(s_last.result, s_last.provider);
    rectLabel(layout::kResVerdict, bigFont(verdict, (int16_t)(layout::kResVerdict.w - 8)),
              s_last.result == Result::HumanWin ? CT_COLOR_ACCENT_HI : CT_COLOR_TEXT, verdict);

    // 予測は**答え合わせのあとだけ**出す（設計書 13.3）
    char predict[64];
    std::snprintf(predict, sizeof(predict), "相手の予測：%s %u%%",
                  kHandName[(size_t)s_last.predicted], (unsigned)s_last.percent);
    rectLabel(layout::kResPredict, &ct_font_jp_20, CT_COLOR_SUBTEXT, predict);

    char source[64];
    if (s_last.fell_back) {
        std::snprintf(source, sizeof(source), "この回は統計AIで対戦しました");
    } else {
        std::snprintf(source, sizeof(source), "この回の相手：%s", providerName(s_last.provider));
    }
    rectLabel(layout::kResSource, &ct_font_jp_20, CT_COLOR_DIM, source);

    char score[64];
    std::snprintf(score, sizeof(score), "あなた %u ・ 相手 %u ・ あいこ %u",
                  (unsigned)s_score[0], (unsigned)s_score[1], (unsigned)s_score[2]);
    rectLabel(layout::kResScore, &ct_font_jp_20, CT_COLOR_TEXT, score);

    const bool last_round = s_last.round_no >= core::kRoundsPerMatch;
    rectButton(layout::kResNext, last_round ? "10回のけっかへ" : "次へ", Act::RoundNext,
               true, true);
    rectButton(layout::kResCafe, "カフェ", Act::Cafe);
    rectButton(layout::kResQuit, "やめる", Act::QuitAsk, !last_round);
}

void buildSummary()
{
    makeTitle("10回戦のけっか");

    const Result winner = core::matchWinner(s_score[(size_t)Result::HumanWin],
                                            s_score[(size_t)Result::AiWin]);
    // 10 回戦の勝者名は、その対戦で多く戦った相手の名前で呼ぶ
    const Provider main_provider =
        s_provider_rounds[(size_t)Provider::Jev] > s_provider_rounds[(size_t)Provider::Stats]
            ? Provider::Jev : Provider::Stats;
    const char *verdict = winner == Result::Draw ? "今回は引き分け"
                                                 : verdictText(winner, main_provider);
    rectLabel(layout::kSumVerdict, bigFont(verdict, (int16_t)(layout::kSumVerdict.w - 8)),
              winner == Result::HumanWin ? CT_COLOR_ACCENT_HI : CT_COLOR_TEXT, verdict);

    char tally[64];
    std::snprintf(tally, sizeof(tally), "あなた %u ・ 相手 %u ・ あいこ %u",
                  (unsigned)s_score[0], (unsigned)s_score[1], (unsigned)s_score[2]);
    rectLabel(layout::kSumTally, &ct_font_jp_22, CT_COLOR_TEXT, tally);

    char breakdown[128];
    std::snprintf(breakdown, sizeof(breakdown),
                  "Jev %u 回 / 統計AI %u 回\n予測の的中 %u 回 / 10 回",
                  (unsigned)s_provider_rounds[(size_t)Provider::Jev],
                  (unsigned)s_provider_rounds[(size_t)Provider::Stats],
                  (unsigned)(s_provider_hits[0] + s_provider_hits[1]));
    rectLabel(layout::kSumBreak, &ct_font_jp_20, CT_COLOR_SUBTEXT, breakdown);

    rectLabel(layout::kSumNote, &ct_font_jp_20, CT_COLOR_DIM,
              isGuest() ? "今回だけの記録です。\n次回には引き継ぎません"
                        : "記録しました");

    rectButton(layout::kSumAgain, "もう一度", Act::SumAgain, true, true);
    rectButton(layout::kSumHabits, "癖を見る", Act::SumHabits);
    rectButton(layout::kSumEnd, "おわる", Act::SumEnd);
}

void buildHabits()
{
    makeTitle("あなたの癖");
    HabitCard cards[core::kMaxHabitCards];
    const uint8_t n = core::habitCards(activeStats(), cards, core::kMaxHabitCards);
    for (uint8_t i = 0; i < n && i < 3; ++i) {
        const Rect &r = layout::kHabitCard[i];
        makePanelBox(r);
        char head[80] = {0};
        char body[48] = {0};
        habitText(cards[i], head, sizeof(head), body, sizeof(body));
        rectLabel(Rect{(int16_t)(r.x + 8), (int16_t)(r.y + 8), (int16_t)(r.w - 16), 40},
                  &ct_font_jp_22, CT_COLOR_TEXT, head);
        rectLabel(Rect{(int16_t)(r.x + 8), (int16_t)(r.y + 48), (int16_t)(r.w - 16), 32},
                  &ct_font_jp_20, CT_COLOR_ACCENT_HI, body);
    }
    // 記述統計であって、性格や実力の診断ではない（設計書 4.5）
    rectLabel(layout::kHabitNote, &ct_font_jp_20, CT_COLOR_DIM,
              "20 件・60% は表示の目安です");
    rectButton(layout::kHabitBack, "もどる", Act::HabitsBack, true, true);
}

void buildConfirm()
{
    makeTitle("とちゅう終了");
    rectLabel(layout::kAskBody, &ct_font_jp_22, CT_COLOR_ALERT,
              "途中終了しますか？\n確定済みの記録は残ります");
    rectButton(layout::kAskPrimary, "やめる", Act::QuitYes);
    rectButton(layout::kAskSecond, "つづける", Act::QuitNo, true, true);
}

void buildCafe()
{
    makeTitle("カフェ");
    rectLabel(layout::kPanelBody, &ct_font_jp_22, CT_COLOR_TEXT,
              "コーヒーを1杯とりましたか？\nゲームはそのまま続きます。");
    rectButton(layout::kPanel[0], "＋1杯を記録", Act::CafeCoffee, true, true);
    rectButton(layout::kPanel[1], "ゲームへ戻る", Act::CafeBack);
    rectButton(layout::kPanel[2], "HOME へ", Act::CafeHome);
}

void buildPaused()
{
    makeTitle("ひと休み");
    rectLabel(layout::kPanelBody, &ct_font_jp_22, CT_COLOR_TEXT,
              "しばらく操作がありません。\nつづきから遊べます。");
    rectButton(layout::kPanel[0], "つづける", Act::PausedResume, true, true);
    rectButton(layout::kPanel[1], "＋1杯を記録", Act::CafeCoffee);
    rectButton(layout::kPanel[2], "やめて 一覧へ", Act::PausedQuit);
}

// ---------------------------------------------------------------------------
// 作り直し
// ---------------------------------------------------------------------------
void rebuild()
{
    if (s_content == nullptr) {
        return;
    }
    lv_obj_clean(s_content);
    switch (s_view) {
    case View::Entry:    buildEntry(); break;
    case View::HowTo:    buildHowTo(); break;
    case View::Players:  buildPlayers(); break;
    case View::Profile:  buildProfile(); break;
    case View::Intro:    buildIntro(); break;
    case View::Choose:   buildChoose(); break;
    case View::RoundEnd: buildRoundEnd(); break;
    case View::Summary:  buildSummary(); break;
    case View::Habits:   buildHabits(); break;
    case View::Confirm:  buildConfirm(); break;
    case View::Cafe:     buildCafe(); break;
    case View::Paused:   buildPaused(); break;
    }
}

void setView(View v)
{
    s_view = v;
    s_dirty = true;
    s_view_ms = millis();
}

// ---------------------------------------------------------------------------
// 入力
// ---------------------------------------------------------------------------
void avatarCb(lv_event_t *e)
{
    const int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= (int)duel::kProfileSlots) {
        return;
    }
    s_slot = (size_t)index;
    setView(View::Profile);
}

// 記録を消すのは 1.5 秒の長押しだけ（HOME の補充と同じ作法）
void eraseCb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_erase_ms = lv_tick_get();
        s_erase_fired = false;
        return;
    }
    if (code != LV_EVENT_PRESSING || s_erase_fired || isGuest()) {
        return;
    }
    if (lv_tick_elaps(s_erase_ms) < kEraseHoldMs) {
        return;
    }
    s_erase_fired = true;
    const bool ok = duel::resetProfile(s_slot);
    ui::showToast(s_screen, ok ? "記録を消しました" : "消せませんでした");
    s_dirty = true;
}

void actionCb(lv_event_t *e)
{
    const Act act = (Act)(intptr_t)lv_event_get_user_data(e);

    switch (act) {
    case Act::EntryStart:
        setView(View::Players);
        break;
    case Act::EntryHow:
        setView(View::HowTo);
        break;
    case Act::EntryBack:
        ui::pop();                  // ゲーム一覧へ戻る
        return;
    case Act::HowBack:
        setView(View::Entry);
        break;

    case Act::GuestPick:
        s_slot = kGuestSlot;
        s_guest = core::Stats{};
        setView(View::Profile);
        break;
    case Act::PlayersBack:
        setView(View::Entry);
        break;

    case Act::ProfPlay:
        if (millis() - s_view_ms < kInputLatchMs) {
            break;      // 画面が変わった直後の指の残り・連打
        }
        startMatch();
        setView(View::Intro);
        break;
    case Act::ProfHabits:
        s_habits_return = View::Profile;
        setView(View::Habits);
        break;
    case Act::ProfBack:
        setView(View::Players);
        break;

    case Act::IntroStart:
        // 二度押しよけ。もう準備が始まっていたら何もしない。
        // ここで prepareRound をやり直すと AI の手を選び直すことになり（設計書 1.2 違反）、
        // 依頼箱もふさがっていて Jev の回を 1 つ落としてしまう
        if (s_req_pending || s_ai_fixed || millis() - s_view_ms < kInputLatchMs) {
            break;
        }
        // intro を抜けるところで第 1 回の予測を始める（計画 §5 の 1）
        prepareRound(1);
        setView(View::Choose);
        break;
    case Act::IntroBack:
        abortMatch();
        setView(View::Profile);
        break;

    case Act::HandRock:     playHand(Hand::Rock); break;
    case Act::HandScissors: playHand(Hand::Scissors); break;
    case Act::HandPaper:    playHand(Hand::Paper); break;

    case Act::RoundNext:
        if (s_last.round_no >= core::kRoundsPerMatch) {
            // 10 回そろった。ここで初めて 1 対戦ぶんの記録をまとめて送る
            flushLogs();
            setView(View::Summary);
        } else {
            s_round = (uint8_t)(s_last.round_no + 1);
            setView(View::Choose);
        }
        break;

    case Act::SumAgain:
        if (millis() - s_view_ms < kInputLatchMs) {
            break;
        }
        startMatch();
        setView(View::Intro);
        break;
    case Act::SumHabits:
        s_habits_return = View::Summary;
        setView(View::Habits);
        break;
    case Act::SumEnd:
        setView(View::Profile);
        break;

    case Act::HabitsBack:
        setView(s_habits_return);
        break;

    case Act::QuitAsk:
        // どの画面から確認へ来たかを覚えておく（この先で s_round_done は次の回のために戻る）
        s_confirm_return = s_view;
        setView(View::Confirm);
        break;
    case Act::QuitYes:
        abortMatch();
        setView(View::Profile);
        break;
    case Act::QuitNo:
        setView(s_confirm_return);
        break;

    case Act::Cafe:
        s_cafe_return = s_view;
        setView(View::Cafe);
        break;
    case Act::CafeCoffee:
        // HOME の「+1」と同じ処理（記録も通知も同じ経路を通る）
        home::addOneCup();
        ui::showToast(s_screen, "＋1杯 記録しました");
        break;
    case Act::CafeBack:
        setView(s_cafe_return);
        break;
    case Act::CafeHome:
        abortMatch();
        ui::goHome();
        return;

    case Act::PausedResume:
        setView(s_paused_return);   // 途中の状態はそのまま残っている
        break;
    case Act::PausedQuit:
        abortMatch();
        ui::pop();
        return;
    }
}

// ---------------------------------------------------------------------------
// 100ms ごとの処理
// ---------------------------------------------------------------------------
bool isPlayingView(View v)
{
    return v == View::Players || v == View::Profile || v == View::Intro ||
           v == View::Choose || v == View::RoundEnd || v == View::Summary ||
           v == View::Habits || v == View::HowTo;
}

// Jev の返事を待つ。9 秒で見切って統計 AI に決める（計画 §5 の 4）。
// ここは**使えなかった理由まで 1 行に出す**（URL・合言葉・本文は出さない）
void pollPrediction()
{
    if (s_ai_fixed || !s_req_pending) {
        return;
    }
    net::GasResult r;
    if (net::gasTakeResult(r)) {
        Probs p;
        const bool took = parseJevResult(r, p, s_req_reason, sizeof(s_req_reason));
        if (took) {
            s_used = p;
            s_provider = Provider::Jev;
            s_req_reason[0] = '\0';
            Serial.printf("[DUEL] req=%lu -> jev %lums\n", (unsigned long)r.req,
                          (unsigned long)r.elapsed_ms);
        } else {
            Serial.printf("[DUEL] req=%lu -> stats %lums (%s)\n", (unsigned long)r.req,
                          (unsigned long)r.elapsed_ms, s_req_reason);
        }
        s_req_pending = false;
        fixAiHand();
        return;
    }
    if (millis() - s_req_ms >= kJevWaitMs) {
        // 遅れて届く返事は依頼番号が合わないので捨てられる
        net::gasCancel();
        s_req_pending = false;
        std::snprintf(s_req_reason, sizeof(s_req_reason), "timeout");
        Serial.printf("[DUEL] req=%lu -> stats %lums (timeout)\n", (unsigned long)s_req_no,
                      (unsigned long)(millis() - s_req_ms));
        fixAiHand();
    }
}

void tickCb(lv_timer_t *t)
{
    (void)t;
    pollPrediction();

    if (s_dirty) {
        rebuild();
        s_dirty = false;
        return;
    }
    // 誰も待っていない返事（記録だけの依頼・見切ったあとの予測）は受け取って捨てる。
    // 捨てないと箱がふさがったままになり、次の依頼が入らない
    if (!s_req_pending) {
        net::GasResult drop;
        if (net::gasTakeResult(drop)) {
            Serial.printf("[DUEL] req=%lu reply %s %lums (unwatched)\n",
                          (unsigned long)drop.req, drop.ok ? "ok" : "failed",
                          (unsigned long)drop.elapsed_ms);
        }
    }

    // 180 秒さわられなければ「ひと休み」を重ねる。途中の状態は RAM に残したまま
    if (isPlayingView(s_view) && lv_disp_get_inactive_time(nullptr) > kIdlePauseMs) {
        s_paused_return = s_view;
        setView(View::Paused);
    }
}

// ---------------------------------------------------------------------------
// 画面の生成・破棄
// ---------------------------------------------------------------------------
void screenDeletedCb(lv_event_t *e)
{
    // 画面の破棄は遷移アニメーション（200ms）の完了時なので、その間に次の DUEL 画面が
    // 作られていることがある。古い画面の後始末で新しい画面を壊さないよう照合する
    if (lv_event_get_target(e) != s_screen) {
        return;
    }
    display::setGameActive(false);
    if (s_tick != nullptr) {
        lv_timer_del(s_tick);
        s_tick = nullptr;
    }
    // abortMatch が待っている予測を捨て、確定済みラウンドの記録を 1 往復ぶんだけ預ける。
    // **そのあとで gasCancel を呼んではいけない**（いま預けた記録まで捨ててしまう）
    abortMatch();
    s_req_pending = false;
    s_dirty = false;
    s_ai_fixed = false;
    s_round_done = false;
    // 開発用コマンドが古い画面名を出さないよう、画面の種類も初期値に戻す
    s_view = View::Entry;
    s_cafe_return = View::Entry;
    s_paused_return = View::Entry;
    s_habits_return = View::Profile;
    s_confirm_return = View::Choose;
    s_slot = kGuestSlot;
    s_guest = core::Stats{};
    // 解放済みオブジェクトを触らないよう、静的ポインタは必ず全部消す
    s_screen = nullptr;
    s_content = nullptr;
}

const char *viewName(View v)
{
    switch (v) {
    case View::Entry:    return "entry";
    case View::HowTo:    return "howto";
    case View::Players:  return "players";
    case View::Profile:  return "profile";
    case View::Intro:    return "intro";
    case View::Choose:   return "choose";
    case View::RoundEnd: return "result";
    case View::Summary:  return "summary";
    case View::Habits:   return "habits";
    case View::Confirm:  return "confirm";
    case View::Cafe:     return "cafe";
    case View::Paused:   return "paused";
    }
    return "?";
}

}  // namespace

// ---------------------------------------------------------------------------
lv_obj_t *createGameScreen()
{
    // 前の DUEL 画面がまだ消えていない（遷移中の再入・開発用コマンドの連打）ことがある。
    // 古いタイマーを残すと 2 本が同じ部品を作り替えに来るので、ここで止める
    if (s_tick != nullptr) {
        lv_timer_del(s_tick);
        s_tick = nullptr;
    }
    // 対戦の途中で自動的に暗くならないようにする
    display::setGameActive(true);

    lv_obj_t *scr = ui::makeScreen();

    s_screen = scr;
    s_content = lv_obj_create(scr);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_pos(s_content, 0, 0);
    lv_obj_set_size(s_content, ui::kScreenSize, ui::kScreenSize);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);

    duel::load();
    // 前回の残り（遅れて届いた返事）を捨ててから始める
    net::gasCancel();
    net::GasResult drop;
    net::gasTakeResult(drop);

    // 入り直したときのため、状態を毎回そろえる
    s_view = View::Entry;
    s_cafe_return = View::Entry;
    s_paused_return = View::Entry;
    s_habits_return = View::Profile;
    s_confirm_return = View::Choose;
    s_slot = kGuestSlot;
    s_guest = core::Stats{};
    s_in_match = false;
    s_match_id = 0;
    s_round = 1;
    s_resolved = 0;
    s_log_count = 0;
    s_score[0] = s_score[1] = s_score[2] = 0;
    s_provider_rounds[0] = s_provider_rounds[1] = 0;
    s_provider_hits[0] = s_provider_hits[1] = 0;
    s_counted = false;
    s_used = Probs::uniform();
    s_provider = Provider::Stats;
    s_jev_requested = false;
    s_ai_fixed = false;
    s_round_done = false;
    s_req_pending = false;
    s_req_reason[0] = '\0';
    s_last = RoundView{};
    s_erase_fired = false;
    s_view_ms = millis();

    rebuild();
    s_dirty = false;

    s_tick = lv_timer_create(tickCb, 100, nullptr);
    lv_obj_add_event_cb(scr, screenDeletedCb, LV_EVENT_DELETE, nullptr);
    return scr;
}

void debugPrintPublicState()
{
    // **手を選ぶ前の AI の手は出さない**（開発用の表示で後出しできてしまうため）
    char slot[8];
    if (s_screen == nullptr) {
        std::snprintf(slot, sizeof(slot), "-");
    } else if (s_slot >= kProfileSlots) {
        std::snprintf(slot, sizeof(slot), "guest");
    } else {
        std::snprintf(slot, sizeof(slot), "p%u", (unsigned)s_slot);
    }
    // AI の手は、もう画面で公開されている場面（結果・10 回のけっか）でだけ出す
    const bool revealed = s_screen != nullptr &&
                          (s_view == View::RoundEnd || s_view == View::Summary);
    Serial.printf("[DUEL] view=%s slot=%s match=%u round=%u/%u score=%u-%u-%u "
                  "provider=%s fixed=%u pending=%u ai=%s rounds=%lu\n",
                  s_screen != nullptr ? viewName(s_view) : "-", slot,
                  (unsigned)s_match_id, (unsigned)s_round, (unsigned)core::kRoundsPerMatch,
                  (unsigned)s_score[0], (unsigned)s_score[1], (unsigned)s_score[2],
                  providerId(s_provider), s_ai_fixed ? 1u : 0u, s_req_pending ? 1u : 0u,
                  revealed ? kHandName[(size_t)s_last.ai] : "-",
                  s_screen != nullptr ? (unsigned long)activeStats().rounds : 0ul);
}

}  // namespace duel
