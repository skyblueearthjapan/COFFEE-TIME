/**
 * COFFEE TIME — 段階1「+1 カウンター」
 * ボード初期化と LVGL 移植は ESP32_Display_Panel v1.0.4 の lvgl_v8/simple_port を踏襲し、
 * UI 生成部分だけを CoffeeCounterUI に置き換えている。
 */

#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "CoffeeCounterUI.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

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

    Serial.println("Creating UI");
    if (!lvgl_port_lock(-1)) {
        Serial.println("ERROR: LVGL lock failed");
        return;
    }
    const bool ui_ok = coffee_counter_create();
    lvgl_port_unlock();
    if (!ui_ok) {
        Serial.println("ERROR: COFFEE TIME UI creation failed");
        return;
    }
    Serial.println("COFFEE TIME ready");
}

void loop()
{
    delay(1000);
}
