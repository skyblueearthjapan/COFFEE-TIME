#include "Buzzer.h"

#include <Arduino.h>
#include <lvgl.h>

#include "Settings.h"

namespace buzzer {

namespace {

// EXIO8 = エキスパンダーのピン番号 7（0 始まり。SdLog.cpp の EXIO4 = 3 と同じ数え方）
constexpr uint8_t kExpanderPin = 7;

// ⚠ 極性は実機で未確認。鳴りっぱなし／無音のときはここだけを反転させる。
// HIGH で鳴る想定（鳴らないときは LOW に、常時鳴るときも LOW にする）
constexpr uint8_t kActiveLevel = HIGH;
constexpr uint8_t kIdleLevel = (kActiveLevel == HIGH) ? LOW : HIGH;

constexpr uint32_t kClickMs = 30;       // 1 回の長さ

esp_panel::board::Board *s_board = nullptr;
bool s_configured = false;
lv_timer_t *s_off_timer = nullptr;

// ピンを出力にするのは**最初に鳴らすときだけ**。極性が逆だった場合に、
// 操作音をオフにしている人まで起動直後から鳴りっぱなしになるのを避ける
bool ensurePin()
{
    if (s_configured) {
        return true;
    }
    if (s_board == nullptr) {
        return false;
    }
    auto expander = s_board->getIO_Expander();
    if (expander == nullptr || expander->getBase() == nullptr) {
        return false;
    }
    expander->getBase()->pinMode(kExpanderPin, OUTPUT);
    expander->getBase()->digitalWrite(kExpanderPin, kIdleLevel);
    s_configured = true;
    return true;
}

void write(uint8_t level)
{
    if (!s_configured) {
        return;
    }
    auto expander = s_board->getIO_Expander();
    if (expander == nullptr || expander->getBase() == nullptr) {
        return;
    }
    expander->getBase()->digitalWrite(kExpanderPin, level);
}

void offCb(lv_timer_t *t)
{
    write(kIdleLevel);
    s_off_timer = nullptr;
    lv_timer_del(t);        // LVGL はタイマーの自己削除に対応している
}

}  // namespace

void begin(esp_panel::board::Board *board)
{
    // ここでは Board を覚えるだけ（ピンには触らない。ensurePin() を参照）
    s_board = board;
}

void click()
{
    if (!settings::sound() || !ensurePin()) {
        return;
    }
    if (s_off_timer != nullptr) {
        // 連打されたら鳴り終わりを延ばすだけ（二重に鳴らさない）
        lv_timer_reset(s_off_timer);
        return;
    }
    write(kActiveLevel);
    s_off_timer = lv_timer_create(offCb, kClickMs, nullptr);
}

}  // namespace buzzer
