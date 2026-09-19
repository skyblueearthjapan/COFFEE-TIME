/**
 * Wi-Fi 受信の切り分け用テスト。画面・タッチを一切初期化せず、5 秒ごとにスキャンする。
 * 周囲の Wi-Fi 名はログに出さず、件数・チャンネル・電波強度だけを出す。
 */
#include <Arduino.h>
#include <WiFi.h>

void setup()
{
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
}

void loop()
{
    const int n = WiFi.scanNetworks(false, true);
    Serial.printf("[WIFITEST] %d networks:", n);
    for (int i = 0; i < n && i < 12; ++i) {
        Serial.printf(" ch%d/%d", WiFi.channel(i), WiFi.RSSI(i));
    }
    Serial.println();
    WiFi.scanDelete();
    delay(5000);
}
