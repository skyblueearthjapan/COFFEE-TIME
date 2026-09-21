#include "ReversiGame.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_random.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "../../CupState.h"
#include "../../Display.h"
#include "../../HomeScreen.h"
#include "../../NetService.h"
#include "../../ui/ScreenManager.h"
#include "../../ui/UiKit.h"
#include "ReversiContent.h"
#include "ReversiStore.h"
#include "core/reversi_extra.hpp"

LV_FONT_DECLARE(ct_font_jp_20);
LV_FONT_DECLARE(ct_font_jp_22);
LV_FONT_DECLARE(ct_font_jp_40);

namespace reversi {
namespace {

namespace core = ct_rev;
namespace rev = coffee::rev;
namespace text_db = coffee::rev::content;

using core::Cell;
using core::Closure;
using core::Mode;
using core::Session;
using core::Source;
using ui::Rect;

const char *text(const char *key)
{
    return text_db::findText(key);
}

// ---------------------------------------------------------------------------
// 盤面の色（設計一式 data/layout.json の提案値）。
// カフェの暗い背景の上に濃い緑の盤を置き、黒石・白石の**どちらにも輪郭**を付ける。
// 合法手は色だけでなく「輪（形）」で示す（設計書 4.1）
// ---------------------------------------------------------------------------
#define REV_BOARD_BG   lv_color_hex(0x245548)
#define REV_GRID       lv_color_hex(0xC7D3C8)
#define REV_STONE_B    lv_color_hex(0x17201C)
#define REV_STONE_W    lv_color_hex(0xF9F5EB)
#define REV_EDGE_B     lv_color_hex(0x7E9187)   // 黒石の輪郭（緑の盤から浮かせる）
#define REV_ACCENT     lv_color_hex(0xD7A94C)

// ---------------------------------------------------------------------------
// 画面の配置
//
// 盤面と 3 つのボタンは設計書 4.1 の表そのまま（312×312・左上 (84,84)、
// 操作 132,400,48,44 / 置く 188,402,104,52 / 候補 300,400,48,44）。
// この 3 つは中心 (240,240) から最も遠い角で 230.8px、設計書の安全域 232px の内側。
// ほかの画面は、この作業台の共通値（半径 228px）の内側に収めてある。
// ---------------------------------------------------------------------------
namespace layout {

constexpr Rect kTitle      {120,  34, 240, 26};

// --- entry ------------------------------------------------------------------
constexpr Rect kEntryLogo  { 84,  68, 312, 54};
constexpr Rect kEntrySub   { 84, 126, 312, 26};
constexpr Rect kEntryRecord{ 84, 158, 312, 54};   // 「きろく」の見出し ＋ 6×6 / 8×8 の 2 行
constexpr Rect kEntryResume{130, 218, 220, 50};
constexpr Rect kEntryStart {130, 274, 220, 50};
constexpr Rect kEntryHow   {130, 330, 220, 44};
constexpr Rect kEntryBack  {140, 380, 200, 44};

// --- tutorial ---------------------------------------------------------------
constexpr Rect kTutTitle   { 84,  62, 312, 30};
constexpr Rect kTutBody    { 84, 100, 312, 190};
constexpr Rect kTutPrev    {130, 300, 100, 54};
constexpr Rect kTutNext    {250, 300, 100, 54};
constexpr Rect kTutBack    {160, 366, 160, 46};

// --- setup: size ------------------------------------------------------------
constexpr Rect kSizeBtn[2]  = {{90, 106, 300, 68}, {90, 214, 300, 68}};
constexpr Rect kSizeNote[2] = {{90, 178, 300, 26}, {90, 286, 300, 26}};
constexpr Rect kSizeBack    {150, 330, 180, 48};

// --- setup: opponent --------------------------------------------------------
constexpr Rect kModeBtn[4] = {{100,  92, 280, 58}, {100, 154, 280, 58},
                              {100, 216, 280, 58}, {100, 278, 280, 58}};
constexpr Rect kModeNote   { 84, 340, 312, 46};
constexpr Rect kModeBack   {150, 388, 180, 44};

// --- setup: colour ----------------------------------------------------------
constexpr Rect kColorBtn[3] = {{110, 108, 260, 56}, {110, 172, 260, 56}, {110, 236, 260, 56}};
constexpr Rect kColorGo     {130, 306, 220, 54};
constexpr Rect kColorBack   {150, 370, 180, 46};

// --- board（設計書 4.1）-----------------------------------------------------
constexpr Rect kBoardTitle {120,  20, 240, 24};
constexpr Rect kBoardScore { 84,  48, 312, 30};
constexpr Rect kBoardArea  { 84,  84, 312, 312};
constexpr Rect kBoardMenu  {132, 400,  48, 44};
constexpr Rect kBoardPlace {188, 402, 104, 52};
constexpr Rect kBoardList  {300, 400,  48, 44};

// --- choices（設計一式 data/layout.json の list_page）------------------------
constexpr Rect kChoiceHint  { 84,  64, 312, 56};
constexpr Rect kChoiceCard[4] = {{130, 130, 100, 78}, {250, 130, 100, 78},
                                 {130, 220, 100, 78}, {250, 220, 100, 78}};
constexpr Rect kChoicePage  {138, 302, 204, 26};
constexpr Rect kChoicePrev  {138, 330,  88, 56};
constexpr Rect kChoiceNext  {254, 330,  88, 56};
constexpr Rect kChoiceBack  {190, 394, 100, 46};

// --- menu -------------------------------------------------------------------
constexpr Rect kMenuItem[6] = {{110,  78, 260, 46}, {110, 130, 260, 46}, {110, 182, 260, 46},
                               {110, 234, 260, 46}, {110, 286, 260, 46}, {110, 338, 260, 46}};

// --- details ----------------------------------------------------------------
// 3 行 ＋ 候補 5 行 ＋ 注記 3 行（ct_font_jp_20 = 1 行 26px）で、
// いちばん詰まった場合でも「もどる」に重ならない高さを確保してある
constexpr Rect kDetHead    { 84,  62, 312, 82};
constexpr Rect kDetList    { 84, 148, 312, 130};
constexpr Rect kDetNote    { 84, 282, 312, 82};
constexpr Rect kDetBack    {170, 368, 140, 44};

// --- network choice ---------------------------------------------------------
constexpr Rect kNetBody    { 84,  92, 312, 92};
constexpr Rect kNetBtn[3]  = {{110, 196, 260, 54}, {110, 260, 260, 54}, {110, 324, 260, 54}};

// --- confirm ----------------------------------------------------------------
constexpr Rect kAskBody    { 84, 110, 312, 116};
constexpr Rect kAskYes     {110, 248, 260, 54};
constexpr Rect kAskNo      {110, 314, 260, 50};

// --- result -----------------------------------------------------------------
constexpr Rect kResVerdict { 84,  84, 312, 56};
constexpr Rect kResCount   { 84, 144, 312, 30};
constexpr Rect kResDetail  { 84, 178, 312, 82};
constexpr Rect kResSync    { 84, 262, 312, 26};
constexpr Rect kResAgain   {130, 296, 220, 54};
constexpr Rect kResHome    {130, 358, 220, 48};

// --- カフェ / ひと休み（ほかのゲームと同じ並び）------------------------------
constexpr Rect kPanelBody  { 94, 138, 292, 58};
constexpr Rect kPanel[3]   = {{110, 214, 260, 54}, {110, 278, 260, 54}, {110, 342, 260, 54}};

}  // namespace layout

// ---------------------------------------------------------------------------
// 画面と進行の種類（設計書 4.4 / 5.1）
// ---------------------------------------------------------------------------
enum class View : uint8_t {
    Entry,          // 再開・始める・遊び方・カフェ
    SetupSize,      // 6×6 / 8×8
    SetupMode,      // JEV / JEV PRO / CASUAL / 端末AI
    SetupColor,     // 黒 / 白 / おまかせ
    Board,          // 盤・枚数・手番・選択・操作
    Choices,        // 合法手の一覧（4 件 / 頁）
    Menu,           // 操作
    Details,        // 直前の Jev の評価
    NetworkChoice,  // もう一度 / 端末AIで続行 / カフェ
    Confirm,        // 破棄・投了・端末AI切替の確認
    Paused,         // 一時停止（無操作・再開の入口）
    Result,         // 勝敗
    Tutorial,       // 遊び方 8 ページ
    Cafe,           // 共通カフェパネル
};

// 盤の進行（設計書 5.1 の状態機械を、この画面に必要なぶんだけ）
enum class Phase : uint8_t {
    Idle,       // 人間の番・対局外
    Pass,       // 強制パスの表示（0.7 秒）
    Think,      // 端末 AI が選ぶ（次の巡回で計算する）
    Wait,       // Jev の返事待ち
    Animate,    // 反転の見せ場（0.18 秒）
};

enum class Ask : uint8_t { None, NewGame, Resign, EndGame, GoLocal };

enum class Act : int {
    EntryResume = 1, EntryStart, EntryHow, EntryBack,
    TutPrev, TutNext, TutBack,
    Size6, Size8, SizeBack,
    ModeJev, ModePro, ModeCasual, ModeLocal, ModeBack,
    ColorBlack, ColorWhite, ColorRandom, ColorGo, ColorBack,
    BoardPlace, BoardMenu, BoardList,
    ChoicePrev, ChoiceNext, ChoiceBack,
    MenuContinue, MenuDetails, MenuRules, MenuCafe, MenuResign, MenuEnd,
    DetailsBack,
    NetRetry, NetLocal, NetCafe,
    AskYes, AskNo,
    PausedResume, PausedQuit,
    ResultAgain, ResultHome,
    CafeCoffee, CafeBack, CafeHome,
};

constexpr uint32_t kIdlePauseMs = 120 * 1000;   // 無操作 120 秒でひと休み（設計書 1.3）
constexpr uint32_t kPassNoticeMs = 700;         // 強制パスの表示（rules.json automatic_pass_ms）
constexpr uint32_t kAnimateMs = 180;            // 反転の見せ場（rules.json ai_animation_ms）
constexpr uint32_t kJevWaitMs = 12000;          // ここで見切る（計画 §5）
// 1 回だけ黙って同じ棋譜を送り直す。実機では 6〜10 回に 1 回ほど、GAS が手を決めて
// 控えに入れたあとで転送先の読み取りだけが時間切れ（http -11）になる。
// 送り直しは GAS の控えが返るので Jev は呼ばれず、待ち時間も短い（2026-09-22 実機）
constexpr uint8_t kJevAttempts = 2;
// 依頼箱がふさがっているときにねばる時間。12 秒で見切った直後は、前の通信が
// 終わる（最大 14.5 秒）まで箱が空かないので、その差を待てるだけ待つ
constexpr uint32_t kSendBusyMs = 8000;
constexpr uint32_t kInputLatchMs = 250;         // 画面が変わった直後の誤タップよけ
constexpr uint8_t kJevCandidates = 12;          // 評価の画面に残す候補の数
constexpr size_t kSnapshotJsonMax = 1600;       // 棋譜 128 件ぶんでも余る

// ---------------------------------------------------------------------------
// 対局の状態。**画面を開いたときに PSRAM へ確保し、閉じるときに手放す**
// （内蔵メモリは TLS 1 本で約 35KB 使うので、大きな静的配列を作らない）
// ---------------------------------------------------------------------------
struct GameState {
    Session session;
    Session scratch;                 // commit の作業用（深い呼び出しでスタックを使わない）

    int selected = -1;               // 選んでいるマス（-1 = 未選択）
    uint8_t preview[64] = {};        // 選んだ手で返る石
    uint8_t preview_count = 0;
    int last_move = -1;              // 直前に置いたマス（印）
    uint8_t anim[64] = {};           // 見せ場の対象
    uint8_t anim_count = 0;
    bool started = false;            // この画面で局を開いたか（Session の既定は Active なので必要）
    bool counted = false;            // 結果を 1 回だけ数えたか
    bool sent = false;               // 結果を送れたか
    bool pass_by_human = false;      // 強制パスの主
    bool save_failed = false;        // 最後の保存に失敗した

    // 直前の Jev の評価（1 件だけ。設計書 13.2）
    bool jev_valid = false;
    bool jev_sampled = false;        // CASUAL で最大以外を引いた
    bool jev_cached = false;
    uint16_t jev_ply = 0;
    char jev_move[rev::kCoordMax] = {};
    char jev_top[rev::kCoordMax] = {};
    float jev_confidence = 0;
    uint8_t jev_count = 0;                  // 控えた候補の数（上位 kJevCandidates まで）
    uint8_t jev_total = 0;                  // 返ってきた候補の総数（「ほか n 手」用）
    char jev_names[kJevCandidates][rev::kCoordMax] = {};
    uint8_t jev_percent[kJevCandidates] = {};

    net::GasResult reply;            // 依頼箱からの返事（内蔵メモリを使わない）
    char snapshot[kSnapshotJsonMax] = {};
    char body[net::kGasRequestMax] = {};
};

// ---------------------------------------------------------------------------
// 画面の状態
// ---------------------------------------------------------------------------
lv_obj_t *s_screen = nullptr;
lv_obj_t *s_content = nullptr;
lv_obj_t *s_board = nullptr;        // 盤ごと描く 1 つの部品
lv_timer_t *s_tick = nullptr;

GameState *s_st = nullptr;

View s_view = View::Entry;
View s_cafe_return = View::Entry;
View s_paused_return = View::Board;
View s_ask_return = View::Board;
View s_tut_return = View::Entry;
Phase s_phase = Phase::Idle;
Ask s_ask = Ask::None;

bool s_dirty = true;
uint32_t s_view_ms = 0;
uint32_t s_phase_ms = 0;
bool s_resume_pause = false;        // ひと休み画面を「再開の入口」として出している

// 設定中の新しい局
uint8_t s_setup_n = 6;
Mode s_setup_mode = Mode::Jev;
int s_setup_color = 0;              // 0 = 黒, 1 = 白, 2 = おまかせ

uint8_t s_tut_page = 0;
uint8_t s_choice_page = 0;
uint16_t s_choice_ply = 0;

uint32_t s_req_no = 0;
uint32_t s_req_ms = 0;
uint16_t s_req_ply = 0;
char s_req_reason[40] = {0};
uint8_t s_attempt = 0;              // 1 = 最初の依頼、2 = 自動の送り直し
bool s_send_pending = false;        // 依頼箱がふさがっていて、まだ預けられていない
uint32_t s_send_since_ms = 0;       // 預けようとし始めた時刻

// 終わった対局の送信（投げっぱなし）。箱がふさがっていたら数秒ねばる
char s_result_reason[12] = {0};     // "completed" など。空なら送るものがない
bool s_result_pending = false;
uint32_t s_result_since_ms = 0;

bool s_resume_available = false;    // 再開できる局があるか（入口で 1 回だけ読む）

// ---------------------------------------------------------------------------
// 部品づくり（AI DUEL・エスパーと同じ考え方）
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

const lv_font_t *bigFont(const char *t, int16_t width)
{
    return (int16_t)(widestUnits(t) * 20) <= width ? &ct_font_jp_40 : &ct_font_jp_22;
}

lv_obj_t *rectLabel(const Rect &r, const lv_font_t *font, lv_color_t color, const char *t)
{
    lv_obj_t *l = lv_label_create(s_content);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    // 日本語は自動折返しが効かない。改行は文言側に入れてあるので幅で切る
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
void choiceCb(lv_event_t *e);
void boardClickCb(lv_event_t *e);
void boardDrawCb(lv_event_t *e);

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

// ---------------------------------------------------------------------------
// 文言づくり
// ---------------------------------------------------------------------------
const char *sizeLabel(uint8_t n)
{
    return text(n == 8 ? "ui.size.label8" : "ui.size.label6");
}

const char *modeLabel(Mode m)
{
    switch (m) {
    case Mode::Jev:    return text("mode.jev");
    case Mode::Pro:    return text("mode.pro");
    case Mode::Casual: return text("mode.casual");
    default:           return text("mode.local");
    }
}

const char *opponentLabel(rev::Opponent o)
{
    switch (o) {
    case rev::Opponent::Jev:    return text("result.jev");
    case rev::Opponent::Pro:    return text("mode.pro");
    case rev::Opponent::Casual: return text("mode.casual");
    case rev::Opponent::Local:  return text("result.local");
    default:                    return text("result.mixed");
    }
}

// 進行中の局があるか。**Session の既定値は Closure::Active なので、
// 「まだ 1 局も開いていない」状態と区別するために started も見る**
bool active()
{
    return s_st != nullptr && s_st->started && s_st->session.closure == Closure::Active;
}

// 人間が石を置ける状態か（設計書 4.2 の 1）。
// 保存に失敗した直後も入力は止めない（盤面は進んでいないので、同じ手をもう一度
// 確定させられる。設計書 11.3 の「再タップを許す前に読み直す」は saveGame の中）
bool humanCanPlay()
{
    return active() && s_phase == Phase::Idle && s_st->session.pos.side == s_st->session.human;
}

// ---------------------------------------------------------------------------
// 盤面の描画（LVGL の部品を 64 個作らず、1 つの部品の描画コールバックで描く）
// ---------------------------------------------------------------------------
void fillCircle(lv_draw_ctx_t *ctx, int16_t cx, int16_t cy, int16_t radius,
                lv_color_t fill, lv_opa_t fill_opa, lv_color_t edge, int16_t edge_width)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = LV_RADIUS_CIRCLE;
    dsc.bg_color = fill;
    dsc.bg_opa = fill_opa;
    dsc.border_color = edge;
    dsc.border_width = edge_width;
    dsc.border_opa = edge_width > 0 ? LV_OPA_COVER : LV_OPA_TRANSP;
    const lv_area_t area = {(lv_coord_t)(cx - radius), (lv_coord_t)(cy - radius),
                            (lv_coord_t)(cx + radius), (lv_coord_t)(cy + radius)};
    lv_draw_rect(ctx, &dsc, &area);
}

void frameCell(lv_draw_ctx_t *ctx, const lv_area_t &cell, lv_color_t color, int16_t width,
               int16_t inset)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = 4;
    dsc.bg_opa = LV_OPA_TRANSP;
    dsc.border_color = color;
    dsc.border_width = width;
    dsc.border_opa = LV_OPA_COVER;
    const lv_area_t area = {(lv_coord_t)(cell.x1 + inset), (lv_coord_t)(cell.y1 + inset),
                            (lv_coord_t)(cell.x2 - inset), (lv_coord_t)(cell.y2 - inset)};
    lv_draw_rect(ctx, &dsc, &area);
}

void boardDrawCb(lv_event_t *e)
{
    if (s_st == nullptr || lv_event_get_target(e) != s_board) {
        return;
    }
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t box;
    lv_obj_get_coords(s_board, &box);

    const Session &s = s_st->session;
    const int n = s.pos.n;
    const int unit = 312 / n;
    const int16_t x0 = (int16_t)box.x1, y0 = (int16_t)box.y1;

    // ます目の線
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = REV_GRID;
    line.width = 1;
    line.opa = LV_OPA_60;
    for (int i = 1; i < n; ++i) {
        lv_point_t a = {(lv_coord_t)(x0 + i * unit), y0};
        lv_point_t b = {(lv_coord_t)(x0 + i * unit), (lv_coord_t)(y0 + 312)};
        lv_draw_line(ctx, &line, &a, &b);
        lv_point_t c = {x0, (lv_coord_t)(y0 + i * unit)};
        lv_point_t d = {(lv_coord_t)(x0 + 312), (lv_coord_t)(y0 + i * unit)};
        lv_draw_line(ctx, &line, &c, &d);
    }

    // 合法手は 1 回だけ数えて印にする（描画は 1 フレームで何度か呼ばれる）
    uint64_t legal_mask = 0;
    if (humanCanPlay()) {
        const auto moves = core::legal(s.pos, s.pos.side);
        for (uint8_t k = 0; k < moves.count; ++k) {
            legal_mask |= uint64_t(1) << moves.cells[k];
        }
    }
    const int16_t stone_r = (int16_t)(unit * 2 / 5);

    for (int i = 0; i < n * n; ++i) {
        const int16_t cx = (int16_t)(x0 + (i % n) * unit + unit / 2);
        const int16_t cy = (int16_t)(y0 + (i / n) * unit + unit / 2);
        const lv_area_t cell = {(lv_coord_t)(x0 + (i % n) * unit), (lv_coord_t)(y0 + (i / n) * unit),
                                (lv_coord_t)(x0 + (i % n) * unit + unit - 1),
                                (lv_coord_t)(y0 + (i / n) * unit + unit - 1)};

        if (s.pos.board[i] != Cell::Empty) {
            const bool black = s.pos.board[i] == Cell::Black;
            // **黒白とも輪郭を付ける**（設計書 4.1）
            fillCircle(ctx, cx, cy, stone_r, black ? REV_STONE_B : REV_STONE_W, LV_OPA_COVER,
                       black ? REV_EDGE_B : REV_STONE_B, 2);
        } else if (legal_mask & (uint64_t(1) << i)) {
            // 合法手は「輪」で示す。色だけに頼らない（設計書 4.1）
            fillCircle(ctx, cx, cy, (int16_t)(unit / 5), REV_ACCENT, LV_OPA_TRANSP, REV_ACCENT, 3);
        }

        // 直前に置かれたマスの印（小さい四角＝石とは別の形）
        if (i == s_st->last_move) {
            lv_draw_rect_dsc_t mark;
            lv_draw_rect_dsc_init(&mark);
            mark.radius = 1;
            mark.bg_color = REV_ACCENT;
            mark.bg_opa = LV_OPA_COVER;
            const lv_area_t dot = {(lv_coord_t)(cell.x1 + 3), (lv_coord_t)(cell.y1 + 3),
                                   (lv_coord_t)(cell.x1 + 8), (lv_coord_t)(cell.y1 + 8)};
            lv_draw_rect(ctx, &mark, &dot);
        }
    }

    // 反転の見せ場（0.18 秒）。返った石を輪で囲む
    if (s_phase == Phase::Animate) {
        for (uint8_t k = 0; k < s_st->anim_count; ++k) {
            const int i = s_st->anim[k];
            const lv_area_t cell = {(lv_coord_t)(x0 + (i % n) * unit),
                                    (lv_coord_t)(y0 + (i / n) * unit),
                                    (lv_coord_t)(x0 + (i % n) * unit + unit - 1),
                                    (lv_coord_t)(y0 + (i / n) * unit + unit - 1)};
            frameCell(ctx, cell, REV_ACCENT, 3, 2);
        }
    }

    // 選んでいるマスと、返る石のプレビュー（棋譜はまだ変えていない）
    if (s_st->selected >= 0 && s_st->selected < n * n) {
        const int i = s_st->selected;
        const int16_t cx = (int16_t)(x0 + (i % n) * unit + unit / 2);
        const int16_t cy = (int16_t)(y0 + (i / n) * unit + unit / 2);
        const lv_area_t cell = {(lv_coord_t)(x0 + (i % n) * unit), (lv_coord_t)(y0 + (i / n) * unit),
                                (lv_coord_t)(x0 + (i % n) * unit + unit - 1),
                                (lv_coord_t)(y0 + (i / n) * unit + unit - 1)};
        frameCell(ctx, cell, REV_ACCENT, 3, 1);
        const bool black = s.pos.side == Cell::Black;
        fillCircle(ctx, cx, cy, stone_r, black ? REV_STONE_B : REV_STONE_W, LV_OPA_70,
                   REV_ACCENT, 2);
        for (uint8_t k = 0; k < s_st->preview_count; ++k) {
            const int j = s_st->preview[k];
            const int16_t px = (int16_t)(x0 + (j % n) * unit + unit / 2);
            const int16_t py = (int16_t)(y0 + (j / n) * unit + unit / 2);
            fillCircle(ctx, px, py, (int16_t)(unit / 4), REV_ACCENT, LV_OPA_TRANSP,
                       black ? REV_STONE_B : REV_STONE_W, 3);
        }
    }
}

// ---------------------------------------------------------------------------
// 盤面の見出し・状態の行
// ---------------------------------------------------------------------------
void boardStatusText(char *out, size_t size)
{
    if (s_st == nullptr) {
        out[0] = '\0';
        return;
    }
    const Session &s = s_st->session;
    const auto c = core::counts(s.pos);
    char tail[80];
    if (s_st->save_failed) {
        // **保存できるまで出しっぱなしにする**（2 秒のトーストだけでは気付けない）。
        // 盤面は保存前のままなので、同じ手をもう一度「置く」で確定させられる
        std::snprintf(tail, sizeof(tail), "%s", text("ui.board.save_failed"));
    } else if (s.closure != Closure::Active) {
        std::snprintf(tail, sizeof(tail), "%s", text("ui.result.title"));
    } else if (s_phase == Phase::Pass) {
        std::snprintf(tail, sizeof(tail), "%s", text(s_st->pass_by_human ? "pass.you" : "pass.ai"));
    } else if (s_phase == Phase::Wait) {
        std::snprintf(tail, sizeof(tail), "%s", text("board.thinking"));
    } else if (s_phase == Phase::Think) {
        std::snprintf(tail, sizeof(tail), "%s", text("board.local_thinking"));
    } else if (s.pos.side != s.human) {
        std::snprintf(tail, sizeof(tail), "%s", text("board.ai_turn"));
    } else if (s_st->selected >= 0) {
        char coord[rev::kCoordMax];
        rev::coordName(s.pos.n, s_st->selected, coord);
        std::snprintf(tail, sizeof(tail), "%s・%u枚", coord, (unsigned)s_st->preview_count);
    } else {
        std::snprintf(tail, sizeof(tail), "%s", text("board.your_turn"));
    }
    std::snprintf(out, size, "%s%d %s%d　%s", text("board.black"), c.black,
                  text("board.white"), c.white, tail);
}

void setView(View v)
{
    s_view = v;
    s_dirty = true;
    s_view_ms = millis();
}

void setPhase(Phase p)
{
    s_phase = p;
    s_phase_ms = millis();
    s_dirty = true;
}

// ---------------------------------------------------------------------------
// 通信（計画 §5・§6）
//
// **LVGL のコールバックの中で通信しない。** net:: の依頼箱に預けるだけで、
// 実際の送受信はメインループ（net::poll）が行う。
// 送ってよいもの: 盤面と棋譜だけ。端末名・杯数・ほかのゲームの記録は入れない。
// ログに URL・合言葉・本文は絶対に出さない（依頼番号と手と所要時間だけ）。
// ---------------------------------------------------------------------------
bool buildRequest(uint32_t req)
{
    if (rev::snapshotJson(s_st->session, s_st->snapshot, sizeof(s_st->snapshot)) == 0) {
        return false;
    }
    const int n = std::snprintf(s_st->body, sizeof(s_st->body),
                                "{\"event\":\"reversi\",\"req\":%lu,\"snapshot\":%s}",
                                (unsigned long)req, s_st->snapshot);
    return n > 0 && (size_t)n < sizeof(s_st->body);
}

void enterTurn();

// 依頼を諦める（投了・途中終了・端末AIへの切替）。箱を空けてから次の用に使う
void cancelJevRequest()
{
    if (s_phase == Phase::Wait || s_send_pending) {
        net::gasCancel();
    }
    s_send_pending = false;
    s_attempt = 0;
}

// 依頼箱へ預ける。ふさがっていたら s_send_pending のまま次の巡回でまた試し、
// kSendBusyMs ねばっても空かなければ本人に選んでもらう。
// **同じ局面は同じ snapshot をそのまま送る**ので、GAS は控えの手を返す（Jev は呼ばれない）
void tryPostJev()
{
    const uint32_t req = s_req_no + 1;
    if (buildRequest(req) && net::gasRequest(req, s_st->body)) {
        s_req_no = req;
        s_req_ms = millis();
        s_req_ply = s_st->session.pos.ply;
        s_send_pending = false;
        s_req_reason[0] = '\0';
        return;
    }
    s_send_pending = true;
    if (millis() - s_send_since_ms >= kSendBusyMs) {
        s_send_pending = false;
        std::snprintf(s_req_reason, sizeof(s_req_reason), "busy");
        Serial.printf("[REV] req=- -> failed %lums (mailbox busy)\n",
                      (unsigned long)(millis() - s_send_since_ms));
        setPhase(Phase::Idle);
        setView(View::NetworkChoice);
    }
}

// attempt = 1 で新しい依頼、2 で自動の送り直し（画面は「判断中」のまま）
void beginJevRequest(uint8_t attempt)
{
    if (!net::gasReady()) {
        std::snprintf(s_req_reason, sizeof(s_req_reason), "offline");
        Serial.println("[REV] req=- -> failed 0ms (offline)");
        setPhase(Phase::Idle);
        setView(View::NetworkChoice);
        return;
    }
    s_attempt = attempt;
    s_send_since_ms = millis();
    setPhase(Phase::Wait);
    tryPostJev();
}

// 終わった対局を 1 行だけ送る（投げっぱなし）。箱がふさがっていたら数秒ねばる
void trySendResult()
{
    if (s_result_reason[0] == '\0' || s_st == nullptr) {
        return;
    }
    if (rev::snapshotJson(s_st->session, s_st->snapshot, sizeof(s_st->snapshot)) == 0) {
        s_result_reason[0] = '\0';
        s_result_pending = false;
        return;
    }
    const uint32_t req = s_req_no + 1;
    const int n = std::snprintf(s_st->body, sizeof(s_st->body),
                                "{\"event\":\"reversi\",\"req\":%lu,"
                                "\"result\":{\"snapshot\":%s,\"end_reason\":\"%s\"}}",
                                (unsigned long)req, s_st->snapshot, s_result_reason);
    if (n > 0 && (size_t)n < sizeof(s_st->body) && net::gasRequest(req, s_st->body, true)) {
        s_req_no = req;
        s_st->sent = true;
        s_send_pending = false;
        s_result_pending = false;
        Serial.printf("[REV] result %s queued req=%lu\n", s_result_reason, (unsigned long)req);
        s_result_reason[0] = '\0';
        return;
    }
    s_result_pending = true;
    if (millis() - s_result_since_ms >= kSendBusyMs) {
        s_result_pending = false;
        Serial.printf("[REV] result %s dropped (mailbox busy)\n", s_result_reason);
        s_result_reason[0] = '\0';
    }
}

void sendResult(const char *end_reason)
{
    s_st->sent = false;
    s_result_pending = false;
    s_result_reason[0] = '\0';
    if (!net::gasReady()) {
        Serial.printf("[REV] result %s not sent (offline)\n", end_reason);
        return;
    }
    std::snprintf(s_result_reason, sizeof(s_result_reason), "%s", end_reason);
    s_result_since_ms = millis();
    trySendResult();
}

// 返事を読む。合法な手を取り出せたら true（**手は端末で必ず検証する**。計画 §5）
bool parseJevReply(const net::GasResult &r, int &move, char *reason, size_t reason_size)
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
    const char *status = doc["status"] | "failed";
    if (std::strcmp(status, "ready") != 0) {
        const char *why = doc["reason"] | "none";
        std::snprintf(reason, reason_size, "none:%.20s", why);
        return false;
    }
    // 手数が今の局面と違えば古い返事。適用しない（設計書 12.2）
    if (!doc["ply"].is<uint16_t>() || doc["ply"].as<uint16_t>() != s_st->session.pos.ply ||
        s_st->session.pos.ply != s_req_ply) {
        std::snprintf(reason, reason_size, "stale reply");
        return false;
    }
    const char *name = doc["move"] | "";
    const uint8_t n = s_st->session.pos.n;
    const int square = rev::squareOf(n, name);
    if (square < 0 || core::captures(s_st->session.pos, square, s_st->session.pos.side) == 0) {
        std::snprintf(reason, reason_size, "illegal reply");
        return false;
    }
    move = square;

    // 評価の控え（DETAILS 用）。確率が読めなくても手は使う
    GameState &st = *s_st;
    st.jev_valid = true;
    st.jev_ply = st.session.pos.ply;
    std::snprintf(st.jev_move, sizeof(st.jev_move), "%s", name);
    std::snprintf(st.jev_top, sizeof(st.jev_top), "%s", doc["top"] | name);
    st.jev_confidence = doc["confidence"] | 0.0f;
    st.jev_cached = doc["cached"] | false;
    st.jev_sampled = std::strcmp(st.jev_move, st.jev_top) != 0;
    st.jev_count = 0;
    st.jev_total = 0;
    JsonObjectConst probs = doc["p"].as<JsonObjectConst>();
    for (JsonPairConst kv : probs) {
        if (st.jev_total < 255) {
            ++st.jev_total;
        }
        const uint8_t percent = (uint8_t)(kv.value().as<float>() * 100.0f + 0.5f);
        // 大きい順に挿し込み、上位 kJevCandidates 件だけ残す（画面に出せるのは数件）
        uint8_t at = 0;
        while (at < st.jev_count && st.jev_percent[at] >= percent) {
            ++at;
        }
        if (at >= kJevCandidates) {
            continue;
        }
        const uint8_t last = st.jev_count < kJevCandidates ? st.jev_count
                                                           : (uint8_t)(kJevCandidates - 1);
        for (uint8_t j = last; j > at; --j) {
            st.jev_percent[j] = st.jev_percent[j - 1];
            std::memcpy(st.jev_names[j], st.jev_names[j - 1], rev::kCoordMax);
        }
        st.jev_percent[at] = percent;
        std::snprintf(st.jev_names[at], rev::kCoordMax, "%s", kv.key().c_str());
        if (st.jev_count < kJevCandidates) {
            ++st.jev_count;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// 1 手の確定（設計書 5.4：**保存してから**画面を確定表示にする）
// ---------------------------------------------------------------------------
void finishGame(const char *end_reason);

bool commitMove(int move, Source source)
{
    if (s_st == nullptr) {
        return false;
    }
    GameState &st = *s_st;
    st.scratch = st.session;
    const uint8_t flips = move < 0 ? 0 : rev::flipList(st.scratch.pos, move, st.anim);

    if (!core::commit(st.scratch, move, source, st.scratch.pos.ply)) {
        // 起こらないはずの状態。盤面は 1 ミリも動かさない
        Serial.printf("[REV] the core refused move %d (source %c)\n", move,
                      rev::sourceChar(source));
        ui::showToast(s_screen, text("board.illegal"));
        setPhase(Phase::Idle);
        return false;
    }
    if (!store::saveGame(st.scratch)) {
        // 保存できなかった。**画面は保存前の盤面のまま**止める（設計書 11.3）
        st.save_failed = true;
        ui::showToast(s_screen, text("save.failure"));
        setPhase(Phase::Idle);
        return false;
    }

    st.session = st.scratch;
    st.anim_count = flips;
    st.last_move = move;
    st.selected = -1;
    st.preview_count = 0;
    st.save_failed = false;

    if (st.session.closure == Closure::Completed) {
        finishGame("completed");
        return true;
    }
    if (move >= 0) {
        // 返った石を 0.18 秒だけ見せてから次の手番へ。**人間の手でも見せる**
        // （相手の合法手が 1 つしかない局面では、ここで見せないと自分の反転が
        //   相手の反転と同時に出てしまう）
        setPhase(Phase::Animate);
        return true;
    }
    enterTurn();
    return true;
}

// ---------------------------------------------------------------------------
// 手番へ入る共通処理（設計書 5.2。ここ 1 か所にまとめる）
// ---------------------------------------------------------------------------
void enterTurn()
{
    if (!active()) {
        return;
    }
    Session &s = s_st->session;
    const auto mine = core::legal(s.pos, s.pos.side);
    if (mine.count == 0) {
        // 双方 0 手なら commit の時点で終局しているので、ここは片側だけのパス
        s_st->pass_by_human = (s.pos.side == s.human);
        setPhase(Phase::Pass);
        return;
    }
    if (s.pos.side == s.human) {
        setPhase(Phase::Idle);          // 合法手が 1 つでも本人が「置く」で確定する
        return;
    }
    if (mine.count == 1) {
        commitMove(mine.cells[0], Source::Forced);   // 一択は Jev を呼ばない（設計書 2.4）
        return;
    }
    if (s.mode == Mode::Local || s.localOnly) {
        setPhase(Phase::Think);         // 次の巡回で計算する（画面に「選んでいます」を出すため）
        return;
    }
    beginJevRequest(1);
}

void finishGame(const char *end_reason)
{
    GameState &st = *s_st;
    if (!st.counted) {
        st.counted = true;
        rev::Outcome outcome;
        const rev::Opponent opponent = rev::opponentOf(st.session);
        if (rev::outcomeOf(st.session, outcome)) {
            store::noteResult(st.session.pos.n, opponent, outcome);
        }
        if (st.session.closure == Closure::Completed ||
            st.session.closure == Closure::Resigned) {
            // 数えるのは回数だけ。盤面も勝敗も SD には残さない
            char note[40];
            std::snprintf(note, sizeof(note), "reversi %ux%u %s", (unsigned)st.session.pos.n,
                          (unsigned)st.session.pos.n, rev::modeId(st.session.mode));
            cup::stats::gamePlayed(cup::GameId::Reversi, note);
        }
        sendResult(end_reason);
    }
    s_resume_available = false;     // 終わった局は再開の対象にしない
    setPhase(Phase::Idle);
    setView(View::Result);
}

// 1 局ぶんの表示用の控えを白紙に戻す（GameState 全体の一時変数を作ると
// 6KB がスタックに乗るので、必要なところだけ書き戻す）
void resetRoundState(GameState &st)
{
    st.selected = -1;
    st.preview_count = 0;
    st.last_move = -1;
    st.anim_count = 0;
    st.started = false;
    st.counted = false;
    st.sent = false;
    st.pass_by_human = false;
    st.save_failed = false;
    st.jev_valid = false;
    st.jev_sampled = false;
    st.jev_cached = false;
    st.jev_ply = 0;
    st.jev_confidence = 0;
    st.jev_count = 0;
    st.jev_total = 0;
    st.jev_move[0] = '\0';
    st.jev_top[0] = '\0';
}

// 新しい局を始める。保存中の局があれば aborted として閉じ、結果だけ送ってから上書きする
void startGame()
{
    GameState &st = *s_st;
    Cell human = s_setup_color == 1 ? Cell::White : Cell::Black;
    if (s_setup_color == 2) {
        // おまかせは開始時に一度だけ引く。保存・再開で引き直さない（設計書 1.3）
        human = (esp_random() & 1u) ? Cell::White : Cell::Black;
    }
    std::array<uint8_t, 16> id{};
    esp_fill_random(id.data(), id.size());

    // 前の対局の記録がまだ預けられていないなら、ここで諦める
    // （このあと session を入れ替えるので、送るべき棋譜が変わってしまう）
    if (s_result_pending) {
        Serial.printf("[REV] result %s dropped (new game)\n", s_result_reason);
        s_result_pending = false;
        s_result_reason[0] = '\0';
    }
    resetRoundState(st);
    if (!core::start(st.session, s_setup_n, human, s_setup_mode, id)) {
        ui::showToast(s_screen, text("save.corrupt"));
        setView(View::Entry);
        return;
    }
    if (!store::saveGame(st.session)) {
        st.save_failed = true;
        ui::showToast(s_screen, text("save.failure"));
        setView(View::Entry);
        return;
    }
    st.started = true;
    s_resume_available = true;
    Serial.printf("[REV] new game %ux%u mode=%s human=%c\n", (unsigned)s_setup_n,
                  (unsigned)s_setup_n, rev::modeId(s_setup_mode), core::symbol(human));
    setView(View::Board);
    enterTurn();
}

// 保存中の局を aborted で閉じて、結果を投げる（新しい局で上書きする前）
void discardSavedGame()
{
    Session old;
    if (!store::loadGame(old) || old.closure != Closure::Active) {
        return;
    }
    if (!core::close(old, Closure::Aborted)) {
        return;
    }
    // 送信のときだけ古い局に差し替える（snapshotJson が session を見るため）
    s_st->scratch = s_st->session;
    s_st->session = old;
    sendResult("aborted");
    s_st->session = s_st->scratch;
    // 送り直しの待ち行列には入れない（このあと session が新しい局に変わるため）
    s_result_pending = false;
    s_result_reason[0] = '\0';
    // 本人が「終了して始める」を選んだので、この局はもう再開の対象にしない
    store::clearGame();
    s_resume_available = false;
}

// ---------------------------------------------------------------------------
// 画面づくり
// ---------------------------------------------------------------------------
void buildEntry()
{
    makeTitle(text("ui.entry.title"));
    rectLabel(layout::kEntryLogo, &ct_font_jp_40, CT_COLOR_ACCENT_HI, text("game.title"));
    rectLabel(layout::kEntrySub, &ct_font_jp_20, CT_COLOR_TEXT, text("game.subtitle"));

    uint32_t w6, l6, d6, w8, l8, d8;
    store::totals(6, w6, l6, d6);
    store::totals(8, w8, l8, d8);
    char record[96];
    std::snprintf(record, sizeof(record), "%s\n6×6 %lu-%lu-%lu　8×8 %lu-%lu-%lu",
                  text("ui.entry.record"), (unsigned long)w6, (unsigned long)l6,
                  (unsigned long)d6, (unsigned long)w8, (unsigned long)l8, (unsigned long)d8);
    rectLabel(layout::kEntryRecord, &ct_font_jp_20, CT_COLOR_DIM, record);

    // 再開できるかは画面を開いたときに 1 回だけ読んである（棋譜の再生は重いので、
    // 入口を描き直すたびにやらない）
    const bool resume = s_resume_available;
    rectButton(layout::kEntryResume, text("start.resume"), Act::EntryResume, resume, resume);
    rectButton(layout::kEntryStart, text("start.new"), Act::EntryStart, true, !resume);
    rectButton(layout::kEntryHow, text("menu.rules"), Act::EntryHow);
    rectButton(layout::kEntryBack, text("ui.back_list"), Act::EntryBack);
}

void buildTutorial()
{
    const size_t page = s_tut_page < text_db::kTutorialCount ? s_tut_page : 0;
    char title[40];
    std::snprintf(title, sizeof(title), "%s  %u / %u", text("ui.tutorial.title"),
                  (unsigned)(page + 1), (unsigned)text_db::kTutorialCount);
    makeTitle(title);
    rectLabel(layout::kTutTitle, &ct_font_jp_22, CT_COLOR_ACCENT_HI,
              text_db::kTutorial[page].title);
    rectLabel(layout::kTutBody, &ct_font_jp_22, CT_COLOR_TEXT, text_db::kTutorial[page].body);
    rectButton(layout::kTutPrev, text("ui.prev"), Act::TutPrev, page > 0);
    rectButton(layout::kTutNext, text("common.next"), Act::TutNext,
               page + 1 < text_db::kTutorialCount, true);
    rectButton(layout::kTutBack, text("common.back"), Act::TutBack);
}

void buildSetupSize()
{
    makeTitle(text("start.size"));
    rectButton(layout::kSizeBtn[0], text("start.quick"), Act::Size6, true, s_setup_n == 6);
    rectLabel(layout::kSizeNote[0], &ct_font_jp_20, CT_COLOR_SUBTEXT, text("start.quick.note"));
    rectButton(layout::kSizeBtn[1], text("start.classic"), Act::Size8, true, s_setup_n == 8);
    rectLabel(layout::kSizeNote[1], &ct_font_jp_20, CT_COLOR_SUBTEXT, text("start.classic.note"));
    rectButton(layout::kSizeBack, text("common.back"), Act::SizeBack);
}

void buildSetupMode()
{
    makeTitle(text("start.opponent"));
    const bool online = net::gasReady();
    static const Act kActs[4] = {Act::ModeJev, Act::ModePro, Act::ModeCasual, Act::ModeLocal};
    static const char *const kNames[4] = {"mode.jev", "mode.pro", "mode.casual", "mode.local"};
    static const char *const kNotes[4] = {"mode.jev.note", "mode.pro.note", "mode.casual.note",
                                          "mode.local.note"};
    for (size_t i = 0; i < 4; ++i) {
        char body[128];
        std::snprintf(body, sizeof(body), "%s\n%s", text(kNames[i]), text(kNotes[i]));
        const bool enabled = (i == 3) || online;
        rectButton(layout::kModeBtn[i], body, kActs[i], enabled,
                   enabled && (size_t)s_setup_mode == i);
    }
    // Jev 系が選べない理由を出す。**黙って端末 AI に変えない**（設計書 1.2）
    char note[160];
    std::snprintf(note, sizeof(note), "%s", online ? text("mode.notice") : text("start.no_online"));
    rectLabel(layout::kModeNote, &ct_font_jp_20, online ? CT_COLOR_DIM : CT_COLOR_WARN, note);
    rectButton(layout::kModeBack, text("common.back"), Act::ModeBack);
}

void buildSetupColor()
{
    makeTitle(text("start.side"));
    static const Act kActs[3] = {Act::ColorBlack, Act::ColorWhite, Act::ColorRandom};
    static const char *const kNames[3] = {"start.black", "start.white", "start.random"};
    for (size_t i = 0; i < 3; ++i) {
        rectButton(layout::kColorBtn[i], text(kNames[i]), kActs[i], true,
                   (size_t)s_setup_color == i);
    }
    rectButton(layout::kColorGo, text("start.go"), Act::ColorGo, true, true);
    rectButton(layout::kColorBack, text("common.back"), Act::ColorBack);
}

void buildBoard()
{
    const Session &s = s_st->session;
    char title[48];
    std::snprintf(title, sizeof(title), "%s %s", text("game.title"), sizeLabel(s.pos.n));
    rectLabel(layout::kBoardTitle, &ct_font_jp_20, CT_COLOR_SUBTEXT, title);

    char line[128];
    boardStatusText(line, sizeof(line));
    rectLabel(layout::kBoardScore, fitFont(line, layout::kBoardScore.w),
              s_st->save_failed ? CT_COLOR_ALERT : CT_COLOR_TEXT, line);

    s_board = lv_obj_create(s_content);
    lv_obj_remove_style_all(s_board);
    lv_obj_set_pos(s_board, layout::kBoardArea.x, layout::kBoardArea.y);
    lv_obj_set_size(s_board, layout::kBoardArea.w, layout::kBoardArea.h);
    lv_obj_clear_flag(s_board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_board, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_board, REV_BOARD_BG, 0);
    lv_obj_set_style_bg_opa(s_board, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_board, 6, 0);
    lv_obj_set_style_border_color(s_board, REV_GRID, 0);
    lv_obj_set_style_border_width(s_board, 1, 0);
    lv_obj_add_event_cb(s_board, boardDrawCb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(s_board, boardClickCb, LV_EVENT_CLICKED, nullptr);

    const bool can_place = humanCanPlay() && s_st->selected >= 0;
    rectButton(layout::kBoardMenu, text("board.menu"), Act::BoardMenu);
    rectButton(layout::kBoardPlace, text("board.place"), Act::BoardPlace, can_place, can_place);
    rectButton(layout::kBoardList, text("board.choices"), Act::BoardList, humanCanPlay());
}

void buildChoices()
{
    makeTitle(text("ui.choices.title"));
    const Session &s = s_st->session;
    const auto moves = core::legal(s.pos, s.pos.side);
    const uint8_t pages = moves.count == 0 ? 1 : (uint8_t)((moves.count + 3) / 4);
    if (s_choice_page >= pages) {
        s_choice_page = 0;
    }
    const auto c = core::counts(s.pos);
    char hint[128];
    std::snprintf(hint, sizeof(hint), "%s\n%s%d　%s%d",
                  moves.count == 0 ? text("ui.choices.none") : text("board.select"),
                  text("board.black"), c.black, text("board.white"), c.white);
    rectLabel(layout::kChoiceHint, &ct_font_jp_20, CT_COLOR_SUBTEXT, hint);

    for (uint8_t i = 0; i < 4; ++i) {
        const uint8_t index = (uint8_t)(s_choice_page * 4 + i);
        if (index >= moves.count) {
            break;
        }
        const int square = moves.cells[index];
        char coord[rev::kCoordMax];
        rev::coordName(s.pos.n, square, coord);
        uint8_t flips[64];
        const uint8_t count = rev::flipList(s.pos, square, flips);
        char body[32];
        std::snprintf(body, sizeof(body), "%s\n%u枚", coord, (unsigned)count);
        makeButton(layout::kChoiceCard[i], body, choiceCb, (void *)(intptr_t)square, true,
                   square == s_st->selected);
    }

    char page[32];
    std::snprintf(page, sizeof(page), "%u / %u", (unsigned)(s_choice_page + 1), (unsigned)pages);
    rectLabel(layout::kChoicePage, &ct_font_jp_20, CT_COLOR_SUBTEXT, page);
    rectButton(layout::kChoicePrev, text("ui.prev"), Act::ChoicePrev, pages > 1);
    rectButton(layout::kChoiceNext, text("common.next"), Act::ChoiceNext, pages > 1);
    rectButton(layout::kChoiceBack, text("common.back"), Act::ChoiceBack, true, true);
}

void buildMenu()
{
    makeTitle(text("ui.menu.title"));
    rectButton(layout::kMenuItem[0], text("menu.continue"), Act::MenuContinue, true, true);
    rectButton(layout::kMenuItem[1], text("menu.decision"), Act::MenuDetails);
    rectButton(layout::kMenuItem[2], text("menu.rules"), Act::MenuRules);
    rectButton(layout::kMenuItem[3], text("menu.pause"), Act::MenuCafe);
    rectButton(layout::kMenuItem[4], text("menu.resign"), Act::MenuResign, active());
    rectButton(layout::kMenuItem[5], text("menu.end"), Act::MenuEnd, active());
}

void buildDetails()
{
    makeTitle(text("detail.title"));
    const GameState &st = *s_st;
    if (!st.jev_valid) {
        rectLabel(layout::kDetHead, &ct_font_jp_22, CT_COLOR_TEXT, text("detail.none"));
        rectLabel(layout::kDetNote, &ct_font_jp_20, CT_COLOR_DIM, text("detail.forced"));
        rectButton(layout::kDetBack, text("common.back"), Act::DetailsBack, true, true);
        return;
    }
    // 3 行め は「くじ引きだった」「控えから返ってきた」のどちらか（無ければ空行）
    const char *aside = st.jev_sampled ? text("detail.sampled")
                      : st.jev_cached  ? text("ui.detail.cached")
                                       : "";
    char head[160];
    std::snprintf(head, sizeof(head), "%s %s　%s %s\n%s %.2f\n%s",
                  text("ui.detail.chosen"), st.jev_move, text("ui.detail.top"), st.jev_top,
                  text("ui.detail.conf"), (double)st.jev_confidence, aside);
    rectLabel(layout::kDetHead, &ct_font_jp_20, CT_COLOR_TEXT, head);

    // 上位 4 手だけ並べ、残りは件数でまとめる（8×8 は合法手が 20 手を超えることがあり、
    // 全部並べると注記と「もどる」に重なる）
    constexpr uint8_t kShown = 4;
    char list[192];
    size_t at = 0;
    uint8_t shown = 0;
    for (; shown < st.jev_count && shown < kShown && at + 24 < sizeof(list); ++shown) {
        at += (size_t)std::snprintf(list + at, sizeof(list) - at, "%s %u%%%s\n",
                                    st.jev_names[shown], (unsigned)st.jev_percent[shown],
                                    std::strcmp(st.jev_names[shown], st.jev_move) == 0 ? " ←" : "");
    }
    if (st.jev_total > shown) {
        std::snprintf(list + at, sizeof(list) - at, "%s%u手", text("ui.detail.more"),
                      (unsigned)(st.jev_total - shown));
    } else if (at == 0) {
        std::snprintf(list, sizeof(list), "%s", text("detail.none"));
    }
    rectLabel(layout::kDetList, &ct_font_jp_20, CT_COLOR_ACCENT_HI, list);

    char note[192];
    std::snprintf(note, sizeof(note), "%s\n%s", text("detail.notice"), text("detail.not_reason"));
    rectLabel(layout::kDetNote, &ct_font_jp_20, CT_COLOR_DIM, note);
    rectButton(layout::kDetBack, text("common.back"), Act::DetailsBack, true, true);
}

void buildNetworkChoice()
{
    makeTitle(text("ui.net.title"));
    // 何が起きたかを平たい日本語で 1 行だけ足す（状態コードは出さない。設計書 4.4）
    const char *why = nullptr;
    if (std::strcmp(s_req_reason, "offline") == 0) {
        why = text("start.no_online");
    } else if (std::strcmp(s_req_reason, "busy") == 0) {
        why = text("ui.net.busy");
    } else if (std::strncmp(s_req_reason, "none:DAILY_LIMIT", 16) == 0) {
        why = text("net.quota");
    } else if (std::strncmp(s_req_reason, "none:NO_KEY", 11) == 0) {
        why = text("start.no_key");
    } else if (std::strcmp(s_req_reason, "illegal reply") == 0 ||
               std::strcmp(s_req_reason, "bad reply") == 0 ||
               std::strncmp(s_req_reason, "none:", 5) == 0) {
        why = text("net.bad_response");
    } else if (std::strcmp(s_req_reason, "stale reply") == 0) {
        why = text("net.stale");
    }
    char body[224];
    std::snprintf(body, sizeof(body), "%s%s%s", text("net.slow"), why != nullptr ? "\n" : "",
                  why != nullptr ? why : "");
    rectLabel(layout::kNetBody, &ct_font_jp_20, CT_COLOR_WARN, body);
    rectButton(layout::kNetBtn[0], text("net.retry"), Act::NetRetry, true, true);
    rectButton(layout::kNetBtn[1], text("net.local"), Act::NetLocal);
    rectButton(layout::kNetBtn[2], text("net.pause"), Act::NetCafe);
}

void buildConfirm()
{
    makeTitle(text("ui.confirm.title"));
    const char *body = text("start.confirm_new");
    switch (s_ask) {
    case Ask::Resign:  body = text("menu.resign.confirm"); break;
    case Ask::EndGame: body = text("menu.end.confirm"); break;
    case Ask::GoLocal: body = text("net.local.confirm"); break;
    default: break;
    }
    rectLabel(layout::kAskBody, &ct_font_jp_22, CT_COLOR_ALERT, body);
    rectButton(layout::kAskYes, text("common.yes"), Act::AskYes);
    rectButton(layout::kAskNo, text("common.no"), Act::AskNo, true, true);
}

void buildPaused()
{
    makeTitle(text("ui.pause.title"));
    rectLabel(layout::kPanelBody, &ct_font_jp_22, CT_COLOR_TEXT,
              s_resume_pause ? text("save.resume") : text("pause.idle"));
    rectButton(layout::kPanel[0], text("ui.pause.resume"), Act::PausedResume, true, true);
    rectButton(layout::kPanel[1], text("ui.cafe.take"), Act::CafeCoffee);
    rectButton(layout::kPanel[2], text("ui.pause.quit"), Act::PausedQuit);
}

void buildResult()
{
    makeTitle(text("ui.result.title"));
    const Session &s = s_st->session;
    const auto c = core::counts(s.pos);

    // 勝敗を付けずに終えた局は、大きい行は短く、理由は下の行に出す
    const char *verdict = text("ui.result.ended");
    rev::Outcome outcome;
    if (rev::outcomeOf(s, outcome)) {
        verdict = outcome == rev::Outcome::Win    ? text("result.win")
                : outcome == rev::Outcome::Loss   ? text("result.loss")
                                                  : text("result.draw");
    }
    rectLabel(layout::kResVerdict, bigFont(verdict, (int16_t)(layout::kResVerdict.w - 8)),
              CT_COLOR_TEXT, verdict);

    char counts[64];
    std::snprintf(counts, sizeof(counts), "%s%d　%s%d", text("board.black"), c.black,
                  text("board.white"), c.white);
    rectLabel(layout::kResCount, &ct_font_jp_22, CT_COLOR_ACCENT_HI, counts);

    const char *reason = s.closure == Closure::Completed  ? text("result.reason")
                       : s.closure == Closure::Resigned   ? text("result.resigned")
                                                          : text("result.aborted");
    char detail[224];
    std::snprintf(detail, sizeof(detail), "%s ・ %s\n%s", sizeLabel(s.pos.n),
                  opponentLabel(rev::opponentOf(s)), reason);
    rectLabel(layout::kResDetail, &ct_font_jp_20, CT_COLOR_SUBTEXT, detail);

    // 投げっぱなしの送信なので「届いた」とは言わない。預けられたかどうかだけを出す
    rectLabel(layout::kResSync, &ct_font_jp_20, CT_COLOR_DIM,
              (s_st->sent || s_result_pending) ? text("result.sync_wait")
                                               : text("result.sync_failed"));
    rectButton(layout::kResAgain, text("result.again"), Act::ResultAgain, true, true);
    rectButton(layout::kResHome, text("result.home"), Act::ResultHome);
}

void buildCafe()
{
    makeTitle(text("ui.cafe.title"));
    rectLabel(layout::kPanelBody, &ct_font_jp_22, CT_COLOR_TEXT, text("ui.cafe.body"));
    rectButton(layout::kPanel[0], text("ui.cafe.take"), Act::CafeCoffee, true, true);
    rectButton(layout::kPanel[1], text("ui.cafe.return"), Act::CafeBack);
    rectButton(layout::kPanel[2], text("ui.cafe.home"), Act::CafeHome);
}

// ---------------------------------------------------------------------------
// 作り直し
// ---------------------------------------------------------------------------
void rebuild()
{
    if (s_content == nullptr || s_st == nullptr) {
        return;
    }
    s_board = nullptr;
    lv_obj_clean(s_content);
    switch (s_view) {
    case View::Entry:         buildEntry(); break;
    case View::SetupSize:     buildSetupSize(); break;
    case View::SetupMode:     buildSetupMode(); break;
    case View::SetupColor:    buildSetupColor(); break;
    case View::Board:         buildBoard(); break;
    case View::Choices:       buildChoices(); break;
    case View::Menu:          buildMenu(); break;
    case View::Details:       buildDetails(); break;
    case View::NetworkChoice: buildNetworkChoice(); break;
    case View::Confirm:       buildConfirm(); break;
    case View::Paused:        buildPaused(); break;
    case View::Result:        buildResult(); break;
    case View::Tutorial:      buildTutorial(); break;
    case View::Cafe:          buildCafe(); break;
    }
}

// ---------------------------------------------------------------------------
// 入力
// ---------------------------------------------------------------------------
void selectSquare(int square)
{
    if (!humanCanPlay() || s_st == nullptr) {
        return;
    }
    const Session &s = s_st->session;
    if (square < 0 || square >= s.pos.n * s.pos.n) {
        return;
    }
    if (s.pos.board[square] != Cell::Empty) {
        ui::showToast(s_screen, text("board.occupied"));
        return;
    }
    if (core::captures(s.pos, square, s.pos.side) == 0) {
        ui::showToast(s_screen, text("board.illegal"));
        return;
    }
    s_st->selected = square;
    s_st->preview_count = rev::flipList(s.pos, square, s_st->preview);
    // 「置く」のボタンは押せる / 押せないが変わるので、部品ごと作り直す
    s_dirty = true;
}

void boardClickCb(lv_event_t *e)
{
    if (lv_event_get_target(e) != s_board || s_st == nullptr) {
        return;
    }
    if (millis() - s_view_ms < kInputLatchMs) {
        return;     // 画面が変わった直後の指の残り（設計書 4.2 の 6）
    }
    lv_indev_t *indev = lv_indev_get_act();
    if (indev == nullptr) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    // 当たり判定はコアの hit_test（84 ≤ x < 396・右端 396 は盤外）
    selectSquare(core::hit_test(p.x, p.y, s_st->session.pos.n));
}

void choiceCb(lv_event_t *e)
{
    const int square = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_st == nullptr || s_st->session.pos.ply != s_choice_ply) {
        // 一覧を開いたときと手数が変わった（設計書 4.3）。捨てて盤へ戻す
        setView(View::Board);
        return;
    }
    selectSquare(square);
    setView(View::Board);
}

void confirmAsk(Ask ask)
{
    s_ask = ask;
    s_ask_return = s_view;
    setView(View::Confirm);
}

void actionCb(lv_event_t *e)
{
    const Act act = (Act)(intptr_t)lv_event_get_user_data(e);
    if (s_st == nullptr) {
        return;
    }

    switch (act) {
    case Act::EntryResume: {
        if (!store::loadGame(s_st->session) ||
            s_st->session.closure != Closure::Active) {
            ui::showToast(s_screen, text("save.corrupt"));
            store::clearGame();
            s_resume_available = false;
            s_dirty = true;
            break;
        }
        resetRoundState(*s_st);
        s_st->started = true;
        // 再開は必ずひと休みを一度はさむ（設計書 11.4）
        s_resume_pause = true;
        s_paused_return = View::Board;
        setPhase(Phase::Idle);
        setView(View::Paused);
        break;
    }
    case Act::EntryStart:
        if (s_resume_available) {
            confirmAsk(Ask::NewGame);
        } else {
            setView(View::SetupSize);
        }
        break;
    case Act::EntryHow:
        s_tut_page = 0;
        s_tut_return = View::Entry;
        setView(View::Tutorial);
        break;
    case Act::EntryBack:
        ui::pop();                  // ゲーム一覧へ戻る
        return;

    case Act::TutPrev:
        if (s_tut_page > 0) {
            --s_tut_page;
        }
        s_dirty = true;
        break;
    case Act::TutNext:
        if (s_tut_page + 1 < text_db::kTutorialCount) {
            ++s_tut_page;
        }
        s_dirty = true;
        break;
    case Act::TutBack:
        setView(s_tut_return);
        break;

    case Act::Size6:
        s_setup_n = 6;
        setView(View::SetupMode);
        break;
    case Act::Size8:
        s_setup_n = 8;
        setView(View::SetupMode);
        break;
    case Act::SizeBack:
        setView(View::Entry);
        break;

    case Act::ModeJev:    s_setup_mode = Mode::Jev;    setView(View::SetupColor); break;
    case Act::ModePro:    s_setup_mode = Mode::Pro;    setView(View::SetupColor); break;
    case Act::ModeCasual: s_setup_mode = Mode::Casual; setView(View::SetupColor); break;
    case Act::ModeLocal:  s_setup_mode = Mode::Local;  setView(View::SetupColor); break;
    case Act::ModeBack:   setView(View::SetupSize); break;

    case Act::ColorBlack:  s_setup_color = 0; s_dirty = true; break;
    case Act::ColorWhite:  s_setup_color = 1; s_dirty = true; break;
    case Act::ColorRandom: s_setup_color = 2; s_dirty = true; break;
    case Act::ColorGo:
        if (millis() - s_view_ms < kInputLatchMs) {
            break;
        }
        startGame();
        break;
    case Act::ColorBack:
        setView(View::SetupMode);
        break;

    case Act::BoardPlace:
        if (humanCanPlay() && s_st->selected >= 0 && millis() - s_view_ms >= kInputLatchMs) {
            // 確定は 1 回だけ。expectedPly はコアが見張る（設計書 4.2 の 5）
            commitMove(s_st->selected, Source::Human);
        }
        break;
    case Act::BoardMenu:
        setView(View::Menu);
        break;
    case Act::BoardList:
        s_choice_page = 0;
        s_choice_ply = s_st->session.pos.ply;
        setView(View::Choices);
        break;

    case Act::ChoicePrev:
        s_choice_page = (uint8_t)(s_choice_page == 0 ? 0 : s_choice_page - 1);
        s_dirty = true;
        break;
    case Act::ChoiceNext:
        ++s_choice_page;
        s_dirty = true;
        break;
    case Act::ChoiceBack:
        setView(View::Board);
        break;

    case Act::MenuContinue:
        setView(active() ? View::Board : View::Result);
        // 保存に失敗して止まっていたら、ここでやり直す（自分の強制パスが保存できずに
        //「置ける場所が無いのにボタンも押せない」状態も、ここで抜けられる）。
        // 人間の手番でも enterTurn は場面を組み直すだけで、選んだマスは残る
        if (active() && s_phase == Phase::Idle) {
            enterTurn();
        }
        break;
    case Act::MenuDetails:
        setView(View::Details);
        break;
    case Act::MenuRules:
        s_tut_page = 0;
        s_tut_return = View::Menu;
        setView(View::Tutorial);
        break;
    case Act::MenuCafe:
        s_cafe_return = View::Board;
        setView(View::Cafe);
        break;
    case Act::MenuResign:
        confirmAsk(Ask::Resign);
        break;
    case Act::MenuEnd:
        confirmAsk(Ask::EndGame);
        break;

    case Act::DetailsBack:
        setView(View::Menu);
        break;

    case Act::NetRetry:
        setView(View::Board);
        beginJevRequest(1);         // 同じ棋譜を送り直す（GAS は控えの手を返す）
        break;
    case Act::NetLocal:
        confirmAsk(Ask::GoLocal);
        break;
    case Act::NetCafe:
        s_cafe_return = View::NetworkChoice;
        setView(View::Cafe);
        break;

    case Act::AskYes: {
        const Ask ask = s_ask;
        s_ask = Ask::None;
        if (ask == Ask::NewGame) {
            discardSavedGame();
            setView(View::SetupSize);
        } else if (ask == Ask::Resign) {
            s_st->scratch = s_st->session;
            if (core::close(s_st->scratch, Closure::Resigned) && store::saveGame(s_st->scratch)) {
                s_st->session = s_st->scratch;
                // 待っている依頼を先に捨てて箱を空ける（結果が busy で消えないように）
                cancelJevRequest();
                finishGame("resigned");
            } else {
                ui::showToast(s_screen, text("save.failure"));
                setView(View::Board);
            }
        } else if (ask == Ask::EndGame) {
            s_st->scratch = s_st->session;
            if (core::close(s_st->scratch, Closure::Aborted)) {
                s_st->session = s_st->scratch;
                store::clearGame();     // 途中終了した局は再開の対象にしない
                s_resume_available = false;
                cancelJevRequest();
                finishGame("aborted");
            } else {
                setView(View::Board);
            }
        } else if (ask == Ask::GoLocal) {
            // 端末 AI への切替は不可逆。**先に保存してから**進む（設計書 12.3）
            s_st->scratch = s_st->session;
            if (core::freezeLocal(s_st->scratch) && store::saveGame(s_st->scratch)) {
                s_st->session = s_st->scratch;
                cancelJevRequest();
                Serial.println("[REV] switched to the on-device AI for this game");
                setView(View::Board);
                enterTurn();
            } else {
                ui::showToast(s_screen, text("save.failure"));
                setView(View::NetworkChoice);
            }
        } else {
            setView(View::Entry);
        }
        break;
    }
    case Act::AskNo:
        s_ask = Ask::None;
        setView(s_ask_return);
        break;

    case Act::PausedResume:
        s_resume_pause = false;
        setView(s_paused_return);
        if (s_paused_return == View::Board && active() && s_phase == Phase::Idle) {
            enterTurn();
        }
        break;
    case Act::PausedQuit:
        ui::pop();
        return;

    case Act::ResultAgain:
        if (millis() - s_view_ms < kInputLatchMs) {
            break;
        }
        setView(View::SetupSize);
        break;
    case Act::ResultHome:
        ui::goHome();
        return;

    case Act::CafeCoffee:
        // HOME の「+1」と同じ処理（記録も通知も同じ経路を通る）
        home::addOneCup();
        ui::showToast(s_screen, text("ui.cafe.added"));
        break;
    case Act::CafeBack:
        setView(s_cafe_return);
        break;
    case Act::CafeHome:
        ui::goHome();
        return;
    }
}

// ---------------------------------------------------------------------------
// 50ms ごとの処理
// ---------------------------------------------------------------------------
// ひと休みを重ねてよい画面（カフェの板とひと休み自身は除く）
bool playingView(View v)
{
    return v == View::Board || v == View::Choices || v == View::Menu || v == View::Details ||
           v == View::NetworkChoice || v == View::Confirm || v == View::Result ||
           v == View::Tutorial || v == View::Entry || v == View::SetupSize ||
           v == View::SetupMode || v == View::SetupColor;
}

// 盤が見えていない画面の間は対局を進めない（設計書 12.2 の「カフェ表示中は進めない」）。
// カフェ・ひと休みに加えて、盤の上に重ねて読む画面（操作・評価・遊び方・候補・確認）も
// 止める。強制パスや相手の着手が、読んでいる最中に裏で進むと見逃されるため。
// Jev の返事は依頼箱に置いたまま（gasTakeResult を呼ばない）なので消えない。
// この間は待ち時間の起点を毎回いまに合わせるので、**戻ったところから測り直す**
bool gameSuspended()
{
    return s_view == View::Cafe || s_view == View::Paused || s_view == View::Menu ||
           s_view == View::Details || s_view == View::Tutorial || s_view == View::Choices ||
           s_view == View::Confirm;
}

// Jev の返事を待つ。12 秒で見切って本人に選ばせる（計画 §5）。
// **使えなかった理由まで 1 行に出す**（URL・合言葉・本文は出さない）
void pollJev()
{
    if (s_phase != Phase::Wait || s_st == nullptr) {
        return;
    }
    if (s_send_pending) {
        tryPostJev();       // まだ依頼箱に入れられていない
        return;
    }
    if (net::gasTakeResult(s_st->reply)) {
        int move = -1;
        if (parseJevReply(s_st->reply, move, s_req_reason, sizeof(s_req_reason))) {
            char coord[rev::kCoordMax];
            rev::coordName(s_st->session.pos.n, move, coord);
            Serial.printf("[REV] req=%lu -> jev %s %lums%s%s\n", (unsigned long)s_st->reply.req,
                          coord, (unsigned long)s_st->reply.elapsed_ms,
                          s_st->jev_cached ? " (cached" : "",
                          s_st->jev_cached ? (s_attempt > 1 ? ", auto-retry)" : ")")
                                           : (s_attempt > 1 ? " (auto-retry)" : ""));
            s_attempt = 0;
            commitMove(move, Source::Jev);
            return;
        }
        // 1 回だけ黙って送り直す。GAS は控えの手を返すので Jev は呼ばれない
        if (s_attempt < kJevAttempts) {
            Serial.printf("[REV] req=%lu -> retry %lums (%s)\n", (unsigned long)s_st->reply.req,
                          (unsigned long)s_st->reply.elapsed_ms, s_req_reason);
            beginJevRequest((uint8_t)(s_attempt + 1));
            return;
        }
        Serial.printf("[REV] req=%lu -> failed %lums (%s)\n", (unsigned long)s_st->reply.req,
                      (unsigned long)s_st->reply.elapsed_ms, s_req_reason);
        setPhase(Phase::Idle);
        setView(View::NetworkChoice);
        return;
    }
    if (millis() - s_req_ms >= kJevWaitMs) {
        // 遅れて届く返事は依頼箱ごと捨てる（前の通信が終わるまで箱は空かない）
        const uint32_t waited = millis() - s_req_ms;
        net::gasCancel();
        std::snprintf(s_req_reason, sizeof(s_req_reason), "timeout");
        if (s_attempt < kJevAttempts) {
            Serial.printf("[REV] req=%lu -> retry %lums (timeout)\n", (unsigned long)s_req_no,
                          (unsigned long)waited);
            beginJevRequest((uint8_t)(s_attempt + 1));
            return;
        }
        Serial.printf("[REV] req=%lu -> failed %lums (timeout)\n", (unsigned long)s_req_no,
                      (unsigned long)waited);
        setPhase(Phase::Idle);
        setView(View::NetworkChoice);
    }
}

void tickCb(lv_timer_t *t)
{
    (void)t;
    if (s_st == nullptr) {
        return;
    }

    if (gameSuspended()) {
        // 時計だけ進めないでおく（戻ったところから続く）
        s_phase_ms = millis();
        s_req_ms = millis();
        s_send_since_ms = millis();
        s_result_since_ms = millis();
    } else {
        switch (s_phase) {
        case Phase::Pass:
            if (millis() - s_phase_ms >= kPassNoticeMs) {
                commitMove(core::PASS, Source::Forced);
            }
            break;
        case Phase::Think:
            // 端末 AI の計算はここ（タッチのコールバックの中ではない）。
            // 8×8 でも合法手ごとに 1 手先を評価するだけなので 1ms 級で終わる
            if (millis() - s_phase_ms >= 60) {
                commitMove(core::local_move(s_st->session.pos), Source::Local);
            }
            break;
        case Phase::Animate:
            if (millis() - s_phase_ms >= kAnimateMs) {
                s_st->anim_count = 0;
                setPhase(Phase::Idle);
                enterTurn();
            }
            break;
        case Phase::Wait:
            pollJev();
            break;
        default:
            break;
        }
        // 終わった対局の記録を預け直す（12 秒で見切った直後は箱がふさがっている）
        if (s_result_pending) {
            trySendResult();
        }
    }

    if (s_dirty) {
        rebuild();
        s_dirty = false;
        return;
    }
    // 誰も待っていない返事（見切ったあとの依頼・結果の投げっぱなし）は受け取って捨てる。
    // 捨てないと箱がふさがったままになり、次の依頼が入らない
    if (s_phase != Phase::Wait && !gameSuspended()) {
        if (net::gasTakeResult(s_st->reply)) {
            Serial.printf("[REV] req=%lu reply %s %lums (unwatched)\n",
                          (unsigned long)s_st->reply.req, s_st->reply.ok ? "ok" : "failed",
                          (unsigned long)s_st->reply.elapsed_ms);
        }
    }

    // 120 秒さわられなければ「ひと休み」を重ねる。途中の状態は残したまま。
    // AI が考えている最中は重ねない（返事を受け取れなくなるため）
    if (playingView(s_view) && s_phase == Phase::Idle &&
        lv_disp_get_inactive_time(nullptr) > kIdlePauseMs) {
        s_paused_return = s_view;
        s_resume_pause = false;
        setView(View::Paused);
    }
}

// ---------------------------------------------------------------------------
// 画面の生成・破棄
// ---------------------------------------------------------------------------
void releaseState()
{
    if (s_st != nullptr) {
        s_st->~GameState();
        heap_caps_free(s_st);
        s_st = nullptr;
    }
}

void screenDeletedCb(lv_event_t *e)
{
    // 画面の破棄は遷移アニメーション（200ms）の完了時なので、その間に次の画面が
    // 作られていることがある。古い画面の後始末で新しい画面を壊さないよう照合する
    if (lv_event_get_target(e) != s_screen) {
        return;
    }
    display::setGameActive(false);
    if (s_tick != nullptr) {
        lv_timer_del(s_tick);
        s_tick = nullptr;
    }
    // 待っている依頼はもう要らない（確定した手はすべて NVS に入っている）
    net::gasCancel();
    releaseState();
    s_view = View::Entry;
    s_phase = Phase::Idle;
    s_ask = Ask::None;
    s_dirty = false;
    s_resume_pause = false;
    s_attempt = 0;
    s_send_pending = false;
    s_result_pending = false;
    s_result_reason[0] = '\0';
    // 解放済みオブジェクトを触らないよう、静的ポインタは必ず全部消す
    s_screen = nullptr;
    s_content = nullptr;
    s_board = nullptr;
}

const char *viewName(View v)
{
    switch (v) {
    case View::Entry:         return "entry";
    case View::SetupSize:     return "size";
    case View::SetupMode:     return "mode";
    case View::SetupColor:    return "color";
    case View::Board:         return "board";
    case View::Choices:       return "choices";
    case View::Menu:          return "menu";
    case View::Details:       return "details";
    case View::NetworkChoice: return "network";
    case View::Confirm:       return "confirm";
    case View::Paused:        return "paused";
    case View::Result:        return "result";
    case View::Tutorial:      return "tutorial";
    case View::Cafe:          return "cafe";
    }
    return "?";
}

const char *phaseName(Phase p)
{
    switch (p) {
    case Phase::Pass:    return "pass";
    case Phase::Think:   return "think";
    case Phase::Wait:    return "wait";
    case Phase::Animate: return "anim";
    default:             return "idle";
    }
}

const char *closureName(Closure c)
{
    switch (c) {
    case Closure::Completed: return "completed";
    case Closure::Resigned:  return "resigned";
    case Closure::Aborted:   return "aborted";
    default:                 return "active";
    }
}

}  // namespace

// ---------------------------------------------------------------------------
lv_obj_t *createGameScreen()
{
    // 前の画面がまだ消えていない（遷移中の再入・開発用コマンドの連打）ことがある。
    // 古いタイマーを残すと 2 本が同じ部品を作り替えに来るので、ここで止める
    if (s_tick != nullptr) {
        lv_timer_del(s_tick);
        s_tick = nullptr;
    }
    releaseState();
    // 対局の途中で自動的に暗くならないようにする
    display::setGameActive(true);

    lv_obj_t *scr = ui::makeScreen();
    s_screen = scr;
    s_content = lv_obj_create(scr);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_pos(s_content, 0, 0);
    lv_obj_set_size(s_content, ui::kScreenSize, ui::kScreenSize);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    // 後始末は**この時点で**登録する（下の確保に失敗して戻るときも通るように）
    lv_obj_add_event_cb(scr, screenDeletedCb, LV_EVENT_DELETE, nullptr);

    // 対局の状態は PSRAM に置く（内蔵メモリは TLS 1 本で 35KB 使うので空けておく）
    bool on_psram = true;
    void *raw = heap_caps_malloc(sizeof(GameState), MALLOC_CAP_SPIRAM);
    if (raw == nullptr) {
        on_psram = false;
        raw = heap_caps_malloc(sizeof(GameState), MALLOC_CAP_8BIT);   // PSRAM が無いとき
    }
    if (raw == nullptr) {
        // ここまで来ることはまずないが、戻れない画面は作らない
        Serial.println("[REV] no memory for the game state");
        ui::makeRectLabel(s_content, layout::kNetBody, &ct_font_jp_22, CT_COLOR_ALERT,
                          text("save.failure"));
        ui::makeBackButton(scr, text("ui.back_list"));
        return scr;
    }
    s_st = new (raw) GameState();
    Serial.printf("[REV] state %u bytes on %s\n", (unsigned)sizeof(GameState),
                  on_psram ? "PSRAM" : "internal RAM");

    store::loadStats();
    // 前回の残り（遅れて届いた返事）を捨ててから始める
    net::gasCancel();
    net::gasTakeResult(s_st->reply);

    s_view = View::Entry;
    s_cafe_return = View::Entry;
    s_paused_return = View::Board;
    s_ask_return = View::Board;
    s_tut_return = View::Entry;
    s_phase = Phase::Idle;
    s_ask = Ask::None;
    s_resume_pause = false;
    s_setup_n = 6;
    s_setup_mode = net::gasReady() ? Mode::Jev : Mode::Local;
    s_setup_color = 0;
    s_tut_page = 0;
    s_choice_page = 0;
    s_req_reason[0] = '\0';
    s_attempt = 0;
    s_send_pending = false;
    s_result_pending = false;
    s_result_reason[0] = '\0';
    s_view_ms = millis();
    // 再開できる局があるか。**ここで 1 回だけ**棋譜を再生して確かめる
    s_resume_available = store::hasResumableGame();

    rebuild();
    s_dirty = false;

    s_tick = lv_timer_create(tickCb, 50, nullptr);
    return scr;
}

void debugPrintPublicState()
{
    if (s_screen == nullptr || s_st == nullptr) {
        Serial.println("[REV] view=-");
        return;
    }
    const Session &s = s_st->session;
    const auto c = core::counts(s.pos);
    char last[rev::kTokenMax] = "-";
    if (s.count > 0) {
        rev::historyToken(s.pos.n, s.history[s.count - 1], last);
    }
    // 盤面・棋譜は公開情報なので、そのまま出してよい（人狼の秘密とは違う）
    Serial.printf("[REV] view=%s phase=%s n=%u mode=%s human=%c ply=%u side=%c "
                  "B=%d W=%d closure=%s local=%u pending=%u try=%u saveerr=%u last=%s\n",
                  viewName(s_view), phaseName(s_phase), (unsigned)s.pos.n,
                  rev::modeId(s.mode), core::symbol(s.human), (unsigned)s.pos.ply,
                  core::symbol(s.pos.side), c.black, c.white, closureName(s.closure),
                  s.localOnly ? 1u : 0u, s_phase == Phase::Wait ? 1u : 0u,
                  (unsigned)s_attempt, s_st->save_failed ? 1u : 0u, last);
}

}  // namespace reversi
