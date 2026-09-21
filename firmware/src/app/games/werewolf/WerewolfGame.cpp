#include "WerewolfGame.h"

#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <array>
#include <cstdio>
#include <cstring>

#include "../../CupState.h"
#include "../../Display.h"
#include "../../HomeScreen.h"
#include "../../lvgl_v8_port.h"
#include "../../ui/ScreenManager.h"
#include "../../ui/UiKit.h"
#include "WerewolfContent.h"
#include "WerewolfPort.h"

// Arduino.h の bit(b) マクロが coffee::wolf::bit() と衝突するため、コアを読む前に無効化する
#undef bit
#include "core/paging.hpp"
#include "core/privacy_fence.hpp"
#include "core/public_meta.hpp"
#include "core/werewolf_core.hpp"
#include "core_std/werewolf_std_core.hpp"

LV_FONT_DECLARE(ct_font_jp_20);
LV_FONT_DECLARE(ct_font_jp_22);
LV_FONT_DECLARE(ct_font_jp_40);
LV_FONT_DECLARE(ct_font_time_64);
// 席のキャラクター・役職・お話のマーク（Material Icons Round）。日本語フォントとは別物
LV_FONT_DECLARE(ct_font_icons_36);
LV_FONT_DECLARE(ct_font_icons_88);

namespace werewolf {
namespace {

// content:: / layout:: / rules:: はこの using で coffee::wolf の中のものが見える
using namespace coffee::wolf;

// 通常ルール（多日制）のコアは別の名前空間。Role / Phase / Err の意味が違うので
// using はせず、必ず ws:: を付けて呼ぶ（ワンナイト版のものと取り違えないため）
namespace ws = coffee::wolfstd;

// 遊び方は 2 つ。人数を決めたあとに選ぶ
enum class Mode : uint8_t { OneNight = 0, Standard = 1 };

// ---------------------------------------------------------------------------
// 画面の種類
//
// Engine の Phase で決まるものと、この層だけが持つ小画面（確認・ページ物）がある。
// Engine の revision が変わったときだけ Phase から作り直し、小画面はそのまま維持する。
// Std* が付くものは通常ルール専用（ws::Engine の Phase に対応する）。
// ---------------------------------------------------------------------------
enum class View : uint8_t {
    Lobby,              // 人数を決める（count テンプレート）
    ModeSelect,         // どちらで遊ぶ？（通常ルール / ワンナイト）
    RebootNotice,       // 電源断で無効になった局のお知らせ
    Story,              // 世界観のお話（毎回・7 ページ・スキップ可）
    Brief,              // 始める前の約束（必読 4 ページ）
    SetupConfirm,       // 人数と席順の確認
    Roster,             // あなたは だれ？（席のキャラクター一覧・配る直前）
    Tutorial,           // 遊び方（ロビーからのみ・お話 7 + 17 ページ）
    Error,              // 乱数 / 記録領域の異常
    NightHandoff,
    RoleCheck,          // 秘密
    NightTarget,
    NightTargetConfirm,
    NightResult,        // 秘密
    NightDone,
    DayReady,
    DayTalk,
    DayFinishConfirm,
    VoteReady,
    VoteHandoff,
    VoteSelect,
    VoteConfirm,        // 秘密
    VoteDone,
    RunoffReady,
    RunoffTalk,
    FinalReady,
    Result,
    Pause,
    PauseOwner,         // 「{seat}番の本人ですか？」
    PauseAbortConfirm,
    Aborted,
    // --- 通常ルール（多日制）---------------------------------------------
    StdStory,           // 通常ルールのお話（6 ページ）
    StdBrief,           // 通常ルールの約束（4 ページ）
    StdNightHandoff,
    StdNightBrief,      // 秘密（今夜のあなた）
    StdNightTarget,
    StdNightTargetConfirm,
    StdNightResult,     // 秘密（夜の結果）
    StdNightDone,
    StdMorningReady,    // 「端末をテーブルに置こう」
    StdMorningAnnounce, // 朝の発表（公開）
    StdDayTalk,
    StdVoteReady,
    StdVoteHandoff,
    StdVoteSelect,
    StdVoteConfirm,     // 秘密（自分の投票先）
    StdVoteDone,
    StdRunoffReady,
    StdExecReady,       // 「端末をテーブルに置こう」
    StdExecAnnounce,    // 追放の発表（公開）
    StdFinalReady,
    StdResult,
};

// 押した内容。lv_event の user_data に入れて 1 つのコールバックで処理する
enum class Act : int {
    CountMinus = 1, CountPlus, LobbyStart, LobbyLeave, LobbyHelp,
    ModeStd, ModeOne, ModeBack,
    StoryPrev, StoryNext, StorySkip,
    BriefPrev, BriefNext, BriefDone,
    SetupBack, SetupNext, SetupStart,
    RosterBack, RosterOk,
    TutorialPrev, TutorialNext, TutorialExit,
    NoticeOk,
    NightReceive, RoleAck,
    TargetOk, TargetChange,
    NightAck, NightPass,
    DayStart, DayExtend, DayFinish, DayFinishOk, DayFinishNo,
    VoteBegin, VoteReceive, VoteCommit, VoteChange, VotePass,
    RunoffStart, Reveal,
    PagePrev, PageNext,
    ResultAgain, ResultExit,
    Pause, PauseResume, PauseResumeOk, PauseResumeNo, PauseCoffee,
    PauseAbort, PauseAbortOk, PauseAbortNo,
    AbortedAgain, AbortedExit,
    ErrorBack,
    // --- 通常ルール ---
    StdStoryPrev, StdStoryNext, StdStorySkip,
    StdBriefPrev, StdBriefNext, StdBriefDone,
    StdNightReceive, StdBriefAck,
    StdTargetOk, StdTargetChange,
    StdNightAck, StdNightPass,
    StdMorningOpen, StdMorningNext,
    StdDayExtend, StdDayFinish,
    StdVoteBegin, StdVoteReceive, StdVoteCommit, StdVoteChange, StdVotePass,
    StdRunoffStart,
    StdExecOpen, StdExecNext,
    StdReveal, StdResultAgain,
};

// ---------------------------------------------------------------------------
// 状態
// ---------------------------------------------------------------------------
Engine s_engine;        // ワンナイト（core/werewolf_core.hpp・無改変）
ws::Engine s_std;       // 通常ルール（core_std/werewolf_std_core.hpp）
Mode s_mode = Mode::OneNight;
SecretGate s_gate;
PrivacyFence s_fence;

lv_obj_t *s_screen = nullptr;
lv_obj_t *s_content = nullptr;      // 中身を丸ごと作り替える入れ物
lv_timer_t *s_tick = nullptr;

// 作り直すたびに nullptr へ戻す部品
lv_obj_t *s_role_label = nullptr;   // 役職名（秘密）
lv_obj_t *s_role_icon = nullptr;    // 役職のマーク（秘密。役職名と同じ扱いにする）
lv_obj_t *s_secret_label = nullptr; // 秘密の本文
lv_obj_t *s_cover_label = nullptr;  // 秘密を隠しているときの案内
lv_obj_t *s_timer_label = nullptr;  // 議論の残り時間
uint32_t s_shown_seconds = UINT32_MAX;   // 表示中の残り秒（書き換えの間引き用）

View s_view = View::Lobby;
bool s_dirty = true;                // 中身を作り直す必要がある
bool s_neutral_pending = false;     // 作り直したあと、実際に画面へ出るまで待つ
bool s_clear_armed_pending = false; // 「配った」印を消す（秘密を消してから行う）
uint64_t s_seen_revision = 0;
uint64_t s_seen_generation = 0;

uint8_t s_players = rules::kDefaultPlayers;
uint32_t s_flavor_seq = 0;
uint8_t s_page = 0;                 // 席のページ / 結果のページ
uint8_t s_doc_page = 0;             // 必読・遊び方のページ
int s_pending_target = NONE;        // 夜の対象の確認待ち
const char *s_error_key = nullptr;

// 覗き見防止まわり。volatile の 2 つは監視タスク（別コア）と共有する。
// s_last_tick_ms は 32bit にしてある。64bit だと xtensa では 2 語に分かれて読み書きされ、
// コアをまたぐと上位と下位がちぐはぐな値を拾うことがあるため
bool s_secret_shown = false;
// 結果まで進んだ局を「遊んだ 1 回」として数える予約。秘密が画面から消えてから数える
bool s_play_pending = false;
volatile bool s_secret_on_screen = false;
volatile uint32_t s_last_tick_ms = 0;
TaskHandle_t s_watchdog = nullptr;
uint64_t s_last_touch_ms = 0;

// バックライトの消灯・点灯と「消したか」の記録は 2 つのコアから触るので、
// 1 本のミューテックスで直列化する。LEDC の操作はクリティカルセクションの中で
// 行ってはいけないので、スピンロックではなく FreeRTOS のミューテックスを使う
SemaphoreHandle_t s_backlight_mux = nullptr;
bool s_backlight_cut = false;      // s_backlight_mux を取った中だけで読み書きする

uint32_t nowTicks()
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// 結果・決選候補の一覧（1 行 1 文字列にしてから 4 行ずつページ送りする）。
// 通常ルールの答え合わせは「全員の役職＋日ごとの記録（犠牲者・追放・全員の投票・
// 占いの結果）」なので行数が多い。10 人 7 日でも収まるよう 160 行にしてある
// （1 行は最長 14 全角 = 42 バイトなので 64 バイトで足りる。合計 10KB）
constexpr size_t kMaxRows = 160;
constexpr size_t kRowChars = 64;
char s_rows[kMaxRows][kRowChars];
// 行の左に置くマーク（席のキャラクター）。無い行は nullptr。
// マークはアイコンフォントの 1 文字で、日本語フォントでは描けないので別ラベルにする
const char *s_row_icons[kMaxRows];
uint8_t s_row_count = 0;

// ---------------------------------------------------------------------------
// 文言
// ---------------------------------------------------------------------------
struct Subst { const char *key; const char *value; };

const char *str(const char *key)
{
    const char *s = content::findString(key);
    return s != nullptr ? s : key;   // 生成データに無いときは鍵をそのまま出して気付けるようにする
}

// 途中で切れた UTF-8 の並びを落とす。日本語は 3 バイトなので、
// バイト単位で打ち切ると 1 文字の途中で切れて豆腐が出る
void trimUtf8(char *text)
{
    size_t len = std::strlen(text);
    while (len > 0 && ((unsigned char)text[len - 1] & 0xC0) == 0x80) {
        --len;   // 後続バイトを戻る
    }
    if (len == 0) {
        return;
    }
    const unsigned char lead = (unsigned char)text[len - 1];
    size_t need = 0;
    if ((lead & 0xE0) == 0xC0) need = 2;
    else if ((lead & 0xF0) == 0xE0) need = 3;
    else if ((lead & 0xF8) == 0xF0) need = 4;
    if (need > 0 && std::strlen(text) - (len - 1) < need) {
        text[len - 1] = '\0';   // 先頭バイトだけ残っている
    }
}

// "{seat}" などの差し込みを置き換える。対応しない差し込み語はそのまま残す
void fillText(char *out, size_t cap, const char *tmpl, const Subst *subs, size_t count)
{
    if (out == nullptr || cap == 0) {
        return;
    }
    size_t o = 0;
    const char *p = tmpl != nullptr ? tmpl : "";
    while (*p != '\0' && o + 1 < cap) {
        if (*p == '{') {
            const char *close = std::strchr(p, '}');
            if (close != nullptr) {
                const size_t len = (size_t)(close - p - 1);
                bool matched = false;
                for (size_t i = 0; i < count; ++i) {
                    if (std::strlen(subs[i].key) == len && std::strncmp(p + 1, subs[i].key, len) == 0) {
                        for (const char *v = subs[i].value; *v != '\0' && o + 1 < cap; ++v) {
                            out[o++] = *v;
                        }
                        matched = true;
                        break;
                    }
                }
                if (matched) {
                    p = close + 1;
                    continue;
                }
            }
        }
        out[o++] = *p++;
    }
    out[o] = '\0';
    trimUtf8(out);
}

void numberText(char *out, size_t cap, int value)
{
    std::snprintf(out, cap, "%d", value);
}

// --- 席のキャラクター（公開情報。秘密ではない）-----------------------------
// 番号だけだと取り違えるので、席ごとに喫茶の小物の名前とマークを割り当てる。
// 番号は「本人確認ではない」が、並び順の目印として小さく添える。

// 席の名前だけ（ボタン用）。データに無ければ空文字
const char *seatName(int seat)
{
    const content::SeatCharacter *c = content::findSeatCharacter(seat);
    return (c != nullptr && c->name != nullptr) ? c->name : "";
}

// 席のマーク（アイコンフォントの 1 文字）。データに無ければ空文字
const char *seatIcon(int seat)
{
    const content::SeatCharacter *c = content::findSeatCharacter(seat);
    return (c != nullptr && c->icon != nullptr) ? c->icon : "";
}

// 文章の中の呼び名（「カップさん」）。char.honorific が持っている
void seatHonorific(char *out, size_t cap, int seat)
{
    const char *name = seatName(seat);
    if (name[0] == '\0') {
        std::snprintf(out, cap, "%d", seat + 1);   // 通常は起きない（保険）
        return;
    }
    const Subst subs[] = {{"name", name}};
    fillText(out, cap, str("char.honorific"), subs, 1);
}

// 席番号の添え字（「3番」）
void seatNumberText(char *out, size_t cap, int seat)
{
    char num[8];
    numberText(num, sizeof(num), seat + 1);
    const Subst subs[] = {{"seat", num}};
    fillText(out, cap, str("char.seat_no"), subs, 1);
}

// 席番号や伏せ札の呼び名。{target_label} / {selected_label} に入れる
void targetLabel(char *out, size_t cap, int target)
{
    if (target == RESERVE_A) {
        std::snprintf(out, cap, "%s", str("night.reserve_a"));
    } else if (target == RESERVE_B) {
        std::snprintf(out, cap, "%s", str("night.reserve_b"));
    } else if (target == PEACE) {
        std::snprintf(out, cap, "%s", str("vote.peace"));
    } else if (target == NONE) {
        std::snprintf(out, cap, "%s", str("vote.ineligible"));
    } else {
        seatHonorific(out, cap, target);
    }
}

const char *roleName(Role role)
{
    switch (role) {
    case Role::Wolf: return str("role.wolf.name");
    case Role::Seer: return str("role.seer.name");
    case Role::Villager: return str("role.villager.name");
    default: return "";
    }
}

// 役職のマーク。これは秘密なので、呼べるのは showSecret() の中だけ
const char *roleIcon(Role role)
{
    switch (role) {
    case Role::Wolf: return content::kIconWolf;
    case Role::Seer: return content::kIconSeer;
    case Role::Villager: return content::kIconVillager;
    default: return "";
    }
}

// ---------------------------------------------------------------------------
// 部品づくり
// ---------------------------------------------------------------------------
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

// 日本語は全角なので「半角いくつ分か」で幅をおおよそ見積もり、収まらなければ小さい字にする
const lv_font_t *fitFont(const char *text, int16_t width)
{
    size_t cells = 0;
    for (const char *p = text; *p != '\0'; ++p) {
        if (((unsigned char)*p & 0xC0) == 0x80) {
            continue;               // UTF-8 の後続バイトは数えない
        }
        cells += ((unsigned char)*p < 0x80) ? 1 : 2;
    }
    return (int16_t)(cells * 11) <= width ? &ct_font_jp_22 : &ct_font_jp_20;
}

lv_obj_t *rectLabel(const layout::Rect &r, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(s_content);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    // 日本語は空白が無く自動折返しが効かない。改行は文言側に入っているので幅で切る
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, r.w);
    lv_label_set_text(l, text);
    const uint16_t lines = lineCount(text);
    const int16_t h = (int16_t)(font->line_height * lines);
    if (h > r.h && lines > 1) {
        // 収まらない本文は枠の高さで切る。はみ出したまま描くと、下に置いたボタンの
        // 上に文字が乗ってしまう（文言は最大 4 行あるので必ず起こりうる）
        lv_obj_set_height(l, r.h);
        lv_obj_set_pos(l, r.x, r.y);
    } else {
        lv_obj_set_pos(l, r.x, (int16_t)(r.h > h ? r.y + (r.h - h) / 2 : r.y));
    }
    return l;
}

// マーク（アイコンフォント）のラベル。枠の中央に置く。
// 文字は Material Icons Round の 1 文字で、日本語フォントでは描けない
lv_obj_t *iconLabel(const layout::Rect &r, const lv_font_t *font, lv_color_t color,
                    const char *text)
{
    lv_obj_t *l = lv_label_create(s_content);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, r.w);
    const int16_t h = (int16_t)font->line_height;
    lv_obj_set_height(l, h < r.h ? h : r.h);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, r.x, (int16_t)(r.h > h ? r.y + (r.h - h) / 2 : r.y));
    return l;
}

void actionCb(lv_event_t *e);
void targetCb(lv_event_t *e);

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
    lv_obj_set_style_text_font(l, fitFont(text, r.w), 0);
    lv_obj_set_style_text_color(l, enabled ? CT_COLOR_TEXT : CT_COLOR_DIM, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, r.w);
    lv_label_set_text(l, text);
    lv_obj_center(l);

    if (enabled) {
        lv_obj_add_event_cb(btn, actionCb, LV_EVENT_CLICKED, (void *)(intptr_t)act);
    } else {
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    }
    return btn;
}

// 夜の対象・投票先のボタン（押した対象を user_data で渡す）
lv_obj_t *targetButton(const layout::Rect &r, const char *text, int target, bool enabled)
{
    lv_obj_t *btn = rectButton(r, text, Act::CountMinus, false);
    if (enabled) {
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(btn, targetCb, LV_EVENT_CLICKED, (void *)(intptr_t)target);
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

// 「continue」の帯を左右 2 つに割ったボタン位置。
// layout.json には private テンプレートに 2 ボタンの想定が無いので、丸い画面に収まる
// 範囲（中心から半径 228px 以内）で kContinue と同じ帯を分けて使う。
constexpr layout::Rect kTwoLeft{110, 376, 125, 46};
constexpr layout::Rect kTwoRight{245, 376, 125, 46};

// layout.json の cafe / help は幅 80px しかなく、4 文字の見出しが枠線に触れてしまう。
// 丸い画面の内側（y=90 の行は x が 69〜411 まで）で左右対称に少しだけ広げる
constexpr layout::Rect kCafeWide{76, 90, 104, 44};
constexpr layout::Rect kHelpWide{300, 90, 104, 44};

// ページ物（必読・遊び方・人数確認）の主ボタン。
// 「全員で確認しました」は 9 文字あり footer（124px）にも wide_button（240px）にも
// 余裕が無いので、ページ送りの列の下に少し広めの帯を取る
constexpr layout::Rect kActionWide{110, 348, 260, 52};

// 一時停止の本文と 3 つのボタン（等間隔・丸の内側）
constexpr layout::Rect kMenuBody{90, 140, 300, 56};
constexpr layout::Rect kMenu1{110, 214, 260, 54};
constexpr layout::Rect kMenu2{110, 278, 260, 54};
constexpr layout::Rect kMenu3{110, 342, 260, 54};

// --- キャラクター表示のための追加座標（layout.json は設計書の正本なので変えない）---
// いずれも中心 (240,240) から半径 228px の内側に収まることを確認済み。

// お話のページ: 見出し(46) / マーク(92) / 本文(144) / ページ送り(292) / スキップ(348)
constexpr layout::Rect kStoryIcon{204, 92, 72, 44};

// 手渡しの画面: 見出し(46) / 大きなマーク(92) / 番号(196) / 本文(226) / 受け取りました(338)
constexpr layout::Rect kHandoffIcon{176, 92, 128, 100};
constexpr layout::Rect kHandoffSeat{180, 196, 120, 26};
constexpr layout::Rect kHandoffBody{90, 226, 300, 100};

// 「あなたは だれ？」の一覧: 案内(94) / 4 枠(142〜280) / ページ送り(292) / おぼえた(348)
constexpr layout::Rect kRosterNote{110, 94, 260, 32};

// 役職のマーク（秘密）。役職名 role_name は x108〜372 の中央に描かれるので左側が空く
constexpr layout::Rect kRoleIcon{112, 136, 48, 48};

// 枠の中に「マーク＋名前」を並べるときの、マーク側の幅（左の余白 10px を含む）。
// 154px の枠なら名前に 96px 残る（いちばん長い名前は 4 文字 = 88px）
constexpr int16_t kIconSlotW = 52;

// 席のボタン（夜の対象・投票先）。左にキャラクターのマーク、右に名前を置く。
// 自分・対象外のときも席が分かるようにマークは出し、色だけ落とす（枠は詰めない）
lv_obj_t *seatTargetButton(const layout::Rect &r, const char *text, const char *icon,
                           int target, bool enabled)
{
    lv_obj_t *btn = targetButton(r, text, target, enabled);
    if (icon == nullptr || icon[0] == '\0') {
        return btn;
    }
    // rectButton が作った文字を右側へ寄せ、空いた左側にマークを入れる
    lv_obj_t *l = lv_obj_get_child(btn, 0);
    if (l != nullptr) {
        lv_obj_set_width(l, (int16_t)(r.w - kIconSlotW - 6));
        lv_obj_align(l, LV_ALIGN_RIGHT_MID, -6, 0);
    }
    lv_obj_t *ic = lv_label_create(btn);
    lv_obj_set_style_text_font(ic, &ct_font_icons_36, 0);
    lv_obj_set_style_text_color(ic, enabled ? CT_COLOR_ACCENT_HI : CT_COLOR_DIM, 0);
    lv_label_set_long_mode(ic, LV_LABEL_LONG_CLIP);
    lv_label_set_text(ic, icon);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 10, 0);
    return btn;
}

// 「マーク＋名前＋番号」の 1 枠（あなたは だれ？の一覧。押せない表示だけの枠）
void rosterCell(const layout::Rect &r, int seat)
{
    const layout::Rect icon_rect{(int16_t)(r.x + 6), r.y, 44, r.h};
    iconLabel(icon_rect, &ct_font_icons_36, CT_COLOR_ACCENT_HI, seatIcon(seat));

    const int16_t text_x = (int16_t)(r.x + kIconSlotW);
    const int16_t text_w = (int16_t)(r.w - kIconSlotW - 6);
    rectLabel(layout::Rect{text_x, (int16_t)(r.y + 4), text_w, 26}, &ct_font_jp_22,
              CT_COLOR_TEXT, seatName(seat));

    char num[16];
    seatNumberText(num, sizeof(num), seat);
    rectLabel(layout::Rect{text_x, (int16_t)(r.y + 34), text_w, 24}, &ct_font_jp_20,
              CT_COLOR_DIM, num);
}

bool isPrivateView(View v)
{
    return v == View::RoleCheck || v == View::NightResult || v == View::VoteConfirm ||
           v == View::StdNightBrief || v == View::StdNightResult || v == View::StdVoteConfirm;
}

// 1 人が端末を持って操作している場面（無操作で自動的に一時停止する）
bool isSoloView(View v)
{
    return isPrivateView(v) || v == View::NightTarget || v == View::NightTargetConfirm ||
           v == View::VoteSelect || v == View::StdNightTarget ||
           v == View::StdNightTargetConfirm || v == View::StdVoteSelect;
}

// 通常ルールの進行中の画面か（左上を「一時停止」にする対象）
bool isStdGameView(View v)
{
    switch (v) {
    case View::StdNightHandoff: case View::StdNightBrief: case View::StdNightTarget:
    case View::StdNightTargetConfirm: case View::StdNightResult: case View::StdNightDone:
    case View::StdMorningReady: case View::StdMorningAnnounce: case View::StdDayTalk:
    case View::StdVoteReady: case View::StdVoteHandoff: case View::StdVoteSelect:
    case View::StdVoteConfirm: case View::StdVoteDone: case View::StdRunoffReady:
    case View::StdExecReady: case View::StdExecAnnounce: case View::StdFinalReady:
        return true;
    default:
        return false;
    }
}

// 公開画面（結果・ロビー等）ではない、進行中の局の画面か
bool isInGameView(View v)
{
    if (isStdGameView(v)) {
        return true;
    }
    return v != View::Lobby && v != View::ModeSelect && v != View::RebootNotice &&
           v != View::Story && v != View::Brief && v != View::SetupConfirm &&
           v != View::Roster && v != View::Tutorial && v != View::Error &&
           v != View::Result && v != View::Aborted && v != View::Pause &&
           v != View::PauseOwner && v != View::PauseAbortConfirm &&
           v != View::StdStory && v != View::StdBrief && v != View::StdResult;
}

// いま動いているのは通常ルールのコアか
bool stdMode() { return s_mode == Mode::Standard; }

// どちらかの局が進行中か
bool anyGameActive() { return s_engine.active() || s_std.active(); }

// 一時停止中か（モードに合わせて見る）
bool enginePaused()
{
    return stdMode() ? s_std.publicView().paused : s_engine.publicView().paused;
}

void blankSecretLabels()
{
    // 隠すときは必ず文字列そのものを空にする。非表示フラグだけでは中身が残ってしまう
    if (s_secret_label != nullptr) {
        lv_label_set_text(s_secret_label, "");
    }
    if (s_role_label != nullptr) {
        lv_label_set_text(s_role_label, "");
    }
    if (s_role_icon != nullptr) {
        // マークだけでも役職が分かってしまうので、役職名と必ず同時に消す
        lv_label_set_text(s_role_icon, "");
    }
    if (s_cover_label != nullptr) {
        lv_obj_clear_flag(s_cover_label, LV_OBJ_FLAG_HIDDEN);
    }
    s_secret_shown = false;
    // s_secret_on_screen はここでは下ろさない。ラベルを空にしただけでは
    // フレームバッファにはまだ秘密が残っており、走査し直すまでは画面に出ている。
    // 監視タスクの見張りは、その走査が終わる neutralize() まで続ける必要がある
}

// --- バックライト（2 コアから触るので必ずこの 3 つを通す） ---------------

// 監視タスクから呼ぶ。消してからフラグを立てるところまでを不可分に行う
void backlightCutForPrivacy()
{
    if (s_backlight_mux == nullptr) {
        return;
    }
    xSemaphoreTake(s_backlight_mux, portMAX_DELAY);
    if (!s_backlight_cut) {
        port().cutBacklight();
        s_backlight_cut = true;
    }
    xSemaphoreGive(s_backlight_mux);
}

// LVGL タスクから呼ぶ。中立化が済んだ epoch を渡すこと。
// 点けてからフラグを下ろすところまでを不可分に行う。戻したら true
bool backlightRestoreIfCut(uint64_t verified_epoch)
{
    if (s_backlight_mux == nullptr) {
        return false;
    }
    bool restored = false;
    xSemaphoreTake(s_backlight_mux, portMAX_DELAY);
    if (s_backlight_cut) {
        port().restoreBacklight(verified_epoch);
        s_backlight_cut = false;
        restored = true;
    }
    xSemaphoreGive(s_backlight_mux);
    return restored;
}

bool backlightIsCut()
{
    if (s_backlight_mux == nullptr) {
        return false;
    }
    xSemaphoreTake(s_backlight_mux, portMAX_DELAY);
    const bool cut = s_backlight_cut;
    xSemaphoreGive(s_backlight_mux);
    return cut;
}

// 画面が消される途中では lv_refr_now を呼べない。遷移アニメーション（200ms）が終わり、
// 次の画面（HOME など）が描かれてから点け直すための一発タイマー
lv_timer_t *s_backlight_recover = nullptr;

void backlightRecoverCb(lv_timer_t *t)
{
    s_backlight_recover = nullptr;
    lv_timer_del(t);                       // LVGL はタイマーの自己削除に対応している
    const uint64_t epoch = s_fence.beginHide();
    port().requestNeutralScanout(epoch);   // 次の画面が実際にパネルへ出るまで待つ
    backlightRestoreIfCut(epoch);
}

// 今の描画内容が実際にパネルへ出るまで待ち、PrivacyFence に完了を伝える
void neutralize()
{
    const uint64_t epoch = s_fence.beginHide();
    port().requestNeutralScanout(epoch);
    // ここまで来て初めて「秘密は物理的に画面から消えた」と言える
    s_secret_on_screen = false;
    // 走査待ちで数十 ms 止まるので、監視タスクに「tick は生きている」と伝え直す
    s_last_tick_ms = nowTicks();
}

void clearArmed()
{
    PublicMeta meta;
    meta.armed = false;
    meta.sound = false;
    meta.players = validPlayers(s_players) ? s_players : rules::kDefaultPlayers;
    meta.discussion_s = 0;
    meta.flavor_seq = s_flavor_seq;
    std::array<uint8_t, 20> bytes{};
    if (encodeMeta(meta, bytes)) {
        port().writePublicMetaAndReadBack(bytes);
    }
}

void setView(View v)
{
    s_view = v;
    s_dirty = true;
}

void showError(const char *key)
{
    s_error_key = key;
    setView(View::Error);
}

// ---------------------------------------------------------------------------
// 画面の中身
// ---------------------------------------------------------------------------

// 上段の 2 つ（左：カフェ／一時停止、右：遊び方）
void buildTopButtons()
{
    if (isInGameView(s_view)) {
        // 進行中は左上が一時停止。押すと先に画面を中立にしてから Engine を止める。
        // ちょうど良い長さの文言が content に無いので短い見出しを置く
        rectButton(kCafeWide, "一時停止", Act::Pause);
    } else if (s_view == View::Result || s_view == View::StdResult) {
        rectButton(kCafeWide, str("result.exit"), Act::ResultExit);
    } else if (s_view == View::Lobby) {
        rectButton(kCafeWide, str("menu.back"), Act::LobbyLeave);
        // 遊び方はロビーからだけ。進行中に開くと自分の役職を思い出す材料になってしまう
        rectButton(kHelpWide, str("menu.help"), Act::LobbyHelp);
    }
}

void buildLobby()
{
    makeTitle(str("setup.title"));

    char num[8];
    numberText(num, sizeof(num), s_players);
    rectLabel(layout::kCountNumber, &ct_font_time_64, CT_COLOR_TEXT, num);

    rectButton(layout::kCountMinus, str("setup.minus"), Act::CountMinus, s_players > MIN_PLAYERS);
    rectButton(layout::kCountPlus, str("setup.plus"), Act::CountPlus, s_players < MAX_PLAYERS);

    char hint[64];
    const Subst subs[] = {{"players", num}};
    fillText(hint, sizeof(hint), str("setup.count"), subs, 1);
    rectLabel(layout::kCountHint, &ct_font_jp_20, CT_COLOR_SUBTEXT, hint);

    rectButton(layout::kWideButton, str("menu.start"), Act::LobbyStart, true, true);
}

// --- 遊び方の選択（人数を決めたあと）---------------------------------------
// 3 人は通常ルールが成立しないのでワンナイトだけにする（理由も画面に出す）。

// 見出し(46) / 通常ルール(140) と注記(196) / ワンナイト(232) と注記(288) / 戻る(348)
constexpr layout::Rect kModeStd{100, 140, 280, 56};
constexpr layout::Rect kModeStdNote{90, 200, 300, 28};
constexpr layout::Rect kModeOne{100, 234, 280, 56};
constexpr layout::Rect kModeOneNote{90, 292, 300, 26};
constexpr layout::Rect kModeFoot{90, 322, 300, 56};   // 2 行になることがあるので高さを取る

// 通常ルールの配役を 1 行にする（公開情報。ロビーと約束の両方で使う）
void stdCompositionText(char *out, size_t cap, uint8_t players)
{
    const ws::Composition c = ws::compositionFor(players);
    char w[8], s[8], v[8];
    numberText(w, sizeof(w), c.wolves);
    numberText(s, sizeof(s), c.seers);
    numberText(v, sizeof(v), c.villagers(players));
    const Subst subs[] = {{"wolves", w}, {"seers", s}, {"villagers", v}};
    fillText(out, cap, str("mode.summary"), subs, 3);
}

void buildModeSelect()
{
    makeTitle(str("mode.title"));

    const bool std_ok = ws::validPlayers(s_players);
    rectButton(kModeStd, str("mode.std"), Act::ModeStd, std_ok, std_ok);
    if (std_ok) {
        // 選ぶ前にこの人数の配役を見せる（人狼が何人になるかは公開情報）
        char comp[64];
        stdCompositionText(comp, sizeof(comp), s_players);
        rectLabel(kModeStdNote, &ct_font_jp_20, CT_COLOR_SUBTEXT, comp);
    } else {
        rectLabel(kModeStdNote, &ct_font_jp_20, CT_COLOR_DIM, str("mode.std.note"));
    }

    rectButton(kModeOne, str("mode.one"), Act::ModeOne, true, !std_ok);
    rectLabel(kModeOneNote, &ct_font_jp_20, CT_COLOR_SUBTEXT, str("mode.one.note"));

    if (!std_ok) {
        rectLabel(kModeFoot, &ct_font_jp_20, CT_COLOR_DIM, str("mode.only_one"));
    } else {
        rectLabel(kModeFoot, &ct_font_jp_20, CT_COLOR_DIM,
                  str(s_mode == Mode::Standard ? "mode.last_std" : "mode.last_one"));
    }
    rectButton(layout::kPublicBack, str("common.no"), Act::ModeBack);
}

void buildRebootNotice()
{
    makeTitle(str("abort.title"));
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT, str("abort.reboot"));
    rectButton(layout::kWideButton, str("common.next"), Act::NoticeOk, true, true);
}

// 必読・遊び方・人数確認で共通のページ物。上から
//   見出し(title) / 本文(body) / ページ送り(page_prev・page_label・page_next) / 主ボタン(kActionWide)
// の 4 段で、どれも重ならない。layout.json の footer_* と public_back は
// 361〜411 と 398〜442 で重なるため、この画面では使わない。
void buildDocPager(const char *title, const char *body, size_t page, size_t total,
                   Act prev_act, Act next_act,
                   const char *action_text, Act action_act, bool action_enabled,
                   bool next_past_last = false)
{
    makeTitle(title);
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT, body);

    char status[16];
    char cur[8];
    char pages[8];
    numberText(cur, sizeof(cur), (int)page + 1);
    numberText(pages, sizeof(pages), (int)total);
    const Subst page_subs[] = {{"page", cur}, {"pages", pages}};
    fillText(status, sizeof(status), str("page.status"), page_subs, 2);
    rectLabel(layout::kPageLabel, &ct_font_jp_20, CT_COLOR_SUBTEXT, status);

    // 1 ページ目の「前へ」は前の画面へ戻る出口にする（行き止まりを作らない）
    rectButton(layout::kPagePrev, page > 0 ? str("common.prev") : str("common.no"), prev_act, true);
    // next_past_last のときは最後のページの「次へ」が次の画面への出口になる
    rectButton(layout::kPageNext, str("common.next"), next_act,
               next_past_last || page + 1 < total);
    rectButton(kActionWide, action_text, action_act, action_enabled, action_enabled);
}

// 世界観のお話（毎回・遊び方の先頭にも出す）。本文の上にマークを 1 つ置く
void buildStory(size_t page)
{
    const content::StoryPage &entry = content::kStory[page];
    buildDocPager(entry.title, entry.body, page, content::kStoryCount,
                  Act::StoryPrev, Act::StoryNext,
                  str("story.skip"), Act::StorySkip, true, true);
    iconLabel(kStoryIcon, &ct_font_icons_36, CT_COLOR_ACCENT_HI, entry.icon);
}

// 「あなたは だれ？」— 遊ぶ席のキャラクターだけを並べる（公開情報）
void buildRoster()
{
    makeTitle(str("roster.title"));
    rectLabel(kRosterNote, &ct_font_jp_20, CT_COLOR_SUBTEXT, str("roster.note"));

    const uint8_t pages = (uint8_t)((s_players + PAGE_SIZE - 1) / PAGE_SIZE);
    if (pages > 0 && s_page >= pages) {
        s_page = (uint8_t)(pages - 1);
    }
    for (uint8_t i = 0; i < PAGE_SIZE; ++i) {
        const int seat = s_page * PAGE_SIZE + i;
        if (seat >= (int)s_players) {
            break;
        }
        rosterCell(layout::kSlots[i], seat);
    }

    char status[16];
    char cur[8];
    char total[8];
    numberText(cur, sizeof(cur), s_page + 1);
    numberText(total, sizeof(total), pages > 0 ? pages : 1);
    const Subst subs[] = {{"page", cur}, {"pages", total}};
    fillText(status, sizeof(status), str("page.status"), subs, 2);
    rectLabel(layout::kPageLabel, &ct_font_jp_20, CT_COLOR_SUBTEXT, status);
    // 1 ページ目の「前へ」は人数確認へ戻る出口にする（行き止まりを作らない）
    rectButton(layout::kPagePrev, s_page > 0 ? str("common.prev") : str("common.no"),
               s_page > 0 ? Act::PagePrev : Act::RosterBack, true);
    rectButton(layout::kPageNext, str("common.next"), Act::PageNext, s_page + 1 < pages);
    rectButton(kActionWide, str("roster.ok"), Act::RosterOk, true, true);
}

void buildBrief()
{
    const size_t total = content::kMandatoryBriefCount;
    const size_t page = s_doc_page < total ? s_doc_page : 0;
    const content::PagedEntry &entry = content::kMandatoryBrief[page];

    char players[8];
    char pool[8];
    numberText(players, sizeof(players), s_players);
    numberText(pool, sizeof(pool), s_players + 2);
    const Subst subs[] = {{"players", players}, {"pool", pool}};
    char body[256];
    fillText(body, sizeof(body), entry.body, subs, 2);

    // 「全員で確認しました」は最後のページまで押せない（4 ページとも読んでもらう）
    buildDocPager(entry.title, body, page, total, Act::BriefPrev, Act::BriefNext,
                  str("common.understood"), Act::BriefDone, page + 1 >= total);
}

// 通常ルールのお話（6 ページ）。作りはワンナイトと同じで、文言だけ差し替える
void buildStdStory(size_t page)
{
    const content::StoryPage &entry = content::kStoryStd[page];
    buildDocPager(entry.title, entry.body, page, content::kStoryStdCount,
                  Act::StdStoryPrev, Act::StdStoryNext,
                  str("story.skip"), Act::StdStorySkip, true, true);
    iconLabel(kStoryIcon, &ct_font_icons_36, CT_COLOR_ACCENT_HI, entry.icon);
}

// 通常ルールの約束（4 ページ）。2 ページ目にこの人数の配役を差し込む
void buildStdBrief()
{
    const size_t total = content::kBriefStdCount;
    const size_t page = s_doc_page < total ? s_doc_page : 0;
    const content::PagedEntry &entry = content::kBriefStd[page];

    const ws::Composition c = ws::compositionFor(s_players);
    char players[8], wolves[8], seers[8], villagers[8];
    numberText(players, sizeof(players), s_players);
    numberText(wolves, sizeof(wolves), c.wolves);
    numberText(seers, sizeof(seers), c.seers);
    numberText(villagers, sizeof(villagers), c.villagers(s_players));
    const Subst subs[] = {{"players", players}, {"wolves", wolves},
                          {"seers", seers}, {"villagers", villagers}};
    char body[256];
    fillText(body, sizeof(body), entry.body, subs, 4);

    buildDocPager(entry.title, body, page, total, Act::StdBriefPrev, Act::StdBriefNext,
                  str("common.understood"), Act::StdBriefDone, page + 1 >= total);
}

// 人数確認も 2 ページに分ける。1 枚に詰めると setup.pool の 4 行がボタンの下へ潜る
constexpr size_t kSetupPages = 2;

void buildSetupConfirm()
{
    const size_t page = s_doc_page < kSetupPages ? s_doc_page : 0;

    char players[8];
    char pool[8];
    numberText(players, sizeof(players), s_players);
    numberText(pool, sizeof(pool), s_players + 2);
    const Subst subs[] = {{"players", players}, {"pool", pool}};

    // 通常ルールには伏せ札が無いので、人数確認の文も差し替える
    const char *key = stdMode() ? (page == 0 ? "std.setup.body" : "std.setup.pool")
                                : (page == 0 ? "setup.body" : "setup.pool");
    char body[256];
    fillText(body, sizeof(body), str(key), subs, 2);

    buildDocPager(str("setup.title"), body, page, kSetupPages, Act::SetupBack, Act::SetupNext,
                  str("setup.check"), Act::SetupStart, page + 1 >= kSetupPages);
}

// 遊び方 = 世界観のお話（6 ページ）＋ もとの遊び方（17 ページ）
const size_t kTutorialTotal = content::kStoryCount + content::kTutorialCount;

void buildTutorial()
{
    const size_t page = s_doc_page < kTutorialTotal ? s_doc_page : 0;
    if (page < content::kStoryCount) {
        const content::StoryPage &entry = content::kStory[page];
        buildDocPager(entry.title, entry.body, page, kTutorialTotal,
                      Act::TutorialPrev, Act::TutorialNext,
                      str("help.return"), Act::TutorialExit, true);
        iconLabel(kStoryIcon, &ct_font_icons_36, CT_COLOR_ACCENT_HI, entry.icon);
        return;
    }
    const content::PagedEntry &entry = content::kTutorial[page - content::kStoryCount];
    buildDocPager(entry.title, entry.body, page, kTutorialTotal,
                  Act::TutorialPrev, Act::TutorialNext,
                  str("help.return"), Act::TutorialExit, true);
}

void buildError()
{
    makeTitle(str("abort.title"));
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_ALERT,
              str(s_error_key != nullptr ? s_error_key : "error.storage"));
    rectButton(layout::kWideButton, str("common.no"), Act::ErrorBack, true, true);
}

// 手渡しの画面（夜・投票で共通）。大きなマークと名前で「誰の番か」を一目で伝える。
// 席番号は本人確認ではないので、名前の下に小さく添えるだけにする
void buildHandoff(const PublicView &pv, const char *title_key, const char *body_key,
                  Act receive_act)
{
    char seat[8];
    numberText(seat, sizeof(seat), pv.actor + 1);
    const Subst subs[] = {{"seat", seat}, {"name", seatName(pv.actor)}};

    char title[64];
    fillText(title, sizeof(title), str(title_key), subs, 2);
    makeTitle(title);

    iconLabel(kHandoffIcon, &ct_font_icons_88, CT_COLOR_ACCENT_HI, seatIcon(pv.actor));

    char num[16];
    seatNumberText(num, sizeof(num), pv.actor);
    rectLabel(kHandoffSeat, &ct_font_jp_20, CT_COLOR_DIM, num);

    char body[192];
    fillText(body, sizeof(body), str(body_key), subs, 2);
    rectLabel(kHandoffBody, &ct_font_jp_22, CT_COLOR_TEXT, body);

    rectButton(layout::kWideButton, str("handoff.receive"), receive_act, true, true);
}

void buildNightHandoff(const PublicView &pv)
{
    buildHandoff(pv, "handoff.night.title", "handoff.night.body", Act::NightReceive);
}

// 秘密を出す画面の骨組み。秘密のラベルは空で作り、押している間だけ中身を入れる
void buildPrivate(const char *title_key, const char *hold_key, const char *continue_key,
                  Act continue_act, bool with_role_name, bool two_buttons,
                  const char *second_key, Act second_act)
{
    makeTitle(str(title_key));

    if (with_role_name) {
        s_role_label = rectLabel(layout::kRoleName, &ct_font_jp_40, CT_COLOR_ACCENT_HI, "");
        // 役職のマークも秘密。必ず空で作り、showSecret() の中でだけ中身を入れる
        s_role_icon = iconLabel(kRoleIcon, &ct_font_icons_36, CT_COLOR_ACCENT_HI, "");
    }
    s_cover_label = rectLabel(layout::kSecretBody, &ct_font_jp_20, CT_COLOR_DIM, str("role.cover"));
    s_secret_label = rectLabel(layout::kSecretBody, &ct_font_jp_22, CT_COLOR_TEXT, "");

    // 押している間だけ表示する枠。判定は LVGL ではなくタッチの生データで行う
    rectButton(layout::kHold, str(hold_key), Act::CountMinus, false);

    if (two_buttons) {
        rectButton(kTwoLeft, str(second_key), second_act);
        rectButton(kTwoRight, str(continue_key), continue_act, true, true);
    } else {
        rectButton(layout::kContinue, str(continue_key), continue_act, true, true);
    }
}

void buildSeatPage(const PublicView &pv, bool vote)
{
    // 人数が減るなどしてページ番号が範囲外になったら先頭に戻す（進めなくなるのを防ぐ）
    const uint8_t page_total =
        (uint8_t)((pv.player_count + PAGE_SIZE - 1) / PAGE_SIZE);
    if (page_total > 0 && s_page >= page_total) {
        s_page = (uint8_t)(page_total - 1);
    }
    const SeatPage sp = seatPage(pv.player_count, s_page);
    for (uint8_t i = 0; i < PAGE_SIZE; ++i) {
        const int8_t seat = sp.valid ? sp.seats[i] : NONE;
        if (seat == NONE) {
            continue;   // 席が無い枠は空ける（詰めない）
        }
        const bool self = (uint8_t)seat == pv.actor;
        bool eligible = true;
        if (vote) {
            eligible = (pv.eligible & bit((uint8_t)seat)) != 0;
        }
        char label[24];
        if (self) {
            std::snprintf(label, sizeof(label), "%s", str("vote.self"));
        } else if (!eligible) {
            std::snprintf(label, sizeof(label), "%s", str("vote.ineligible"));
        } else {
            // ボタンの上では敬称なしの短い名前にする（幅が 154px しかない）
            std::snprintf(label, sizeof(label), "%s", seatName(seat));
        }
        seatTargetButton(layout::kSlots[i], label, seatIcon(seat), seat, !self && eligible);
    }

    char status[16];
    char cur[8];
    char pages[8];
    numberText(cur, sizeof(cur), (int)s_page + 1);
    numberText(pages, sizeof(pages), (int)(sp.pages > 0 ? sp.pages : 1));
    const Subst page_subs[] = {{"page", cur}, {"pages", pages}};
    fillText(status, sizeof(status), str("page.status"), page_subs, 2);
    rectLabel(layout::kPageLabel, &ct_font_jp_20, CT_COLOR_SUBTEXT, status);
    rectButton(layout::kPagePrev, str("common.prev"), Act::PagePrev, s_page > 0);
    rectButton(layout::kPageNext, str("common.next"), Act::PageNext, s_page + 1 < sp.pages);
}

void buildNightTarget(const PublicView &pv)
{
    makeTitle(str("night.target.title"));
    buildSeatPage(pv, false);
    // 伏せ札はページに混ぜず、常に同じ位置に置く（「追加の参加者」に見せないため）
    targetButton(layout::kReserveA, str("night.reserve_a"), RESERVE_A, true);
    targetButton(layout::kReserveB, str("night.reserve_b"), RESERVE_B, true);
    rectLabel(layout::kSmallNote, &ct_font_jp_20, CT_COLOR_DIM, str("night.target.note"));
}

void buildNightTargetConfirm()
{
    makeTitle(str("night.target.title"));
    char label[24];
    targetLabel(label, sizeof(label), s_pending_target);
    const Subst subs[] = {{"target_label", label}};
    char body[192];
    fillText(body, sizeof(body), str("night.target.confirm"), subs, 1);
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT, body);
    rectButton(layout::kWideButton, str("night.target.ok"), Act::TargetOk, true, true);
    rectButton(layout::kPublicBack, str("common.change"), Act::TargetChange);
}

void buildNightDone(const PublicView &pv)
{
    makeTitle(str("night.done.title"));
    const bool last = (pv.actor + 1 >= pv.player_count);
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT,
              str(last ? "night.done.last" : "night.done.body"));
    rectButton(layout::kWideButton, str(last ? "night.done.day" : "handoff.pass"),
               Act::NightPass, true, true);
}

void buildDayReady()
{
    makeTitle(str("day.ready.title"));
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT, str("day.ready.body"));
    rectButton(layout::kWideButton, str("day.start"), Act::DayStart, true, true);
}

void buildTalk(const PublicView &pv, bool runoff)
{
    makeTitle(runoff ? str("runoff.start") : "話し合い");

    s_timer_label = rectLabel(layout::kLargeTimer, &ct_font_time_64, CT_COLOR_TEXT, "0:00");

    const char *prompt = str("day.prompt.1");
    if (pv.remaining_ms == 0) {
        prompt = str("day.timer_ended");
    } else if (runoff) {
        prompt = str("runoff.no_extend");
    }
    rectLabel(layout::kTimerPrompt, &ct_font_jp_20, CT_COLOR_SUBTEXT, prompt);

    if (runoff) {
        // 決選は延長なし。理由は timer_prompt に出すので、ここは幅 124px に収まる短い表示にする
        rectButton(layout::kFooterLeft, str("day.extension.used"), Act::DayExtend, false);
    } else {
        rectButton(layout::kFooterLeft,
                   pv.extension_used ? str("day.extension.used") : str("day.extension"),
                   Act::DayExtend, !pv.extension_used);
    }
    rectButton(layout::kFooterRight, str("day.finish"), Act::DayFinish, true, true);
}

void buildDayFinishConfirm()
{
    makeTitle(str("day.finish"));
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT, str("day.finish.confirm"));
    rectButton(layout::kWideButton, str("common.yes"), Act::DayFinishOk, true, true);
    rectButton(layout::kPublicBack, str("common.no"), Act::DayFinishNo);
}

void buildVoteReady()
{
    makeTitle(str("vote.ready.title"));
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT, str("vote.ready.body"));
    rectButton(layout::kWideButton, str("vote.begin"), Act::VoteBegin, true, true);
}

void buildVoteHandoff(const PublicView &pv)
{
    buildHandoff(pv, "vote.handoff.title", "vote.handoff.body", Act::VoteReceive);
}

void buildVoteSelect(const PublicView &pv)
{
    makeTitle(str("vote.choose.title"));
    buildSeatPage(pv, true);

    // 「人狼はいない」は席のページに混ぜず常に同じ位置。決選で候補外なら押せない
    const bool peace_ok = (pv.eligible & bit(PEACE)) != 0;
    targetButton(layout::kPeace, str("vote.peace"), PEACE, peace_ok);

    if (pv.vote_cycle >= 2) {
        rectLabel(layout::kSmallNote, &ct_font_jp_20, CT_COLOR_DIM, str("runoff.vote.note"));
    } else {
        char count[8];
        char players[8];
        numberText(count, sizeof(count), pv.actor);
        numberText(players, sizeof(players), pv.player_count);
        const Subst subs[] = {{"count", count}, {"players", players}};
        char note[48];
        fillText(note, sizeof(note), str("vote.progress"), subs, 2);
        rectLabel(layout::kSmallNote, &ct_font_jp_20, CT_COLOR_DIM, note);
    }
}

void buildVoteDone(const PublicView &pv)
{
    makeTitle(str("vote.done.title"));
    const bool last = (pv.actor + 1 >= pv.player_count);
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT,
              str(last ? "vote.done.last" : "vote.done.body"));
    rectButton(layout::kWideButton, str(last ? "common.next" : "handoff.pass"),
               Act::VotePass, true, true);
}

void buildFinalReady()
{
    makeTitle(str("final.ready.title"));
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT, str("final.ready.body"));
    rectButton(layout::kWideButton, str("final.reveal"), Act::Reveal, true, true);
}

// ---- 4 行ずつのページ物（決選の候補・結果の一覧） -------------------------

void addRowIcon(const char *text, const char *icon)
{
    if (s_row_count >= kMaxRows) {
        return;   // 行があふれたら静かに捨てる（進めなくなるよりまし）
    }
    std::snprintf(s_rows[s_row_count], kRowChars, "%s", text);
    trimUtf8(s_rows[s_row_count]);
    s_row_icons[s_row_count] = icon;
    ++s_row_count;
}

void addRow(const char *text)
{
    addRowIcon(text, nullptr);
}

// 改行入りの文言を 1 行ずつに分けて積む（行の高さが 43px しかないため）
void addLines(const char *text)
{
    char buf[kRowChars];
    size_t o = 0;
    for (const char *p = text; ; ++p) {
        if (*p == '\n' || *p == '\0') {
            buf[o < kRowChars ? o : kRowChars - 1] = '\0';
            if (o > 0) {
                trimUtf8(buf);
                addRow(buf);
            }
            o = 0;
            if (*p == '\0') {
                break;
            }
            continue;
        }
        if (o + 1 < kRowChars) {
            buf[o++] = *p;
        }
    }
}

void buildRowPages(const char *title, const char *footer_right_text, Act footer_right_act,
                   bool footer_right_primary)
{
    makeTitle(title);

    const uint8_t pages = (uint8_t)((s_row_count + 3) / 4);
    // 行数が変わってページ番号が範囲外になったら最後のページに寄せる（進めなくなるのを防ぐ）
    if (pages > 0 && s_page >= pages) {
        s_page = (uint8_t)(pages - 1);
    }
    const uint8_t page = s_page;
    for (uint8_t i = 0; i < 4; ++i) {
        const uint8_t index = (uint8_t)(page * 4 + i);
        if (index >= s_row_count) {
            break;
        }
        const layout::Rect &r = layout::kRows[i];
        const char *icon = s_row_icons[index];
        if (icon != nullptr && icon[0] != '\0') {
            // 席のキャラクターのマークを左に置き、文字はその右へ寄せる
            iconLabel(layout::Rect{(int16_t)(r.x + 2), r.y, 40, r.h}, &ct_font_icons_36,
                      CT_COLOR_ACCENT_HI, icon);
            const layout::Rect text_rect{(int16_t)(r.x + 44), r.y, (int16_t)(r.w - 44), r.h};
            rectLabel(text_rect, fitFont(s_rows[index], text_rect.w), CT_COLOR_TEXT,
                      s_rows[index]);
        } else {
            rectLabel(r, fitFont(s_rows[index], r.w), CT_COLOR_TEXT, s_rows[index]);
        }
    }

    char status[16];
    char cur[8];
    char total[8];
    numberText(cur, sizeof(cur), page + 1);
    numberText(total, sizeof(total), pages > 0 ? pages : 1);
    const Subst subs[] = {{"page", cur}, {"pages", total}};
    fillText(status, sizeof(status), str("page.status"), subs, 2);
    rectLabel(layout::kSmallNote, &ct_font_jp_20, CT_COLOR_DIM, status);

    rectButton(layout::kFooterLeft, str("common.prev"), Act::PagePrev, page > 0);
    const bool last = (page + 1 >= pages);
    if (last) {
        rectButton(layout::kFooterRight, footer_right_text, footer_right_act, true,
                   footer_right_primary);
    } else {
        rectButton(layout::kFooterRight, str("common.next"), Act::PageNext);
    }
}

void buildRunoffReady(const PublicView &pv)
{
    s_row_count = 0;
    char seconds[8];
    numberText(seconds, sizeof(seconds), runoffDiscussion(pv.player_count));
    const Subst subs[] = {{"seconds", seconds}};
    char body[192];
    fillText(body, sizeof(body), str("runoff.body"), subs, 1);
    addLines(body);
    addRow(str("runoff.candidates"));
    for (uint8_t t = 0; t < TARGET_SLOTS; ++t) {
        if ((pv.eligible & bit(t)) == 0) {
            continue;
        }
        char label[24];
        targetLabel(label, sizeof(label), t);
        addRow(label);
    }
    addRow(str("runoff.no_extend"));

    buildRowPages(str("runoff.title"), str("runoff.start"), Act::RunoffStart, true);
}

void buildResult()
{
    Result r;
    if (s_engine.result(r) != Err::Ok) {
        // 通常は起きない。Revealed のまま残すと次の開始が Err::Phase で弾かれるので、
        // 画面もお知らせに切り替える（Engine を Idle に戻すのは ErrorBack）
        s_error_key = "error.storage";
        s_view = View::Error;
        buildError();
        return;
    }

    const char *title = str("result.draw.title");
    const char *body = str("result.draw.body");
    switch (r.outcome) {
    case Outcome::CaughtWolf:
        title = str("result.village.title"); body = str("result.village.body"); break;
    case Outcome::WolfEscaped:
        title = str("result.wolf.title"); body = str("result.wolf.body"); break;
    case Outcome::PeaceCorrect:
        title = str("result.peace.title"); body = str("result.peace.body"); break;
    case Outcome::FalseAccusation:
        title = str("result.false.title"); body = str("result.false.body"); break;
    default:
        break;
    }

    s_row_count = 0;
    addLines(body);

    char label[24];
    if (r.selected == NONE) {
        addRow(str("result.no_selected"));
    } else {
        targetLabel(label, sizeof(label), r.selected);
        const Subst subs[] = {{"selected_label", label}};
        char line[64];
        fillText(line, sizeof(line), str("result.selected"), subs, 1);
        addRow(line);
    }

    // 参加者の役職
    addRow(str("result.roles"));
    for (uint8_t seat = 0; seat < r.player_count; ++seat) {
        char num[8];
        numberText(num, sizeof(num), seat + 1);
        const Subst subs[] = {{"seat", num}, {"name", seatName(seat)},
                              {"role", roleName(r.roles[seat])}};
        char line[64];
        fillText(line, sizeof(line), str("result.role_row"), subs, 3);
        addRow(line);
    }

    // 伏せ札 2 枚
    addRow(str("result.reserves"));
    for (uint8_t i = 0; i < 2; ++i) {
        const uint8_t slot = i == 0 ? RESERVE_A : RESERVE_B;
        const Subst subs[] = {{"reserve_label", str(i == 0 ? "night.reserve_a" : "night.reserve_b")},
                              {"role", roleName(r.roles[slot])}};
        char line[64];
        fillText(line, sizeof(line), str("result.reserve_row"), subs, 2);
        addRow(line);
    }

    // 投票のふり返り（1 回目、あれば決選）
    addRow(str("result.votes"));
    for (uint8_t seat = 0; seat < r.player_count; ++seat) {
        char num[8];
        numberText(num, sizeof(num), seat + 1);
        targetLabel(label, sizeof(label), r.first[seat]);
        const Subst subs[] = {{"seat", num}, {"name", seatName(seat)},
                              {"target_label", label}};
        char line[64];
        fillText(line, sizeof(line), str("result.vote_row"), subs, 3);
        addRow(line);
    }
    if (r.had_runoff) {
        addRow(str("runoff.title"));
        for (uint8_t seat = 0; seat < r.player_count; ++seat) {
            char num[8];
            numberText(num, sizeof(num), seat + 1);
            targetLabel(label, sizeof(label), r.runoff[seat]);
            const Subst subs[] = {{"seat", num}, {"name", seatName(seat)},
                                  {"target_label", label}};
            char line[64];
            fillText(line, sizeof(line), str("result.vote_row"), subs, 3);
            addRow(line);
        }
    }

    // 本当の占い結果（占い師が伏せ札だったときは無い）
    addRow(str("result.seer"));
    if (r.seer == NONE) {
        addLines(str("result.seer_absent"));
    } else {
        char num[8];
        numberText(num, sizeof(num), r.seer + 1);
        targetLabel(label, sizeof(label), r.inspected);
        const Subst subs[] = {{"seat", num}, {"name", seatName(r.seer)},
                              {"target_label", label}};
        char line[64];
        fillText(line, sizeof(line), str("result.seer_row"), subs, 3);
        // 重ね書きの文言は 2 行に分かれている（1 行 43px には収まらないため）
        addLines(line);
        // 占い結果は「人狼」か「人狼ではない」の二択。文言は既存のものを流用する
        addRow(str(r.finding == Finding::Wolf ? "role.wolf.name" : "vote.peace"));
    }

    buildRowPages(title, str("result.again"), Act::ResultAgain, true);
}

void buildPause()
{
    makeTitle(str("pause.title"));
    // 本文を上に寄せ、3 つのボタンを 10px ずつ空けて等間隔に並べる
    rectLabel(kMenuBody, &ct_font_jp_22, CT_COLOR_TEXT, str("pause.body"));
    rectButton(kMenu1, str("pause.resume"), Act::PauseResume, true, true);
    rectButton(kMenu2, str("pause.coffee"), Act::PauseCoffee);
    rectButton(kMenu3, str("pause.abort"), Act::PauseAbort);
}

void buildPauseOwner(const PublicView &pv)
{
    makeTitle(str("pause.title"));
    const uint8_t actor = stdMode() ? s_std.publicView().actor : pv.actor;
    char seat[8];
    numberText(seat, sizeof(seat), actor + 1);
    const Subst subs[] = {{"seat", seat}, {"name", seatName(actor)}};
    char body[192];
    fillText(body, sizeof(body), str("pause.resume.owner"), subs, 2);
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT, body);
    rectButton(layout::kWideButton, str("common.yes"), Act::PauseResumeOk, true, true);
    rectButton(layout::kPublicBack, str("common.no"), Act::PauseResumeNo);
}

void buildPauseAbortConfirm()
{
    makeTitle(str("pause.abort"));
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_ALERT, str("abort.confirm"));
    rectButton(layout::kWideButton, str("common.yes"), Act::PauseAbortOk, true, true);
    rectButton(layout::kPublicBack, str("common.no"), Act::PauseAbortNo);
}

void buildAborted()
{
    makeTitle(str("abort.title"));
    const char *body = str("abort.body");
    if (stdMode()) {
        switch (s_std.abortReason()) {
        case ws::AbortReason::Privacy:    body = str("abort.privacy"); break;
        case ws::AbortReason::HardLimit:  body = str("std.abort.timeout"); break;
        case ws::AbortReason::ClockFault: body = str("error.stale"); break;
        case ws::AbortReason::Internal:   body = str("error.storage"); break;
        default: break;
        }
    } else {
        switch (s_engine.abortReason()) {
        case AbortReason::Privacy:   body = str("abort.privacy"); break;
        case AbortReason::HardLimit: body = str("abort.timeout"); break;
        case AbortReason::ClockFault: body = str("error.stale"); break;
        default: break;
        }
    }
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT, body);
    rectButton(layout::kWideButton, str("result.again"), Act::AbortedAgain, true, true);
    rectButton(layout::kPublicBack, str("result.exit"), Act::AbortedExit);
}

// ===========================================================================
// 通常ルール（多日制）の画面
//
// 覗き見防止の仕組み（SecretGate・PrivacyFence・中立化・見張りタスク・バックライト）は
// ワンナイトとまったく同じものをそのまま使う。ここで作るのは中身だけ。
// 秘密のラベルは必ず空で作り、showSecret() の中でだけ文字を入れる。
// ===========================================================================

// 発表の画面で使う中立なマーク（Material Icons Round E541 local_cafe）。
// 役職のマーク（kIconWolf など＝秘密）とは別物。日本語フォントには無い文字なので、
// tools/collect_ui_chars.py に拾われないようエスケープで書く
constexpr const char *kIconCafe = "\xEE\x95\x81";

// 発表の画面: マーク(96) / 大きな名前(150) / 本文(214) / ボタン(338)
constexpr layout::Rect kStdAnnounceIcon{204, 96, 72, 48};
constexpr layout::Rect kStdBigName{100, 150, 280, 52};
constexpr layout::Rect kStdAnnounce{90, 214, 300, 90};

// 席の呼び名（通常ルールには伏せ札も「人狼はいない」も無いので席だけ）
void stdSeatLabel(char *out, size_t cap, int seat)
{
    if (seat < 0 || seat >= (int)ws::MAX_PLAYERS) {
        std::snprintf(out, cap, "%s", str("vote.ineligible"));
        return;
    }
    seatHonorific(out, cap, seat);
}

// 席のページ（4 枠ずつ）。抜けた人は「脱落」と出して押せなくする（誰が抜けたかは公開情報）
void buildStdSeatPage(const ws::PublicView &pv, bool vote)
{
    const uint8_t page_total = (uint8_t)((pv.player_count + PAGE_SIZE - 1) / PAGE_SIZE);
    if (page_total > 0 && s_page >= page_total) {
        s_page = (uint8_t)(page_total - 1);
    }
    for (uint8_t i = 0; i < PAGE_SIZE; ++i) {
        const int seat = s_page * PAGE_SIZE + i;
        if (seat >= (int)pv.player_count) {
            break;
        }
        const bool self = (uint8_t)seat == pv.actor;
        const bool alive = (pv.alive & ws::bit((uint8_t)seat)) != 0;
        const bool candidate = !vote || (pv.eligible & ws::bit((uint8_t)seat)) != 0;
        const bool usable = !self && alive && candidate;
        char label[24];
        if (self) {
            std::snprintf(label, sizeof(label), "%s", str("vote.self"));
        } else if (!alive) {
            std::snprintf(label, sizeof(label), "%s", str("std.seat.dead"));
        } else if (!candidate) {
            std::snprintf(label, sizeof(label), "%s", str("vote.ineligible"));
        } else {
            std::snprintf(label, sizeof(label), "%s", seatName(seat));
        }
        seatTargetButton(layout::kSlots[i], label, seatIcon(seat), seat, usable);
    }

    char status[16], cur[8], pages[8];
    numberText(cur, sizeof(cur), (int)s_page + 1);
    numberText(pages, sizeof(pages), (int)(page_total > 0 ? page_total : 1));
    const Subst page_subs[] = {{"page", cur}, {"pages", pages}};
    fillText(status, sizeof(status), str("page.status"), page_subs, 2);
    rectLabel(layout::kPageLabel, &ct_font_jp_20, CT_COLOR_SUBTEXT, status);
    rectButton(layout::kPagePrev, str("common.prev"), Act::PagePrev, s_page > 0);
    rectButton(layout::kPageNext, str("common.next"), Act::PageNext, s_page + 1 < page_total);
}

void buildStdHandoff(const ws::PublicView &pv, const char *title_key, const char *body_key,
                     Act receive_act)
{
    char seat[8];
    numberText(seat, sizeof(seat), pv.actor + 1);
    const Subst subs[] = {{"seat", seat}, {"name", seatName(pv.actor)}};

    char title[64];
    fillText(title, sizeof(title), str(title_key), subs, 2);
    makeTitle(title);

    iconLabel(kHandoffIcon, &ct_font_icons_88, CT_COLOR_ACCENT_HI, seatIcon(pv.actor));
    char num[16];
    seatNumberText(num, sizeof(num), pv.actor);
    rectLabel(kHandoffSeat, &ct_font_jp_20, CT_COLOR_DIM, num);

    char body[192];
    fillText(body, sizeof(body), str(body_key), subs, 2);
    rectLabel(kHandoffBody, &ct_font_jp_22, CT_COLOR_TEXT, body);

    rectButton(layout::kWideButton, str("handoff.receive"), receive_act, true, true);
}

void buildStdNightTarget(const ws::PublicView &pv)
{
    // 見出しは役職で変えない。何の選択なのかは直前の秘密画面（今夜のあなた）で伝える
    makeTitle(str("std.night.target.title"));
    buildStdSeatPage(pv, false);
    rectLabel(layout::kSmallNote, &ct_font_jp_20, CT_COLOR_DIM, str("std.night.target.note"));
}

void buildStdNightTargetConfirm()
{
    makeTitle(str("std.night.target.title"));
    char label[24];
    stdSeatLabel(label, sizeof(label), s_pending_target);
    const Subst subs[] = {{"target_label", label}};
    char body[192];
    fillText(body, sizeof(body), str("std.night.target.confirm"), subs, 1);
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT, body);
    rectButton(layout::kWideButton, str("night.target.ok"), Act::StdTargetOk, true, true);
    rectButton(layout::kPublicBack, str("common.change"), Act::StdTargetChange);
}

// 次に端末を渡す相手が居るか（居なければ朝の発表へ進む）
bool stdHasNextActor(const ws::PublicView &pv)
{
    for (uint8_t a = (uint8_t)(pv.actor + 1); a < pv.player_count; ++a) {
        if (pv.alive & ws::bit(a)) return true;
    }
    return false;
}

void buildStdNightDone(const ws::PublicView &pv)
{
    makeTitle(str("night.done.title"));
    const bool last = !stdHasNextActor(pv);
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT,
              str(last ? "night.done.last" : "night.done.body"));
    rectButton(layout::kWideButton, str(last ? "std.night.done.last" : "handoff.pass"),
               Act::StdNightPass, true, true);
}

// 公開の発表の前に必ず出す案内（「端末をテーブルに置いて、みんなで見よう」）
void buildStdTable(const char *ok_key, Act ok_act)
{
    makeTitle(str("std.table.title"));
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT, str("std.table.body"));
    rectButton(layout::kWideButton, str(ok_key), ok_act, true, true);
}

void buildStdMorning(const ws::PublicView &pv)
{
    makeTitle(str("std.morning.title"));
    if (pv.last_victim == ws::NONE) {
        iconLabel(kStdAnnounceIcon, &ct_font_icons_36, CT_COLOR_ACCENT_HI, kIconCafe);
        rectLabel(kStdAnnounce, &ct_font_jp_22, CT_COLOR_TEXT, str("std.morning.safe"));
    } else {
        const int victim = pv.last_victim;
        iconLabel(kStdAnnounceIcon, &ct_font_icons_36, CT_COLOR_ALERT, seatIcon(victim));
        char name[24];
        seatHonorific(name, sizeof(name), victim);
        rectLabel(kStdBigName, &ct_font_jp_40, CT_COLOR_ALERT, name);
        rectLabel(kStdAnnounce, &ct_font_jp_22, CT_COLOR_TEXT, str("std.morning.victim"));
    }
    // 決着していたら答え合わせへ、そうでなければ話し合いへ
    const bool over = pv.winner != ws::Winner::None;
    rectButton(layout::kWideButton, str(over ? "std.final.next" : "std.morning.next"),
               Act::StdMorningNext, true, true);
}

void buildStdDayTalk(const ws::PublicView &pv)
{
    char day[8];
    numberText(day, sizeof(day), pv.day);
    const Subst day_subs[] = {{"day", day}};
    char title[48];
    fillText(title, sizeof(title), str("std.day.title"), day_subs, 1);
    makeTitle(title);

    s_timer_label = rectLabel(layout::kLargeTimer, &ct_font_time_64, CT_COLOR_TEXT, "0:00");
    rectLabel(layout::kTimerPrompt, &ct_font_jp_20, CT_COLOR_SUBTEXT,
              pv.remaining_ms == 0 ? str("day.timer_ended") : str("day.prompt.1"));

    rectButton(layout::kFooterLeft,
               pv.extension_used ? str("day.extension.used") : str("day.extension"),
               Act::StdDayExtend, !pv.extension_used);
    rectButton(layout::kFooterRight, str("day.finish"), Act::StdDayFinish, true, true);

    char alive[8];
    numberText(alive, sizeof(alive), pv.alive_count);
    const Subst subs[] = {{"alive", alive}};
    char note[48];
    fillText(note, sizeof(note), str("std.day.alive"), subs, 1);
    rectLabel(layout::kSmallNote, &ct_font_jp_20, CT_COLOR_DIM, note);
}

void buildStdVoteReady()
{
    makeTitle(str("std.vote.ready.title"));
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT, str("std.vote.ready.body"));
    rectButton(layout::kWideButton, str("std.vote.begin"), Act::StdVoteBegin, true, true);
}

void buildStdVoteSelect(const ws::PublicView &pv)
{
    const bool runoff = pv.vote_cycle >= 2;
    makeTitle(str(runoff ? "std.runoff.vote.title" : "vote.choose.title"));
    buildStdSeatPage(pv, true);
    if (runoff) {
        rectLabel(layout::kSmallNote, &ct_font_jp_20, CT_COLOR_DIM, str("std.runoff.note"));
    } else {
        char count[8], alive[8];
        numberText(count, sizeof(count), pv.voted_count);
        numberText(alive, sizeof(alive), pv.alive_count);
        const Subst subs[] = {{"count", count}, {"players", alive}};
        char note[48];
        fillText(note, sizeof(note), str("vote.progress"), subs, 2);
        rectLabel(layout::kSmallNote, &ct_font_jp_20, CT_COLOR_DIM, note);
    }
}

void buildStdVoteDone(const ws::PublicView &pv)
{
    makeTitle(str("vote.done.title"));
    const bool last = !stdHasNextActor(pv);
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT,
              str(last ? "vote.done.last" : "vote.done.body"));
    rectButton(layout::kWideButton, str(last ? "std.vote.done.last" : "handoff.pass"),
               Act::StdVotePass, true, true);
}

void buildStdRunoffReady(const ws::PublicView &pv)
{
    s_row_count = 0;
    addLines(str("std.runoff.body"));
    addRow(str("std.runoff.candidates"));
    for (uint8_t t = 0; t < pv.player_count; ++t) {
        if ((pv.eligible & ws::bit(t)) == 0) {
            continue;
        }
        char label[24];
        stdSeatLabel(label, sizeof(label), t);
        addRow(label);
    }
    buildRowPages(str("std.runoff.title"), str("std.runoff.start"), Act::StdRunoffStart, true);
}

void buildStdExecAnnounce(const ws::PublicView &pv)
{
    makeTitle(str("std.exec.title"));
    if (pv.last_executed == ws::NONE) {
        iconLabel(kStdAnnounceIcon, &ct_font_icons_36, CT_COLOR_ACCENT_HI, kIconCafe);
        rectLabel(kStdAnnounce, &ct_font_jp_22, CT_COLOR_TEXT, str("std.exec.none"));
    } else {
        const int seat = pv.last_executed;
        iconLabel(kStdAnnounceIcon, &ct_font_icons_36, CT_COLOR_ALERT, seatIcon(seat));
        char name[24];
        seatHonorific(name, sizeof(name), seat);
        rectLabel(kStdBigName, &ct_font_jp_40, CT_COLOR_ALERT, name);
        rectLabel(kStdAnnounce, &ct_font_jp_22, CT_COLOR_TEXT, str("std.exec.done"));
    }
    const bool over = pv.winner != ws::Winner::None;
    rectButton(layout::kWideButton, str(over ? "std.final.next" : "std.exec.next"),
               Act::StdExecNext, true, true);
}

void buildStdFinalReady()
{
    makeTitle(str("std.final.ready.title"));
    rectLabel(layout::kBody, &ct_font_jp_22, CT_COLOR_TEXT, str("std.final.ready.body"));
    rectButton(layout::kWideButton, str("std.final.reveal"), Act::StdReveal, true, true);
}

// 最後の全公開。勝った陣営・全員の役職・日ごとの記録を 4 行ずつのページ送りで出す
void buildStdResult()
{
    ws::Summary sum;
    if (s_std.summary(sum) != ws::Err::Ok) {
        // 通常は起きない。Revealed のまま残すと次の開始が弾かれるのでお知らせに切り替える
        s_error_key = "error.storage";
        s_view = View::Error;
        buildError();
        return;
    }

    const bool village = sum.winner == ws::Winner::Village;
    const char *title = str(village ? "std.result.village" : "std.result.wolves");

    s_row_count = 0;
    addLines(str(village ? "std.result.village.body" : "std.result.wolves.body"));

    // 全員の役職（生死も添える）
    addRow(str("std.result.roles"));
    for (uint8_t seat = 0; seat < sum.player_count; ++seat) {
        const ws::Role role = sum.roles[seat];
        const char *role_text = role == ws::Role::Wolf ? str("role.wolf.name")
                              : role == ws::Role::Seer ? str("role.seer.name")
                                                       : str("role.villager.name");
        const bool alive = (sum.alive & ws::bit(seat)) != 0;
        const Subst subs[] = {{"name", seatName(seat)}, {"role", role_text}};
        char line[64];
        fillText(line, sizeof(line),
                 str(alive ? "std.result.role_alive" : "std.result.role_dead"), subs, 2);
        // 席のキャラクターのマークを添える（誰のことか一目で分かるように）
        addRowIcon(line, seatIcon(seat));
    }

    // 日ごとの記録（犠牲者・追放・全員の投票・占いの結果）
    for (uint8_t day = 1; day <= sum.days; ++day) {
        ws::DayLog d;
        if (s_std.dayLog(day, d) != ws::Err::Ok) {
            break;
        }
        char num[8];
        numberText(num, sizeof(num), day);
        {
            const Subst subs[] = {{"day", num}};
            char line[64];
            fillText(line, sizeof(line), str("std.result.day"), subs, 1);
            addRow(line);
        }
        if (d.victim == ws::NONE) {
            addRow(str("std.result.no_victim"));
        } else {
            const Subst subs[] = {{"name", seatName(d.victim)}};
            char line[64];
            fillText(line, sizeof(line), str("std.result.victim"), subs, 1);
            addRow(line);
        }
        if (d.executed == ws::NONE) {
            addRow(str("std.result.no_executed"));
        } else {
            const Subst subs[] = {{"name", seatName(d.executed)}};
            char line[64];
            fillText(line, sizeof(line), str("std.result.executed"), subs, 1);
            addRow(line);
        }
        // 占い師が複数いても、それぞれの占い先と結果を並べる
        if (d.seer_count > 0) {
            addRow(str("std.result.seer"));
            for (uint8_t i = 0; i < d.seer_count; ++i) {
                const ws::SeerRecord &r = d.seers[i];
                char label[24];
                stdSeatLabel(label, sizeof(label), r.target);
                const Subst subs[] = {{"name", seatName(r.seer)}, {"target_label", label}};
                char line[64];
                fillText(line, sizeof(line), str("std.result.seer_row"), subs, 2);
                addRow(line);
                addRow(str(r.finding == ws::Finding::Wolf ? "std.result.finding_wolf"
                                                          : "std.result.finding_not"));
            }
        }
        // その日の投票（生きていた人の分だけ）
        addRow(str("std.result.votes"));
        for (uint8_t seat = 0; seat < sum.player_count; ++seat) {
            if (d.first[seat] == ws::NONE) {
                continue;
            }
            char label[24];
            stdSeatLabel(label, sizeof(label), d.first[seat]);
            const Subst subs[] = {{"name", seatName(seat)}, {"target_label", label}};
            char line[64];
            fillText(line, sizeof(line), str("result.vote_row"), subs, 2);
            addRow(line);
        }
        if (d.had_runoff) {
            addRow(str("std.result.runoff"));
            for (uint8_t seat = 0; seat < sum.player_count; ++seat) {
                if (d.runoff[seat] == ws::NONE) {
                    continue;
                }
                char label[24];
                stdSeatLabel(label, sizeof(label), d.runoff[seat]);
                const Subst subs[] = {{"name", seatName(seat)}, {"target_label", label}};
                char line[64];
                fillText(line, sizeof(line), str("result.vote_row"), subs, 2);
                addRow(line);
            }
        }
    }

    buildRowPages(title, str("result.again"), Act::StdResultAgain, true);
}

// ---------------------------------------------------------------------------
// 作り直し
// ---------------------------------------------------------------------------
void rebuild()
{
    if (s_content == nullptr) {
        return;
    }
    // 秘密が出たまま作り直す場合（Engine が自分で中断したときなど）は、
    // 新しい中身が実際にパネルへ出るまで待つ必要があるので中立化を予約する
    if (s_secret_on_screen) {
        s_neutral_pending = true;
    }
    // 秘密が残らないよう、部品はいったん全部消してから作り直す（アニメーションはしない）
    s_role_label = nullptr;
    s_role_icon = nullptr;
    s_secret_label = nullptr;
    s_cover_label = nullptr;
    s_timer_label = nullptr;
    s_shown_seconds = UINT32_MAX;
    s_secret_shown = false;
    lv_obj_clean(s_content);

    const PublicView pv = s_engine.publicView();
    const ws::PublicView sv = s_std.publicView();
    buildTopButtons();

    switch (s_view) {
    case View::Lobby:             buildLobby(); break;
    case View::ModeSelect:        buildModeSelect(); break;
    case View::RebootNotice:      buildRebootNotice(); break;
    case View::Story:
        buildStory(s_doc_page < content::kStoryCount ? s_doc_page : 0);
        break;
    case View::Brief:             buildBrief(); break;
    case View::SetupConfirm:      buildSetupConfirm(); break;
    case View::Roster:            buildRoster(); break;
    case View::Tutorial:          buildTutorial(); break;
    case View::Error:             buildError(); break;
    case View::NightHandoff:      buildNightHandoff(pv); break;
    case View::RoleCheck:
        buildPrivate("role.title", "role.hold", "role.next", Act::RoleAck, true, false,
                     nullptr, Act::RoleAck);
        break;
    case View::NightTarget:       buildNightTarget(pv); break;
    case View::NightTargetConfirm: buildNightTargetConfirm(); break;
    case View::NightResult:
        buildPrivate("night.result.title", "night.result.hold", "night.result.next",
                     Act::NightAck, false, false, nullptr, Act::NightAck);
        break;
    case View::NightDone:         buildNightDone(pv); break;
    case View::DayReady:          buildDayReady(); break;
    case View::DayTalk:           buildTalk(pv, false); break;
    case View::DayFinishConfirm:  buildDayFinishConfirm(); break;
    case View::VoteReady:         buildVoteReady(); break;
    case View::VoteHandoff:       buildVoteHandoff(pv); break;
    case View::VoteSelect:        buildVoteSelect(pv); break;
    case View::VoteConfirm:
        buildPrivate("vote.choose.title", "vote.confirm.cover", "vote.confirm.commit",
                     Act::VoteCommit, false, true, "common.change", Act::VoteChange);
        break;
    case View::VoteDone:          buildVoteDone(pv); break;
    case View::RunoffReady:       buildRunoffReady(pv); break;
    case View::RunoffTalk:        buildTalk(pv, true); break;
    case View::FinalReady:        buildFinalReady(); break;
    case View::Result:            buildResult(); break;
    case View::Pause:             buildPause(); break;
    case View::PauseOwner:        buildPauseOwner(pv); break;
    case View::PauseAbortConfirm: buildPauseAbortConfirm(); break;
    case View::Aborted:           buildAborted(); break;

    // --- 通常ルール -------------------------------------------------------
    case View::StdStory:
        buildStdStory(s_doc_page < content::kStoryStdCount ? s_doc_page : 0);
        break;
    case View::StdBrief:          buildStdBrief(); break;
    case View::StdNightHandoff:
        buildStdHandoff(sv, "handoff.night.title", "handoff.night.body", Act::StdNightReceive);
        break;
    case View::StdNightBrief:
        buildPrivate("std.brief.title", "std.brief.hold", "std.brief.next",
                     Act::StdBriefAck, true, false, nullptr, Act::StdBriefAck);
        break;
    case View::StdNightTarget:        buildStdNightTarget(sv); break;
    case View::StdNightTargetConfirm: buildStdNightTargetConfirm(); break;
    case View::StdNightResult:
        buildPrivate("std.night.result.title", "std.night.result.hold",
                     "std.night.result.next", Act::StdNightAck, false, false,
                     nullptr, Act::StdNightAck);
        break;
    case View::StdNightDone:      buildStdNightDone(sv); break;
    case View::StdMorningReady:   buildStdTable("std.morning.open", Act::StdMorningOpen); break;
    case View::StdMorningAnnounce: buildStdMorning(sv); break;
    case View::StdDayTalk:        buildStdDayTalk(sv); break;
    case View::StdVoteReady:      buildStdVoteReady(); break;
    case View::StdVoteHandoff:
        buildStdHandoff(sv, "vote.handoff.title", "vote.handoff.body", Act::StdVoteReceive);
        break;
    case View::StdVoteSelect:     buildStdVoteSelect(sv); break;
    case View::StdVoteConfirm:
        buildPrivate("vote.choose.title", "vote.confirm.cover", "vote.confirm.commit",
                     Act::StdVoteCommit, false, true, "common.change", Act::StdVoteChange);
        break;
    case View::StdVoteDone:       buildStdVoteDone(sv); break;
    case View::StdRunoffReady:    buildStdRunoffReady(sv); break;
    case View::StdExecReady:      buildStdTable("std.exec.open", Act::StdExecOpen); break;
    case View::StdExecAnnounce:   buildStdExecAnnounce(sv); break;
    case View::StdFinalReady:     buildStdFinalReady(); break;
    case View::StdResult:         buildStdResult(); break;
    }
}

// ---------------------------------------------------------------------------
// Engine の状態から画面を決める（revision が変わったときだけ）
// ---------------------------------------------------------------------------
// 通常ルールの Phase → 画面
void adoptStdPhaseView()
{
    const ws::PublicView pv = s_std.publicView();
    if (pv.paused) {
        if (s_view != View::Pause && s_view != View::PauseOwner &&
            s_view != View::PauseAbortConfirm) {
            s_view = View::Pause;
        }
        return;
    }
    switch (pv.phase) {
    case ws::Phase::Idle:            s_view = View::Lobby; break;
    case ws::Phase::NightHandoff:    s_view = View::StdNightHandoff; break;
    case ws::Phase::NightBrief:      s_view = View::StdNightBrief; break;
    case ws::Phase::NightTarget:     s_view = View::StdNightTarget; s_page = 0;
                                     s_pending_target = NONE; break;
    case ws::Phase::NightResult:     s_view = View::StdNightResult; break;
    case ws::Phase::NightDone:       s_view = View::StdNightDone; break;
    case ws::Phase::MorningReady:    s_view = View::StdMorningReady; break;
    case ws::Phase::MorningAnnounce: s_view = View::StdMorningAnnounce; break;
    case ws::Phase::DayTalk:         s_view = View::StdDayTalk; break;
    case ws::Phase::VoteReady:       s_view = View::StdVoteReady; break;
    case ws::Phase::VoteHandoff:     s_view = View::StdVoteHandoff; s_page = 0; break;
    case ws::Phase::VoteSelect:      s_view = View::StdVoteSelect; break;
    case ws::Phase::VoteConfirm:     s_view = View::StdVoteConfirm; break;
    case ws::Phase::VoteDone:        s_view = View::StdVoteDone; break;
    case ws::Phase::RunoffReady:     s_view = View::StdRunoffReady; s_page = 0; break;
    case ws::Phase::ExecutionReady:  s_view = View::StdExecReady; break;
    case ws::Phase::ExecutionAnnounce: s_view = View::StdExecAnnounce; break;
    case ws::Phase::FinalReady:      s_view = View::StdFinalReady; break;
    // NVS への書き込みは数十 ms 止まる。秘密を消してから行いたいので tick に任せる
    case ws::Phase::Revealed:        s_view = View::StdResult; s_page = 0;
                                     s_clear_armed_pending = true; s_play_pending = true; break;
    case ws::Phase::Aborted:         s_view = View::Aborted; s_clear_armed_pending = true; break;
    }
}

void adoptPhaseView()
{
    if (stdMode()) {
        adoptStdPhaseView();
        return;
    }
    const PublicView pv = s_engine.publicView();
    if (pv.paused) {
        // 一時停止中の小画面（本人確認・中断確認）は維持する
        if (s_view != View::Pause && s_view != View::PauseOwner &&
            s_view != View::PauseAbortConfirm) {
            s_view = View::Pause;
        }
        return;
    }
    switch (pv.phase) {
    case Phase::Idle:         s_view = View::Lobby; break;
    case Phase::NightHandoff: s_view = View::NightHandoff; break;
    case Phase::RoleCheck:    s_view = View::RoleCheck; break;
    case Phase::NightTarget:  s_view = View::NightTarget; s_page = 0; s_pending_target = NONE; break;
    case Phase::NightResult:  s_view = View::NightResult; break;
    case Phase::NightDone:    s_view = View::NightDone; break;
    case Phase::DayReady:     s_view = View::DayReady; break;
    case Phase::DayTalk:      s_view = View::DayTalk; break;
    case Phase::VoteReady:    s_view = View::VoteReady; break;
    case Phase::VoteHandoff:  s_view = View::VoteHandoff; s_page = 0; break;
    case Phase::VoteSelect:   s_view = View::VoteSelect; break;
    case Phase::VoteConfirm:  s_view = View::VoteConfirm; break;
    case Phase::VoteDone:     s_view = View::VoteDone; break;
    case Phase::RunoffReady:  s_view = View::RunoffReady; s_page = 0; break;
    case Phase::RunoffTalk:   s_view = View::RunoffTalk; break;
    case Phase::FinalReady:   s_view = View::FinalReady; break;
    // NVS への書き込みは数十 ms 止まることがある。秘密を消してからにしたいので tick に任せる
    // （s_play_pending = 結果まで進んだ局を「遊んだ 1 回」として数える。無効になった局は数えない）
    case Phase::Revealed:     s_view = View::Result; s_page = 0; s_clear_armed_pending = true;
                              s_play_pending = true; break;
    case Phase::Aborted:      s_view = View::Aborted; s_clear_armed_pending = true; break;
    }
}

// ---------------------------------------------------------------------------
// 秘密の表示・消去
// ---------------------------------------------------------------------------
// --- 通常ルールの秘密（役職・今夜やること・仲間と仲間の選択・占い結果・投票先）------
// 呼ばれるのは showSecret() の中だけ。body には秘密が入るので、呼び出し側が必ず消す
void fillStdSecret(char *body, size_t cap, const char *&role_text, const char *&role_mark,
                   bool &ok)
{
    ok = false;
    const ws::PublicView pv = s_std.publicView();
    char label[24];
    char name[24];
    label[0] = name[0] = '\0';

    if (s_view == View::StdNightBrief) {
        ws::Brief b;
        if (s_std.readBrief(pv.actor, b) != ws::Err::Ok || b.role == ws::Role::Empty) {
            return;
        }
        role_text = b.role == ws::Role::Wolf ? str("role.wolf.name")
                  : b.role == ws::Role::Seer ? str("role.seer.name")
                                             : str("role.villager.name");
        role_mark = b.role == ws::Role::Wolf ? content::kIconWolf
                  : b.role == ws::Role::Seer ? content::kIconSeer
                                             : content::kIconVillager;
        if (b.role == ws::Role::Villager) {
            std::snprintf(body, cap, "%s", str("std.brief.villager"));
        } else if (b.role == ws::Role::Seer) {
            std::snprintf(body, cap, "%s", str("std.brief.seer"));
        } else if (b.partner_pick != ws::NONE && b.partner_pick_by != ws::NONE &&
                   !b.first_night) {
            // 先に操作した仲間の選択を見せる（最終的な襲撃先は後に選んだほうになる）。
            // 文言側が「仲間の{name}さんは」なので、{name} は敬称なしの名前を入れる
            std::snprintf(name, sizeof(name), "%s", seatName(b.partner_pick_by));
            stdSeatLabel(label, sizeof(label), b.partner_pick);
            const Subst subs[] = {{"name", name}, {"target_label", label}};
            fillText(body, cap, str("std.brief.wolf.pick"), subs, 2);
        } else {
            std::snprintf(body, cap, "%s",
                          str(b.first_night ? "std.brief.wolf.first" : "std.brief.wolf"));
            // 仲間は最大 2 人。名前が長いので 1 行に 1 人だけ足す
            for (uint8_t a = 0; a < pv.player_count; ++a) {
                if ((b.partners & ws::bit(a)) == 0) {
                    continue;
                }
                std::snprintf(name, sizeof(name), "%s", seatName(a));
                const Subst subs[] = {{"name", name}};
                char mate[48];
                fillText(mate, sizeof(mate), str("std.brief.wolf.mate"), subs, 1);
                const size_t used = std::strlen(body);
                std::snprintf(body + used, cap > used ? cap - used : 0, "\n%s", mate);
            }
        }
        ok = true;
    } else if (s_view == View::StdNightResult) {
        ws::NightOutcome o;
        if (s_std.readNightResult(pv.actor, o) != ws::Err::Ok) {
            return;
        }
        if (o.role == ws::Role::Seer && o.finding != ws::Finding::None) {
            stdSeatLabel(label, sizeof(label), o.target);
            const Subst subs[] = {{"target_label", label}};
            fillText(body, cap,
                     str(o.finding == ws::Finding::Wolf ? "std.night.result.wolf"
                                                        : "std.night.result.not_wolf"),
                     subs, 1);
        } else if (o.role == ws::Role::Wolf) {
            std::snprintf(body, cap, "%s",
                          str(o.first_night ? "std.night.result.first" : "std.night.result.attack"));
        } else {
            std::snprintf(body, cap, "%s", str("std.night.result.none"));
        }
        ok = true;
    } else if (s_view == View::StdVoteConfirm) {
        int8_t target = ws::NONE;
        if (s_std.pendingVote(pv.actor, target) != ws::Err::Ok) {
            return;
        }
        stdSeatLabel(label, sizeof(label), target);
        const Subst subs[] = {{"target_label", label}};
        fillText(body, cap, str("vote.confirm.target"), subs, 1);
        ok = true;
    }

    // 手元の一時コピー（調べた相手・投票先・仲間の名前）も消す
    volatile char *w1 = label;
    for (size_t i = 0; i < sizeof(label); ++i) w1[i] = '\0';
    volatile char *w2 = name;
    for (size_t i = 0; i < sizeof(name); ++i) w2[i] = '\0';
}

void showSecret()
{
    if (s_secret_label == nullptr) {
        return;
    }
    const PublicView pv = s_engine.publicView();
    char body[256];
    char label[24];       // 調べた相手・投票先。これも秘密なので後で消す
    body[0] = '\0';
    label[0] = '\0';
    const char *role_text = nullptr;
    const char *role_mark = nullptr;

    if (s_view == View::StdNightBrief || s_view == View::StdNightResult ||
        s_view == View::StdVoteConfirm) {
        bool ok = false;
        fillStdSecret(body, sizeof(body), role_text, role_mark, ok);
        if (!ok) {
            volatile char *kill = body;
            for (size_t i = 0; i < sizeof(body); ++i) kill[i] = '\0';
            return;
        }
    } else if (s_view == View::RoleCheck) {
        Role role = Role::Empty;
        if (s_engine.readRole(pv.actor, role) != Err::Ok || role == Role::Empty) {
            return;
        }
        role_text = roleName(role);
        role_mark = roleIcon(role);
        const char *key = role == Role::Wolf ? "role.wolf.body"
                        : role == Role::Seer ? "role.seer.body" : "role.villager.body";
        std::snprintf(body, sizeof(body), "%s", str(key));
    } else if (s_view == View::NightResult) {
        NightInfo info;
        if (s_engine.readNight(pv.actor, info) != Err::Ok) {
            return;
        }
        if (info.finding == Finding::None) {
            std::snprintf(body, sizeof(body), "%s", str("night.result.none"));
        } else {
            targetLabel(label, sizeof(label), info.target);
            const Subst subs[] = {{"target_label", label}};
            fillText(body, sizeof(body),
                     str(info.finding == Finding::Wolf ? "night.result.wolf" : "night.result.not_wolf"),
                     subs, 1);
        }
    } else if (s_view == View::VoteConfirm) {
        int8_t target = NONE;
        if (s_engine.pendingVote(pv.actor, target) != Err::Ok) {
            return;
        }
        if (target == PEACE) {
            std::snprintf(body, sizeof(body), "%s", str("vote.confirm.peace"));
        } else {
            targetLabel(label, sizeof(label), target);
            const Subst subs[] = {{"target_label", label}};
            fillText(body, sizeof(body), str("vote.confirm.target"), subs, 1);
        }
    } else {
        return;
    }

    if (s_cover_label != nullptr) {
        lv_obj_add_flag(s_cover_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_role_label != nullptr && role_text != nullptr) {
        lv_label_set_text(s_role_label, role_text);
    }
    if (s_role_icon != nullptr && role_mark != nullptr) {
        lv_label_set_text(s_role_icon, role_mark);
    }
    lv_label_set_text(s_secret_label, body);
    // ラベルは空（1 行分）の状態で枠の中央に置かれているので、行数が決まった今、置き直す。
    // これをしないと 4 行の本文が中央から下へ伸び、「押して見る」のボタンに重なる
    {
        const layout::Rect &r = layout::kSecretBody;
        const int16_t text_h = (int16_t)(ct_font_jp_22.line_height * lineCount(body));
        lv_obj_set_height(s_secret_label, text_h < r.h ? text_h : r.h);
        lv_obj_set_pos(s_secret_label, r.x, (int16_t)(text_h < r.h ? r.y + (r.h - text_h) / 2 : r.y));
    }
    // 手元の一時コピーも消しておく（スタックに役職名や調べた相手が残らないように）
    volatile char *wipe = body;
    for (size_t i = 0; i < sizeof(body); ++i) {
        wipe[i] = '\0';
    }
    volatile char *wipe_label = label;
    for (size_t i = 0; i < sizeof(label); ++i) {
        wipe_label[i] = '\0';
    }
    s_secret_shown = true;
    s_secret_on_screen = true;
}

void hideSecretNow()
{
    blankSecretLabels();
    neutralize();               // 実際に中立の絵がパネルへ出るまで待つ
    s_gate.enter(s_fence.epoch());
}

// ---------------------------------------------------------------------------
// 一時停止・離脱
// ---------------------------------------------------------------------------
void enterPause()
{
    blankSecretLabels();
    s_gate.close();
    setView(View::Pause);
    s_neutral_pending = true;
    // Engine を止めるのは中立化の後（tick で neutralize してから pause する）
}

void leaveGame(bool to_home)
{
    // 進行中に画面を離れたら局は無効。秘密はここで消える（画面破棄時にも念のため行う）
    blankSecretLabels();
    s_gate.close();
    // 遷移アニメーションの間は何も動かさない（作り直しも中立化も不要）
    if (s_tick != nullptr) {
        lv_timer_del(s_tick);
        s_tick = nullptr;
    }
    // 秘密が画面に残っていたら、遷移を始める前に消えたことを確かめる。
    // 監視タスクが明かりを切っていた場合も、ここが最後に戻せる場所
    if (s_secret_on_screen || backlightIsCut()) {
        neutralize();
        backlightRestoreIfCut(s_fence.epoch());
    }
    if (anyGameActive()) {
        s_engine.abort(s_engine.stamp(), AbortReason::User);
        s_std.abort(s_std.stamp(), ws::AbortReason::User);
        clearArmed();
    }
    // 終わった局（Revealed / Aborted）でも Engine の中には役職と票が残っている。
    // active() では拾えないので、離れるときは必ず両方 Idle に戻して消す
    s_engine.reset(s_engine.stamp());
    s_std.reset(s_std.stamp());
    s_clear_armed_pending = false;
    if (to_home) {
        port().openExistingCafePanel();   // 既にある HOME（カフェ）画面へ戻る
    } else {
        ui::pop();
    }
}

// ---------------------------------------------------------------------------
// 開始（port_contract.hpp が定める順番どおりに行う）
// ---------------------------------------------------------------------------
bool randomWordThunk(void *ctx, uint32_t &out)
{
    (void)ctx;
    return port().randomWord(out);
}

void doStart()
{
    // 1) 人数を確かめる
    if (!validPlayers(s_players)) {
        showError("error.roster");
        return;
    }
    // 2) Engine が Idle でないと配れない。NVS に「配った」印を書く前に必ず戻しておく
    //    （Revealed / Aborted のまま armed を書くと、偽の「電源断」表示が残る）
    if (!s_engine.active() && s_engine.publicView().phase != Phase::Idle) {
        s_engine.reset(s_engine.stamp());
        // この reset は画面の切り替えを意味しない。revision を見たことにしておかないと、
        // このあと失敗したときに tick がお知らせ画面を人数決めへ戻してしまう
        const Stamp fresh = s_engine.stamp();
        s_seen_revision = fresh.revision;
        s_seen_generation = fresh.generation;
    }
    if (s_engine.publicView().phase != Phase::Idle) {
        showError("error.storage");
        return;
    }
    // 3) 偏りのない配り方の番号 k を作る。k はどこにも出さない（ログ厳禁）
    uint32_t k = 0;
    if (!uniformBelow(dealCount(s_players), randomWordThunk, nullptr, k)) {
        showError("error.random");
        return;
    }
    // 4) 「配った」印と公開設定を NVS に書き、読み戻して一致を確かめる
    PublicMeta meta;
    meta.armed = true;
    meta.sound = false;
    meta.players = s_players;
    meta.discussion_s = 0;
    meta.flavor_seq = s_flavor_seq + 1;
    std::array<uint8_t, 20> bytes{};
    if (!encodeMeta(meta, bytes) || !port().writePublicMetaAndReadBack(bytes)) {
        volatile uint32_t *kill = &k;
        *kill = 0;
        showError("error.storage");
        return;
    }
    s_flavor_seq = meta.flavor_seq;
    // 5) ここで初めて配役する
    const Err e = s_engine.start(s_engine.stamp(), s_players, (uint16_t)k,
                                 port().monotonicNowMs(), 0);
    // 6) 手元の k を消す
    volatile uint32_t *kill = &k;
    *kill = 0;
    if (e != Err::Ok) {
        // 配れなかったので「配った」印を取り消す（次回の偽の「電源断」表示を防ぐ）
        clearArmed();
        showError("error.storage");
        return;
    }
    // 7) 夜の受け渡しを出したあと、実際に画面へ出るまで待つ（tick が行う）
    s_neutral_pending = true;
}

// 遊び方を切り替える。使わないほうのコアは必ず空にして、役職も票も残さない
void setMode(Mode m)
{
    s_mode = m;
    if (m == Mode::Standard) {
        if (s_engine.active()) {
            s_engine.abort(s_engine.stamp(), AbortReason::User);
        }
        s_engine.reset(s_engine.stamp());
        const ws::Stamp st = s_std.stamp();
        s_seen_revision = st.revision;
        s_seen_generation = st.generation;
    } else {
        if (s_std.active()) {
            s_std.abort(s_std.stamp(), ws::AbortReason::User);
        }
        s_std.reset(s_std.stamp());
        const Stamp st = s_engine.stamp();
        s_seen_revision = st.revision;
        s_seen_generation = st.generation;
    }
    writeLastMode((uint8_t)m);
}

// 通常ルールの開始。手順は port_contract.hpp の 1)〜7) と同じ
void doStartStd()
{
    if (!ws::validPlayers(s_players)) {
        showError("error.roster");
        return;
    }
    if (!s_std.active() && s_std.publicView().phase != ws::Phase::Idle) {
        s_std.reset(s_std.stamp());
        const ws::Stamp fresh = s_std.stamp();
        s_seen_revision = fresh.revision;
        s_seen_generation = fresh.generation;
    }
    if (s_std.publicView().phase != ws::Phase::Idle) {
        showError("error.storage");
        return;
    }
    uint32_t k = 0;
    if (!ws::uniformBelow(ws::dealCount(s_players), randomWordThunk, nullptr, k)) {
        showError("error.random");
        return;
    }
    // 「配った」印はワンナイトと共通（電源断の検出に使う公開情報だけ）
    PublicMeta meta;
    meta.armed = true;
    meta.sound = false;
    meta.players = s_players;
    meta.discussion_s = 0;
    meta.flavor_seq = s_flavor_seq + 1;
    std::array<uint8_t, 20> bytes{};
    if (!encodeMeta(meta, bytes) || !port().writePublicMetaAndReadBack(bytes)) {
        volatile uint32_t *kill = &k;
        *kill = 0;
        showError("error.storage");
        return;
    }
    s_flavor_seq = meta.flavor_seq;
    const ws::Err e = s_std.start(s_std.stamp(), s_players, (uint16_t)k,
                                  port().monotonicNowMs());
    volatile uint32_t *kill = &k;
    *kill = 0;
    if (e != ws::Err::Ok) {
        clearArmed();
        showError("error.storage");
        return;
    }
    s_neutral_pending = true;
}

// ---------------------------------------------------------------------------
// 入力
// ---------------------------------------------------------------------------
void targetCb(lv_event_t *e)
{
    const int target = (int)(intptr_t)lv_event_get_user_data(e);
    const PublicView pv = s_engine.publicView();
    if (s_view == View::StdNightTarget) {
        const ws::PublicView sv = s_std.publicView();
        if (!s_std.legalNightTarget(sv.actor, target)) {
            ui::showToast(s_screen, str("error.target"));
            return;
        }
        s_pending_target = target;
        setView(View::StdNightTargetConfirm);
        return;
    }
    if (s_view == View::StdVoteSelect) {
        const ws::PublicView sv = s_std.publicView();
        if (s_std.selectVote(s_std.stamp(), sv.actor, target) != ws::Err::Ok) {
            ui::showToast(s_screen, str("error.target"));
        }
        return;
    }
    if (s_view == View::NightTarget) {
        if (!legalNightTarget(pv.player_count, pv.actor, target)) {
            ui::showToast(s_screen, str("error.target"));
            return;
        }
        s_pending_target = target;
        setView(View::NightTargetConfirm);
        return;
    }
    if (s_view == View::VoteSelect) {
        // 選んだ時点で Engine が VoteConfirm へ進む（保留の票はコアの中だけに置く）
        if (s_engine.selectVote(s_engine.stamp(), pv.actor, target) != Err::Ok) {
            ui::showToast(s_screen, str("error.target"));
        }
    }
}

void actionCb(lv_event_t *e)
{
    const Act act = (Act)(intptr_t)lv_event_get_user_data(e);
    const Stamp st = s_engine.stamp();
    const PublicView pv = s_engine.publicView();
    const uint64_t now = port().monotonicNowMs();

    // 秘密の画面から動くときは、まず文字列を消してから（消去の確認は tick が行う）
    if (isPrivateView(s_view)) {
        blankSecretLabels();
    }

    switch (act) {
    case Act::CountMinus:
        if (s_players > MIN_PLAYERS) { --s_players; s_dirty = true; }
        break;
    case Act::CountPlus:
        if (s_players < MAX_PLAYERS) { ++s_players; s_dirty = true; }
        break;
    case Act::LobbyStart:
        // 人数のつぎは遊び方を選ぶ（3 人はワンナイトだけ）
        s_doc_page = 0;
        setView(View::ModeSelect);
        break;

    case Act::ModeStd:
        setMode(Mode::Standard);
        s_doc_page = 0;
        setView(View::StdStory);
        break;
    case Act::ModeOne:
        setMode(Mode::OneNight);
        s_doc_page = 0;
        setView(View::Story);
        break;
    case Act::ModeBack:
        setView(View::Lobby);
        break;

    // --- 通常ルールのお話と約束 -------------------------------------------
    case Act::StdStoryPrev:
        if (s_doc_page > 0) { --s_doc_page; s_dirty = true; }
        else { setView(View::ModeSelect); }
        break;
    case Act::StdStoryNext:
        if (s_doc_page + 1 < content::kStoryStdCount) { ++s_doc_page; s_dirty = true; }
        else { s_doc_page = 0; setView(View::StdBrief); }
        break;
    case Act::StdStorySkip:
        s_doc_page = 0;
        setView(View::StdBrief);
        break;
    case Act::StdBriefPrev:
        if (s_doc_page > 0) {
            --s_doc_page;
            s_dirty = true;
        } else {
            s_doc_page = (uint8_t)(content::kStoryStdCount - 1);
            setView(View::StdStory);
        }
        break;
    case Act::StdBriefNext:
        if (s_doc_page + 1 < content::kBriefStdCount) { ++s_doc_page; s_dirty = true; }
        break;
    case Act::StdBriefDone:
        s_doc_page = 0;
        setView(View::SetupConfirm);
        break;
    case Act::LobbyLeave:
        leaveGame(false);
        break;
    case Act::LobbyHelp:
        s_doc_page = 0;
        setView(View::Tutorial);
        break;

    case Act::StoryPrev:
        // 1 ページ目より前は遊び方の選択へ戻る
        if (s_doc_page > 0) { --s_doc_page; s_dirty = true; }
        else { setView(View::ModeSelect); }
        break;
    case Act::StoryNext:
        // 最後のページの「次へ」は、そのまま約束（必読）へ進む
        if (s_doc_page + 1 < content::kStoryCount) { ++s_doc_page; s_dirty = true; }
        else { s_doc_page = 0; setView(View::Brief); }
        break;
    case Act::StorySkip:
        s_doc_page = 0;
        setView(View::Brief);
        break;

    case Act::BriefPrev:
        // 1 ページ目より前はお話の最後のページへ戻る（行き止まりを作らない）
        if (s_doc_page > 0) {
            --s_doc_page;
            s_dirty = true;
        } else {
            s_doc_page = (uint8_t)(content::kStoryCount - 1);
            setView(View::Story);
        }
        break;
    case Act::BriefNext:
        if (s_doc_page + 1 < content::kMandatoryBriefCount) { ++s_doc_page; s_dirty = true; }
        break;
    case Act::BriefDone:
        s_doc_page = 0;
        setView(View::SetupConfirm);
        break;

    case Act::SetupBack:
        // 1 ページ目より前は必読の最後のページへ戻る（遊び方ごとに戻り先が違う）
        if (s_doc_page > 0) {
            --s_doc_page;
            s_dirty = true;
        } else if (stdMode()) {
            s_doc_page = (uint8_t)(content::kBriefStdCount - 1);
            setView(View::StdBrief);
        } else {
            s_doc_page = (uint8_t)(content::kMandatoryBriefCount - 1);
            setView(View::Brief);
        }
        break;
    case Act::SetupNext:
        if (s_doc_page + 1 < kSetupPages) { ++s_doc_page; s_dirty = true; }
        break;
    case Act::SetupStart:
        // 配る前に、この局で使う席のキャラクターを覚えてもらう
        s_page = 0;
        setView(View::Roster);
        break;

    case Act::RosterBack:
        s_page = 0;
        s_doc_page = (uint8_t)(kSetupPages - 1);
        setView(View::SetupConfirm);
        break;
    case Act::RosterOk:
        if (stdMode()) { doStartStd(); } else { doStart(); }
        break;

    case Act::TutorialPrev:
        // 1 ページ目より前はロビーへ戻る
        if (s_doc_page > 0) { --s_doc_page; s_dirty = true; }
        else { s_doc_page = 0; setView(View::Lobby); }
        break;
    case Act::TutorialNext:
        if (s_doc_page + 1 < kTutorialTotal) { ++s_doc_page; s_dirty = true; }
        break;
    case Act::TutorialExit:
        s_doc_page = 0;
        setView(View::Lobby);
        break;

    case Act::NoticeOk:
        setView(View::Lobby);
        break;

    case Act::NightReceive:
        s_engine.receiveNight(st, pv.actor);
        break;
    case Act::RoleAck:
        // 一度も見ていないと Unseen が返る。異常ではないので案内を出し直す
        if (s_engine.acknowledgeRole(st, pv.actor) == Err::Unseen) {
            ui::showToast(s_screen, str("role.cover"));
        }
        s_neutral_pending = true;
        break;

    case Act::TargetOk:
        if (s_engine.chooseNight(st, pv.actor, s_pending_target) != Err::Ok) {
            ui::showToast(s_screen, str("error.target"));
        }
        s_pending_target = NONE;
        break;
    case Act::TargetChange:
        s_pending_target = NONE;
        setView(View::NightTarget);
        break;

    case Act::NightAck:
        if (s_engine.acknowledgeNight(st, pv.actor) == Err::Unseen) {
            ui::showToast(s_screen, str("role.cover"));
        }
        s_neutral_pending = true;
        break;
    case Act::NightPass:
        s_engine.passNight(st, pv.actor);
        break;

    case Act::DayStart:
    case Act::RunoffStart:
        // 失敗を握りつぶすとボタンが効かないように見えるので、他と同じく案内を出す
        if (s_engine.startTalk(st, now) != Err::Ok) {
            ui::showToast(s_screen, str("error.stale"));
        }
        break;
    case Act::DayExtend:
        s_engine.extendTalk(st);
        break;
    case Act::DayFinish:
    case Act::StdDayFinish:
        setView(View::DayFinishConfirm);
        break;
    case Act::DayFinishOk:
        if (stdMode()) {
            s_std.finishTalk(s_std.stamp(), true);
        } else {
            s_engine.finishTalk(st, true);
        }
        break;
    case Act::DayFinishNo:
        if (stdMode()) {
            setView(View::StdDayTalk);
        } else {
            setView(pv.phase == Phase::RunoffTalk ? View::RunoffTalk : View::DayTalk);
        }
        break;

    case Act::VoteBegin:
        s_engine.beginVote(st);
        break;
    case Act::VoteReceive:
        s_engine.receiveVote(st, pv.actor);
        break;
    case Act::VoteCommit:
        if (s_engine.confirmVote(st, pv.actor) == Err::Unseen) {
            ui::showToast(s_screen, str("vote.confirm.cover"));
        }
        s_neutral_pending = true;
        break;
    case Act::VoteChange:
        s_engine.changeVote(st, pv.actor);
        s_neutral_pending = true;
        break;
    case Act::VotePass:
        s_engine.passVote(st, pv.actor);
        break;

    case Act::Reveal:
        s_engine.reveal(st, true);
        break;

    case Act::PagePrev:
        if (s_page > 0) { --s_page; s_dirty = true; }
        break;
    case Act::PageNext:
        ++s_page;
        s_dirty = true;
        break;

    case Act::ResultAgain:
        // 人数はそのままに、役職を配り直す（Engine を最初の状態へ戻す）
        s_engine.reset(s_engine.stamp());
        s_page = 0;
        break;
    case Act::StdResultAgain:
        s_std.reset(s_std.stamp());
        s_page = 0;
        break;
    case Act::ResultExit:
        leaveGame(true);
        break;

    // --- 通常ルールの進行 -------------------------------------------------
    case Act::StdNightReceive:
        s_std.receiveNight(s_std.stamp(), s_std.publicView().actor);
        break;
    case Act::StdBriefAck:
        if (s_std.acknowledgeBrief(s_std.stamp(), s_std.publicView().actor) == ws::Err::Unseen) {
            ui::showToast(s_screen, str("role.cover"));
        }
        s_neutral_pending = true;
        break;
    case Act::StdTargetOk:
        if (s_std.chooseNight(s_std.stamp(), s_std.publicView().actor, s_pending_target) !=
            ws::Err::Ok) {
            // 人狼が仲間を選んだときもここに来る（仲間の名前は直前の秘密画面に出ている）。
            // 場面は変わらないので、選び直せるように自分で対象の画面へ戻す
            ui::showToast(s_screen, str("error.target"));
            setView(View::StdNightTarget);
        }
        s_pending_target = NONE;
        break;
    case Act::StdTargetChange:
        s_pending_target = NONE;
        setView(View::StdNightTarget);
        break;
    case Act::StdNightAck:
        if (s_std.acknowledgeNightResult(s_std.stamp(), s_std.publicView().actor) ==
            ws::Err::Unseen) {
            ui::showToast(s_screen, str("role.cover"));
        }
        s_neutral_pending = true;
        break;
    case Act::StdNightPass:
        s_std.passNight(s_std.stamp(), s_std.publicView().actor);
        break;
    case Act::StdMorningOpen:
        s_std.openMorning(s_std.stamp(), true);
        break;
    case Act::StdMorningNext:
        if (s_std.closeMorning(s_std.stamp(), now) != ws::Err::Ok) {
            ui::showToast(s_screen, str("error.stale"));
        }
        break;
    case Act::StdDayExtend:
        s_std.extendTalk(s_std.stamp());
        break;
    case Act::StdVoteBegin:
        s_std.beginVote(s_std.stamp());
        break;
    case Act::StdVoteReceive:
        s_std.receiveVote(s_std.stamp(), s_std.publicView().actor);
        break;
    case Act::StdVoteCommit:
        if (s_std.confirmVote(s_std.stamp(), s_std.publicView().actor) == ws::Err::Unseen) {
            ui::showToast(s_screen, str("vote.confirm.cover"));
        }
        s_neutral_pending = true;
        break;
    case Act::StdVoteChange:
        s_std.changeVote(s_std.stamp(), s_std.publicView().actor);
        s_neutral_pending = true;
        break;
    case Act::StdVotePass:
        s_std.passVote(s_std.stamp(), s_std.publicView().actor);
        break;
    case Act::StdRunoffStart:
        s_std.beginRunoff(s_std.stamp());
        break;
    case Act::StdExecOpen:
        s_std.openExecution(s_std.stamp(), true);
        break;
    case Act::StdExecNext:
        s_std.closeExecution(s_std.stamp());
        break;
    case Act::StdReveal:
        s_std.reveal(s_std.stamp(), true);
        break;

    case Act::Pause:
        enterPause();
        break;
    case Act::PauseResume: {
        // 秘密の画面に戻るときだけ「本人ですか？」をはさむ
        const ws::Phase sp = s_std.publicView().phase;
        const bool secret_phase =
            stdMode() ? (sp == ws::Phase::NightBrief || sp == ws::Phase::NightResult ||
                         sp == ws::Phase::VoteConfirm)
                      : (pv.phase == Phase::RoleCheck || pv.phase == Phase::NightResult ||
                         pv.phase == Phase::VoteConfirm);
        if (isPrivateView(s_view) || secret_phase) {
            setView(View::PauseOwner);
        } else if (stdMode()) {
            s_std.resume(s_std.stamp(), now);
        } else {
            s_engine.resume(s_engine.stamp(), now);
        }
        break;
    }
    case Act::PauseResumeOk:
        if (stdMode()) { s_std.resume(s_std.stamp(), now); }
        else { s_engine.resume(s_engine.stamp(), now); }
        break;
    case Act::PauseResumeNo:
        setView(View::Pause);
        break;
    case Act::PauseCoffee:
        // HOME の「+1」と同じ処理（記録・通知も同じ経路で行われる）
        home::addOneCup();
        ui::showToast(s_screen, str("pause.coffee"));
        break;
    case Act::PauseAbort:
        setView(View::PauseAbortConfirm);
        break;
    case Act::PauseAbortOk:
        // 一時停止中は abort が通らないので、戻してから無効にする
        if (stdMode()) {
            s_std.resume(s_std.stamp(), now);
            s_std.abort(s_std.stamp(), ws::AbortReason::User);
        } else {
            s_engine.resume(s_engine.stamp(), now);
            s_engine.abort(s_engine.stamp(), AbortReason::User);
        }
        break;
    case Act::PauseAbortNo:
        setView(View::Pause);
        break;

    case Act::AbortedAgain:
        if (stdMode()) { s_std.reset(s_std.stamp()); }
        else { s_engine.reset(s_engine.stamp()); }
        break;
    case Act::AbortedExit:
        leaveGame(true);
        break;

    case Act::ErrorBack:
        s_error_key = nullptr;
        // 終わった局（Revealed / Aborted）が残っていると次の開始が通らない
        if (!s_engine.active()) {
            s_engine.reset(s_engine.stamp());
        }
        if (!s_std.active()) {
            s_std.reset(s_std.stamp());
        }
        setView(View::Lobby);
        break;
    }
}

// ---------------------------------------------------------------------------
// 20ms ごとの処理
// ---------------------------------------------------------------------------
void updateGate(uint64_t now, const TouchSample &sample)
{
    if (!isPrivateView(s_view) || enginePaused()) {
        return;
    }
    bool want = false;
    if (!sample.valid || sample.multiple_points) {
        // 生データが無い／複数点で触れている間は見せない。
        // close() を呼ぶと「全部離すまで」再表示されない状態になる
        s_gate.close();
    } else {
        want = s_gate.update(s_fence.epoch(), now, sample.sampled_ms, sample.down,
                             sample.inside_hold);
    }
    if (want == s_secret_shown) {
        return;
    }
    if (want) {
        showSecret();
    } else {
        hideSecretNow();
    }
}

void updateTimer(uint64_t remaining_ms)
{
    if (s_timer_label == nullptr) {
        return;
    }
    const uint32_t total_s = (uint32_t)((remaining_ms + 999) / 1000);
    // 20ms ごとに書き換えると毎回描き直しになるので、秒が変わったときだけ触る
    if (total_s == s_shown_seconds) {
        return;
    }
    s_shown_seconds = total_s;
    lv_label_set_text_fmt(s_timer_label, "%u:%02u", (unsigned)(total_s / 60),
                          (unsigned)(total_s % 60));
}

void tickCb(lv_timer_t *t)
{
    (void)t;
    const uint64_t now = port().monotonicNowMs();
    s_last_tick_ms = nowTicks();

    // 動いているのは 1 つだけ。止まっているほうは Idle のままなので tick しても何も起きない
    s_engine.tick(now);
    s_std.tick(now);

    // 版数はモードごとに別（generation / revision の系列が違う）
    uint64_t rev = 0, gen = 0;
    if (stdMode()) {
        const ws::Stamp sst = s_std.stamp();
        rev = sst.revision; gen = sst.generation;
    } else {
        const Stamp ost = s_engine.stamp();
        rev = ost.revision; gen = ost.generation;
    }
    if (rev != s_seen_revision || gen != s_seen_generation) {
        s_seen_revision = rev;
        s_seen_generation = gen;
        adoptPhaseView();
        s_dirty = true;
    }

    bool changed = false;
    if (s_dirty) {
        rebuild();
        s_dirty = false;
        changed = true;
    }
    if (s_neutral_pending) {
        neutralize();
        s_neutral_pending = false;
        changed = true;
        // 中立化のあとに Engine を止める（一時停止を押したとき）
        if (s_view == View::Pause && !enginePaused()) {
            if (stdMode() && s_std.active()) {
                s_std.pause(s_std.stamp(), now);
            } else if (!stdMode() && s_engine.active()) {
                s_engine.pause(s_engine.stamp(), now);
            }
        }
    }
    // 監視タスクがバックライトを切っていたら、中立を確かめてから戻す
    if (backlightIsCut()) {
        blankSecretLabels();
        neutralize();                    // ここで s_last_tick_ms も打ち直される
        if (backlightRestoreIfCut(s_fence.epoch())) {
            changed = true;
        }
        // 点け直した直後にまた「止まっている」と判定されないよう、時刻を入れ直す。
        // それでも tick が遅れていれば、監視タスクは次の 50ms でもう一度切る
        s_last_tick_ms = nowTicks();
    }
    // 「配った」印を消すのは、秘密が画面から消えたあと（NVS 書き込みは時間がかかる）
    if (s_clear_armed_pending && !s_secret_on_screen) {
        clearArmed();
        s_clear_armed_pending = false;
        s_last_tick_ms = nowTicks();
    }
    // 遊んだ回数を 1 だけ数える。数えるのは**回数と人数**だけで、役職・投票・勝敗は残さない
    if (s_play_pending && !s_secret_on_screen) {
        s_play_pending = false;
        char note[32];
        if (stdMode()) {
            const ws::PublicView sv = s_std.publicView();
            std::snprintf(note, sizeof(note), "wolf std players=%u days=%u",
                          (unsigned)sv.player_count, (unsigned)sv.day);
        } else {
            std::snprintf(note, sizeof(note), "wolf players=%u",
                          (unsigned)s_engine.publicView().player_count);
        }
        cup::stats::gamePlayed(cup::GameId::Werewolf, note);
        s_last_tick_ms = nowTicks();
    }

    // ここまでで数十 ms 止まっていることがあるので、時刻を取り直す。
    // 古い now のままだとタッチの採取時刻の方が新しくなり、SecretGate が
    // 「時計が戻った」と判断して閉じてしまう
    const uint64_t gate_now = port().monotonicNowMs();

    if (changed) {
        // 中立化で epoch が進むので、秘密の画面なら押し直しから受け付ける
        if (isPrivateView(s_view)) {
            s_gate.enter(s_fence.epoch());
        } else {
            s_gate.close();
        }
        s_last_touch_ms = gate_now;
    }

    // タッチの生データは 1 tick に 1 回だけ取る（覗き見防止の判定と無操作の計測に使う）
    const TouchSample sample = port().latestFreshDriverSample();
    if (sample.valid && sample.down) {
        s_last_touch_ms = gate_now;
    }

    updateGate(gate_now, sample);
    updateTimer(stdMode() ? s_std.publicView().remaining_ms
                          : s_engine.publicView().remaining_ms);

    // 1 人が持っている場面で無操作が続いたら自動で一時停止する
    if (isSoloView(s_view) && !enginePaused() && s_last_touch_ms != 0 &&
        gate_now > s_last_touch_ms && gate_now - s_last_touch_ms > rules::kPrivateIdlePauseMs) {
        enterPause();
    }

    // 結果を出したまま放置されたらカフェへ戻る（「カフェへ」を押したのと同じ経路）。
    // leaveGame() がこのタイマー自身を消すので、そのあとは何も触らずに抜ける
    if ((s_view == View::Result || s_view == View::StdResult) &&
        s_last_touch_ms != 0 && gate_now > s_last_touch_ms &&
        gate_now - s_last_touch_ms > rules::kResultIdleCloseMs) {
        leaveGame(true);
        return;
    }

    // 1 回の tick が長引くことがある（作り直し・中立化・NVS 書き込み）。
    // 監視タスクが健全な tick を「止まった」と誤判定しないよう、最後にも打ち直す
    s_last_tick_ms = nowTicks();
}

// ---------------------------------------------------------------------------
// 覗き見防止の監視タスク
//
// 20ms の tick が 250ms 以上止まったのに秘密が出ていたら、指を離しても消えない。
// その場合はバックライトを切ってしまうのが唯一確実な手段。戻すのは tick の側。
// ---------------------------------------------------------------------------
void watchdogTask(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (!s_secret_on_screen) {
            continue;
        }
        const uint32_t last = s_last_tick_ms;
        // 32bit の引き算なので、49 日で折り返しても差は正しく出る
        if (last != 0 && (uint32_t)(nowTicks() - last) > rules::kPrivacyHideStaleMaxMs) {
            // 消灯とフラグ立てはミューテックスの中。LVGL タスクの点灯と入れ違わない
            if (!backlightIsCut()) {
                backlightCutForPrivacy();
                Serial.println("[WOLF] privacy watchdog: backlight cut");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 画面の生成・破棄
// ---------------------------------------------------------------------------
void screenDeletedCb(lv_event_t *e)
{
    // 画面の破棄は遷移アニメーション（200ms）の完了時なので、その間に次の人狼画面が
    // 作られていることがある。古い画面の後始末で新しい画面を壊さないよう照合する
    if (lv_event_get_target(e) != s_screen) {
        return;
    }
    // ゲーム画面を離れたので、自動で暗くする仕組みを元に戻す
    display::setGameActive(false);
    if (s_tick != nullptr) {
        lv_timer_del(s_tick);
        s_tick = nullptr;
    }
    blankSecretLabels();
    // この画面はこれから消え、次の画面が全面を描き直す。監視タスクの見張りは解く
    s_secret_on_screen = false;
    s_gate.close();
    // 監視タスクが明かりを切ったままなら、次の画面が描かれたころに点け直す。
    // ここで直接戻さないのは、破棄の途中で描き直しを強制できないため
    if (backlightIsCut() && s_backlight_recover == nullptr) {
        s_backlight_recover = lv_timer_create(backlightRecoverCb, 300, nullptr);
    }
    if (anyGameActive()) {
        // どの経路で離れても、進行中の局は無効にして秘密を消す
        s_engine.abort(s_engine.stamp(), AbortReason::User);
        s_std.abort(s_std.stamp(), ws::AbortReason::User);
        clearArmed();
    }
    // 終わった局でも Engine には役職と票が残るので、必ず両方 Idle に戻して消す
    s_engine.reset(s_engine.stamp());
    s_std.reset(s_std.stamp());
    s_clear_armed_pending = false;
    s_play_pending = false;
    s_neutral_pending = false;
    s_dirty = false;
    // 開発用コマンド 'G' が古い画面名を出さないよう、画面の種類も初期値に戻す
    s_view = View::Lobby;
    s_page = 0;
    s_doc_page = 0;
    s_pending_target = NONE;
    // 解放済みオブジェクトを触らないよう、静的ポインタは必ず全部消す
    s_screen = nullptr;
    s_content = nullptr;
    s_role_label = nullptr;
    s_role_icon = nullptr;
    s_secret_label = nullptr;
    s_cover_label = nullptr;
    s_timer_label = nullptr;
}

void loadPublicMeta()
{
    s_players = rules::kDefaultPlayers;
    s_flavor_seq = 0;
    std::array<uint8_t, 20> bytes{};
    bool exists = false;
    if (!port().readPublicMeta(bytes, exists) || !exists) {
        return;
    }
    PublicMeta meta;
    if (decodeMeta(bytes.data(), bytes.size(), meta) == MetaFormat::Invalid) {
        return;
    }
    s_players = meta.players;
    s_flavor_seq = meta.flavor_seq;
    if (meta.armed) {
        // 前回の局が途中で電源断になっている。再開はせず、印を消してお知らせだけ出す
        s_view = View::RebootNotice;
        clearArmed();
    }
}

}  // namespace

// ---------------------------------------------------------------------------
lv_obj_t *createGameScreen()
{
    // 前の人狼画面がまだ消えていない（遷移中の再入・開発用コマンドの連打）ことがある。
    // 古いタイマーを残すと 2 本が同じ部品を作り替えに来るので、ここで止める
    if (s_tick != nullptr) {
        lv_timer_del(s_tick);
        s_tick = nullptr;
    }
    // 点け直しの予約が残っていたら取り消す（この関数の最後で自分で戻す）
    if (s_backlight_recover != nullptr) {
        lv_timer_del(s_backlight_recover);
        s_backlight_recover = nullptr;
    }
    if (s_backlight_mux == nullptr) {
        s_backlight_mux = xSemaphoreCreateMutex();
    }
    // 読み物の途中や秘密の受け渡し中に自動で暗くならないようにする
    display::setGameActive(true);

    lv_obj_t *scr = ui::makeScreen();

    s_screen = scr;
    s_content = lv_obj_create(scr);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_pos(s_content, 0, 0);
    lv_obj_set_size(s_content, layout::kScreenWidth, layout::kScreenHeight);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);

    port().setFence(&s_fence);

    // 前の画面から入り直したときのため、状態を毎回そろえる
    s_view = View::Lobby;
    s_page = 0;
    s_doc_page = 0;
    s_pending_target = NONE;
    s_error_key = nullptr;
    s_secret_shown = false;
    s_secret_on_screen = false;
    s_clear_armed_pending = false;
    s_play_pending = false;
    s_neutral_pending = false;
    s_gate.close();
    if (!s_engine.active()) {
        // 前の局が Revealed / Aborted のまま残っていると Engine::start が通らない。
        // 役職と票もここで消える
        s_engine.reset(s_engine.stamp());
    }
    if (!s_std.active()) {
        s_std.reset(s_std.stamp());
    }
    // 前回選んだ遊び方を思い出す（公開情報。画面には「前回：…」として出すだけ）
    if (!anyGameActive()) {
        s_mode = readLastMode() == 1 ? Mode::Standard : Mode::OneNight;
    } else if (s_std.active()) {
        s_mode = Mode::Standard;
    } else {
        s_mode = Mode::OneNight;
    }
    if (stdMode()) {
        const ws::Stamp st = s_std.stamp();
        s_seen_revision = st.revision;
        s_seen_generation = st.generation;
    } else {
        const Stamp st = s_engine.stamp();
        s_seen_revision = st.revision;
        s_seen_generation = st.generation;
    }
    if (anyGameActive()) {
        // 直前の局がまだ生きている（通常は起きない）。画面を状態に合わせる
        adoptPhaseView();
    } else {
        loadPublicMeta();
    }
    s_dirty = true;
    s_last_touch_ms = port().monotonicNowMs();
    s_last_tick_ms = nowTicks();

    rebuild();
    s_dirty = false;

    if (backlightIsCut()) {
        // 監視タスクが明かりを切ったまま画面が閉じられていた。
        // ここは秘密の無い作りたての画面なので、出たことを確かめてから戻す
        neutralize();
        backlightRestoreIfCut(s_fence.epoch());
    }

    s_tick = lv_timer_create(tickCb, rules::kUiTickMs, nullptr);
    lv_obj_add_event_cb(scr, screenDeletedCb, LV_EVENT_DELETE, nullptr);

    if (s_watchdog == nullptr) {
        // LVGL タスク（優先度 2・コア 1）より高く、別のコアに置く
        xTaskCreatePinnedToCore(watchdogTask, "wolf_wd", 2560, nullptr, 6, &s_watchdog, 0);
    }
    return scr;
}

bool gameActive()
{
    return s_engine.active() || s_std.active();
}

bool secretOnScreen()
{
    // ラベルを空にしただけでは画面から消えていないので、走査し直すまで true のまま
    return s_secret_on_screen || s_secret_shown;
}

void debugPrintPublicState()
{
    // 公開情報のみ。役職・占い結果・襲撃先・投票先はここに絶対に載せない。
    // 通常ルールでは日付・生存人数・直近の発表（誰が抜けたか＝公開情報）も出す
    if (s_mode == Mode::Standard) {
        ws::PublicView sv = s_std.publicView();
        // 犠牲者と追放者は「発表の画面」を開くまでは公開情報ではない。コアは夜・投票の集計が
        // 終わった時点で値を持つので、発表前（テーブルに置く案内の間）はここで伏せる。
        // 生存人数も同じ理由で、発表前は前の値が分からないよう 0 にして出す
        if (sv.phase == ws::Phase::MorningReady) {
            sv.last_victim = ws::NONE;
            sv.alive_count = 0;
        }
        if (sv.phase == ws::Phase::ExecutionReady) {
            sv.last_executed = ws::NONE;
            sv.alive_count = 0;
            sv.winner = ws::Winner::None;
        }
        if (sv.phase == ws::Phase::FinalReady) {
            sv.winner = ws::Winner::None;   // 勝敗は「結果を見る」を押すまで伏せる
        }
        Serial.printf("[WOLF] mode=std view=%u phase=%u day=%u actor=%u players=%u alive=%u "
                      "cycle=%u voted=%u victim=%d executed=%d winner=%u paused=%u "
                      "remaining=%lums secret=%u bl_cut=%u lvgl_stack=%u wd_stack=%u\n",
                      (unsigned)s_view, (unsigned)sv.phase, (unsigned)sv.day,
                      (unsigned)sv.actor, (unsigned)sv.player_count, (unsigned)sv.alive_count,
                      (unsigned)sv.vote_cycle, (unsigned)sv.voted_count,
                      (int)sv.last_victim, (int)sv.last_executed, (unsigned)sv.winner,
                      (unsigned)sv.paused, (unsigned long)sv.remaining_ms,
                      (unsigned)secretOnScreen(), (unsigned)backlightIsCut(),
                      (unsigned)lvgl_port_task_stack_free(),
                      (unsigned)(s_watchdog != nullptr
                                     ? uxTaskGetStackHighWaterMark(s_watchdog) * sizeof(StackType_t)
                                     : 0));
        return;
    }
    const PublicView pv = s_engine.publicView();
    Serial.printf("[WOLF] mode=one view=%u phase=%u actor=%u players=%u cycle=%u paused=%u "
                  "remaining=%lums secret=%u bl_cut=%u lvgl_stack=%u wd_stack=%u\n",
                  (unsigned)s_view, (unsigned)pv.phase, (unsigned)pv.actor,
                  (unsigned)pv.player_count, (unsigned)pv.vote_cycle, (unsigned)pv.paused,
                  (unsigned long)pv.remaining_ms, (unsigned)secretOnScreen(),
                  (unsigned)backlightIsCut(),
                  // スタックの余り（バイト）。少なすぎたらタスクの大きさを見直す
                  (unsigned)lvgl_port_task_stack_free(),
                  (unsigned)(s_watchdog != nullptr
                                 ? uxTaskGetStackHighWaterMark(s_watchdog) * sizeof(StackType_t)
                                 : 0));
}

}  // namespace werewolf
