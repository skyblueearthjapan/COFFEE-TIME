/**
 * COFFEE TIME — 段階2「時計・天気・残り杯数」
 * ボード初期化と LVGL 移植は ESP32_Display_Panel v1.0.4 の lvgl_v8/simple_port を踏襲している。
 */

#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "CupState.h"
#include "HomeScreen.h"
#include "NetService.h"

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

    Serial.println("Initializing LVGL");
    if (!lvgl_port_init(board->getLCD(), board->getTouch())) {
        Serial.println("ERROR: LVGL port init failed");
        return;
    }

    cup::load();

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

    net::Weather weather;
    const bool got_weather = net::poll(weather);

    if (lvgl_port_lock(-1)) {
        if (got_weather) {
            home::setWeather(weather);
        }
        cup::saveIfDirty();
        while (Serial.available() > 0) {
            if (Serial.read() == 'S') {
                sendSnapshot();
            }
        }
        lvgl_port_unlock();
    }
    delay(200);
}
