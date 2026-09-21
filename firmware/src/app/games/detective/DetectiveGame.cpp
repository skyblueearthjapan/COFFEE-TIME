#include "DetectiveGame.h"

#include <Arduino.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../../HomeScreen.h"
#include "../../ui/ScreenManager.h"
#include "../../ui/UiKit.h"
#include "DetectiveContent.h"
#include "DetectiveProgress.h"
#include "core/DetectiveGate.hpp"

LV_FONT_DECLARE(ct_font_jp_20);
LV_FONT_DECLARE(ct_font_jp_22);
LV_FONT_DECLARE(ct_font_jp_40);

namespace detective {
namespace {

namespace c = coffee::det::content;

// ---------------------------------------------------------------------------
// 画面の配置
//
// 設計書 5.1 の表をそのまま使う。丸型なので中心 (240,240)・半径 220px の円の
// 内側に四隅が入っていることを座標ごとに確かめてある（円の外に出る値は使わない）。
// 設計書に無い部品（手掛かり集めの並びなど）は、同じ円の条件を満たす位置を決めた。
// ---------------------------------------------------------------------------
namespace layout {

struct Rect { int16_t x, y, w, h; };

// --- 設計書 5.1 の共通レイアウト -------------------------------------------
// 上部タイトルは表の 140,48,200,32 だと「雨上がりのしおり」が枠からはみ出すので、
// y=48 の行で円に収まる限界（x は 133〜347）まで左右へ広げた
constexpr Rect kTitle    {134,  48, 212, 32};
constexpr Rect kCafe     { 78,  96,  74, 48};   // 共通カフェ。3 文字なのでこの幅で足りる
constexpr Rect kStatus   {160,  98, 160, 36};   // 人物名／状態
constexpr Rect kBody     { 90, 154, 300, 184};  // 本文（22px・行高 28・最大 6 行）
constexpr Rect kNavLeft  { 92, 342,  64, 52};   // 戻る
constexpr Rect kNavMid   {180, 348, 120, 52};   // 次へ／読んだ
constexpr Rect kNavRight {324, 342,  64, 52};   // 予備（この段階では使わない）
constexpr Rect kFooter   {144, 410, 192, 24};   // ページ数など。操作は置かない
constexpr Rect kChoice[3] = {{96, 148, 288, 56}, {96, 216, 288, 56}, {96, 284, 288, 56}};

// --- 設計書に無い部品（円の内側になるよう決めた） ---------------------------
constexpr Rect kTopRight {326,  96,  74, 48};   // もどる（一覧へ／ゲーム一覧へ）
constexpr Rect kQuestion {160,  96, 230, 48};   // 三択画面の問い（状態欄を広く使う）
constexpr Rect kSymbol   {326,  90,  64, 64};   // 人物の記号（設計書 2.4 の 64x64）

constexpr Rect kHubClue[3] = {{96, 138, 288, 46}, {96, 188, 288, 46}, {96, 238, 288, 46}};
constexpr Rect kHubTool[3] = {{96, 288, 92, 44}, {194, 288, 92, 44}, {292, 288, 92, 44}};
constexpr Rect kHubGuess  {110, 336, 260, 50};
constexpr Rect kHubGiveUp {132, 392, 216, 40};

constexpr Rect kWide      {110, 336, 260, 50};  // 主ボタン
constexpr Rect kWideLow   {136, 392, 208, 40};  // 副ボタン（主ボタンの下）
constexpr Rect kChoiceBack{132, 356, 216, 46};  // 三択画面の「手掛かりへ戻る」

// カフェパネル（人狼の一時停止メニューと同じ並び）
constexpr Rect kPanelBody {90, 140, 300, 56};
constexpr Rect kPanel[3]  = {{110, 214, 260, 54}, {110, 278, 260, 54}, {110, 342, 260, 54}};

}  // namespace layout

// ---------------------------------------------------------------------------
// 画面の種類（設計書 5.2 のうち、ひとり推理で使うものだけ）
//
// 通信専用の CD_PREPARE / CD_SUBMIT / CD_PAUSED / モード選択は作らない。
// ---------------------------------------------------------------------------
enum class View : uint8_t {
    Menu,          // CD_MENU    話の一覧
    Spoiler,       // 後の話を先に開くときの 1 回だけの確認
    Cover,         // CD_COVER
    Intro,         // CD_INTRO
    Rules,         // CD_RULES   今回の約束
    Hub,           // CD_HUB
    Clue,          // CD_CLUE
    Notebook,      // CD_NOTEBOOK
    Ambient,       // CD_AMBIENT
    HintConfirm,   // CD_HINT_CONFIRM
    Hint,          // CD_HINT
    Choice,        // CD_CHOICE
    Confirm,       // CD_CONFIRM
    GiveUp,        // 「答えとつづきを読む」の確認
    Result,        // CD_RESULT
    Explain,       // CD_EXPLAIN
    Ending,        // CD_ENDING
    Collect,       // CD_COLLECTIBLE
    ChapterEnd,    // 3 つそろったときの章の記念ページ
    Cafe,          // 共通カフェパネル
    Paused,        // 読書中の無操作でひと休み（読みかけの位置は RAM に残す）
};

// 押した内容。lv_event の user_data に入れて 1 つのコールバックで処理する
enum class Act : int {
    MenuBack = 1, MenuChapter,
    SpoilerOk, SpoilerNo,
    CoverStart, CoverBack,
    PagePrev, PageNext, PageDone,
    HubBack, HubNotebook, HubAmbient, HubHint, HubGuess, HubGiveUp,
    HintOk, HintNo,
    GiveUpOk, GiveUpNo,
    ChoiceBack,
    ConfirmOk, ConfirmChange,
    ResultNext,
    CollectDone,
    ChapterDone,
    Cafe, CafeCoffee, CafeBack, CafeHome,
    PausedResume, PausedMenu,
};

// ---------------------------------------------------------------------------
// 状態
// ---------------------------------------------------------------------------
lv_obj_t *s_screen = nullptr;
lv_obj_t *s_content = nullptr;      // 中身を丸ごと作り替える入れ物
lv_timer_t *s_tick = nullptr;

cd::Gate s_gate;                    // 二重確定を止める入力ラッチ（reference をそのまま使用）

View s_view = View::Menu;
View s_cafe_return = View::Menu;    // カフェパネルから戻る先
View s_paused_return = View::Menu;  // ひと休みから戻る先
bool s_dirty = true;

uint8_t s_episode = 0;              // 今の話（0..2）
uint8_t s_pending_episode = 0;      // ネタバレ確認中の話
bool s_spoiler_warned = false;      // 「前の話の結末に触れます」は 1 回だけ
// この挑戦が復習かどうかは「話を開いた時点」で決める。答えを記録した直後に
// 「初見 → 復習」と表示が変わってしまわないようにするため（一覧の表示は別に数える）
bool s_replay_attempt = false;

uint16_t s_page = 0;                // 読み物の中のページ番号
uint8_t s_clue = 0;                 // 今開いている証拠（0..2）
bool s_clue_read[c::kClueCount] = {false, false, false};
uint8_t s_hint_level = 0;           // この挑戦で読んだヒントの段階（0..2）
int8_t s_choice = -1;               // 選んだ人物（options の添字）

bool s_answered = false;            // 答えを確定した
bool s_gave_up = false;
bool s_correct = false;

// ---------------------------------------------------------------------------
// 文言・文字の幅
// ---------------------------------------------------------------------------
const char *str(const char *key)
{
    const char *s = c::findString(key);
    return s != nullptr ? s : key;   // 生成データに無いときは鍵をそのまま出して気付けるようにする
}

// 本文欄へ出す文言。生成時に全角 13 文字で折り返してある版を使う。
// 折り返していない版を本文に貼ると、日本語は自動折返しされないので 1 行目しか読めない
const char *body(const char *key)
{
    const char *s = c::findWrappedString(key);
    return s != nullptr ? s : str(key);
}

const c::Episode &episode()
{
    return c::kEpisodes[s_episode < c::kEpisodeCount ? s_episode : 0];
}

// 半角いくつ分か。日本語は全角なので 2 つ分として数える（生成側の見積りと同じ）
uint16_t lineUnits(const char *begin, const char *end)
{
    uint16_t cells = 0;
    for (const char *p = begin; p < end; ++p) {
        if (((unsigned char)*p & 0xC0) == 0x80) {
            continue;               // UTF-8 の後続バイトは数えない
        }
        cells = (uint16_t)(cells + (((unsigned char)*p < 0x80) ? 1 : 2));
    }
    return cells;
}

uint16_t lineCount(const char *text)
{
    uint16_t n = 1;
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p == '\n') {
            ++n;
        }
    }
    return n;
}

// 一番長い行が枠に収まるかで字の大きさを決める（1 全角 ≒ 11px 半角 2 つ分）
const lv_font_t *fitFont(const char *text, int16_t width)
{
    uint16_t widest = 0;
    const char *line = text;
    for (const char *p = text; ; ++p) {
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
    return (int16_t)(widest * 11) <= width ? &ct_font_jp_22 : &ct_font_jp_20;
}

// ---------------------------------------------------------------------------
// 部品づくり（人狼の rectLabel / rectButton と同じ考え方）
// ---------------------------------------------------------------------------
lv_obj_t *rectLabel(const layout::Rect &r, const lv_font_t *font, lv_color_t color,
                    const char *text)
{
    lv_obj_t *l = lv_label_create(s_content);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    // 日本語は自動折返しが効かない。改行は生成側で入れてあるので幅で切る
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, r.w);
    lv_label_set_text(l, text);
    const uint16_t lines = lineCount(text);
    const int16_t h = (int16_t)(font->line_height * lines);
    if (h > r.h && lines > 1) {
        // はみ出したまま描くと下のボタンに文字が乗るので枠の高さで切る
        lv_obj_set_height(l, r.h);
        lv_obj_set_pos(l, r.x, r.y);
    } else {
        lv_obj_set_pos(l, r.x, (int16_t)(r.h > h ? r.y + (r.h - h) / 2 : r.y));
    }
    return l;
}

void actionCb(lv_event_t *e);
void indexCb(lv_event_t *e);

lv_obj_t *rectButton(const layout::Rect &r, const char *text, Act act, bool enabled = true,
                     bool primary = false)
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
    lv_obj_set_style_text_font(l, fitFont(text, (int16_t)(r.w - 8)), 0);
    lv_obj_set_style_text_color(l, enabled ? CT_COLOR_TEXT : CT_COLOR_DIM, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, (int16_t)(r.w - 8));
    lv_label_set_text(l, text);
    lv_obj_center(l);

    if (enabled) {
        lv_obj_add_event_cb(btn, actionCb, LV_EVENT_CLICKED, (void *)(intptr_t)act);
    } else {
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    }
    return btn;
}

// 証拠・人物のように「何番目を押したか」が要るボタン
lv_obj_t *indexButton(const layout::Rect &r, const char *text, int index, bool enabled,
                      bool primary = false)
{
    lv_obj_t *btn = rectButton(r, text, Act::MenuBack, false, primary);
    if (enabled) {
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(btn, indexCb, LV_EVENT_CLICKED, (void *)(intptr_t)index);
        lv_obj_set_style_border_color(btn, CT_COLOR_ACCENT_HI, 0);
        lv_obj_t *l = lv_obj_get_child(btn, 0);
        if (l != nullptr) {
            lv_obj_set_style_text_color(l, CT_COLOR_TEXT, 0);
        }
    }
    return btn;
}

void makeTitle(const char *text)
{
    rectLabel(layout::kTitle, &ct_font_jp_20, CT_COLOR_SUBTEXT, text);
}

// ---------------------------------------------------------------------------
// 人物の記号（設計書 2.4）
//
// 外部の画像はひとつも使わず、LVGL の角丸矩形と円だけで 64x64 に描く。
// 表情は眉と口だけを変える。プレイヤーの選択や正誤で表情を変えてはいけない。
// ---------------------------------------------------------------------------
lv_obj_t *box(lv_obj_t *parent, int16_t x, int16_t y, int16_t w, int16_t h,
              lv_color_t color, int16_t radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

// 中を塗らない輪（丸眼鏡に使う）
void ring(lv_obj_t *parent, int16_t x, int16_t y, int16_t d, lv_color_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(o, color, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

void buildSpeakerSymbol(c::Speaker speaker, c::Emotion emotion)
{
    const c::Character *ch = c::findCharacter(speaker);
    const lv_color_t theme = lv_color_hex(ch != nullptr ? ch->color : 0x445164);
    const lv_color_t paper = lv_color_hex(0xEADFCB);
    const lv_color_t ink = lv_color_hex(0x332C27);

    lv_obj_t *card = lv_obj_create(s_content);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, layout::kSymbol.x, layout::kSymbol.y);
    lv_obj_set_size(card, layout::kSymbol.w, layout::kSymbol.h);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);

    if (speaker == c::Speaker::Narrator) {
        // 手帳：横罫のメモ（設計書 2.4 の (17,10,30,43)）
        box(card, 17, 10, 30, 43, paper, 3);
        for (int16_t i = 0; i < 4; ++i) {
            box(card, 21, (int16_t)(18 + i * 8), 22, 1, theme, 0);
        }
        return;
    }

    // 肩 → 顔の順に置く（顔が手前に来るように）
    box(card, 12, 42, 40, 20, theme, 8);
    box(card, 15, 8, 34, 34, paper, LV_RADIUS_CIRCLE);

    // 身なりの記号
    switch (speaker) {
    case c::Speaker::Latte:   // 丸眼鏡（中心 25,25 と 39,25・半径 7）
        ring(card, 18, 18, 14, ink);
        ring(card, 32, 18, 14, ink);
        break;
    case c::Speaker::Mocha:   // 四角い胸当て
        box(card, 23, 46, 18, 16, paper, 2);
        break;
    case c::Speaker::Chai:    // 縞のスカーフ
        box(card, 16, 43, 32, 9, theme, 2);
        box(card, 16, 45, 32, 1, paper, 0);
        box(card, 16, 49, 32, 1, paper, 0);
        break;
    case c::Speaker::Cocoa:   // 葉のブローチ
        box(card, 20, 45, 8, 8, paper, LV_RADIUS_CIRCLE);
        break;
    default:
        break;
    }

    // 目は常に同じ。表情は眉と口だけで作る（設計書 2.3）
    box(card, 24, 24, 3, 3, ink, 1);
    box(card, 38, 24, 3, 3, ink, 1);

    switch (emotion) {
    case c::Emotion::Puzzled:
        box(card, 22, 18, 7, 2, ink, 1);   // 片方だけ上がった眉
        box(card, 36, 20, 7, 2, ink, 1);
        box(card, 29, 34, 6, 2, ink, 1);
        break;
    case c::Emotion::Soft:
        box(card, 22, 20, 7, 2, ink, 1);
        box(card, 36, 20, 7, 2, ink, 1);
        box(card, 28, 34, 8, 2, ink, 1);
        break;
    case c::Emotion::Smile:
        box(card, 27, 32, 10, 4, ink, 2);  // 角を丸めて笑った口にする
        break;
    case c::Emotion::Neutral:
    default:
        box(card, 28, 33, 8, 2, ink, 1);
        break;
    }
}

// ---------------------------------------------------------------------------
// 読み物のページ列
//
// 画面ごとに「どの PageRun をつなげて読ませるか」を組み立てる。
// 手帳は 約束 ＋ 既読の証拠の要約 なので最大 4 本つながる。
// ---------------------------------------------------------------------------
constexpr size_t kMaxParts = 4;
c::PageRun s_parts[kMaxParts];
uint8_t s_part_count = 0;
uint16_t s_total_pages = 0;

void addPart(const c::PageRun &run)
{
    if (run.pages == nullptr || run.count == 0 || s_part_count >= kMaxParts) {
        return;
    }
    s_parts[s_part_count++] = run;
    s_total_pages = (uint16_t)(s_total_pages + run.count);
}

const c::Page *pageAt(uint16_t index)
{
    uint16_t at = index;
    for (uint8_t i = 0; i < s_part_count; ++i) {
        if (at < s_parts[i].count) {
            return &s_parts[i].pages[at];
        }
        at = (uint16_t)(at - s_parts[i].count);
    }
    return nullptr;
}

// s_view に合わせて読み物を組み立てる。読み物でない画面では 0 ページになる
void buildRun()
{
    s_part_count = 0;
    s_total_pages = 0;
    const c::Episode &ep = episode();

    switch (s_view) {
    case View::Intro:
        addPart(ep.intro);
        break;
    case View::Rules:
        addPart(ep.premises);
        break;
    case View::Clue:
        addPart(ep.clues[s_clue].pages);
        break;
    case View::Notebook:
        // 手帳には「今回の約束」と、読み終えた証拠の要約だけを並べる。
        // まだ読んでいない証拠の中身をここで先に見せてはいけない（設計書 5.2）
        addPart(ep.premises);
        for (size_t i = 0; i < c::kClueCount; ++i) {
            if (s_clue_read[i]) {
                addPart(ep.clues[i].summary);
            }
        }
        break;
    case View::Ambient:
        if (ep.ambient_count > 0) {
            addPart(ep.ambient[0].pages);
        }
        break;
    case View::Hint:
        if (s_hint_level >= 1 && s_hint_level <= c::kHintLevels) {
            addPart(ep.hints[s_hint_level - 1]);
        }
        break;
    case View::Explain:
        // 間違えた人物への短い言葉を先に、そのあと全員共通の解説（設計書 3.4）
        if (!s_gave_up && s_choice >= 0 && !s_correct) {
            addPart(ep.wrong_feedback[s_choice]);
        }
        addPart(ep.explanation);
        break;
    case View::Ending:
        addPart(ep.epilogue);
        break;
    default:
        break;
    }
    if (s_page >= s_total_pages) {
        s_page = s_total_pages > 0 ? (uint16_t)(s_total_pages - 1) : 0;
    }
}

// ---------------------------------------------------------------------------
// 画面の中身
// ---------------------------------------------------------------------------
bool isReadingView(View v)
{
    return v == View::Intro || v == View::Rules || v == View::Clue || v == View::Notebook ||
           v == View::Ambient || v == View::Hint || v == View::Explain || v == View::Ending;
}

// カフェボタンを出す画面か（設計書 3.6：捜査と結果の画面には必ず置く）
bool hasCafeButton(View v)
{
    return v != View::Cafe && v != View::Paused && v != View::Spoiler && v != View::ChapterEnd;
}

bool isReplay(size_t index)
{
    return detective::get(index).first_result != FirstResult::None;
}

const char *modeLabel()
{
    // ひとり推理で固定。復習かどうかだけを添える（設計書 3.2）。
    // 判定は話を開いたときの s_replay_attempt を使う（今回の結果で途中から変わらない）
    return s_replay_attempt ? str("mode_practice") : str("mode_solo");
}

void makeFooterPages()
{
    char text[48];
    std::snprintf(text, sizeof(text), "%s  %u / %u", modeLabel(),
                  (unsigned)(s_page + 1), (unsigned)(s_total_pages > 0 ? s_total_pages : 1));
    rectLabel(layout::kFooter, &ct_font_jp_20, CT_COLOR_DIM, text);
}

void buildTopButtons()
{
    if (hasCafeButton(s_view)) {
        rectButton(layout::kCafe, "カフェ", Act::Cafe);
    }
    if (s_view == View::Menu) {
        rectButton(layout::kTopRight, "もどる", Act::MenuBack);
    } else if (s_view == View::Hub) {
        rectButton(layout::kTopRight, "もどる", Act::HubBack);
    }
}

// --- 話の一覧 ---------------------------------------------------------------

// 「次のお話」として勧める話＝まだ結末を読んでいない一番小さい番号
uint8_t recommendedEpisode()
{
    for (uint8_t i = 0; i < c::kEpisodeCount; ++i) {
        if (!detective::get(i).story_completed) {
            return i;
        }
    }
    return 0;
}

void buildMenu()
{
    makeTitle(c::kWorldTitle);

    const uint8_t recommend = recommendedEpisode();
    for (uint8_t i = 0; i < c::kEpisodeCount && i < 3; ++i) {
        const c::Episode &ep = c::kEpisodes[i];
        const EpisodeProgress &pr = detective::get(i);
        char label[96];
        std::snprintf(label, sizeof(label), "%s %s\n%s・%s%s",
                      ep.chapter, ep.short_title,
                      isReplay(i) ? str("mode_practice") : "初見",
                      pr.story_completed ? "読了" : "未読",
                      pr.collectible_owned ? "・手帳" : "");
        indexButton(layout::kChoice[i], label, i, true, i == recommend);
    }

    if (detective::allCollectiblesOwned()) {
        rectButton(layout::kHubGiveUp, str("chapter_end"), Act::MenuChapter);
    }

    // フッターは幅 192px しかないので短くまとめる（pack_title「つづきは、ここで。」は 180px あり、
    // 点数を足すと左右が切れてしまう）
    char footer[48];
    std::snprintf(footer, sizeof(footer), "第一章  %u / 6 点",
                  (unsigned)detective::totalPoints());
    rectLabel(layout::kFooter, &ct_font_jp_20, CT_COLOR_DIM, footer);
}

void buildSpoiler()
{
    makeTitle(c::kEpisodes[s_pending_episode].chapter);
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT,
              "前の話の結末に触れます。\nこの話から読みますか？");
    rectButton(layout::kWide, "読む", Act::SpoilerOk, true, true);
    rectButton(layout::kWideLow, "やめる", Act::SpoilerNo);
}

// --- 表紙 -------------------------------------------------------------------
void buildCover()
{
    const c::Episode &ep = episode();
    makeTitle(ep.chapter);

    char body[256];
    std::snprintf(body, sizeof(body), "%s\n\n%s", ep.title, ep.teaser);
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT, body);

    rectButton(layout::kNavLeft, "戻る", Act::CoverBack);
    rectButton(layout::kNavMid, str(s_replay_attempt ? "menu_replay" : "start"),
               Act::CoverStart, true, true);

    char footer[48];
    std::snprintf(footer, sizeof(footer), "%s・%s", ep.difficulty, modeLabel());
    rectLabel(layout::kFooter, &ct_font_jp_20, CT_COLOR_DIM, footer);
}

// --- 読み物（導入・約束・証拠・手帳・店の中・ヒント・解説・結末） -----------
const char *readingTitle()
{
    const c::Episode &ep = episode();
    switch (s_view) {
    case View::Clue:     return ep.clues[s_clue].label;
    case View::Notebook: return str("notebook");
    case View::Ambient:  return ep.ambient_count > 0 ? ep.ambient[0].label : str("investigate");
    case View::Hint:     return s_hint_level >= 2 ? "ヒント 2" : "ヒント 1";
    case View::Explain:  return str("explanation");
    case View::Ending:   return str("ending");
    case View::Rules:    return "今回の約束";
    default:             return ep.short_title;
    }
}

void buildReading()
{
    makeTitle(readingTitle());

    const c::Page *page = pageAt(s_page);
    if (page == nullptr) {
        rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_SUBTEXT,
                  "まだ手掛かりがありません。\n証拠を読んでみましょう。");
        rectButton(layout::kNavMid, str("previous"), Act::PageDone, true, true);
        return;
    }

    const c::Character *ch = c::findCharacter(page->speaker);
    rectLabel(layout::kStatus, &ct_font_jp_22, CT_COLOR_SUBTEXT, ch != nullptr ? ch->name : "");
    buildSpeakerSymbol(page->speaker, page->emotion);

    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT, page->text);

    const bool last = (s_page + 1 >= s_total_pages);
    rectButton(layout::kNavLeft, str("previous"), Act::PagePrev);
    if (last) {
        // 証拠は最後のページの「読んだ」を押して初めて既読になる（設計書 5.2）
        rectButton(layout::kNavMid, str(s_view == View::Clue ? "read" : "next"),
                   Act::PageDone, true, true);
    } else {
        rectButton(layout::kNavMid, str("next"), Act::PageNext, true, true);
    }
    makeFooterPages();
}

// --- 手掛かり集め -----------------------------------------------------------
void buildHub()
{
    const c::Episode &ep = episode();
    makeTitle(ep.short_title);

    uint8_t read_count = 0;
    for (size_t i = 0; i < c::kClueCount; ++i) {
        char label[80];
        std::snprintf(label, sizeof(label), "%s・%s", ep.clues[i].label,
                      s_clue_read[i] ? str("read") : str("unread"));
        indexButton(layout::kHubClue[i], label, (int)i, true);
        if (s_clue_read[i]) {
            ++read_count;
        }
    }

    // 「あと何件読めば推理できるか」は状態欄に出す。フッターの帯（y=410〜434）は
    // 「答えとつづきを読む」のボタン（y=392〜432）と重なるので、この画面では使わない
    // 右上に「もどる」ボタン（x=326〜）があるので、幅の広い欄は使わず短い文にする
    // （「ひとり推理・証拠 0/3」は 220px あり、ボタンの下にもぐっていた）
    char status[32];
    std::snprintf(status, sizeof(status), "証拠 %u/%u", (unsigned)read_count, (unsigned)c::kClueCount);
    rectLabel(layout::kStatus, &ct_font_jp_20, CT_COLOR_SUBTEXT, status);

    rectButton(layout::kHubTool[0], str("notebook"), Act::HubNotebook);
    rectButton(layout::kHubTool[1], "店の中", Act::HubAmbient, ep.ambient_count > 0);
    rectButton(layout::kHubTool[2], str("hint"), Act::HubHint, s_hint_level < c::kHintLevels);

    const bool ready = (read_count >= c::kClueCount);
    rectButton(layout::kHubGuess, str("guess"), Act::HubGuess, ready, ready);
    rectButton(layout::kHubGiveUp, str("give_up"), Act::HubGiveUp);
}

// --- ヒント・答えを読む の確認 ----------------------------------------------
void buildHintConfirm()
{
    makeTitle(str("hint"));
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT, body("hint_notice"));
    rectButton(layout::kWide, str(s_hint_level == 0 ? "hint_read" : "hint_again"),
               Act::HintOk, true, true);
    rectButton(layout::kWideLow, "やめる", Act::HintNo);
}

void buildGiveUpConfirm()
{
    makeTitle(str("give_up"));
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_ALERT, body("give_up_confirm"));
    rectButton(layout::kWide, str("give_up"), Act::GiveUpOk, true, true);
    rectButton(layout::kWideLow, "やめる", Act::GiveUpNo);
}

// --- 三択・確認 -------------------------------------------------------------
void buildChoice()
{
    const c::Episode &ep = episode();
    makeTitle(str("choose"));
    rectLabel(layout::kQuestion, &ct_font_jp_20, CT_COLOR_TEXT, ep.question);

    for (size_t i = 0; i < c::kOptionCount; ++i) {
        indexButton(layout::kChoice[i], ep.options[i].label, (int)i, true);
    }
    // ボタンの見出しは短い語だけにする（「話と手掛かりを集めよう」は 242px あり
    // 216px の枠では切れてしまう）。説明はフッターのラベルで出す
    rectButton(layout::kChoiceBack, str("previous"), Act::ChoiceBack);
    rectLabel(layout::kFooter, &ct_font_jp_20, CT_COLOR_DIM, modeLabel());
}

void buildConfirm()
{
    const c::Episode &ep = episode();
    makeTitle(str("guess"));

    char body[192];
    std::snprintf(body, sizeof(body), "%sで、決定しますか？\n\n確定した答えは\n変えられません。",
                  s_choice >= 0 ? ep.options[s_choice].label : "");
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT, body);

    rectButton(layout::kWide, str("confirm"), Act::ConfirmOk, true, true);
    rectButton(layout::kWideLow, str("change"), Act::ConfirmChange);
}

// --- 答え合わせ -------------------------------------------------------------
void buildResult()
{
    const c::Episode &ep = episode();
    makeTitle("答え合わせ");

    // 「答えとつづきを読む」で降参したときは、不正解とは別の見出しにする（文言は設計パッケージの PC 参考版と同じ）
    const char *headline = s_gave_up ? "答えを確認しましょう。"
                         : s_correct ? (s_hint_level > 0 ? str("assisted") : str("solo_correct"))
                                     : str("solo_incorrect");
    char body[256];
    std::snprintf(body, sizeof(body), "%s\n\n正解：%s\n%s", headline,
                  ep.options[ep.answer_index].label, modeLabel());
    rectLabel(layout::kBody, &ct_font_jp_22,
              s_correct ? CT_COLOR_TEXT : CT_COLOR_SUBTEXT, body);

    rectButton(layout::kWide, str("explanation"), Act::ResultNext, true, true);

    char footer[48];
    std::snprintf(footer, sizeof(footer), "%s  %u 点", ep.chapter,
                  (unsigned)detective::points(s_episode));
    rectLabel(layout::kFooter, &ct_font_jp_20, CT_COLOR_DIM, footer);
}

// --- 記念の品・章のおわり ---------------------------------------------------
void buildCollect()
{
    const c::Episode &ep = episode();
    makeTitle("手帳に残ったもの");

    char body[192];
    std::snprintf(body, sizeof(body), "%s\n\n%s", ep.collectible.label, ep.collectible.text);
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT, body);

    rectButton(layout::kWide, str("collectible"), Act::CollectDone, true, true);
}

void buildChapterEnd()
{
    makeTitle(str("chapter_end"));

    // 3 話ぶんの 1 行まとめを重ねて出す（どれも 1 ページに収まるよう生成してある）
    char body[384];
    size_t used = 0;
    for (size_t i = 0; i < c::kEpisodeCount && used + 1 < sizeof(body); ++i) {
        const c::Episode &ep = c::kEpisodes[i];
        const char *line = (ep.collectible.label != nullptr) ? ep.collectible.label : "";
        const int n = std::snprintf(body + used, sizeof(body) - used, "%s%s・%s",
                                    used == 0 ? "" : "\n", ep.chapter, line);
        if (n < 0) {
            break;
        }
        used += (size_t)n;
    }
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT, body);

    char footer[48];
    std::snprintf(footer, sizeof(footer), "第一章  %u / 6 点",
                  (unsigned)detective::totalPoints());
    rectLabel(layout::kFooter, &ct_font_jp_20, CT_COLOR_DIM, footer);

    rectButton(layout::kWide, str("back_home"), Act::ChapterDone, true, true);
    rectButton(layout::kWideLow, "もどる", Act::MenuBack);
}

// --- ひと休み（読書中に 180 秒さわられなかったとき） -------------------------
//
// 読みかけの位置は RAM に残したまま、この画面だけを重ねる。捨てるのは
// 「お話をえらぶ」を押したときと、HOME・ゲーム一覧へ出たときだけ
void buildPaused()
{
    makeTitle(str("pause"));
    rectLabel(layout::kPanelBody, &ct_font_jp_22, CT_COLOR_TEXT, body("resuming"));
    rectButton(layout::kPanel[0], "つづきを読む", Act::PausedResume, true, true);
    rectButton(layout::kPanel[1], str("coffee_add"), Act::CafeCoffee);
    rectButton(layout::kPanel[2], "お話をえらぶ", Act::PausedMenu);
}

// --- カフェパネル（設計書 3.6） ---------------------------------------------
void buildCafe()
{
    makeTitle("カフェ");
    rectLabel(layout::kPanelBody, &ct_font_jp_22, CT_COLOR_TEXT,
              "コーヒーの記録は\nいつでもできます。");
    rectButton(layout::kPanel[0], str("coffee_add"), Act::CafeCoffee, true, true);
    rectButton(layout::kPanel[1], "ゲームへ戻る", Act::CafeBack);
    rectButton(layout::kPanel[2], str("back_home"), Act::CafeHome);
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
    buildRun();
    buildTopButtons();

    switch (s_view) {
    case View::Menu:        buildMenu(); break;
    case View::Spoiler:     buildSpoiler(); break;
    case View::Cover:       buildCover(); break;
    case View::Intro:
    case View::Rules:
    case View::Clue:
    case View::Notebook:
    case View::Ambient:
    case View::Hint:
    case View::Explain:
    case View::Ending:      buildReading(); break;
    case View::Hub:         buildHub(); break;
    case View::HintConfirm: buildHintConfirm(); break;
    case View::GiveUp:      buildGiveUpConfirm(); break;
    case View::Choice:      buildChoice(); break;
    case View::Confirm:     buildConfirm(); break;
    case View::Result:      buildResult(); break;
    case View::Collect:     buildCollect(); break;
    case View::ChapterEnd:  buildChapterEnd(); break;
    case View::Cafe:        buildCafe(); break;
    case View::Paused:      buildPaused(); break;
    }
}

void setView(View v)
{
    s_view = v;
    s_dirty = true;
}

void setReading(View v)
{
    s_page = 0;
    setView(v);
}

// 話を始める（同じ話をもう一度読むときもここを通る）
void startEpisode(uint8_t index)
{
    s_episode = index < c::kEpisodeCount ? index : 0;
    // 「復習かどうか」はここで固定する。答えを記録したあとで表示が変わらないように
    s_replay_attempt = isReplay(s_episode);
    s_page = 0;
    s_clue = 0;
    for (size_t i = 0; i < c::kClueCount; ++i) {
        s_clue_read[i] = false;
    }
    s_hint_level = 0;
    s_choice = -1;
    s_answered = false;
    s_gave_up = false;
    s_correct = false;
    s_gate.newScreen();
    setView(View::Cover);
}

// 答えを確定する（ここから先、この挑戦では答えを変えられない）
// 進み具合を保存する。書けなかったときはフラッシュの内容を読み直し、メモリー上の記録だけが先へ進んだ
// 状態を残さない（残すと、再起動後に「初回の結果」をもう一度記録できてしまう）
void saveProgress()
{
    if (detective::save() || detective::save()) {   // 1 回だけやり直す
        return;
    }
    Serial.println("[DET] progress save failed, reloading from flash");
    detective::load();
}

void resolveAnswer(bool gave_up)
{
    const c::Episode &ep = episode();
    s_gave_up = gave_up;
    s_correct = !gave_up && s_choice >= 0 && (uint8_t)s_choice == ep.answer_index;
    s_answered = true;

    FirstResult result = FirstResult::Incorrect;
    if (gave_up) {
        result = FirstResult::GaveUp;
    } else if (s_correct) {
        result = s_hint_level > 0 ? FirstResult::AssistedCorrect : FirstResult::Correct;
    }
    // 初回のときだけ記録される（復習では何も変わらない）
    if (detective::recordFirstResult(s_episode, result)) {
        saveProgress();
    }
    setView(View::Result);
}

// 結末を読み終えた。記念の品は正解と無関係に全員へ渡す（設計書 3.4）
void finishStory()
{
    bool changed = detective::markStoryCompleted(s_episode);
    changed = detective::markCollectible(s_episode) || changed;
    if (changed) {
        saveProgress();
    }
    setView(View::Collect);
}

// ---------------------------------------------------------------------------
// 入力
// ---------------------------------------------------------------------------
void indexCb(lv_event_t *e)
{
    const int index = (int)(intptr_t)lv_event_get_user_data(e);

    if (s_view == View::Menu) {
        if (index < 0 || index >= (int)c::kEpisodeCount) {
            return;
        }
        // 後の話を先に開くときだけ 1 回確認する（強制ロックはしない／設計書 3.1）
        bool earlier_unread = false;
        for (int i = 0; i < index; ++i) {
            if (!detective::get((size_t)i).story_completed) {
                earlier_unread = true;
            }
        }
        if (earlier_unread && !s_spoiler_warned) {
            s_pending_episode = (uint8_t)index;
            setView(View::Spoiler);
            return;
        }
        startEpisode((uint8_t)index);
        return;
    }

    if (s_view == View::Hub) {
        if (index < 0 || index >= (int)c::kClueCount) {
            return;
        }
        s_clue = (uint8_t)index;
        setReading(View::Clue);
        return;
    }

    if (s_view == View::Choice) {
        // 確定済みなら Gate が弾く（遅れて届いた 2 回目のタップを答えにしない）
        if (!s_gate.choose(index)) {
            return;
        }
        s_choice = (int8_t)index;
        setView(View::Confirm);
    }
}

// 読み物の「戻る」。1 ページ目より前はそれぞれの出口へ（行き止まりを作らない）
void readingBack()
{
    if (s_page > 0) {
        --s_page;
        s_dirty = true;
        return;
    }
    // 前の読み物へ戻るときは、その最後のページから読み直せるようにする。
    // 大きすぎるページ番号は buildRun() が最後のページに丸めてくれる
    switch (s_view) {
    case View::Intro:    setView(View::Cover); break;
    case View::Rules:    s_page = UINT16_MAX; setView(View::Intro); break;
    case View::Explain:  setView(View::Result); break;
    case View::Ending:   s_page = UINT16_MAX; setView(View::Explain); break;
    default:             setView(View::Hub); break;   // 証拠・手帳・店の中・ヒント
    }
}

// 読み物の最後の 1 つ先
void readingDone()
{
    switch (s_view) {
    case View::Intro:
        setReading(View::Rules);
        break;
    case View::Rules:
        setView(View::Hub);
        break;
    case View::Clue:
        s_clue_read[s_clue] = true;   // ここで初めて既読になる
        setView(View::Hub);
        break;
    case View::Notebook:
    case View::Ambient:
    case View::Hint:
        setView(View::Hub);
        break;
    case View::Explain:
        setReading(View::Ending);
        break;
    case View::Ending:
        finishStory();
        break;
    default:
        setView(View::Hub);
        break;
    }
}

void actionCb(lv_event_t *e)
{
    const Act act = (Act)(intptr_t)lv_event_get_user_data(e);

    switch (act) {
    case Act::MenuBack:
        ui::pop();                 // ゲーム一覧へ戻る
        return;
    case Act::MenuChapter:
        setView(View::ChapterEnd);
        break;

    case Act::SpoilerOk:
        s_spoiler_warned = true;   // 案内は 1 回だけ
        startEpisode(s_pending_episode);
        break;
    case Act::SpoilerNo:
        setView(View::Menu);
        break;

    case Act::CoverStart:
        setReading(View::Intro);
        break;
    case Act::CoverBack:
        setView(View::Menu);
        break;

    case Act::PagePrev:
        readingBack();
        break;
    case Act::PageNext:
        if (s_page + 1 < s_total_pages) {
            ++s_page;
            s_dirty = true;
        }
        break;
    case Act::PageDone:
        readingDone();
        break;

    case Act::HubBack:
        setView(View::Menu);
        break;
    case Act::HubNotebook:
        setReading(View::Notebook);
        break;
    case Act::HubAmbient:
        setReading(View::Ambient);
        break;
    case Act::HubHint:
        setView(View::HintConfirm);
        break;
    case Act::HubGuess:
        s_gate.enableChoices();
        setView(View::Choice);
        break;
    case Act::HubGiveUp:
        setView(View::GiveUp);
        break;

    case Act::HintOk:
        // 段階は必ず 1 → 2 の順。読んだ事実は戻る操作でも消えない（設計書 3.5）
        if (s_hint_level < c::kHintLevels) {
            ++s_hint_level;
        }
        if (detective::raiseHintLevel(s_episode, s_hint_level)) {
            saveProgress();
        }
        setReading(View::Hint);
        break;
    case Act::HintNo:
        setView(View::Hub);
        break;

    case Act::GiveUpOk:
        s_choice = -1;
        resolveAnswer(true);
        break;
    case Act::GiveUpNo:
        setView(View::Hub);
        break;

    case Act::ChoiceBack:
        s_gate.newScreen();
        setView(View::Hub);
        break;

    case Act::ConfirmOk:
        // 確定は 1 回だけ。Gate が受け付けたときだけ答え合わせへ進む
        if (s_gate.submit(1) && s_gate.receive(s_gate.epoch(), 1, true)) {
            resolveAnswer(false);
        }
        break;
    case Act::ConfirmChange:
        if (s_gate.change()) {
            s_choice = -1;
            setView(View::Choice);
        }
        break;

    case Act::ResultNext:
        setReading(View::Explain);
        break;

    case Act::CollectDone:
        if (detective::allCollectiblesOwned()) {
            setView(View::ChapterEnd);
        } else {
            setView(View::Menu);
        }
        break;

    case Act::ChapterDone:
        ui::goHome();
        return;

    case Act::Cafe:
        s_cafe_return = s_view;
        setView(View::Cafe);
        break;
    case Act::CafeCoffee:
        // HOME の「+1」と同じ処理（記録も通知も同じ経路を通る）
        home::addOneCup();
        ui::showToast(s_screen, str("coffee_added"));
        break;
    case Act::CafeBack:
        setView(s_cafe_return);    // 元のページへ戻る
        break;
    case Act::CafeHome:
        ui::goHome();
        return;

    case Act::PausedResume:
        setView(s_paused_return);   // 読みかけのページはそのまま残っている
        break;
    case Act::PausedMenu:
        setView(View::Menu);        // ここで初めて読みかけの位置を捨てる
        break;
    }
}

// ---------------------------------------------------------------------------
// 100ms ごとの処理
// ---------------------------------------------------------------------------
void tickCb(lv_timer_t *t)
{
    (void)t;
    if (s_dirty) {
        rebuild();
        s_dirty = false;
    }

    // 読書中に 180 秒さわられなければ「ひと休み」を出す（設計書 3.6 / config.json）。
    // 読みかけの位置とここまでの既読・ヒントは RAM に残したままなので、
    // 「つづきを読む」で同じページへ戻れる。捨てるのは一覧・HOME へ出たときだけ。
    // 電源が切れても残るブックマーク（設計書 13.2 のジャーナル）は段階 B の対象外
    if ((isReadingView(s_view) || s_view == View::Hub || s_view == View::Cover) &&
        lv_disp_get_inactive_time(nullptr) > c::kPageIdlePauseMs) {
        s_paused_return = s_view;
        setView(View::Paused);
    }
}

// ---------------------------------------------------------------------------
// 画面の生成・破棄
// ---------------------------------------------------------------------------
void screenDeletedCb(lv_event_t *e)
{
    // 画面の破棄は遷移アニメーション（200ms）の完了時なので、その間に次の探偵画面が
    // 作られていることがある。古い画面の後始末で新しい画面を壊さないよう照合する
    if (lv_event_get_target(e) != s_screen) {
        return;
    }
    if (s_tick != nullptr) {
        lv_timer_del(s_tick);
        s_tick = nullptr;
    }
    s_gate.detach();
    s_dirty = false;
    s_part_count = 0;
    s_total_pages = 0;
    // 開発用コマンド 'D' が古い画面名を出さないよう、画面の種類も初期値に戻す
    s_view = View::Menu;
    s_cafe_return = View::Menu;
    s_paused_return = View::Menu;
    s_page = 0;
    // 解放済みオブジェクトを触らないよう、静的ポインタは必ず全部消す
    s_screen = nullptr;
    s_content = nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
lv_obj_t *createGameScreen()
{
    // 前の探偵画面がまだ消えていない（遷移中の再入・開発用コマンドの連打）ことがある。
    // 古いタイマーを残すと 2 本が同じ部品を作り替えに来るので、ここで止める
    if (s_tick != nullptr) {
        lv_timer_del(s_tick);
        s_tick = nullptr;
    }

    lv_obj_t *scr = ui::makeScreen();

    s_screen = scr;
    s_content = lv_obj_create(scr);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_pos(s_content, 0, 0);
    lv_obj_set_size(s_content, ui::kScreenSize, ui::kScreenSize);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);

    detective::load();

    // 入り直したときのため、状態を毎回そろえる
    s_view = View::Menu;
    s_cafe_return = View::Menu;
    s_paused_return = View::Menu;
    s_episode = recommendedEpisode();
    s_replay_attempt = isReplay(s_episode);
    s_pending_episode = 0;
    s_spoiler_warned = false;
    s_page = 0;
    s_clue = 0;
    for (size_t i = 0; i < c::kClueCount; ++i) {
        s_clue_read[i] = false;
    }
    s_hint_level = 0;
    s_choice = -1;
    s_answered = false;
    s_gave_up = false;
    s_correct = false;
    s_gate.newScreen();

    rebuild();
    s_dirty = false;

    s_tick = lv_timer_create(tickCb, 100, nullptr);
    lv_obj_add_event_cb(scr, screenDeletedCb, LV_EVENT_DELETE, nullptr);
    return scr;
}

void debugResetProgress()
{
    const bool ok = detective::resetAll();
    // 画面が出ていれば、一覧の「初見／未読」表示も作り直す
    s_replay_attempt = false;
    if (s_screen != nullptr) {
        s_dirty = true;
    }
    Serial.printf("[DET] progress reset: all 3 episodes back to 初見・未読 (save %s)\n",
                  ok ? "ok" : "FAILED");
}

void debugPrintPublicState()
{
    // 公開情報のみ。正解の人物 id はここに出さない
    Serial.printf("[DET] view=%u case=%s page=%u/%u clue=%u read=%u%u%u hint=%u "
                  "choice=%d answered=%u gaveup=%u points=%u/%u\n",
                  (unsigned)s_view,
                  s_screen != nullptr ? c::kEpisodes[s_episode].case_id : "-",
                  (unsigned)(s_page + 1), (unsigned)s_total_pages, (unsigned)s_clue,
                  (unsigned)s_clue_read[0], (unsigned)s_clue_read[1], (unsigned)s_clue_read[2],
                  (unsigned)s_hint_level, (int)s_choice, (unsigned)s_answered,
                  (unsigned)s_gave_up, (unsigned)detective::points(s_episode),
                  (unsigned)detective::totalPoints());
}

}  // namespace detective
