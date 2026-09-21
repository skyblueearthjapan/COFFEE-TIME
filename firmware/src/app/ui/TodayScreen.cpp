#include "TodayScreen.h"

#include <stdio.h>

#include "../CupState.h"
#include "MenuIcons.h"
#include "ScreenManager.h"
#include "UiKit.h"

LV_FONT_DECLARE(ct_font_jp_20);
LV_FONT_DECLARE(ct_font_jp_22);
LV_FONT_DECLARE(ct_font_jp_40);
LV_FONT_DECLARE(ct_font_time_104);
LV_FONT_DECLARE(ct_font_icons_36);

namespace ui {

namespace {

// --- 1 枚目の配置（中心 (240,240) から半径 228px の円の内側）-----------------
constexpr Rect kBigNumber{100, 82, 140, 76};    // 大きな杯数（ct_font_time_104 の行の高さは 75）
constexpr Rect kBigSuffix{248, 122, 120, 34};   // 「/ 10 杯」
constexpr int16_t kGaugeTop = 168;              // カップのゲージの上端
constexpr int16_t kGaugeH = 36;
constexpr Rect kProgress{110, 214, 260, 12};
constexpr Rect kRemain{90, 230, 300, 46};
constexpr Rect kLastLine{60, 282, 360, 24};
constexpr Rect kGameLine{60, 308, 360, 24};     // 「今日のゲーム 3 回」（遊んだ日だけ出す）
constexpr Rect kHourlyBtn{130, 338, 220, 46};

// --- 2 枚目（時間ごと）------------------------------------------------------
constexpr int16_t kChartLeft = 62;
constexpr int16_t kChartWidth = 356;
constexpr int16_t kChartBaseline = 288;         // 棒の下端
constexpr int16_t kChartMaxH = 150;             // 棒の最大の高さ
constexpr Rect kAxisRow{62, 292, 356, 24};
constexpr Rect kPeakLine{60, 320, 360, 26};
constexpr Rect kTotalLine{60, 348, 360, 26};

// 0 時からの分を「14:32」にする。無ければ「--:--」
void minutesText(char *out, size_t cap, int16_t minutes)
{
    if (minutes < 0) {
        snprintf(out, cap, "--:--");
        return;
    }
    snprintf(out, cap, "%d:%02d", minutes / 60, minutes % 60);
}

void openHourlyCb(lv_event_t *e)
{
    (void)e;
    push(createTodayHourlyScreen);
}

// カップのゲージ。個数は「1 回に作る杯数」に連動し、**残っている分が明るい**
void buildGauge(lv_obj_t *scr, uint32_t left, uint32_t max_cups)
{
    if (max_cups == 0) {
        return;
    }
    // 15 個でも丸い画面に収まるよう、並びの幅は 420px までに抑える
    int16_t step = (int16_t)(420 / max_cups);
    if (step > 30) {
        step = 30;
    }
    const int16_t total = (int16_t)(step * max_cups);
    const int16_t x0 = (int16_t)(240 - total / 2);
    for (uint32_t i = 0; i < max_cups; ++i) {
        const Rect r{(int16_t)(x0 + step * i), kGaugeTop, step, kGaugeH};
        makeIconLabel(scr, r, &ct_font_icons_36,
                      i < left ? CT_COLOR_ACCENT_HI : CT_COLOR_DIM, icon::kLocalCafe);
    }
}

}  // namespace

lv_obj_t *createTodayScreen()
{
    lv_obj_t *scr = makeScreen();
    makeHeading(scr, "今日のコーヒー");

    const uint32_t taken = cup::taken();
    const uint32_t left = cup::remaining();
    const uint32_t max_cups = cup::maxCups();
    const cup::Today &t = cup::today();

    char buf[96];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)taken);
    makeRectLabel(scr, kBigNumber, &ct_font_time_104, CT_COLOR_TEXT, buf, LV_TEXT_ALIGN_RIGHT);
    snprintf(buf, sizeof(buf), "/ %lu 杯", (unsigned long)max_cups);
    makeRectLabel(scr, kBigSuffix, &ct_font_jp_22, CT_COLOR_SUBTEXT, buf, LV_TEXT_ALIGN_LEFT);

    buildGauge(scr, left, max_cups);

    // 進み具合（今日つくった分のうち、どれだけ出たか）
    lv_obj_t *bar = lv_bar_create(scr);
    lv_obj_set_pos(bar, kProgress.x, kProgress.y);
    lv_obj_set_size(bar, kProgress.w, kProgress.h);
    lv_obj_set_style_bg_color(bar, CT_COLOR_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, CT_COLOR_ACCENT_HI, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, (int32_t)(max_cups > 0 ? max_cups : 1));
    lv_bar_set_value(bar, (int32_t)(taken > max_cups ? max_cups : taken), LV_ANIM_OFF);

    // 残り杯数の色は HOME と同じ決まり（3 以下オレンジ・0 赤）
    lv_color_t c = CT_COLOR_TEXT;
    if (left == 0) {
        c = CT_COLOR_ALERT;
    } else if (left <= 3) {
        c = CT_COLOR_WARN;
    }
    snprintf(buf, sizeof(buf), "残り %lu 杯", (unsigned long)left);
    makeRectLabel(scr, kRemain, &ct_font_jp_40, c, buf);

    char take_at[8], refill_at[8];
    minutesText(take_at, sizeof(take_at), t.last_take_min);
    minutesText(refill_at, sizeof(refill_at), t.last_refill_min);
    snprintf(buf, sizeof(buf), "最後の 1 杯 %s ・ 補充 %s", take_at, refill_at);
    makeRectLabel(scr, kLastLine, &ct_font_jp_20, CT_COLOR_SUBTEXT, buf);

    // 今日ゲームを遊んでいれば回数を添える（遊んでいない日は何も出さない）
    int plays = 0;
    for (size_t i = 0; i < cup::kGameCount; ++i) {
        plays += cup::stats::todayGames((cup::GameId)i);
    }
    if (plays > 0) {
        snprintf(buf, sizeof(buf), "今日のゲーム %d 回", plays);
        makeRectLabel(scr, kGameLine, &ct_font_jp_20, CT_COLOR_ACCENT_HI, buf);
    }

    makeRectButton(scr, kHourlyBtn, "時間ごとに見る", openHourlyCb, nullptr, true, true);
    makeMenuBackButton(scr);
    return scr;
}

lv_obj_t *createTodayHourlyScreen()
{
    lv_obj_t *scr = makeScreen();
    makeHeading(scr, "時間ごと（今日）");

    const cup::Today &t = cup::today();

    // 表示は 8〜18 時。その外に記録がある日は自動で広げる
    int lo = 8, hi = 18, peak = -1, peak_n = 0, total = 0;
    for (int h = 0; h < 24; ++h) {
        const int n = t.hour[h];
        total += n;
        if (n == 0) {
            continue;
        }
        if (h < lo) lo = h;
        if (h > hi) hi = h;
        if (n > peak_n) {
            peak_n = n;
            peak = h;
        }
    }
    const int cols = hi - lo + 1;
    const int16_t step = (int16_t)(kChartWidth / cols);
    const int16_t bar_w = (int16_t)(step > 8 ? (step * 2 / 3) : 4);

    // 日本語は 1 文字 3 バイト。「合計 12 杯 ・ 補充 2 回 ・ 時刻不明 2 杯」で 60 バイト近くなる
    char buf[96];
    for (int i = 0; i < cols; ++i) {
        const int h = lo + i;
        const int n = t.hour[h];
        const int16_t cx = (int16_t)(kChartLeft + step * i + step / 2);
        const int16_t bh = (int16_t)(peak_n > 0 ? (int)kChartMaxH * n / peak_n : 0);
        makeBar(scr, cx, kChartBaseline, bar_w, bh,
                n > 0 ? CT_COLOR_ACCENT : CT_COLOR_PANEL);
        // 棒が細いときは数字が重なるので出さない
        if (n > 0 && step >= 24) {
            snprintf(buf, sizeof(buf), "%d", n);
            makeRectLabel(scr, Rect{(int16_t)(cx - step / 2), (int16_t)(kChartBaseline - bh - 22),
                                    step, 22},
                          &ct_font_jp_20, CT_COLOR_SUBTEXT, buf);
        }
        // 目盛りは 2 時間おき（細いときは 4 時間おき）
        const int tick = step >= 24 ? 2 : 4;
        if (h % tick == 0) {
            snprintf(buf, sizeof(buf), "%d", h);
            makeRectLabel(scr, Rect{(int16_t)(cx - step / 2), kAxisRow.y, step, kAxisRow.h},
                          &ct_font_jp_20, CT_COLOR_DIM, buf);
        }
    }

    if (peak >= 0) {
        snprintf(buf, sizeof(buf), "いちばん多い時間 %d 時台", peak);
    } else {
        snprintf(buf, sizeof(buf), "まだ記録がありません");
    }
    makeRectLabel(scr, kPeakLine, &ct_font_jp_20, CT_COLOR_TEXT, buf);

    // 合計は HOME と同じ「今日の杯数」を正とする。グラフに入っていない分（時刻が分からない間の 1 杯や、
    // この記録の仕組みが入る前に数えた分）は「時刻不明」として添える
    const int taken_today = (int)cup::taken();
    const int outside = taken_today > total ? taken_today - total : (int)t.unknown;
    if (outside > 0) {
        snprintf(buf, sizeof(buf), "合計 %d 杯 ・ 補充 %u 回 ・ 時刻不明 %d 杯",
                 total + outside, (unsigned)t.refills, outside);
    } else {
        snprintf(buf, sizeof(buf), "合計 %d 杯 ・ 補充 %u 回", total, (unsigned)t.refills);
    }
    makeRectLabel(scr, kTotalLine, &ct_font_jp_20, CT_COLOR_SUBTEXT, buf);

    makeMenuBackButton(scr);
    return scr;
}

}  // namespace ui
