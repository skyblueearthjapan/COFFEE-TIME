#include "SettingsScreen.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "../Battery.h"
#include "../CupState.h"
#include "../Display.h"
#include "../NetService.h"
#include "../RtcClock.h"
#include "../SdLog.h"
#include "../Settings.h"
#include "../SysInfo.h"
#include "MenuIcons.h"
#include "ScreenManager.h"
#include "UiKit.h"

LV_FONT_DECLARE(ct_font_jp_20);
LV_FONT_DECLARE(ct_font_jp_22);
LV_FONT_DECLARE(ct_font_jp_40);
LV_FONT_DECLARE(ct_font_time_64);
LV_FONT_DECLARE(ct_font_icons_36);
LV_FONT_DECLARE(ct_font_icons_54);

namespace ui {

namespace {

// --- 一覧の配置（中心 (240,240) から半径 228px の円の内側）-------------------
constexpr int16_t kRowX = 76;
constexpr int16_t kRowW = 328;
constexpr int16_t kRowH = 50;
constexpr int16_t kRowTop = 100;
constexpr int16_t kRowStep = 62;
constexpr Rect kPagePrev{96, 350, 92, 40};
constexpr Rect kPageLabel{200, 350, 80, 40};
constexpr Rect kPageNext{292, 350, 92, 40};

// --- 子画面で使いまわす配置 -------------------------------------------------
constexpr Rect kChoiceRect{110, 0, 260, 44};    // y は count から決める
constexpr Rect kNote{70, 300, 340, 26};
constexpr Rect kDecide{150, 334, 180, 46};

const char *const kWeekdays[] = {"日", "月", "火", "水", "木", "金", "土"};

// 「画面を暗くする」の選択肢（秒数は settings::kDimSeconds と同じ並び）
const char *const kDimLabels[settings::kDimChoiceCount] = {
    "1 分", "5 分", "10 分", "30 分", "しない",
};
const char *const kOnOff[2] = {"オフ", "オン"};
// 日付が変わったときの LEFT（残り杯数）。「朝いちばんの残り」では意味が伝わらなかったので言い換えた
const char *const kMorning[2] = {"残りを 0 杯にする", "残りを満杯にする"};
const char *const kMorningNote =
    "日付が変わったとき、HOME の\nLEFT（残り杯数）をどうするか。\n0 杯なら、作ったあと長押しで補充";

// 一覧の 1 項目
struct Item {
    const char *icon;
    const char *name;
    SettingsSub sub;
};

// 「日付が変わったときの残り」は一覧に出さない（2026-09-21 ユーザー確認: 朝は必ず 0 杯から始める運用。
// 夜の残りは配るかアイスコーヒーにするので、満杯から始める場面が無い）。設定値は既定の 0 杯のまま使う
const Item kItems[] = {
    {icon::kBrightnessMid, "画面の明るさ", SettingsSub::Brightness},
    {icon::kBedtime, "画面を暗くする", SettingsSub::DimTimeout},
    {icon::kSchedule, "時刻を合わせる", SettingsSub::TimeSet},
    {icon::kVolumeUp, "操作音", SettingsSub::Sound},
    {icon::kLocalCafe, "1 回に作る杯数", SettingsSub::MaxCups},
    {icon::kWifi, "Wi-Fi", SettingsSub::Wifi},
    {icon::kInfo, "システム情報", SettingsSub::SystemInfo},
};
constexpr int kItemCount = (int)(sizeof(kItems) / sizeof(kItems[0]));
constexpr int kItemsPerPage = 4;
constexpr int kPageCount = (kItemCount + kItemsPerPage - 1) / kItemsPerPage;

int s_page = 0;                         // 一覧のページ (0/1)
SettingsSub s_sub = SettingsSub::Brightness;

lv_obj_t *s_list_screen = nullptr;
lv_obj_t *s_list_content = nullptr;
lv_obj_t *s_sub_screen = nullptr;
lv_obj_t *s_sub_content = nullptr;
lv_obj_t *s_bright_value = nullptr;     // 明るさ画面の「80%」

// 時刻合わせ用の編集中の値
struct tm s_edit = {};
bool s_edit_date = false;               // false: 時・分 / true: 年・月・日

void buildList();
void buildSub();

void rebuildList()
{
    if (s_list_content == nullptr) {
        return;
    }
    lv_obj_clean(s_list_content);
    buildList();
}

void rebuildSub()
{
    if (s_sub_content == nullptr) {
        return;
    }
    s_bright_value = nullptr;
    lv_obj_clean(s_sub_content);
    buildSub();
}

// 押されたボタンごと作り替えるので、イベントの中では消さず LVGL の次の巡回に回す
void rebuildListAsync(void *unused)
{
    (void)unused;
    rebuildList();
}

void rebuildSubAsync(void *unused)
{
    (void)unused;
    rebuildSub();
}

// --- 一覧 -------------------------------------------------------------------

// 各項目の「今の値」。値の無い項目は空文字
void valueText(SettingsSub sub, char *out, size_t cap)
{
    switch (sub) {
    case SettingsSub::Brightness:
        snprintf(out, cap, "%u%%", (unsigned)settings::brightness());
        break;
    case SettingsSub::DimTimeout:
        snprintf(out, cap, "%s", kDimLabels[settings::dimChoice()]);
        break;
    case SettingsSub::Sound:
        snprintf(out, cap, "%s", kOnOff[settings::sound() ? 1 : 0]);
        break;
    case SettingsSub::MaxCups:
        snprintf(out, cap, "%u 杯", (unsigned)settings::maxCups());
        break;
    case SettingsSub::Morning:
        snprintf(out, cap, "%s", settings::morningFull() ? "満杯に" : "0 杯に");
        break;
    case SettingsSub::Wifi:
        snprintf(out, cap, "%s", net::wifiConnected() ? "接続中" : "未接続");
        break;
    default:
        out[0] = '\0';
        break;
    }
}

void openSubCb(lv_event_t *e)
{
    pushSettingsSub((SettingsSub)(intptr_t)lv_event_get_user_data(e));
}

void listPageCb(lv_event_t *e)
{
    s_page = (s_page + (int)(intptr_t)lv_event_get_user_data(e) + kPageCount) % kPageCount;
    lv_async_call(rebuildListAsync, nullptr);
}

void buildList()
{
    lv_obj_t *p = s_list_content;
    makeHeading(p, "設定");

    char buf[48];
    for (int i = 0; i < kItemsPerPage; ++i) {
        const int index = s_page * kItemsPerPage + i;
        if (index >= kItemCount) {
            break;      // 最後のページは項目が少ないことがある
        }
        const Item &item = kItems[index];
        const int16_t y = (int16_t)(kRowTop + kRowStep * i);
        lv_obj_t *btn = makeRectButton(p, Rect{kRowX, y, kRowW, kRowH}, "", openSubCb,
                                       (void *)(intptr_t)item.sub, true, false);
        makeIconLabel(btn, Rect{6, 0, 40, kRowH}, &ct_font_icons_36, CT_COLOR_ACCENT_HI, item.icon);
        makeRectLabel(btn, Rect{50, 0, 165, kRowH}, &ct_font_jp_20, CT_COLOR_TEXT, item.name,
                      LV_TEXT_ALIGN_LEFT);
        valueText(item.sub, buf, sizeof(buf));
        if (buf[0] != '\0') {
            makeRectLabel(btn, Rect{215, 0, 73, kRowH}, &ct_font_jp_20, CT_COLOR_SUBTEXT, buf,
                          LV_TEXT_ALIGN_RIGHT);
        }
        // 枠の内側は border 2px を除いた 324px。アイコンは 36px 幅で欠けないように置く
        makeIconLabel(btn, Rect{286, 0, 36, kRowH}, &ct_font_icons_36, CT_COLOR_DIM,
                      icon::kChevronRight);
    }

    makeRectButton(p, kPagePrev, "前へ", listPageCb, (void *)(intptr_t)-1, true, false);
    snprintf(buf, sizeof(buf), "%d/%d", s_page + 1, kPageCount);
    makeRectLabel(p, kPageLabel, &ct_font_jp_20, CT_COLOR_SUBTEXT, buf);
    makeRectButton(p, kPageNext, "次へ", listPageCb, (void *)(intptr_t)1, true, false);
    makeMenuBackButton(p);
}

// --- 子画面のうち「選ぶだけ」のもの（暗くする時間・操作音・日付が変わったら）---

void choiceCb(lv_event_t *e)
{
    const int index = (int)(intptr_t)lv_event_get_user_data(e);
    switch (s_sub) {
    case SettingsSub::DimTimeout:
        settings::setDimChoice((uint8_t)index);
        display::applySettings();
        break;
    case SettingsSub::Sound:
        settings::setSound(index != 0);
        break;
    case SettingsSub::Morning:
        settings::setMorningFull(index != 0);
        break;
    default:
        return;
    }
    settings::save();
    pop();
}

// note は 2 択の画面だけで使う説明文（見出しと選択肢の間。5 択の画面では場所が無いので渡さない）
void buildChoice(lv_obj_t *p, const char *title, const char *const *labels, int count, int current,
                 const char *note = nullptr)
{
    makeHeading(p, title);
    const bool with_note = note != nullptr && count <= 2;
    if (with_note) {
        makeRectLabel(p, Rect{70, 96, 340, 72}, &ct_font_jp_20, CT_COLOR_SUBTEXT, note);
    }
    const int16_t base = (int16_t)(with_note ? 236 : 214);
    for (int i = 0; i < count; ++i) {
        const int16_t y = (int16_t)(base - (count - 1) * 26 + i * 52);
        makeRectButton(p, Rect{kChoiceRect.x, y, kChoiceRect.w, kChoiceRect.h}, labels[i],
                       choiceCb, (void *)(intptr_t)i, true, i == current);
    }
    makeMenuBackButton(p);
}

// --- 画面の明るさ -----------------------------------------------------------

void brightnessCb(lv_event_t *e)
{
    const int step = (int)lv_slider_get_value(lv_event_get_target(e));
    settings::setBrightness((uint8_t)(step * 10));
    // その場で反映する（保存は「決定」またはもどるのとき）
    display::applySettings();
    if (s_bright_value != nullptr) {
        lv_label_set_text_fmt(s_bright_value, "%u%%", (unsigned)settings::brightness());
    }
}

void saveCb(lv_event_t *e)
{
    (void)e;
    settings::save();
}

void saveAndPopCb(lv_event_t *e)
{
    (void)e;
    settings::save();
    pop();
}

void buildBrightness(lv_obj_t *p)
{
    makeHeading(p, "画面の明るさ");
    makeIconLabel(p, Rect{200, 92, 80, 64}, &ct_font_icons_54, CT_COLOR_ACCENT_HI,
                  icon::kBrightnessMid);

    char buf[16];
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)settings::brightness());
    s_bright_value = makeRectLabel(p, Rect{140, 168, 200, 72}, &ct_font_time_64, CT_COLOR_TEXT, buf);

    makeIconLabel(p, Rect{86, 258, 40, 36}, &ct_font_icons_36, CT_COLOR_DIM, icon::kBrightnessLow);
    makeIconLabel(p, Rect{354, 258, 40, 36}, &ct_font_icons_36, CT_COLOR_DIM, icon::kBrightnessHigh);

    lv_obj_t *sl = lv_slider_create(p);
    lv_obj_set_pos(sl, 136, 268);
    lv_obj_set_size(sl, 208, 16);
    lv_slider_set_range(sl, 1, 10);     // 10% 刻み
    lv_slider_set_value(sl, settings::brightness() / 10, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, CT_COLOR_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, CT_COLOR_ACCENT_HI, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, CT_COLOR_TEXT, LV_PART_KNOB);
    lv_obj_add_event_cb(sl, brightnessCb, LV_EVENT_VALUE_CHANGED, nullptr);

    makeRectLabel(p, kNote, &ct_font_jp_20, CT_COLOR_SUBTEXT, "電池を長持ちさせるなら 50% 前後");
    makeRectButton(p, kDecide, "決定", saveAndPopCb, nullptr, true, true);
    // もどるで抜けたときも、今あてている明るさをそのまま残す
    lv_obj_add_event_cb(makeMenuBackButton(p), saveCb, LV_EVENT_CLICKED, nullptr);
}

// --- 1 回に作る杯数 ---------------------------------------------------------

void maxCupsStepCb(lv_event_t *e)
{
    const int delta = (int)(intptr_t)lv_event_get_user_data(e);
    settings::setMaxCups((uint8_t)(settings::maxCups() + delta));
    lv_async_call(rebuildSubAsync, nullptr);
}

void buildMaxCups(lv_obj_t *p)
{
    makeHeading(p, "1 回に作る杯数");

    const uint8_t cups = settings::maxCups();
    char buf[16];
    snprintf(buf, sizeof(buf), "%u", (unsigned)cups);
    makeRectLabel(p, Rect{150, 150, 120, 80}, &ct_font_time_64, CT_COLOR_TEXT, buf,
                  LV_TEXT_ALIGN_RIGHT);
    makeRectLabel(p, Rect{278, 178, 60, 32}, &ct_font_jp_22, CT_COLOR_SUBTEXT, "杯",
                  LV_TEXT_ALIGN_LEFT);

    makeRectButton(p, Rect{88, 166, 72, 64}, "-", maxCupsStepCb, (void *)(intptr_t)-1,
                   cups > settings::kMinCups, false);
    makeRectButton(p, Rect{320, 166, 72, 64}, "+", maxCupsStepCb, (void *)(intptr_t)1,
                   cups < settings::kMaxCupsLimit, false);

    makeRectLabel(p, Rect{70, 258, 340, 26}, &ct_font_jp_20, CT_COLOR_SUBTEXT,
                  "補充するとこの杯数に戻ります");
    makeRectButton(p, kDecide, "決定", saveAndPopCb, nullptr, true, true);
    lv_obj_add_event_cb(makeMenuBackButton(p), saveCb, LV_EVENT_CLICKED, nullptr);
}

// --- 時刻を合わせる ---------------------------------------------------------

void timeStepCb(lv_event_t *e)
{
    const int code = (int)(intptr_t)lv_event_get_user_data(e);
    const int field = code >> 1;
    const int dir = (code & 1) ? 1 : -1;
    if (s_edit_date) {
        if (field == 0) s_edit.tm_year += dir;
        else if (field == 1) s_edit.tm_mon += dir;
        else s_edit.tm_mday += dir;
    } else {
        if (field == 0) s_edit.tm_hour += dir;
        else s_edit.tm_min += dir;
    }
    s_edit.tm_sec = 0;
    s_edit.tm_isdst = -1;
    // mktime が「9 月 32 日」→「10 月 2 日」のように直してくれる
    const time_t t = mktime(&s_edit);
    localtime_r(&t, &s_edit);
    lv_async_call(rebuildSubAsync, nullptr);
}

void timeModeCb(lv_event_t *e)
{
    (void)e;
    s_edit_date = !s_edit_date;
    lv_async_call(rebuildSubAsync, nullptr);
}

void timeApplyCb(lv_event_t *e)
{
    (void)e;
    struct tm t = s_edit;
    t.tm_sec = 0;
    t.tm_isdst = -1;
    const time_t epoch = mktime(&t);
    if (epoch > 0) {
        const struct timeval tv = {epoch, 0};
        settimeofday(&tv, nullptr);
        rtc::saveSystemTime();      // 電源を切っても残るよう時計チップにも書く
        sdlog::event("timeset", cup::taken(), cup::remaining(), cup::remaining(), "from screen");
    }
    pop();
}

// 上下の矢印ボタン（field を user_data に入れる）
void makeArrow(lv_obj_t *p, int16_t x, int16_t y, int field, bool up)
{
    lv_obj_t *btn = makeRectButton(p, Rect{x, y, 80, 44}, "", timeStepCb,
                                   (void *)(intptr_t)((field << 1) | (up ? 1 : 0)), true, false);
    makeIconLabel(btn, Rect{22, 0, 36, 44}, &ct_font_icons_36, CT_COLOR_ACCENT_HI,
                  up ? icon::kArrowUp : icon::kArrowDown);
}

void buildTimeSet(lv_obj_t *p)
{
    makeHeading(p, "時刻を合わせる");

    char buf[64];
    if (s_edit_date) {
        snprintf(buf, sizeof(buf), "%d:%02d", s_edit.tm_hour, s_edit.tm_min);
    } else {
        snprintf(buf, sizeof(buf), "%d 年 %d 月 %d 日（%s）", s_edit.tm_year + 1900,
                 s_edit.tm_mon + 1, s_edit.tm_mday, kWeekdays[s_edit.tm_wday % 7]);
    }
    makeRectLabel(p, Rect{70, 86, 340, 32}, &ct_font_jp_22, CT_COLOR_SUBTEXT, buf);

    if (s_edit_date) {
        const int16_t x[3] = {96, 200, 304};
        for (int i = 0; i < 3; ++i) {
            makeArrow(p, x[i], 128, i, true);
            makeArrow(p, x[i], 260, i, false);
        }
        snprintf(buf, sizeof(buf), "%d/%02d/%02d", s_edit.tm_year + 1900, s_edit.tm_mon + 1,
                 s_edit.tm_mday);
        makeRectLabel(p, Rect{90, 186, 300, 52}, &ct_font_jp_40, CT_COLOR_TEXT, buf);
    } else {
        const int16_t x[2] = {150, 250};
        for (int i = 0; i < 2; ++i) {
            makeArrow(p, x[i], 128, i, true);
            makeArrow(p, x[i], 260, i, false);
        }
        snprintf(buf, sizeof(buf), "%02d:%02d", s_edit.tm_hour, s_edit.tm_min);
        makeRectLabel(p, Rect{120, 180, 240, 72}, &ct_font_time_64, CT_COLOR_TEXT, buf);
    }

    makeRectLabel(p, Rect{70, 308, 340, 26}, &ct_font_jp_20, CT_COLOR_SUBTEXT,
                  "Wi-Fi につながると自動で合います");
    makeRectButton(p, Rect{96, 340, 130, 44}, s_edit_date ? "時刻に戻る" : "日付を変える",
                   timeModeCb, nullptr, true, false);
    makeRectButton(p, Rect{254, 340, 130, 44}, "決定", timeApplyCb, nullptr, true, true);
    makeMenuBackButton(p);
}

// --- Wi-Fi（状態の表示のみ）-------------------------------------------------

void buildWifi(lv_obj_t *p)
{
    makeHeading(p, "Wi-Fi");

    const char *names[3] = {"状態", "電波の強さ", "登録している数"};
    char values[3][40];
    snprintf(values[0], sizeof(values[0]), "%s", net::wifiConnected() ? "接続中" : "未接続");
    if (net::wifiConnected()) {
        snprintf(values[1], sizeof(values[1]), "%d dBm", net::rssi());
    } else {
        snprintf(values[1], sizeof(values[1]), "--");
    }
    snprintf(values[2], sizeof(values[2]), "%d 件", net::registeredCount());

    for (int i = 0; i < 3; ++i) {
        const int16_t y = (int16_t)(110 + 52 * i);
        makePanel(p, Rect{88, y, 304, 44}, CT_COLOR_PANEL, 12);
        makeRectLabel(p, Rect{104, y, 150, 44}, &ct_font_jp_20, CT_COLOR_TEXT, names[i],
                      LV_TEXT_ALIGN_LEFT);
        makeRectLabel(p, Rect{240, y, 136, 44}, &ct_font_jp_20, CT_COLOR_ACCENT_HI, values[i],
                      LV_TEXT_ALIGN_RIGHT);
    }
    makeRectLabel(p, Rect{70, 276, 340, 56}, &ct_font_jp_20, CT_COLOR_SUBTEXT,
                  "名前とパスワードは表示しません。\n登録は secrets.h で行います");
    makeMenuBackButton(p);
}

// --- システム情報 -----------------------------------------------------------

const char *resetReasonJa()
{
    const char *id = sysinfo::resetReasonId();
    if (strcmp(id, "poweron") == 0) return "電源投入";
    if (strcmp(id, "software") == 0) return "書き込み";
    if (strcmp(id, "panic") == 0) return "異常終了";
    if (strcmp(id, "watchdog") == 0) return "見張り";
    if (strcmp(id, "brownout") == 0) return "電圧低下";
    if (strcmp(id, "usb") == 0) return "USB";
    return "その他";
}

void uptimeText(char *out, size_t cap)
{
    const uint32_t s = sysinfo::uptimeSeconds();
    if (s < 60) {
        snprintf(out, cap, "%lu 秒前", (unsigned long)s);
    } else if (s < 3600) {
        snprintf(out, cap, "%lu 分前", (unsigned long)(s / 60));
    } else if (s < 86400) {
        snprintf(out, cap, "%lu 時間前", (unsigned long)(s / 3600));
    } else {
        snprintf(out, cap, "%lu 日前", (unsigned long)(s / 86400));
    }
}

void buildSystemInfo(lv_obj_t *p)
{
    makeHeading(p, "システム情報");

    const char *icons[6] = {icon::kBattery, icon::kWifi, icon::kSdCard,
                            icon::kSchedule, icon::kRestart, icon::kInfo};
    const char *names[6] = {"電池", "Wi-Fi", "SD カード", "時計", "前回の起動", "ソフトの版"};
    // 日本語は 1 文字 3 バイト。「2 時間前（電源投入）」だけで 30 バイト近くになる
    char values[6][56];

    const uint32_t mv = battery::millivolts();
    snprintf(values[0], sizeof(values[0]), "%lu.%02lu V (%d%%)", (unsigned long)(mv / 1000),
             (unsigned long)(mv % 1000 / 10), battery::percent());
    if (net::wifiConnected()) {
        snprintf(values[1], sizeof(values[1]), "接続中 %d dBm", net::rssi());
    } else {
        snprintf(values[1], sizeof(values[1]), "未接続");
    }
    if (sdlog::mounted()) {
        const uint32_t mb = (uint32_t)(sdlog::freeBytes() / (1024ULL * 1024ULL));
        snprintf(values[2], sizeof(values[2]), "空き %lu.%lu GB", (unsigned long)(mb / 1024),
                 (unsigned long)(mb % 1024 * 10 / 1024));
    } else {
        snprintf(values[2], sizeof(values[2]), "なし");
    }
    if (net::ntpSynced()) {
        snprintf(values[3], sizeof(values[3]), "自動で同期済み");
    } else if (net::timeSynced()) {
        snprintf(values[3], sizeof(values[3]), "時計チップ");
    } else {
        snprintf(values[3], sizeof(values[3]), "未設定");
    }
    char up[24];
    uptimeText(up, sizeof(up));
    snprintf(values[4], sizeof(values[4]), "%s（%s）", up, resetReasonJa());
    snprintf(values[5], sizeof(values[5]), "%s", sysinfo::revision());

    for (int i = 0; i < 6; ++i) {
        const int16_t y = (int16_t)(92 + 40 * i);
        makeIconLabel(p, Rect{82, y, 36, 38}, &ct_font_icons_36, CT_COLOR_ACCENT_HI, icons[i]);
        makeRectLabel(p, Rect{124, y, 120, 38}, &ct_font_jp_20, CT_COLOR_TEXT, names[i],
                      LV_TEXT_ALIGN_LEFT);
        makeRectLabel(p, Rect{248, y, 150, 38}, &ct_font_jp_20, CT_COLOR_SUBTEXT, values[i],
                      LV_TEXT_ALIGN_RIGHT);
    }
    makeMenuBackButton(p);
}

// --- 子画面の入口 -----------------------------------------------------------

void buildSub()
{
    lv_obj_t *p = s_sub_content;
    switch (s_sub) {
    case SettingsSub::Brightness:
        buildBrightness(p);
        break;
    case SettingsSub::DimTimeout:
        buildChoice(p, "画面を暗くする", kDimLabels, settings::kDimChoiceCount,
                    settings::dimChoice());
        break;
    case SettingsSub::TimeSet:
        buildTimeSet(p);
        break;
    case SettingsSub::Sound:
        buildChoice(p, "操作音", kOnOff, 2, settings::sound() ? 1 : 0);
        break;
    case SettingsSub::MaxCups:
        buildMaxCups(p);
        break;
    case SettingsSub::Morning:
        buildChoice(p, "日付が変わったら", kMorning, 2, settings::morningFull() ? 1 : 0, kMorningNote);
        break;
    case SettingsSub::Wifi:
        buildWifi(p);
        break;
    case SettingsSub::SystemInfo:
        buildSystemInfo(p);
        break;
    }
}

void listLoadCb(lv_event_t *e)
{
    if (lv_event_get_target(e) != s_list_screen) {
        return;
    }
    rebuildList();      // 子画面で値を変えて戻ってきたときに表示を合わせる
}

void listDeletedCb(lv_event_t *e)
{
    // 遷移中に次の画面が作られていることがあるので、自分の画面か必ず照合する
    if (lv_event_get_target(e) != s_list_screen) {
        return;
    }
    s_list_screen = nullptr;
    s_list_content = nullptr;
}

void subDeletedCb(lv_event_t *e)
{
    if (lv_event_get_target(e) != s_sub_screen) {
        return;
    }
    s_sub_screen = nullptr;
    s_sub_content = nullptr;
    s_bright_value = nullptr;
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

lv_obj_t *createSettingsScreen()
{
    lv_obj_t *scr = makeScreen();
    s_list_screen = scr;
    s_list_content = makeContent(scr);
    buildList();
    lv_obj_add_event_cb(scr, listLoadCb, LV_EVENT_SCREEN_LOAD_START, nullptr);
    lv_obj_add_event_cb(scr, listDeletedCb, LV_EVENT_DELETE, nullptr);
    return scr;
}

lv_obj_t *createSettingsSubScreen()
{
    lv_obj_t *scr = makeScreen();
    s_sub_screen = scr;
    s_sub_content = makeContent(scr);
    s_bright_value = nullptr;
    buildSub();
    lv_obj_add_event_cb(scr, subDeletedCb, LV_EVENT_DELETE, nullptr);
    return scr;
}

void pushSettingsSub(SettingsSub which)
{
    s_sub = which;
    if (which == SettingsSub::TimeSet) {
        // 今のシステム時刻から編集を始める（時刻が未設定なら 2026-01-01 12:00 から）
        time_t now = time(nullptr);
        if (now < 1700000000) {
            struct tm start = {};
            start.tm_year = 126;
            start.tm_mon = 0;
            start.tm_mday = 1;
            start.tm_hour = 12;
            start.tm_isdst = -1;
            now = mktime(&start);
        }
        localtime_r(&now, &s_edit);
        s_edit_date = false;
    }
    push(createSettingsSubScreen);
}

}  // namespace ui
