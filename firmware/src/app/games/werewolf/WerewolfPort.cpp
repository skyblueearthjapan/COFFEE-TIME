#include "WerewolfPort.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

#include "../../Display.h"
#include "../../lvgl_v8_port.h"
#include "../../ui/ScreenManager.h"
#include "WerewolfContent.h"

namespace werewolf {

namespace {

using coffee::wolf::TouchSample;
namespace layout = coffee::wolf::layout;
namespace rules = coffee::wolf::rules;

// 端末に 1 つだけの実体。ゲーム画面と main.cpp が共有する
EspPort s_port;

// 中立画面の走査完了を待つ時間。RGB パネルは 60Hz 前後（1 フレーム約 16.7ms）で
// フレームバッファを走査し続けるため、書き換えてから 2 フレーム分は待たないと
// 直前の内容が画面に残っていることがある。取りこぼしが無いよう余裕をみて 40ms。
constexpr uint32_t kNeutralScanoutWaitMs = 40;

bool insideRect(const layout::Rect &r, int16_t x, int16_t y)
{
    return x >= r.x && x < (int16_t)(r.x + r.w) && y >= r.y && y < (int16_t)(r.y + r.h);
}

}  // namespace

uint64_t EspPort::monotonicNowMs()
{
    // esp_timer は 64bit マイクロ秒。millis() と違い 49 日で折り返さないので
    // コアの「時計が戻ったら中断」判定を誤作動させない
    return (uint64_t)(esp_timer_get_time() / 1000);
}

bool EspPort::randomWord(uint32_t &out)
{
    // esp_random() は Wi-Fi/BT の RF が動いているときだけ真の乱数になる。
    // この端末は setup() の net::begin() で常に Wi-Fi STA を起動しているため
    // （接続できていなくても RF は動いている）、配役に使える品質が確保されている。
    out = esp_random();
    return true;
}

void EspPort::cutBacklight()
{
    if (board_ == nullptr) {
        return;
    }
    // バックライトの持ち主は display:: だけ（設定の明るさ・自動暗転もここが握っている）。
    // 直に bl->off() を呼ぶと、戻すときに設定の明るさが分からなくなる
    display::privacyCut();
}

void EspPort::requestNeutralScanout(uint64_t privacy_epoch)
{
    // 呼ぶ側（制御役）が先に秘密のラベルを消し、中立の内容に差し替えてから呼ぶこと。
    // ここでは「その内容が本当に画面へ出るまで」を担保する。
    // 1) LVGL に今すぐ描かせる（次の lv_timer_handler を待たない）
    lv_refr_now(nullptr);
    // 2) 書き換えたフレームバッファがパネルに走査され終わるまで待つ
    vTaskDelay(pdMS_TO_TICKS(kNeutralScanoutWaitMs));
    // 3) ここまで来て初めて「物理的に中立」と宣言できる
    if (fence_ != nullptr) {
        fence_->completeNeutral(privacy_epoch);
    }
}

void EspPort::restoreBacklight(uint64_t verified_privacy_epoch)
{
    if (board_ == nullptr || fence_ == nullptr) {
        return;
    }
    // 中立化が完了していない epoch では絶対に点けない（消し残しが見えてしまう）
    if (!fence_->canRestore(verified_privacy_epoch)) {
        return;
    }
    // 100% ではなく「設定の明るさ」に戻す（docs/MENU_DESIGN.md §4）。
    // 判断（canRestore）は今までどおりここで行い、実際の点灯だけを display:: に任せる
    display::privacyRestore();
}

TouchSample EspPort::latestFreshDriverSample()
{
    TouchSample out;
    lvgl_port_touch_sample_t raw;
    // GT911 をここから直接読むと LVGL の read_cb と取り合いになるので、
    // read_cb が写した最新の 1 件だけを受け取る
    if (!lvgl_port_get_touch_sample(&raw)) {
        return out;   // valid = false のまま（起動直後で 1 件も無い）
    }
    out.valid = true;
    out.sampled_ms = raw.sampled_ms;
    out.down = raw.down;
    out.multiple_points = raw.points > 1;
    out.inside_hold = raw.down && insideRect(layout::kHold, raw.x, raw.y);
    return out;
}

bool EspPort::readPublicMeta(std::array<uint8_t, 20> &bytes, bool &exists)
{
    exists = false;
    bytes.fill(0);
    Preferences prefs;
    if (!prefs.begin(rules::kNvsNamespace, true)) {
        // 名前空間がまだ無い＝一度も保存していない。記録領域の異常ではない
        return true;
    }
    const size_t length = prefs.getBytesLength(rules::kNvsKey);
    if (length == 0) {
        prefs.end();
        return true;
    }
    if (length != bytes.size()) {
        prefs.end();
        return false;   // 想定外の長さ。壊れているとみなす
    }
    const size_t read = prefs.getBytes(rules::kNvsKey, bytes.data(), bytes.size());
    prefs.end();
    if (read != bytes.size()) {
        return false;
    }
    exists = true;
    return true;
}

bool EspPort::writePublicMetaAndReadBack(const std::array<uint8_t, 20> &bytes)
{
    Preferences prefs;
    if (!prefs.begin(rules::kNvsNamespace, false)) {
        return false;
    }
    const size_t written = prefs.putBytes(rules::kNvsKey, bytes.data(), bytes.size());
    std::array<uint8_t, 20> back{};
    const size_t read = prefs.getBytes(rules::kNvsKey, back.data(), back.size());
    prefs.end();
    // 「配った」印は確実に残っていないと、電源断のあと局を無効にできない。書き戻して照合する
    return written == bytes.size() && read == bytes.size() && back == bytes;
}

void EspPort::inhibitDeepSleep(bool inhibit)
{
    (void)inhibit;
    // 省電力（ディープスリープ）はまだ導入していないので何もしない
}

void EspPort::openExistingCafePanel()
{
    ui::goHome();
}

bool EspPort::releasePriorSinglePlayerSession()
{
    // 1 人用の別ゲームはまだ無いので、解放すべきものが常に無い
    return true;
}

EspPort &port()
{
    return s_port;
}

void setBoard(esp_panel::board::Board *board)
{
    s_port.setBoard(board);
}

uint8_t readLastMode()
{
    Preferences prefs;
    if (!prefs.begin(rules::kNvsNamespace, true)) {
        return 0;   // まだ一度も保存していない
    }
    const uint8_t mode = prefs.getUChar("mode", 0);
    prefs.end();
    return mode <= 1 ? mode : 0;
}

void writeLastMode(uint8_t mode)
{
    Preferences prefs;
    if (!prefs.begin(rules::kNvsNamespace, false)) {
        return;
    }
    prefs.putUChar("mode", mode <= 1 ? mode : 0);
    prefs.end();
}

}  // namespace werewolf
