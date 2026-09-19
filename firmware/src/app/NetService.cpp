#include "NetService.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "secrets.h"

namespace net {

// 千葉県千葉市（中央区役所付近）
static constexpr const char *kWeatherUrl =
    "https://api.open-meteo.com/v1/forecast"
    "?latitude=35.6073&longitude=140.1063"
    "&current=temperature_2m,weather_code&timezone=Asia%2FTokyo";

static constexpr uint32_t kWeatherIntervalMs = 15 * 60 * 1000;  // 15 分ごと
static constexpr uint32_t kWeatherRetryMs = 60 * 1000;          // 失敗時は 1 分後に再試行

static bool s_time_configured = false;
static uint32_t s_next_weather_ms = 0;
static wl_status_t s_last_status = WL_IDLE_STATUS;

void begin()
{
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("[NET] connecting to \"%s\"\n", WIFI_SSID);
}

bool wifiConnected()
{
    return WiFi.status() == WL_CONNECTED;
}

bool timeSynced()
{
    return time(nullptr) > 1700000000;  // 2023 年以降なら同期済みとみなす
}

static bool fetchWeather(Weather &out)
{
    // 天気は公開情報のため証明書検証は省略する
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(8000);
    http.useHTTP10(true);   // chunked 転送を避け、getStream() をそのまま JSON として読めるようにする
    if (!http.begin(client, kWeatherUrl)) {
        return false;
    }
    const int status = http.GET();
    if (status != HTTP_CODE_OK) {
        Serial.printf("[NET] weather HTTP %d\n", status);
        http.end();
        return false;
    }
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    if (err) {
        Serial.printf("[NET] weather JSON error: %s\n", err.c_str());
        return false;
    }
    out.temperature = doc["current"]["temperature_2m"] | 0.0f;
    out.code = doc["current"]["weather_code"] | -1;
    out.valid = out.code >= 0;
    Serial.printf("[NET] weather %.1fC code=%d\n", out.temperature, out.code);
    return out.valid;
}

bool poll(Weather &out)
{
    const wl_status_t status = WiFi.status();
    if (status != s_last_status) {
        s_last_status = status;
        if (status == WL_CONNECTED) {
            Serial.printf("[NET] connected, IP %s RSSI %d\n",
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
        } else {
            Serial.printf("[NET] wifi status %d\n", (int)status);
        }
    }
    if (status != WL_CONNECTED) {
        return false;
    }

    if (!s_time_configured) {
        configTzTime("JST-9", "ntp.nict.jp", "time.google.com", "pool.ntp.org");
        s_time_configured = true;
    }

    if ((int32_t)(millis() - s_next_weather_ms) < 0) {
        return false;
    }
    const bool ok = fetchWeather(out);
    s_next_weather_ms = millis() + (ok ? kWeatherIntervalMs : kWeatherRetryMs);
    return ok;
}

}  // namespace net
