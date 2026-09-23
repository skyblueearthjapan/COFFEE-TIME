#include "HistoryScreen.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../CupState.h"
#include "ScreenManager.h"
#include "UiKit.h"

LV_FONT_DECLARE(ct_font_jp_20);
LV_FONT_DECLARE(ct_font_jp_22);

namespace ui {

namespace {

// --- グラフの配置（中心 (240,240) から半径 228px の円の内側）-----------------
constexpr int16_t kChartLeft = 68;
constexpr int16_t kChartWidth = 344;
constexpr int16_t kChartBaseline = 280;
constexpr int16_t kChartMaxH = 144;
constexpr Rect kAxisRow{68, 284, 344, 22};
constexpr Rect kAverage{60, 306, 360, 24};
// 表示の切り替え（4 つ。丸い画面の内側 x 68〜412 に収まる）
constexpr Rect kPill[4] = {
    {68, 336, 80, 42},      // 1 週間
    {156, 336, 80, 42},     // 30 日
    {244, 336, 80, 42},     // ゲーム
    {332, 336, 80, 42},     // 日ごと
};
constexpr Rect kNoData{70, 170, 340, 60};

// --- ゲームの表の配置 -------------------------------------------------------
// 6 行（POKER TABLE を足した）になったので、行の高さと間隔をもう一段詰めた。
// 最後の行は y=276〜306、注記は 308〜332 で、切り替えの丸ボタン（y=336）に重ならない
constexpr int16_t kGameRowX = 84;
constexpr int16_t kGameRowW = 316;
constexpr int16_t kGameRowH = 30;
constexpr int16_t kGameRowTop = 106;
constexpr int16_t kGameRowStep = 34;
constexpr Rect kGameHeadToday{200, 84, 66, 22};
constexpr Rect kGameHeadWeek{266, 84, 66, 22};
constexpr Rect kGameHeadTotal{332, 84, 62, 22};
constexpr Rect kGameNote{70, 308, 340, 24};

// --- 一覧の配置 -------------------------------------------------------------
constexpr int kRowsPerPage = 5;
constexpr int16_t kRowTop = 96;
constexpr int16_t kRowStep = 40;
constexpr int16_t kRowH = 34;
constexpr Rect kPagePrev{96, 306, 96, 42};
constexpr Rect kPageLabel{200, 306, 80, 42};
constexpr Rect kPageNext{288, 306, 96, 42};

const char *const kWeekdays[] = {"日", "月", "火", "水", "木", "金", "土"};

// 表に出すゲーム。**実装済みのものだけ**
struct GameRow {
    cup::GameId id;
    const char *name;
};
// 並び順はゲーム一覧と同じ（AI DUEL → エスパー → 探偵 → 人狼 → リバーシ → POKER TABLE）
const GameRow kGameRows[] = {
    {cup::GameId::Duel, "AI DUEL"},
    {cup::GameId::Esper, "エスパー"},
    {cup::GameId::Detective, "事件簿"},
    {cup::GameId::Werewolf, "人狼会"},
    {cup::GameId::Reversi, "リバーシ"},
    {cup::GameId::Cards, "POKER TABLE"},   // ゲームの名前はユーザー指定（2026-09-23）
};
constexpr int kGameRowCount = (int)(sizeof(kGameRows) / sizeof(kGameRows[0]));

// グラフに出す 1 日分
struct Slot {
    uint32_t ymd;
    int cups;
    int wday;
    bool has;       // その日の記録があるか（電源が入っていなかった日は false）
    uint8_t games[cup::kGameCount];
};

bool timeKnown()
{
    return time(nullptr) > 1700000000;
}

// 今日を 0 として first_offset 日目から days 日分の並びを作る（負 = 過去、正 = これから来る日）。
// これから来る日は has = false・0 杯のままにする
int fillRange(Slot *out, int days, int first_offset)
{
    if (!timeKnown()) {
        return 0;
    }
    const time_t now = time(nullptr);
    for (int i = 0; i < days; ++i) {
        const int offset = first_offset + i;
        const time_t t = now + (time_t)offset * 86400;
        struct tm tm;
        localtime_r(&t, &tm);
        const uint32_t ymd = (uint32_t)((tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday);
        out[i].ymd = ymd;
        out[i].wday = tm.tm_wday;
        out[i].cups = 0;
        out[i].has = false;
        memset(out[i].games, 0, sizeof(out[i].games));
        if (offset > 0) {
            continue;
        }
        if (offset == 0) {
            out[i].cups = (int)cup::taken();    // 今日の分は確定前の値を使う
            out[i].has = true;
            for (size_t g = 0; g < cup::kGameCount; ++g) {
                out[i].games[g] = cup::stats::todayGames((cup::GameId)g);
            }
            continue;
        }
        cup::DayRecord rec;
        out[i].has = cup::historyFor(ymd, rec);
        out[i].cups = out[i].has ? (int)rec.cups : 0;
        if (out[i].has) {
            memcpy(out[i].games, rec.games, sizeof(out[i].games));
        }
    }
    return days;
}

// 今日から数えて days 日分（末尾が今日）の並び
int fillSlots(Slot *out, int days)
{
    return fillRange(out, days, -(days - 1));
}

// 今日が今週（月曜はじまり）の何番目か。月 = 0 … 日 = 6
int todayIndexInWeek()
{
    const time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    return (tm.tm_wday + 6) % 7;
}

// 今週（月曜〜日曜）の 7 日分。週が変わると自動で新しい週になる
int fillThisWeek(Slot *out)
{
    return fillRange(out, 7, -todayIndexInWeek());
}

// グラフの表示の種類
enum class View : uint8_t { Week, Month, Games };
View s_view = View::Week;
int s_list_page = 0;

lv_obj_t *s_chart_screen = nullptr;
lv_obj_t *s_chart_content = nullptr;
lv_obj_t *s_list_screen = nullptr;
lv_obj_t *s_list_content = nullptr;

void buildChart();
void buildList();

void rebuildChart()
{
    if (s_chart_content == nullptr) {
        return;
    }
    lv_obj_clean(s_chart_content);
    buildChart();
}

void rebuildList()
{
    if (s_list_content == nullptr) {
        return;
    }
    lv_obj_clean(s_list_content);
    buildList();
}

// 押されたボタン自身を含む中身を作り替えるので、イベントの中では消さず、
// LVGL の次の巡回に回す（自分のイベント処理中に自分を消すと解放済みメモリを触る）
void rebuildChartAsync(void *unused)
{
    (void)unused;
    rebuildChart();
}

void rebuildListAsync(void *unused)
{
    (void)unused;
    rebuildList();
}

void modeCb(lv_event_t *e)
{
    s_view = (View)(intptr_t)lv_event_get_user_data(e);
    lv_async_call(rebuildChartAsync, nullptr);
}

void openListCb(lv_event_t *e)
{
    (void)e;
    s_list_page = 0;
    push(createHistoryListScreen);
}

void pageCb(lv_event_t *e)
{
    const int delta = (int)(intptr_t)lv_event_get_user_data(e);
    const int total = (int)cup::historyCount() + (timeKnown() ? 1 : 0);
    const int pages = total > 0 ? (total + kRowsPerPage - 1) / kRowsPerPage : 1;
    s_list_page += delta;
    if (s_list_page < 0) {
        s_list_page = pages - 1;
    } else if (s_list_page >= pages) {
        s_list_page = 0;
    }
    lv_async_call(rebuildListAsync, nullptr);
}

// 「ゲーム」の表: 1 行 1 ゲーム、列は 今日 / 今週（月曜はじまり）/ 累計
void buildGamesTable(lv_obj_t *p)
{
    static Slot week[7];
    const int n = fillThisWeek(week);

    makeRectLabel(p, kGameHeadToday, &ct_font_jp_20, CT_COLOR_DIM, "今日", LV_TEXT_ALIGN_RIGHT);
    makeRectLabel(p, kGameHeadWeek, &ct_font_jp_20, CT_COLOR_DIM, "今週", LV_TEXT_ALIGN_RIGHT);
    makeRectLabel(p, kGameHeadTotal, &ct_font_jp_20, CT_COLOR_DIM, "累計", LV_TEXT_ALIGN_RIGHT);

    char buf[24];
    for (int i = 0; i < kGameRowCount; ++i) {
        const cup::GameId id = kGameRows[i].id;
        const int16_t y = (int16_t)(kGameRowTop + kGameRowStep * i);
        makePanel(p, Rect{kGameRowX, y, kGameRowW, kGameRowH}, CT_COLOR_PANEL, 12);
        makeRectLabel(p, Rect{96, y, 140, kGameRowH}, &ct_font_jp_20, CT_COLOR_TEXT,
                      kGameRows[i].name, LV_TEXT_ALIGN_LEFT);

        int week_sum = 0;
        for (int d = 0; d < n; ++d) {
            week_sum += week[d].games[(size_t)id];
        }
        snprintf(buf, sizeof(buf), "%u", (unsigned)cup::stats::todayGames(id));
        makeRectLabel(p, Rect{200, y, 66, kGameRowH}, &ct_font_jp_20, CT_COLOR_ACCENT_HI, buf,
                      LV_TEXT_ALIGN_RIGHT);
        snprintf(buf, sizeof(buf), "%d", week_sum);
        makeRectLabel(p, Rect{266, y, 66, kGameRowH}, &ct_font_jp_20, CT_COLOR_TEXT, buf,
                      LV_TEXT_ALIGN_RIGHT);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)cup::stats::totalGames(id));
        makeRectLabel(p, Rect{332, y, 62, kGameRowH}, &ct_font_jp_20, CT_COLOR_SUBTEXT, buf,
                      LV_TEXT_ALIGN_RIGHT);
    }
    makeRectLabel(p, kGameNote, &ct_font_jp_20, CT_COLOR_DIM, "遊んだ回数だけを数えています");
}

void buildChart()
{
    lv_obj_t *p = s_chart_content;
    makeHeading(p, s_view == View::Games ? "履歴（ゲーム）"
                 : s_view == View::Month ? "履歴（30 日）" : "履歴（今週）");

    if (s_view == View::Games) {
        buildGamesTable(p);
        makeRectButton(p, kPill[0], "今週", modeCb, (void *)(intptr_t)View::Week, true, false);
        makeRectButton(p, kPill[1], "30 日", modeCb, (void *)(intptr_t)View::Month, true, false);
        makeRectButton(p, kPill[2], "ゲーム", modeCb, (void *)(intptr_t)View::Games, true, true);
        makeRectButton(p, kPill[3], "日ごと", openListCb, nullptr, true, false);
        makeMenuBackButton(p);
        return;
    }

    const bool month = s_view == View::Month;
    const int days = month ? 30 : 7;
    static Slot slots[30];
    // 週の表示は「月曜〜日曜の今週」。週が変わると自動で新しい週になる（ユーザー要望）。
    // 先週より前は「30 日」と「日ごと」で見られる
    const int n = month ? fillSlots(slots, days) : fillThisWeek(slots);
    const int today_index = month ? n - 1 : todayIndexInWeek();

    if (n == 0) {
        makeRectLabel(p, kNoData, &ct_font_jp_22, CT_COLOR_SUBTEXT,
                      "時刻がまだ分からないので\n日ごとの記録がありません");
    } else {
        int peak = 1, sum = 0, counted = 0;
        for (int i = 0; i < n; ++i) {
            if (slots[i].cups > peak) {
                peak = slots[i].cups;
            }
            if (slots[i].has) {
                sum += slots[i].cups;
                ++counted;
            }
        }
        const int16_t step = (int16_t)(kChartWidth / n);
        const int16_t bar_w = (int16_t)(step > 9 ? (step * 2 / 3) : 5);
        // 日本語は 1 文字 3 バイト。「1 日の平均 12.4 杯（記録のある 5 日）」で 50 バイト超
        char buf[96];
        for (int i = 0; i < n; ++i) {
            const int16_t cx = (int16_t)(kChartLeft + step * i + step / 2);
            const int16_t bh = (int16_t)((int)kChartMaxH * slots[i].cups / peak);
            // 今日は明るい色で強調する
            const lv_color_t c = (i == today_index) ? CT_COLOR_ACCENT_HI
                               : (slots[i].has ? CT_COLOR_ACCENT : CT_COLOR_PANEL);
            makeBar(p, cx, kChartBaseline, bar_w, bh, c);
            if (!month) {
                // これから来る曜日には数字を出さない（0 杯と区別するため）
                if (i <= today_index) {
                    snprintf(buf, sizeof(buf), "%d", slots[i].cups);
                    makeRectLabel(p, Rect{(int16_t)(cx - step / 2), (int16_t)(kChartBaseline - bh - 22),
                                          step, 22},
                                  &ct_font_jp_20, CT_COLOR_SUBTEXT, buf);
                }
                makeRectLabel(p, Rect{(int16_t)(cx - step / 2), kAxisRow.y, step, kAxisRow.h},
                              &ct_font_jp_20, i == today_index ? CT_COLOR_TEXT : CT_COLOR_DIM,
                              kWeekdays[slots[i].wday]);
            }
        }
        if (month) {
            // 30 日は本数が多いので、両端の日付だけ添える
            snprintf(buf, sizeof(buf), "%lu/%lu",
                     (unsigned long)(slots[0].ymd / 100 % 100), (unsigned long)(slots[0].ymd % 100));
            makeRectLabel(p, Rect{kAxisRow.x, kAxisRow.y, 80, kAxisRow.h}, &ct_font_jp_20,
                          CT_COLOR_DIM, buf, LV_TEXT_ALIGN_LEFT);
            snprintf(buf, sizeof(buf), "%lu/%lu",
                     (unsigned long)(slots[n - 1].ymd / 100 % 100),
                     (unsigned long)(slots[n - 1].ymd % 100));
            makeRectLabel(p, Rect{(int16_t)(kAxisRow.x + kAxisRow.w - 80), kAxisRow.y, 80,
                                  kAxisRow.h},
                          &ct_font_jp_20, CT_COLOR_DIM, buf, LV_TEXT_ALIGN_RIGHT);
        }
        if (counted > 0) {
            // 平均は「記録のある日」だけで割る（電源が入っていなかった日は数に入れない）
            const int tenths = (sum * 10 + counted / 2) / counted;
            snprintf(buf, sizeof(buf), "1 日の平均 %d.%d 杯（記録のある %d 日）",
                     tenths / 10, tenths % 10, counted);
        } else {
            snprintf(buf, sizeof(buf), "まだ記録がありません");
        }
        makeRectLabel(p, kAverage, &ct_font_jp_20, CT_COLOR_TEXT, buf);
    }

    makeRectButton(p, kPill[0], "今週", modeCb, (void *)(intptr_t)View::Week, true, !month);
    makeRectButton(p, kPill[1], "30 日", modeCb, (void *)(intptr_t)View::Month, true, month);
    makeRectButton(p, kPill[2], "ゲーム", modeCb, (void *)(intptr_t)View::Games, true, false);
    makeRectButton(p, kPill[3], "日ごと", openListCb, nullptr, true, false);
    makeMenuBackButton(p);
}

void buildList()
{
    lv_obj_t *p = s_list_content;
    makeHeading(p, "履歴（日ごと）");

    // 新しい日が上。今日は確定前の値を先頭に出す
    const int hist = (int)cup::historyCount();
    const bool has_today = timeKnown();
    const int total = hist + (has_today ? 1 : 0);
    const int pages = total > 0 ? (total + kRowsPerPage - 1) / kRowsPerPage : 1;
    if (s_list_page >= pages) {
        s_list_page = 0;
    }

    char buf[64];
    for (int row = 0; row < kRowsPerPage; ++row) {
        const int index = s_list_page * kRowsPerPage + row;
        if (index >= total) {
            break;
        }
        uint32_t ymd = 0;
        int cups = 0, refills = 0, games = 0;
        if (has_today && index == 0) {
            const time_t now = time(nullptr);
            struct tm tm;
            localtime_r(&now, &tm);
            ymd = (uint32_t)((tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday);
            cups = (int)cup::taken();
            refills = (int)cup::today().refills;
            for (size_t g = 0; g < cup::kGameCount; ++g) {
                games += cup::stats::todayGames((cup::GameId)g);
            }
        } else {
            // historyAt(0) がいちばん古いので、新しい順に読み替える
            const int from_new = has_today ? index - 1 : index;
            const cup::DayRecord rec = cup::historyAt((size_t)(hist - 1 - from_new));
            ymd = rec.ymd;
            cups = rec.cups;
            refills = rec.refills;
            for (size_t g = 0; g < cup::kGameCount; ++g) {
                games += rec.games[g];
            }
        }
        if (ymd == 0) {
            continue;       // 壊れた記録（通常は起きない）
        }
        const int16_t y = (int16_t)(kRowTop + kRowStep * row);
        // ゲームの回数も入れるので、枠は丸の内側いっぱい（x 80〜400）まで広げてある
        makePanel(p, Rect{80, y, 320, kRowH}, CT_COLOR_PANEL, 12);

        // 曜日は日付から求める（記録には入れていない）
        struct tm tm = {};
        tm.tm_year = (int)(ymd / 10000) - 1900;
        tm.tm_mon = (int)(ymd / 100 % 100) - 1;
        tm.tm_mday = (int)(ymd % 100);
        tm.tm_hour = 12;
        const time_t t = mktime(&tm);
        struct tm norm;
        localtime_r(&t, &norm);
        snprintf(buf, sizeof(buf), "%d/%d（%s）", norm.tm_mon + 1, norm.tm_mday,
                 kWeekdays[norm.tm_wday]);
        makeRectLabel(p, Rect{92, y, 112, kRowH}, &ct_font_jp_20, CT_COLOR_TEXT, buf,
                      LV_TEXT_ALIGN_LEFT);
        snprintf(buf, sizeof(buf), "%d 杯", cups);
        makeRectLabel(p, Rect{204, y, 64, kRowH}, &ct_font_jp_20, CT_COLOR_ACCENT_HI, buf,
                      LV_TEXT_ALIGN_RIGHT);
        // 遊んだ日は「補充」を短くして、ゲームの回数を同じ枠に収める
        if (games > 0) {
            snprintf(buf, sizeof(buf), "補%d ゲーム%d", refills, games);
        } else {
            snprintf(buf, sizeof(buf), "補充 %d", refills);
        }
        makeRectLabel(p, Rect{268, y, 124, kRowH}, &ct_font_jp_20, CT_COLOR_SUBTEXT, buf,
                      LV_TEXT_ALIGN_RIGHT);
    }
    if (total == 0) {
        makeRectLabel(p, kNoData, &ct_font_jp_22, CT_COLOR_SUBTEXT, "まだ記録がありません");
    }

    makeRectButton(p, kPagePrev, "前へ", pageCb, (void *)(intptr_t)-1, pages > 1, false);
    snprintf(buf, sizeof(buf), "%d/%d", s_list_page + 1, pages);
    makeRectLabel(p, kPageLabel, &ct_font_jp_20, CT_COLOR_SUBTEXT, buf);
    makeRectButton(p, kPageNext, "次へ", pageCb, (void *)(intptr_t)1, pages > 1, false);
    makeMenuBackButton(p);
}

void chartLoadCb(lv_event_t *e)
{
    if (lv_event_get_target(e) != s_chart_screen) {
        return;
    }
    rebuildChart();     // 一覧から戻ってきたときに作り直す
}

void chartDeletedCb(lv_event_t *e)
{
    // 遷移中に次の画面が作られていることがあるので、自分の画面か必ず照合する
    if (lv_event_get_target(e) != s_chart_screen) {
        return;
    }
    s_chart_screen = nullptr;
    s_chart_content = nullptr;
}

void listDeletedCb(lv_event_t *e)
{
    if (lv_event_get_target(e) != s_list_screen) {
        return;
    }
    s_list_screen = nullptr;
    s_list_content = nullptr;
}

// 中身を丸ごと作り替えられるよう、画面の上に入れ物を 1 枚かぶせる
lv_obj_t *makeContent(lv_obj_t *scr)
{
    lv_obj_t *c = lv_obj_create(scr);
    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, 0, 0);
    lv_obj_set_size(c, kScreenSize, kScreenSize);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

}  // namespace

lv_obj_t *createHistoryScreen()
{
    lv_obj_t *scr = makeScreen();
    s_chart_screen = scr;
    s_chart_content = makeContent(scr);
    buildChart();
    lv_obj_add_event_cb(scr, chartLoadCb, LV_EVENT_SCREEN_LOAD_START, nullptr);
    lv_obj_add_event_cb(scr, chartDeletedCb, LV_EVENT_DELETE, nullptr);
    return scr;
}

lv_obj_t *createHistoryListScreen()
{
    lv_obj_t *scr = makeScreen();
    s_list_screen = scr;
    s_list_content = makeContent(scr);
    buildList();
    lv_obj_add_event_cb(scr, listDeletedCb, LV_EVENT_DELETE, nullptr);
    return scr;
}

}  // namespace ui
