/**
 * COFFEE TIME — 段階2「時計・天気・残り杯数」
 * ボード初期化と LVGL 移植は ESP32_Display_Panel v1.0.4 の lvgl_v8/simple_port を踏襲している。
 */

#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "Battery.h"
#include "CupState.h"
#include "HomeScreen.h"
#include "NetService.h"
#include "RtcClock.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

static bool s_ready = false;

// 開発用：シリアルで 'S' を受け取ったら画面をそのまま送る（tools/snapshot.py で PNG 化）
static void sendSnapshot()
{
    lv_obj_t *scr = lv_scr_act();
    const uint32_t size = lv_snapshot_buf_size_needed(scr, LV_IMG_CF_TRUE_COLOR);
    uint8_t *buf = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (buf == nullptr) {
        Serial.println("[SNAP] no memory");
        return;
    }
    lv_img_dsc_t dsc;
    if (lv_snapshot_take_to_buf(scr, LV_IMG_CF_TRUE_COLOR, &dsc, buf, size) == LV_RES_OK) {
        Serial.printf("\n[SNAP] %d %d %lu\n", dsc.header.w, dsc.header.h, (unsigned long)dsc.data_size);
        Serial.write(dsc.data, dsc.data_size);
        Serial.flush();
        Serial.println("\n[SNAP] END");
    } else {
        Serial.println("[SNAP] failed");
    }
    heap_caps_free(buf);
}

void setup()
{
    Serial.begin(115200);
    // 時刻は日本時間で扱う（Wi-Fi 接続前・RTC から復元した時刻にも適用するため最初に設定）
    setenv("TZ", "JST-9", 1);
    tzset();

    Serial.println("Initializing board");
    Board *board = new Board();
    board->init();
#if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = board->getLCD();
    // When avoid tearing function is enabled, the frame buffer number should be set in the board driver
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto lcd_bus = lcd->getBus();
    if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
    }
#endif
#endif
    if (!board->begin()) {
        Serial.println("ERROR: board begin failed");
        return;
    }

    // Wi-Fi が無くても日付が分かるよう、時計チップから時刻を復元（I2C は board->begin() で初期化済み）
    rtc::restoreSystemTime();

    Serial.println("Initializing LVGL");
    if (!lvgl_port_init(board->getLCD(), board->getTouch())) {
        Serial.println("ERROR: LVGL port init failed");
        return;
    }

    cup::load();
    battery::begin();
    battery::update();

    Serial.println("Creating UI");
    if (!lvgl_port_lock(-1)) {
        Serial.println("ERROR: LVGL lock failed");
        return;
    }
    const bool ui_ok = home::create();
    lvgl_port_unlock();
    if (!ui_ok) {
        Serial.println("ERROR: COFFEE TIME UI creation failed");
        return;
    }

    net::begin();
    s_ready = true;
    Serial.println("COFFEE TIME ready");
}

void loop()
{
    if (!s_ready) {
        delay(1000);
        return;
    }

    static uint32_t s_last_bat_ms = 0;
    static uint32_t s_last_bat_log_ms = 0;
    if (millis() - s_last_bat_ms >= 1000) {
        s_last_bat_ms = millis();
        battery::update();
        if (millis() - s_last_bat_log_ms >= 5000) {
            s_last_bat_log_ms = millis();
            Serial.printf("[BAT] %lu mV (%d%%)\n", (unsigned long)battery::millivolts(), battery::percent());
        }
    }

    net::Weather weather;
    const bool got_weather = net::poll(weather);

    if (lvgl_port_lock(-1)) {
        if (got_weather) {
            home::setWeather(weather);
        }
        cup::saveIfDirty();
        while (Serial.available() > 0) {
            switch (Serial.read()) {
            case 'S': sendSnapshot(); break;
            // 開発用：背景の時間帯を固定 M=朝 N=昼 E=夕方 A=自動
            case 'M': home::debugForceHour(8); break;
            case 'N': home::debugForceHour(13); break;
            case 'E': home::debugForceHour(19); break;
            case 'A': home::debugForceHour(-1); break;
            // 開発用：T=+1 / R=補充（通知やシート記録も実際に行われる）
            case 'T': home::debugTake(); break;
            case 'R': home::debugRefill(); break;
            case 'W': net::debugScan(); break;       // 開発用：Wi-Fi スキャン
            default: break;
            }
        }
        lvgl_port_unlock();
    }
    delay(200);
}
