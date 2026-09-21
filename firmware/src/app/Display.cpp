#include "Display.h"

#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lvgl.h>

#include "Settings.h"
#include "lvgl_v8_port.h"

namespace display {

namespace {

// 暗転中の明るさ。0 にすると「壊れた」と思われるので、うっすら点けたままにする。
// バックライトの消費はおおむね明るさに比例するので、節電の効果はほぼそのまま残る
constexpr uint8_t kDimPercent = 5;

esp_panel::board::Board *s_board = nullptr;
SemaphoreHandle_t s_mux = nullptr;      // 明るさの書き込みを直列化する（2 コアから呼ばれる）

uint8_t s_bright = 100;                 // 設定の明るさ
uint32_t s_dim_s = 300;                 // 暗くするまでの秒数。0 なら暗くしない
int s_applied = -1;                     // 最後にハードへ書いた値（無駄な I2C/LEDC 操作を避ける）
bool s_dimmed = false;
bool s_privacy_cut = false;
bool s_game_active = false;
uint32_t s_last_activity_ms = 0;

lv_obj_t *s_catcher = nullptr;          // 暗い間だけ最前面に置く透明な板

uint32_t nowMs()
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ミューテックスを取った中だけで呼ぶこと
void applyLocked()
{
    const int pct = s_privacy_cut ? 0 : (s_dimmed ? (int)kDimPercent : (int)s_bright);
    if (pct == s_applied || s_board == nullptr) {
        return;
    }
    s_applied = pct;
    auto *bl = s_board->getBacklight();
    if (bl != nullptr) {
        bl->setBrightness(pct);
    }
}

void apply()
{
    if (s_mux == nullptr) {
        applyLocked();
        return;
    }
    xSemaphoreTake(s_mux, portMAX_DELAY);
    applyLocked();
    xSemaphoreGive(s_mux);
}

// LVGL のロックを取った状態で呼ぶこと
void removeCatcher()
{
    if (s_catcher == nullptr) {
        return;
    }
    lv_obj_del(s_catcher);
    s_catcher = nullptr;
}

void catcherCb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        // 暗い間の最初のタッチは「明るくするだけ」。下のボタンには届かせない
        s_dimmed = false;
        s_last_activity_ms = nowMs();
        apply();
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        // 指を離してから板を外す。押されている途中で消すと、LVGL が押し直しと
        // 見なして下のボタンへ届いてしまう
        removeCatcher();
    }
}

// LVGL のロックを取った状態で呼ぶこと
void addCatcher()
{
    if (s_catcher != nullptr) {
        return;
    }
    // 最前面レイヤーは画面を切り替えても残るので、遷移中でも取りこぼさない
    lv_obj_t *top = lv_layer_top();
    s_catcher = lv_obj_create(top);
    lv_obj_remove_style_all(s_catcher);
    lv_obj_set_pos(s_catcher, 0, 0);
    lv_obj_set_size(s_catcher, LV_HOR_RES, LV_VER_RES);
    lv_obj_clear_flag(s_catcher, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_catcher, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_catcher, catcherCb, LV_EVENT_ALL, nullptr);
}

}  // namespace

void begin(esp_panel::board::Board *board)
{
    s_board = board;
    if (s_mux == nullptr) {
        s_mux = xSemaphoreCreateMutex();
    }
    s_last_activity_ms = nowMs();
    applySettings();
}

void applySettings()
{
    s_bright = settings::brightness();
    s_dim_s = settings::dimSeconds();
    s_dimmed = false;
    s_last_activity_ms = nowMs();
    apply();
}

void noteActivity()
{
    s_last_activity_ms = nowMs();
    if (s_dimmed) {
        s_dimmed = false;
        apply();
    }
}

bool dimmed()
{
    return s_dimmed;
}

void setGameActive(bool active)
{
    s_game_active = active;
    if (active) {
        noteActivity();
    }
}

void poll()
{
    // 指を離したあとに透明な板を片付ける（catcherCb の取りこぼし対策）。
    // タッチの生データは LVGL の read_cb が写した最新の 1 件を見る（GT911 を直接読まない）
    lvgl_port_touch_sample_t sample = {};
    const bool have = lvgl_port_get_touch_sample(&sample);
    if (!s_dimmed && s_catcher != nullptr && (!have || !sample.down)) {
        removeCatcher();
    }

    if (s_privacy_cut) {
        return;     // 覗き見防止で消している間は自動暗転の出番はない
    }
    // ゲーム画面が出ている間は暗くしない（読み物の途中・秘密の受け渡し中のため）
    if (s_dim_s == 0 || s_game_active) {
        if (s_dimmed) {
            s_dimmed = false;
            removeCatcher();
            apply();
        }
        return;
    }
    // 無操作の長さは LVGL が持っている値を使う（loop は 200ms 間隔なので、
    // タッチの生データを自分で見張ると短いタップを取りこぼす）。
    // noteActivity() で入れ直した時刻の方が新しければそちらを優先する
    uint32_t idle_ms = lv_disp_get_inactive_time(nullptr);
    const uint32_t since_note = (uint32_t)(nowMs() - s_last_activity_ms);
    if (since_note < idle_ms) {
        idle_ms = since_note;
    }
    if (!s_dimmed && idle_ms >= s_dim_s * 1000UL) {
        s_dimmed = true;
        addCatcher();
        apply();
    }
}

void privacyCut()
{
    if (s_mux != nullptr) {
        xSemaphoreTake(s_mux, portMAX_DELAY);
    }
    s_privacy_cut = true;
    applyLocked();
    if (s_mux != nullptr) {
        xSemaphoreGive(s_mux);
    }
}

void privacyRestore()
{
    if (s_mux != nullptr) {
        xSemaphoreTake(s_mux, portMAX_DELAY);
    }
    s_privacy_cut = false;
    // 覗き見防止から戻るときは 100% ではなく設定の明るさに戻す。
    // 自動暗転も解除する（秘密の受け渡しの直後に暗くなると操作が途切れるため）
    s_dimmed = false;
    s_last_activity_ms = nowMs();
    applyLocked();
    if (s_mux != nullptr) {
        xSemaphoreGive(s_mux);
    }
}

bool privacyCutActive()
{
    return s_privacy_cut;
}

}  // namespace display
