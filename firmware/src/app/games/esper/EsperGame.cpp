#include "EsperGame.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "../../CupState.h"
#include "../../Display.h"
#include "../../HomeScreen.h"
#include "../../ui/ScreenManager.h"
#include "../../ui/UiKit.h"
#include "EsperContent.h"
#include "EsperStats.h"
#include "core/esper_core.hpp"

LV_FONT_DECLARE(ct_font_jp_20);
LV_FONT_DECLARE(ct_font_jp_22);
LV_FONT_DECLARE(ct_font_jp_40);

namespace esper {
namespace {

namespace c = coffee::esp::content;
namespace core = coffee::esper;

using ui::Rect;

// ---------------------------------------------------------------------------
// 画面の配置
//
// 丸型なので中心 (240,240)・半径 228px の円の内側に**四隅すべて**が入ることを
// 座標ごとに確かめてある。円は上下ほど狭いので、y が 430 を超える行には
// 幅 200px 以上のものを置けない（下端 444 で置ける幅は約 203px）。
//
// 質問画面（G40）は設計書 5.2 の表を基にしている。表の値から動かしたところ:
//   - 「1つ戻る」「ヘルプ」を y=360 → 356 へ 4px 上げ、下のコーヒー行と離した
//   - コーヒー行を {158,410,164,34} → {152,404,176,40} へ広げた（指で押しやすくするため）
//   - 質問文は 28px ではなく 22px（この端末のゲーム用フォントは 20/22/40px の 3 種類。
//     字を小さくしないという決まりなので、折り返し幅のほうを全角 13 文字に合わせた）
// ---------------------------------------------------------------------------
namespace layout {

// --- 全画面で共通に使うもの -------------------------------------------------
constexpr Rect kTitle      {140,  34, 200, 26};   // 見出し（20px）
constexpr Rect kTitleWide  {134,  34, 212, 26};

// --- G10 むずかしさ選び -----------------------------------------------------
constexpr Rect kModeLead   { 94,  64, 292, 56};
// 遊び用の 4 モード（ミニ / レギュラー / フル / 超難関）。2 行の文字（20px × 2）が入る高さにする
constexpr Rect kModeBtn[4] = {{88, 114, 304, 52}, {88, 170, 304, 52}, {88, 226, 304, 52},
                              {88, 282, 304, 52}};
constexpr Rect kModeCafe   { 96, 344, 120, 46};
constexpr Rect kModeStats  {230, 344, 120, 46};
constexpr Rect kModeBack   {160, 396, 160, 44};

// --- G20 候補一覧 / G70 本当の答え選び --------------------------------------
constexpr Rect kCatHint    { 94,  66, 292, 26};
constexpr Rect kCatTab[4]  = {{94, 98, 70, 38}, {170, 98, 70, 38},
                              {246, 98, 70, 38}, {322, 98, 70, 38}};
constexpr Rect kCatItem[4] = {{88, 142, 304, 46}, {88, 192, 304, 46},
                              {88, 242, 304, 46}, {88, 292, 304, 46}};
constexpr Rect kCatPrev    { 94, 342,  92, 40};
constexpr Rect kCatPage    {198, 342,  84, 40};
constexpr Rect kCatNext    {294, 342,  92, 40};
constexpr Rect kCatBack    {130, 388,  94, 44};
constexpr Rect kCatDecide  {256, 388,  94, 44};
constexpr Rect kCatWide    {124, 388, 232, 44};   // 本当の答え選びの「えらばずに おわる」

// --- G21 カード -------------------------------------------------------------
constexpr Rect kCardName   { 88,  66, 304, 48};
constexpr Rect kCardDef    { 88, 122, 304, 116};
constexpr Rect kCardLead   { 94, 244, 292, 52};
constexpr Rect kCardBack   {112, 306, 100, 46};
constexpr Rect kCardDecide {268, 306, 100, 46};
constexpr Rect kCardCafe   {160, 362, 160, 44};

// --- G30 じゅんび -----------------------------------------------------------
constexpr Rect kReadyBody  { 94,  66, 292, 256};  // 22px・最大 9 行（252px）
constexpr Rect kReadyStart {130, 330, 220, 52};
constexpr Rect kReadyBack  {132, 390, 100, 44};
constexpr Rect kReadyCafe  {248, 390, 100, 44};

// --- G41 考えています -------------------------------------------------------
constexpr Rect kThinkBig   { 94, 186, 292, 60};
constexpr Rect kThinkSub   { 94, 252, 292, 30};

// --- G40 質問（設計書 5.2） -------------------------------------------------
constexpr Rect kQProgress  {140,  66, 200, 26};
constexpr Rect kQText      { 94,  96, 292, 96};
constexpr Rect kQRemain    {110, 194, 260, 26};
constexpr Rect kQGuide     { 94, 222, 292, 52};
constexpr Rect kQMemo     { 94, 226, 292,  44};   // メモがあるときは案内文の場所にこのボタンを出す
constexpr Rect kMemoNote   { 94, 244, 292,  52};
constexpr Rect kMemoBack   {130, 306, 220,  46};
constexpr Rect kQYes       { 86, 278, 144, 72};
constexpr Rect kQNo        {250, 278, 144, 72};
constexpr Rect kQBack      {108, 356, 116, 42};
constexpr Rect kQHelp      {256, 356, 116, 42};
constexpr Rect kQCafe      {152, 404, 176, 40};

// --- G42 ヘルプ -------------------------------------------------------------
constexpr Rect kHelpQ      { 94,  64, 292, 62};
constexpr Rect kHelpBody   { 94, 130, 292, 144};
constexpr Rect kHelpSkip   { 96, 280, 288, 48};
constexpr Rect kHelpBack   { 96, 334, 288, 44};
constexpr Rect kHelpQuit   {140, 384, 200, 44};

// --- やめる確認 / G90 エラー ------------------------------------------------
constexpr Rect kAskBody    { 94, 120, 292, 116};
constexpr Rect kAskPrimary {110, 268, 260, 52};
constexpr Rect kAskSecond  {110, 332, 260, 48};

// --- G50 最終予想 -----------------------------------------------------------
constexpr Rect kGuessName  { 88,  62, 304, 50};
constexpr Rect kGuessDef   { 94, 116, 292, 116};
constexpr Rect kGuessLead  { 94, 234, 292, 52};
constexpr Rect kGuessYes   { 86, 292, 144, 68};
constexpr Rect kGuessNo    {250, 292, 144, 68};
constexpr Rect kGuessBack  {112, 366, 116, 42};
constexpr Rect kGuessCafe  {252, 366, 116, 42};

// --- G60 結果 ---------------------------------------------------------------
constexpr Rect kResHead    { 94,  62, 292, 88};
constexpr Rect kResDetail  { 94, 154, 292, 78};
constexpr Rect kResTally   { 94, 236, 292, 26};
constexpr Rect kResReveal  { 96, 268, 288, 46};
constexpr Rect kResAgain   { 96, 320, 288, 46};
constexpr Rect kResHome    {132, 372, 100, 44};
constexpr Rect kResCafe    {248, 372, 100, 44};

// --- G71 食い違いの説明 -----------------------------------------------------
constexpr Rect kRevName    { 88,  64, 304, 44};
constexpr Rect kRevIntro   { 94, 110, 292, 56};
constexpr Rect kRevQuestion{ 94, 170, 292, 62};
constexpr Rect kRevAnswer  { 94, 236, 292, 56};
constexpr Rect kRevBody    { 94, 110, 292, 180};  // 一覧外・食い違いなしのときの本文
constexpr Rect kRevPrev    { 96, 296,  92, 42};
constexpr Rect kRevPage    {196, 296,  88, 42};
constexpr Rect kRevNext    {292, 296,  92, 42};
constexpr Rect kRevDone    {110, 346, 260, 48};

// --- G80 きろく -------------------------------------------------------------
constexpr Rect kStatRow[4] = {{84, 68, 312, 44}, {84, 118, 312, 44}, {84, 168, 312, 44},
                              {84, 218, 312, 44}};
constexpr Rect kStatNote   { 94, 270, 292, 80};
constexpr Rect kStatBack   {140, 356, 200, 46};

// --- カフェ / ひと休み（探偵ゲームと同じ並び） ------------------------------
constexpr Rect kPanelBody  { 94, 140, 292, 56};
constexpr Rect kPanel[3]   = {{110, 214, 260, 54}, {110, 278, 260, 54}, {110, 342, 260, 54}};

}  // namespace layout

// ---------------------------------------------------------------------------
// 画面の種類（設計書 5.3）
//
// 通信専用の画面（再接続・同期待ち）は段階 1 では作らない。
// G90 ERROR は「候補が 0 になった」＝データ不一致のときだけ使う（設計書 3.2）。
// ---------------------------------------------------------------------------
enum class View : uint8_t {
    Mode,        // G10 むずかしさ
    Catalog,     // G20 候補一覧（頭の中で 1 つ決める）
    Card,        // G21 カード
    Ready,       // G30 じゅんび
    Thinking,    // G41 考えています（重い計算はこの画面が出ている間に行う）
    Question,    // G40 質問
    Help,        // G42 この質問について
    Quit,        // やめる確認
    Guess,       // G50 最終予想
    Result,      // G60 結果
    RevealPick,  // G70 本当の答えを選ぶ（一覧を作り替えて使う）
    RevealCard,  // G71 食い違いの説明
    Stats,       // G80 きろく
    Error,       // G90 データ不一致
    Cafe,        // 共通カフェパネル
    Paused,      // 無操作でひと休み（状態は残したまま）
    Memo,        // 自分が選んだもののメモ（名前と特徴を読み返す。AI には渡さない）
};

// 押した内容。lv_event の user_data に入れて 1 つのコールバックで処理する
enum class Act : int {
    ModeBack = 1, ModeStats,
    CatPrev, CatNext, CatBack, CatDecide, CatGiveUp,
    CardBack, CardDecide,
    ReadyStart, ReadyBack,
    AnsYes, AnsNo, AnsHelp, AnsUndo,
    HelpSkip, HelpGuessNow, HelpBack, HelpQuit,
    QuitOk, QuitNo,
    GuessYes, GuessNo, GuessUndo,
    ResAgain, ResHome, ResReveal,
    RevPrev, RevNext, RevDone,
    StatsBack,
    ErrorAgain, ErrorHome,
    Cafe, CafeCoffee, CafeBack, CafeHome,
    PausedResume, PausedQuit,
    QMemo, MemoBack,
};

// 次の tick で行う重い計算（「考えています」を先に描いてから動かす）
enum class Pending : uint8_t { None = 0, Start, Yes, No, Skip, GuessNow };

// ---------------------------------------------------------------------------
// 状態
// ---------------------------------------------------------------------------
lv_obj_t *s_screen = nullptr;
lv_obj_t *s_content = nullptr;
lv_timer_t *s_tick = nullptr;

// 推論コアは 50KB 級。LVGL タスクのスタック（8KB）にも .bss にも置かず、
// PSRAM から取って使い回す（取れなければ通常のヒープへ落とす）
core::Engine *s_engine = nullptr;
core::LocalAdvisor s_advisor;

View s_view = View::Mode;
View s_cafe_return = View::Mode;
View s_paused_return = View::Mode;
bool s_dirty = true;

uint8_t s_mode = 0;              // 選んだ遊び用モード（0..3 = ミニ / レギュラー / フル / 超難関）
uint8_t s_group = 0;             // 一覧のタブ（0..3）
uint8_t s_page = 0;              // 一覧のページ
uint16_t s_card = core::kNoItem; // 開いているカード
// 選んだもののメモ（ユーザー要望 2026-09-21:「特徴を忘れないよう、選んだものをどこかで見られるように」）。
// **画面に出すためだけの値**。推論コア（s_engine）にも Advisor にも渡さない。第 2 段階で Jev につなぐときも
// サーバーへ送ってはいけない（送ると「心を読む」遊びが成り立たなくなる）。
// カードを開いて「決めた」を押したときだけ入る。一覧の「決めた」から始めたときはメモなし
uint16_t s_memo = core::kNoItem;

// いま選んでいる遊び用モードの定義（表の中では設計書の 3 モードの後ろにある）
const c::Mode &playMode()
{
    const uint8_t i = s_mode < c::kPlayModeCount ? s_mode : 0;
    return c::kModes[c::kPlayModeFirst + i];
}

Pending s_pending = Pending::None;
uint32_t s_view_ms = 0;          // この画面を作った時刻。指が触れたままの誤回答を防ぐ

bool s_recorded = false;         // この局の結果をもう数えたか（二重計上しない）
uint16_t s_reveal = core::kNoItem;
uint8_t s_contra[8] = {};
uint8_t s_contra_count = 0;
uint8_t s_contra_at = 0;

// 画面を作ってから回答を受け付けるまでの待ち（設計書 5.4）。
// 押したまま画面が切り替わって、次の質問に勝手に答えてしまうのを防ぐ
constexpr uint32_t kInputLatchMs = 250;

// ---------------------------------------------------------------------------
// 文字の幅と部品づくり（探偵ゲームと同じ考え方）
// ---------------------------------------------------------------------------
const char *text(const char *key)
{
    return c::findText(key);
}

// 半角いくつ分か。日本語は全角なので 2 つ分として数える（生成側の見積りと同じ）
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
    for (const char *p = t; ; ++p) {
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

// 候補の名前は大きく出したい。枠に入るなら 40px、入らなければ 22px
const lv_font_t *nameFont(const char *t, int16_t width)
{
    return (int16_t)(widestUnits(t) * 20) <= width ? &ct_font_jp_40 : &ct_font_jp_22;
}

lv_obj_t *rectLabel(const Rect &r, const lv_font_t *font, lv_color_t color, const char *t)
{
    lv_obj_t *l = lv_label_create(s_content);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    // 日本語は自動折返しが効かない。改行は生成側で入れてあるので幅で切る
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
void itemCb(lv_event_t *e);
void tabCb(lv_event_t *e);
void modeCb(lv_event_t *e);

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

    lv_obj_t *l = lv_label_create(btn);
    lv_obj_set_style_text_font(l, font != nullptr ? font : fitFont(t, (int16_t)(r.w - 8)), 0);
    lv_obj_set_style_text_color(l, enabled ? CT_COLOR_TEXT : CT_COLOR_DIM, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, (int16_t)(r.w - 8));
    lv_label_set_text(l, t);
    lv_obj_center(l);

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

void makeTitle(const Rect &r, const char *t)
{
    rectLabel(r, &ct_font_jp_20, CT_COLOR_SUBTEXT, t);
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

// ---------------------------------------------------------------------------
// 状態のちょっとした読み取り
// ---------------------------------------------------------------------------
const core::Session &session()
{
    static const core::Session kEmpty{};
    return s_engine != nullptr ? s_engine->session() : kEmpty;
}

core::Mask modeItems()
{
    return core::Mask::from(playMode().items);
}

// 一覧に出す集合。本当の答えを選ぶときだけ全 128 候補から選べるようにする
// （「一覧に入っていなかった」という申告を受け取るため／設計書 1.2 の invalid_target）
bool revealMode()
{
    return s_view == View::RevealPick;
}

core::Mask browseItems()
{
    if (revealMode()) {
        core::Mask all;
        for (uint8_t g = 0; g < c::kGroupCount; ++g) {
            all = all | core::Mask::from(c::kGroups[g].items);
        }
        return all;
    }
    return modeItems();
}

bool groupHasItems(uint8_t group)
{
    return !(browseItems() & core::Mask::from(c::kGroups[group].items)).empty();
}

// 今のタブに出る候補を集める（ページ分けの元）
uint16_t collectGroup(uint8_t group, uint16_t *out, uint16_t out_max)
{
    const core::Mask set = browseItems() & core::Mask::from(c::kGroups[group].items);
    uint16_t n = 0;
    for (uint16_t i = 0; i < c::kItemCount && n < out_max; ++i) {
        if (set.test(i)) {
            out[n++] = i;
        }
    }
    return n;
}

constexpr uint8_t kItemsPerPage = 4;

uint16_t pageCount(uint8_t group)
{
    static uint16_t buf[c::kItemCount];
    const uint16_t n = collectGroup(group, buf, c::kItemCount);
    return n == 0 ? 1 : (uint16_t)((n + kItemsPerPage - 1) / kItemsPerPage);
}

// ---------------------------------------------------------------------------
// 画面づくり
// ---------------------------------------------------------------------------
void buildMode()
{
    makeTitle(layout::kTitleWide, "エスパー対決");
    rectLabel(layout::kModeLead, &ct_font_jp_20, CT_COLOR_TEXT, text("mode_lead"));

    for (uint8_t i = 0; i < c::kPlayModeCount && i < 4; ++i) {
        const c::Mode &m = c::kModes[c::kPlayModeFirst + i];
        char label[80];
        std::snprintf(label, sizeof(label), "%s  %s\n%u こ / %u 問まで",
                      m.label, m.name_ja, (unsigned)m.item_count, (unsigned)m.max_questions);
        makeButton(layout::kModeBtn[i], label, modeCb, (void *)(intptr_t)i, true, i == s_mode);
    }

    rectButton(layout::kModeCafe, "カフェ", Act::Cafe);
    rectButton(layout::kModeStats, "きろく", Act::ModeStats);
    rectButton(layout::kModeBack, "ゲーム一覧へ", Act::ModeBack);
}

void buildCatalog()
{
    const bool reveal = revealMode();
    // 分野タブ。そのモードに候補が無い分野は押せないようにする
    if (!groupHasItems(s_group)) {
        for (uint8_t g = 0; g < c::kGroupCount; ++g) {
            if (groupHasItems(g)) {
                s_group = g;
                break;
            }
        }
    }
    char title[48];
    std::snprintf(title, sizeof(title), "%s%s", c::kGroups[s_group].name,
                  reveal ? "" : " から 1 つ");
    makeTitle(layout::kTitleWide, title);
    rectLabel(layout::kCatHint, &ct_font_jp_20, CT_COLOR_SUBTEXT,
              text(reveal ? "reveal_hint" : "catalog_hint"));

    for (uint8_t g = 0; g < c::kGroupCount; ++g) {
        const bool has = groupHasItems(g);
        makeButton(layout::kCatTab[g], c::kGroups[g].tab, tabCb, (void *)(intptr_t)g,
                   has && g != s_group, g == s_group);
    }

    static uint16_t items[c::kItemCount];
    const uint16_t total = collectGroup(s_group, items, c::kItemCount);
    const uint16_t pages = total == 0 ? 1 : (uint16_t)((total + kItemsPerPage - 1) / kItemsPerPage);
    if (s_page >= pages) {
        s_page = 0;
    }
    for (uint8_t row = 0; row < kItemsPerPage; ++row) {
        const uint16_t at = (uint16_t)(s_page * kItemsPerPage + row);
        if (at >= total) {
            break;
        }
        makeButton(layout::kCatItem[row], c::kItems[items[at]].name, itemCb,
                   (void *)(intptr_t)items[at], true, false);
    }

    char page[24];
    std::snprintf(page, sizeof(page), "%u / %u", (unsigned)(s_page + 1), (unsigned)pages);
    rectButton(layout::kCatPrev, "前へ", Act::CatPrev, pages > 1);
    rectLabel(layout::kCatPage, &ct_font_jp_20, CT_COLOR_SUBTEXT, page);
    rectButton(layout::kCatNext, "次へ", Act::CatNext, pages > 1);

    if (reveal) {
        rectButton(layout::kCatWide, "えらばずに おわる", Act::CatGiveUp);
    } else {
        rectButton(layout::kCatBack, "もどる", Act::CatBack);
        rectButton(layout::kCatDecide, "決めた", Act::CatDecide, true, true);
    }
}

void buildCard()
{
    const c::Item &it = c::kItems[s_card < c::kItemCount ? s_card : 0];
    makeTitle(layout::kTitleWide, c::kGroups[it.group].name);
    rectLabel(layout::kCardName, nameFont(it.name, (int16_t)(layout::kCardName.w - 8)),
              CT_COLOR_TEXT, it.name);
    rectLabel(layout::kCardDef, &ct_font_jp_22, CT_COLOR_SUBTEXT, it.definition);
    rectLabel(layout::kCardLead, &ct_font_jp_20, CT_COLOR_TEXT, text("card_lead"));
    rectButton(layout::kCardBack, "一覧へ", Act::CardBack);
    rectButton(layout::kCardDecide, "決めた", Act::CardDecide, true, true);
    rectButton(layout::kCardCafe, "カフェ", Act::Cafe);
}

// 選んだもののメモ。カードと同じ見た目で、名前と特徴を読み返せる
void buildMemo()
{
    const c::Item &it = c::kItems[s_memo < c::kItemCount ? s_memo : 0];
    makeTitle(layout::kTitleWide, "えらんだもの（メモ）");
    rectLabel(layout::kCardName, nameFont(it.name, (int16_t)(layout::kCardName.w - 8)),
              CT_COLOR_TEXT, it.name);
    rectLabel(layout::kCardDef, &ct_font_jp_22, CT_COLOR_SUBTEXT, it.definition);
    rectLabel(layout::kMemoNote, &ct_font_jp_20, CT_COLOR_DIM,
              "このメモは画面に出すだけ。\nAI の推理には使いません。");
    rectButton(layout::kMemoBack, "質問へもどる", Act::MemoBack, true, true);
    rectButton(layout::kCardCafe, "カフェ", Act::Cafe);
}

void buildReady()
{
    char title[48];
    std::snprintf(title, sizeof(title), "じゅんび ・ %s", playMode().label);
    makeTitle(layout::kTitleWide, title);
    // 設計書 1.1 の固定文をそのまま出す（ここがいちばん大事な説明）
    rectLabel(layout::kReadyBody, &ct_font_jp_22, CT_COLOR_TEXT, text("ready_notice"));
    rectButton(layout::kReadyStart, "はじめる", Act::ReadyStart, true, true);
    rectButton(layout::kReadyBack, "一覧へ", Act::ReadyBack);
    rectButton(layout::kReadyCafe, "カフェ", Act::Cafe);
}

void buildThinking()
{
    makeTitle(layout::kTitleWide, "エスパー対決");
    rectLabel(layout::kThinkBig, &ct_font_jp_40, CT_COLOR_ACCENT_HI, text("thinking"));
    rectLabel(layout::kThinkSub, &ct_font_jp_20, CT_COLOR_SUBTEXT, text("thinking_sub"));
    // ボタンは 1 つも置かない＝指が触れたままでも次の質問に答えてしまわない（設計書 5.4）
}

void buildQuestion()
{
    const core::Session &s = session();
    if (s.current_question >= c::kQuestionCount) {
        return;
    }
    const c::Question &q = c::kQuestions[s.current_question];

    makeTitle(layout::kTitleWide, "エスパー対決");
    char progress[40];
    std::snprintf(progress, sizeof(progress), "%u 問め / %u 問まで",
                  (unsigned)(s.asked + 1), (unsigned)s.maxQuestions());
    rectLabel(layout::kQProgress, &ct_font_jp_20, CT_COLOR_SUBTEXT, progress);

    rectLabel(layout::kQText, &ct_font_jp_22, CT_COLOR_TEXT, q.text);

    // ルール処理なので「AI の予想 42%」ではなく残り候補数を出す（設計書 3.7）
    char remain[32];
    std::snprintf(remain, sizeof(remain), "のこり %u こ", (unsigned)s.remainingCount());
    rectLabel(layout::kQRemain, &ct_font_jp_20, CT_COLOR_ACCENT_HI, remain);

    if (s_memo < c::kItemCount) {
        // 選んだものをいつでも見られるようにする。押すと名前と特徴を読み返せる
        char memo[96];
        std::snprintf(memo, sizeof(memo), "メモ：%s", c::kItems[s_memo].name);
        rectButton(layout::kQMemo, memo, Act::QMemo);
    } else {
        rectLabel(layout::kQGuide, &ct_font_jp_20, CT_COLOR_TEXT, text("question_guide"));
    }

    rectButton(layout::kQYes, "はい\nYES", Act::AnsYes, true, true);
    rectButton(layout::kQNo, "いいえ\nNO", Act::AnsNo, true, true);
    rectButton(layout::kQBack, "1つ戻る", Act::AnsUndo, s.canUndo());
    rectButton(layout::kQHelp, "？ ヘルプ", Act::AnsHelp);
    rectButton(layout::kQCafe, "カフェ", Act::Cafe);
}

void buildHelp()
{
    const core::Session &s = session();
    const c::Question &q = c::kQuestions[s.current_question < c::kQuestionCount
                                             ? s.current_question : 0];
    makeTitle(layout::kTitleWide, "この質問について");
    rectLabel(layout::kHelpQ, &ct_font_jp_22, CT_COLOR_TEXT, q.text);
    rectLabel(layout::kHelpBody, &ct_font_jp_22, CT_COLOR_SUBTEXT, q.help);

    if (s.canSkip()) {
        char label[48];
        std::snprintf(label, sizeof(label), "わからない（あと %u 回）",
                      (unsigned)(s.maxSkips() - s.skip_count));
        rectButton(layout::kHelpSkip, label, Act::HelpSkip);
    } else {
        // 「わからない」を使い切ったら「今の情報で予想」を出す（設計書 1.4）
        rectButton(layout::kHelpSkip, "今の情報で予想する", Act::HelpGuessNow);
    }
    rectButton(layout::kHelpBack, "質問にもどる", Act::HelpBack, true, true);
    rectButton(layout::kHelpQuit, "やめる", Act::HelpQuit);
}

void buildQuit()
{
    makeTitle(layout::kTitleWide, "やめますか");
    rectLabel(layout::kAskBody, &ct_font_jp_22, CT_COLOR_ALERT, text("quit_confirm"));
    rectButton(layout::kAskPrimary, "やめる", Act::QuitOk);
    rectButton(layout::kAskSecond, "つづける", Act::QuitNo, true, true);
}

void buildGuess()
{
    const core::Session &s = session();
    if (s.guess >= c::kItemCount) {
        return;
    }
    const c::Item &it = c::kItems[s.guess];
    makeTitle(layout::kTitleWide, "さいごの予想");
    rectLabel(layout::kGuessName, nameFont(it.name, (int16_t)(layout::kGuessName.w - 8)),
              CT_COLOR_ACCENT_HI, it.name);
    rectLabel(layout::kGuessDef, &ct_font_jp_22, CT_COLOR_SUBTEXT, it.definition);
    // 候補が 1 つに絞れていないときは、当てられる自信があるように見せない（設計書 3.6）
    rectLabel(layout::kGuessLead, &ct_font_jp_20, CT_COLOR_TEXT,
              text(s.guess_reason == core::GuessReason::Unique ? "guess_lead"
                                                               : "guess_low_info"));
    rectButton(layout::kGuessYes, "はい\n正解", Act::GuessYes, true, true);
    rectButton(layout::kGuessNo, "いいえ\nちがう", Act::GuessNo, true, true);
    rectButton(layout::kGuessBack, "1つ戻る", Act::GuessUndo, s.canUndo());
    rectButton(layout::kGuessCafe, "カフェ", Act::Cafe);
}

void buildResult()
{
    const core::Session &s = session();
    const bool ai_win = s.verdict == core::Verdict::AiWin;
    makeTitle(layout::kTitleWide, "けっか");
    rectLabel(layout::kResHead, &ct_font_jp_22, ai_win ? CT_COLOR_ACCENT_HI : CT_COLOR_TEXT,
              text(ai_win ? "result_ai_win" : "result_human_win"));

    char detail[96];
    std::snprintf(detail, sizeof(detail), "%s  %s\n聞いた質問 %u 問\nわからない %u 回",
                  playMode().label, playMode().name_ja,
                  (unsigned)s.asked, (unsigned)s.skip_count);
    rectLabel(layout::kResDetail, &ct_font_jp_20, CT_COLOR_SUBTEXT, detail);

    const ModeStats &st = stats(s_mode);
    char tally[64];
    std::snprintf(tally, sizeof(tally), "この端末 AI %u 勝 / 人 %u 勝",
                  (unsigned)st.ai_win, (unsigned)st.human_win);
    rectLabel(layout::kResTally, &ct_font_jp_20, CT_COLOR_DIM, tally);

    // 外れたときだけ「本当の答え」を聞く。答えなくてもよい（設計書 5.3 の G70）
    if (!ai_win && s_reveal == core::kNoItem) {
        rectButton(layout::kResReveal, "本当の答えを おしえる", Act::ResReveal);
        rectButton(layout::kResAgain, "もう一度", Act::ResAgain, true, true);
    } else {
        rectButton(layout::kResAgain, "もう一度", Act::ResAgain, true, true);
    }
    rectButton(layout::kResHome, "HOME", Act::ResHome);
    rectButton(layout::kResCafe, "カフェ", Act::Cafe);
}

void buildRevealCard()
{
    const core::Session &s = session();
    const uint16_t item = s_reveal;
    const c::Item &it = c::kItems[item < c::kItemCount ? item : 0];

    if (s_engine != nullptr && !s_engine->revealedWasInMode(item)) {
        makeTitle(layout::kTitleWide, "一覧の外でした");
        rectLabel(layout::kRevName, nameFont(it.name, (int16_t)(layout::kRevName.w - 8)),
                  CT_COLOR_TEXT, it.name);
        rectLabel(layout::kRevBody, &ct_font_jp_22, CT_COLOR_SUBTEXT, text("reveal_outside"));
        rectButton(layout::kRevDone, "おわる", Act::RevDone, true, true);
        return;
    }
    if (s_contra_count == 0) {
        makeTitle(layout::kTitleWide, "食い違いなし");
        rectLabel(layout::kRevName, nameFont(it.name, (int16_t)(layout::kRevName.w - 8)),
                  CT_COLOR_TEXT, it.name);
        rectLabel(layout::kRevBody, &ct_font_jp_22, CT_COLOR_SUBTEXT, text("reveal_none"));
        rectButton(layout::kRevDone, "おわる", Act::RevDone, true, true);
        return;
    }

    char title[40];
    std::snprintf(title, sizeof(title), "食い違い %u / %u",
                  (unsigned)(s_contra_at + 1), (unsigned)s_contra_count);
    makeTitle(layout::kTitleWide, title);
    rectLabel(layout::kRevName, nameFont(it.name, (int16_t)(layout::kRevName.w - 8)),
              CT_COLOR_TEXT, it.name);
    rectLabel(layout::kRevIntro, &ct_font_jp_20, CT_COLOR_SUBTEXT, text("reveal_intro"));

    const core::HistoryEntry &h = s.history[s_contra[s_contra_at]];
    rectLabel(layout::kRevQuestion, &ct_font_jp_22, CT_COLOR_TEXT,
              c::kQuestions[h.question].text);

    // U（適用外）を「いいえ」と言い換えない。そのまま「この分野の外」と出す（設計書 2.4）
    const core::Engine::Cell truth = core::Engine::cellFor(h.question, item);
    const char *card = truth == core::Engine::Cell::Yes   ? "はい"
                     : truth == core::Engine::Cell::No    ? "いいえ"
                                                          : "この分野の外";
    char answer[80];
    std::snprintf(answer, sizeof(answer), "あなた: %s\nカード: %s",
                  h.answer == core::Answer::Yes ? "はい" : "いいえ", card);
    rectLabel(layout::kRevAnswer, &ct_font_jp_20, CT_COLOR_ALERT, answer);

    char page[24];
    std::snprintf(page, sizeof(page), "%u / %u", (unsigned)(s_contra_at + 1),
                  (unsigned)s_contra_count);
    rectButton(layout::kRevPrev, "前へ", Act::RevPrev, s_contra_count > 1);
    rectLabel(layout::kRevPage, &ct_font_jp_20, CT_COLOR_SUBTEXT, page);
    rectButton(layout::kRevNext, "次へ", Act::RevNext, s_contra_count > 1);
    rectButton(layout::kRevDone, "おわる", Act::RevDone, true, true);
}

void buildStats()
{
    makeTitle(layout::kTitleWide, "きろく");
    for (uint8_t m = 0; m < c::kPlayModeCount && m < 4; ++m) {
        const ModeStats &st = stats(m);
        makePanelBox(layout::kStatRow[m]);
        const Rect &r = layout::kStatRow[m];
        rectLabel(Rect{(int16_t)(r.x + 10), r.y, 96, r.h}, &ct_font_jp_20, CT_COLOR_TEXT,
                  c::kModes[c::kPlayModeFirst + m].label);
        char body[48];
        if (st.best_questions > 0) {
            std::snprintf(body, sizeof(body), "AI %u / 人 %u ・ 最少 %u 問",
                          (unsigned)st.ai_win, (unsigned)st.human_win,
                          (unsigned)st.best_questions);
        } else {
            std::snprintf(body, sizeof(body), "AI %u / 人 %u",
                          (unsigned)st.ai_win, (unsigned)st.human_win);
        }
        rectLabel(Rect{(int16_t)(r.x + 100), r.y, (int16_t)(r.w - 110), r.h}, &ct_font_jp_20,
                  CT_COLOR_SUBTEXT, body);
    }
    rectLabel(layout::kStatNote, &ct_font_jp_20, CT_COLOR_DIM, text("stats_note"));
    rectButton(layout::kStatBack, "もどる", Act::StatsBack, true, true);
}

void buildError()
{
    makeTitle(layout::kTitleWide, "データが合いません");
    rectLabel(layout::kAskBody, &ct_font_jp_22, CT_COLOR_ALERT, text("inconsistent"));
    rectButton(layout::kAskPrimary, "はじめから", Act::ErrorAgain, true, true);
    rectButton(layout::kAskSecond, "HOME へ", Act::ErrorHome);
}

void buildCafe()
{
    makeTitle(layout::kTitleWide, "カフェ");
    rectLabel(layout::kPanelBody, &ct_font_jp_22, CT_COLOR_TEXT, text("cafe_note"));
    rectButton(layout::kPanel[0], "＋1杯を記録", Act::CafeCoffee, true, true);
    rectButton(layout::kPanel[1], "ゲームへ戻る", Act::CafeBack);
    rectButton(layout::kPanel[2], "HOME へ", Act::CafeHome);
}

void buildPaused()
{
    makeTitle(layout::kTitleWide, "ひと休み");
    rectLabel(layout::kPanelBody, &ct_font_jp_22, CT_COLOR_TEXT, text("paused_note"));
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
    case View::Mode:       buildMode(); break;
    case View::Catalog:
    case View::RevealPick: buildCatalog(); break;
    case View::Card:       buildCard(); break;
    case View::Ready:      buildReady(); break;
    case View::Thinking:   buildThinking(); break;
    case View::Question:   buildQuestion(); break;
    case View::Help:       buildHelp(); break;
    case View::Quit:       buildQuit(); break;
    case View::Guess:      buildGuess(); break;
    case View::Result:     buildResult(); break;
    case View::RevealCard: buildRevealCard(); break;
    case View::Stats:      buildStats(); break;
    case View::Error:      buildError(); break;
    case View::Cafe:       buildCafe(); break;
    case View::Paused:     buildPaused(); break;
    case View::Memo:       buildMemo(); break;
    }
}

void setView(View v)
{
    s_view = v;
    s_dirty = true;
    s_view_ms = millis();
}

// 「考えています」を先に描いてから重い計算に入る
void think(Pending what)
{
    s_pending = what;
    setView(View::Thinking);
}

// 回答ボタンを受け付けてよいか。押したまま画面が切り替わった直後は無視する（設計書 5.4）
bool inputReady()
{
    return s_pending == Pending::None && (millis() - s_view_ms) >= kInputLatchMs;
}

void afterEngineStep()
{
    const core::Session &s = session();
    if (s.phase == core::Phase::Guess && s.remainingCount() == 0) {
        // 候補が 0＝データ不一致か状態の壊れ。存在しない答えを作らない（設計書 3.2）
        setView(View::Error);
        return;
    }
    setView(s.phase == core::Phase::Question ? View::Question : View::Guess);
}

void runPending()
{
    const Pending what = s_pending;
    s_pending = Pending::None;
    if (s_engine == nullptr) {
        setView(View::Error);
        return;
    }
    switch (what) {
    case Pending::Start:
        // 推論コアの表では、遊び用のモードは設計書の 3 モードの後ろに並んでいる
    s_engine->start((uint8_t)(c::kPlayModeFirst + s_mode), esp_random());
        s_recorded = false;
        s_reveal = core::kNoItem;
        s_contra_count = 0;
        s_contra_at = 0;
        break;
    case Pending::Yes:      s_engine->answer(core::Answer::Yes); break;
    case Pending::No:       s_engine->answer(core::Answer::No); break;
    case Pending::Skip:     s_engine->answer(core::Answer::Skip); break;
    case Pending::GuessNow: s_engine->guessNow(); break;
    case Pending::None:     return;
    }
    afterEngineStep();
}

// 勝敗が決まったので、この端末の記録と「遊んだ回数」を 1 回だけ数える
void finishGame(bool ai_win)
{
    if (s_engine == nullptr || !s_engine->verdict(ai_win)) {
        return;
    }
    if (!s_recorded) {
        s_recorded = true;
        const core::Session &s = session();
        recordResult(s_mode, ai_win, ai_win ? s.asked : 0);
        // 書けなかったときはフラッシュを読み直し、メモリー上だけ進んだ状態を残さない
        if (!saveStats() && !saveStats()) {
            Serial.println("[ESP] stats save failed, reloading from flash");
            loadStats();
        }
        // 数えるのは回数だけ。モード・問数・当たり外れ以外は残さない（答えは持っていない）
        char note[40];
        std::snprintf(note, sizeof(note), "esper %s q=%u ok=%u", playMode().id,
                      (unsigned)s.asked, ai_win ? 1u : 0u);
        cup::stats::gamePlayed(cup::GameId::Esper, note);
    }
    setView(View::Result);
}

// 最初の画面へ戻す（遊び終わり・やめる・入り直し）
void resetToMode()
{
    s_page = 0;
    s_card = core::kNoItem;
    s_memo = core::kNoItem;
    s_reveal = core::kNoItem;
    s_contra_count = 0;
    s_contra_at = 0;
    s_pending = Pending::None;
    setView(View::Mode);
}

// ---------------------------------------------------------------------------
// 入力
// ---------------------------------------------------------------------------
void modeCb(lv_event_t *e)
{
    const int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= (int)c::kPlayModeCount) {
        return;
    }
    s_mode = (uint8_t)index;
    s_group = 0;
    s_page = 0;
    setView(View::Catalog);
}

void tabCb(lv_event_t *e)
{
    const int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= (int)c::kGroupCount) {
        return;
    }
    s_group = (uint8_t)index;
    s_page = 0;
    s_dirty = true;
}

void itemCb(lv_event_t *e)
{
    const int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= (int)c::kItemCount) {
        return;
    }
    if (s_view == View::RevealPick) {
        // 本当の答えの申告。ここで初めて端末が「答え」を知るが、
        // 記録するのは勝敗の回数だけで、候補の番号は保存も送信もしない
        s_reveal = (uint16_t)index;
        if (s_engine != nullptr) {
            s_engine->reveal(s_reveal);
            s_contra_count = s_engine->contradictions(s_reveal, s_contra, 8);
        }
        s_contra_at = 0;
        setView(View::RevealCard);
        return;
    }
    s_card = (uint16_t)index;
    setView(View::Card);
}

void actionCb(lv_event_t *e)
{
    const Act act = (Act)(intptr_t)lv_event_get_user_data(e);

    switch (act) {
    case Act::ModeBack:
        ui::pop();                 // ゲーム一覧へ戻る
        return;
    case Act::ModeStats:
        setView(View::Stats);
        break;

    case Act::CatPrev:
        s_page = (uint8_t)(s_page == 0 ? pageCount(s_group) - 1 : s_page - 1);
        s_dirty = true;
        break;
    case Act::CatNext:
        s_page = (uint8_t)(s_page + 1 >= pageCount(s_group) ? 0 : s_page + 1);
        s_dirty = true;
        break;
    case Act::CatBack:
        setView(View::Mode);
        break;
    case Act::CatDecide:
        s_memo = core::kNoItem;     // カードを開かずに決めた = メモなしで遊ぶ
        setView(View::Ready);
        break;
    case Act::CatGiveUp:       // 本当の答えを言わずに終わる
        setView(View::Result);
        break;

    case Act::CardBack:
        setView(View::Catalog);
        break;
    case Act::CardDecide:
        s_memo = s_card;            // 画面表示用のメモ。推論には使わない
        setView(View::Ready);
        break;

    case Act::ReadyStart:
        think(Pending::Start);
        break;
    case Act::ReadyBack:
        setView(View::Catalog);
        break;

    case Act::QMemo:
        setView(View::Memo);
        break;
    case Act::MemoBack:
        setView(View::Question);
        break;

    case Act::AnsYes:
        if (inputReady()) {
            think(Pending::Yes);
        }
        break;
    case Act::AnsNo:
        if (inputReady()) {
            think(Pending::No);
        }
        break;
    case Act::AnsHelp:
        setView(View::Help);
        break;
    case Act::AnsUndo:
        if (s_engine != nullptr && s_engine->undo()) {
            setView(View::Question);
        }
        break;

    case Act::HelpSkip:
        think(Pending::Skip);
        break;
    case Act::HelpGuessNow:
        think(Pending::GuessNow);
        break;
    case Act::HelpBack:
        setView(View::Question);
        break;
    case Act::HelpQuit:
        setView(View::Quit);
        break;

    case Act::QuitOk:
        // とちゅうでやめた回は勝敗に数えない（設計書 1.2 の aborted）
        resetToMode();
        break;
    case Act::QuitNo:
        setView(View::Question);
        break;

    case Act::GuessYes:
        if (inputReady()) {
            finishGame(true);
        }
        break;
    case Act::GuessNo:
        if (inputReady()) {
            finishGame(false);
        }
        break;
    case Act::GuessUndo:
        if (s_engine != nullptr && s_engine->undo()) {
            setView(View::Question);
        }
        break;

    case Act::ResReveal:
        s_group = 0;
        s_page = 0;
        setView(View::RevealPick);
        break;
    case Act::ResAgain:
        resetToMode();
        break;
    case Act::ResHome:
        ui::goHome();
        return;

    case Act::RevPrev:
        if (s_contra_count > 0) {
            s_contra_at = (uint8_t)(s_contra_at == 0 ? s_contra_count - 1 : s_contra_at - 1);
            s_dirty = true;
        }
        break;
    case Act::RevNext:
        if (s_contra_count > 0) {
            s_contra_at = (uint8_t)(s_contra_at + 1 >= s_contra_count ? 0 : s_contra_at + 1);
            s_dirty = true;
        }
        break;
    case Act::RevDone:
        setView(View::Result);
        break;

    case Act::StatsBack:
        setView(View::Mode);
        break;

    case Act::ErrorAgain:
        resetToMode();
        break;
    case Act::ErrorHome:
        ui::goHome();
        return;

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
        ui::goHome();
        return;

    case Act::PausedResume:
        setView(s_paused_return);   // 途中の状態はそのまま残っている
        break;
    case Act::PausedQuit:
        resetToMode();
        break;
    }
}

// ---------------------------------------------------------------------------
// 100ms ごとの処理
// ---------------------------------------------------------------------------
bool isPlayingView(View v)
{
    return v == View::Catalog || v == View::Card || v == View::Ready ||
           v == View::Question || v == View::Help || v == View::Guess || v == View::Result ||
           v == View::RevealPick || v == View::RevealCard;
}

void tickCb(lv_timer_t *t)
{
    (void)t;
    if (s_dirty) {
        rebuild();
        s_dirty = false;
        return;     // 「考えています」を必ず 1 回描いてから重い計算に入る
    }
    if (s_pending != Pending::None) {
        runPending();
        return;
    }

    // 180 秒さわられなければ「ひと休み」を重ねる。途中の状態は RAM に残したまま
    // なので「つづける」で同じ場面へ戻れる。捨てるのは「やめて 一覧へ」と HOME だけ
    if (isPlayingView(s_view) && lv_disp_get_inactive_time(nullptr) > c::kIdlePauseMs) {
        s_paused_return = s_view;
        setView(View::Paused);
    }
}

// ---------------------------------------------------------------------------
// 画面の生成・破棄
// ---------------------------------------------------------------------------
void destroyEngine()
{
    if (s_engine != nullptr) {
        s_engine->~Engine();
        heap_caps_free(s_engine);
        s_engine = nullptr;
    }
}

bool createEngine()
{
    if (s_engine != nullptr) {
        return true;
    }
    // まず PSRAM（8MB）から。取れなければ内部 RAM へ落とす
    void *buf = heap_caps_malloc(sizeof(core::Engine), MALLOC_CAP_SPIRAM);
    if (buf == nullptr) {
        buf = heap_caps_malloc(sizeof(core::Engine), MALLOC_CAP_8BIT);
    }
    if (buf == nullptr) {
        Serial.printf("[ESP] cannot allocate the engine (%u bytes)\n",
                      (unsigned)sizeof(core::Engine));
        return false;
    }
    s_engine = new (buf) core::Engine();
    s_engine->setAdvisor(&s_advisor);   // 段階 1 はルール基準。段階 2 でここを Jev へ差し替える
    return true;
}

void screenDeletedCb(lv_event_t *e)
{
    // 画面の破棄は遷移アニメーション（200ms）の完了時なので、その間に次のエスパー画面が
    // 作られていることがある。古い画面の後始末で新しい画面を壊さないよう照合する
    if (lv_event_get_target(e) != s_screen) {
        return;
    }
    display::setGameActive(false);
    if (s_tick != nullptr) {
        lv_timer_del(s_tick);
        s_tick = nullptr;
    }
    destroyEngine();
    s_dirty = false;
    s_pending = Pending::None;
    // 開発用コマンドが古い画面名を出さないよう、画面の種類も初期値に戻す
    s_view = View::Mode;
    s_cafe_return = View::Mode;
    s_paused_return = View::Mode;
    s_page = 0;
    s_card = core::kNoItem;
    s_memo = core::kNoItem;
    s_reveal = core::kNoItem;
    s_contra_count = 0;
    s_contra_at = 0;
    // 解放済みオブジェクトを触らないよう、静的ポインタは必ず全部消す
    s_screen = nullptr;
    s_content = nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
lv_obj_t *createGameScreen()
{
    // 前のエスパー画面がまだ消えていない（遷移中の再入・開発用コマンドの連打）ことがある。
    // 古いタイマーを残すと 2 本が同じ部品を作り替えに来るので、ここで止める
    if (s_tick != nullptr) {
        lv_timer_del(s_tick);
        s_tick = nullptr;
    }
    // 質問を読んでいる途中で自動的に暗くならないようにする
    display::setGameActive(true);

    lv_obj_t *scr = ui::makeScreen();

    s_screen = scr;
    s_content = lv_obj_create(scr);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_pos(s_content, 0, 0);
    lv_obj_set_size(s_content, ui::kScreenSize, ui::kScreenSize);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);

    loadStats();
    destroyEngine();
    const bool ready = createEngine();

    // 入り直したときのため、状態を毎回そろえる
    s_view = ready ? View::Mode : View::Error;
    s_cafe_return = View::Mode;
    s_paused_return = View::Mode;
    s_mode = 0;
    s_group = 0;
    s_page = 0;
    s_card = core::kNoItem;
    s_memo = core::kNoItem;
    s_reveal = core::kNoItem;
    s_contra_count = 0;
    s_contra_at = 0;
    s_recorded = false;
    s_pending = Pending::None;
    s_view_ms = millis();

    rebuild();
    s_dirty = false;

    s_tick = lv_timer_create(tickCb, 100, nullptr);
    lv_obj_add_event_cb(scr, screenDeletedCb, LV_EVENT_DELETE, nullptr);
    return scr;
}

void debugPrintPublicState()
{
    // 公開情報のみ。そもそもプレイヤーの答えは端末のどこにも無い
    const core::Session &s = session();
    Serial.printf("[ESP] view=%u mode=%s phase=%u q=%s asked=%u/%u skip=%u undo=%u "
                  "remain=%u guess=%s reason=%u verdict=%u engine=%s\n",
                  (unsigned)s_view,
                  s_engine != nullptr ? playMode().id : "-",
                  (unsigned)s.phase,
                  s.current_question < c::kQuestionCount
                      ? c::kQuestions[s.current_question].id : "-",
                  (unsigned)s.asked, (unsigned)s.maxQuestions(),
                  (unsigned)s.skip_count, (unsigned)s.undo_count,
                  (unsigned)s.remainingCount(),
                  s.guess < c::kItemCount ? c::kItems[s.guess].id : "-",
                  (unsigned)s.guess_reason, (unsigned)s.verdict,
                  s_advisor.engineName());
}

void debugResetStats()
{
    const bool ok = resetStats();
    if (s_screen != nullptr) {
        s_dirty = true;
    }
    Serial.printf("[ESP] stats reset: all modes back to 0 (save %s)\n", ok ? "ok" : "FAILED");
}

}  // namespace esper
