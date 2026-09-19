#include "NetService.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
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

static constexpr uint32_t kReportRetryMs = 30 * 1000;           // 送信失敗時は 30 秒後に再送
static constexpr int kReportQueueLen = 32;

struct Report {
    char event[8];
    char id[20];        // 再送時の重複防止用 (起動ごとの乱数-連番)
    uint32_t taken;
    uint32_t left;
    uint32_t prev;      // イベント前の残り杯数
    uint32_t ts;        // 端末時刻 (UNIX 秒)。未同期なら 0
};

static QueueHandle_t s_reports = nullptr;
static uint32_t s_boot_id = 0;
static uint32_t s_report_seq = 0;
static uint32_t s_next_report_ms = 0;
static String s_device_id;

static bool s_time_configured = false;
static uint32_t s_next_weather_ms = 0;
static wl_status_t s_last_status = WL_IDLE_STATUS;

void begin()
{
    s_reports = xQueueCreate(kReportQueueLen, sizeof(Report));
    s_boot_id = esp_random();
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char id[16];
    snprintf(id, sizeof(id), "coffee-%02x%02x%02x", mac[3], mac[4], mac[5]);
    s_device_id = id;

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

void reportEvent(const char *event, uint32_t taken, uint32_t left, uint32_t prev)
{
    if (s_reports == nullptr) {
        return;
    }
    Report r = {};
    strlcpy(r.event, event, sizeof(r.event));
    snprintf(r.id, sizeof(r.id), "%08lx-%lu", (unsigned long)s_boot_id, (unsigned long)++s_report_seq);
    r.taken = taken;
    r.left = left;
    r.prev = prev;
    r.ts = timeSynced() ? (uint32_t)time(nullptr) : 0;
    if (xQueueSend(s_reports, &r, 0) != pdTRUE) {
        Serial.println("[GAS] queue full, event dropped");
    }
}

static bool sendReport(const Report &r)
{
    JsonDocument doc;
    doc["token"] = GAS_TOKEN;
    doc["device"] = s_device_id;
    doc["event"] = r.event;
    doc["id"] = r.id;
    doc["taken"] = r.taken;
    doc["left"] = r.left;
    doc["prev"] = r.prev;
    doc["rssi"] = WiFi.RSSI();
    if (r.ts != 0) {
        doc["ts"] = r.ts;
    }
    String payload;
    serializeJson(doc, payload);

    // GAS は POST を処理したあと、結果を別ホスト (script.googleusercontent.com) へ 302 で渡す。
    // HTTPClient の自動追従はヘッダーを引き継いで 400 になるため、転送先は新しい接続で GET する。
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(15000);
    if (!http.begin(client, GAS_URL)) {
        return false;
    }
    const char *collect[] = {"Location"};
    http.collectHeaders(collect, 1);
    http.addHeader("Content-Type", "application/json");
    int status = http.POST(payload);
    String resp;
    if (status == HTTP_CODE_FOUND || status == HTTP_CODE_MOVED_PERMANENTLY || status == HTTP_CODE_SEE_OTHER) {
        const String location = http.header("Location");
        http.end();
        WiFiClientSecure client2;
        client2.setInsecure();
        HTTPClient http2;
        http2.setTimeout(15000);
        if (location.isEmpty() || !http2.begin(client2, location)) {
            return false;
        }
        status = http2.GET();
        resp = status > 0 ? http2.getString() : String();
        http2.end();
    } else {
        resp = status > 0 ? http.getString() : String();
        http.end();
    }

    const bool ok = status == HTTP_CODE_OK && resp.indexOf("\"ok\":true") >= 0;
    Serial.printf("[GAS] %s %s left=%lu -> HTTP %d %s\n", r.event, r.id, (unsigned long)r.left, status,
                  ok ? "OK" : resp.substring(0, 80).c_str());
    return ok;
}

static void pollReports()
{
    if (s_reports == nullptr || strlen(GAS_URL) == 0) {
        return;
    }
    if ((int32_t)(millis() - s_next_report_ms) < 0) {
        return;
    }
    Report r;
    while (xQueuePeek(s_reports, &r, 0) == pdTRUE) {
        if (!sendReport(r)) {
            s_next_report_ms = millis() + kReportRetryMs;
            return;
        }
        xQueueReceive(s_reports, &r, 0);
    }
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

    pollReports();

    if ((int32_t)(millis() - s_next_weather_ms) < 0) {
        return false;
    }
    const bool ok = fetchWeather(out);
    s_next_weather_ms = millis() + (ok ? kWeatherIntervalMs : kWeatherRetryMs);
    return ok;
}

}  // namespace net
