#include "NetService.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiMulti.h>
#include <esp_sntp.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <time.h>

#include "RtcClock.h"
#include "Settings.h"
#include "secrets.h"

// 2 つ目の Wi-Fi（自宅など）は任意。secrets.h に無ければ使わない
#ifndef WIFI_SSID2
#define WIFI_SSID2 ""
#define WIFI_PASSWORD2 ""
#endif
// 3 つ目（スマホのテザリングなど）も任意
#ifndef WIFI_SSID3
#define WIFI_SSID3 ""
#define WIFI_PASSWORD3 ""
#endif

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
    uint16_t max;       // そのときの「1 回に作る杯数」（GAS のメールのゲージに使う）
};

static QueueHandle_t s_reports = nullptr;
static uint32_t s_boot_id = 0;
static uint32_t s_report_seq = 0;
static uint32_t s_next_report_ms = 0;
static String s_device_id;

static WiFiMulti s_wifi_multi;
static uint32_t s_next_wifi_try_ms = 0;
static volatile bool s_ntp_synced = false;     // SNTP のコールバックで立て、loop 側で RTC に保存する
static volatile bool s_ntp_ever = false;       // 一度でも NTP で合ったか（システム情報の「時計」表示用）
static int s_registered = 0;                   // 登録済み Wi-Fi の数（名前は保持しない）

static bool s_time_configured = false;
static uint32_t s_next_weather_ms = 0;
static wl_status_t s_last_status = WL_IDLE_STATUS;

// --- GAS への「1 件だけ」の依頼箱（ゲームに依存しない）----------------------
//
// LVGL タスクが gasRequest で預け、loop() 側の pollGasRequest が送受信し、
// LVGL タスクが gasTakeResult で受け取る。LVGL 側は必ず待ち時間 0 で錠を取り、
// 取れなければ「今回は見送り」にする。**LVGL のコールバックは絶対に待たない**。
enum class GasSlot : uint8_t {
    Idle = 0,     // 空
    Pending,      // 依頼を預かった（まだ送っていない）
    Sending,      // loop 側が送信中
    Cancelled,    // 送信中に捨てられた。返事が来ても使わない
    Done,         // 返事が揃った
};

// ゲーム中は Wi-Fi の省電力（受信の間引き）を切る。LVGL タスクが旗を立て、loop 側の poll() が切り替える。
// 入れたままだと GAS の転送先からの返事が 5 秒待っても届かない失敗が 10 回に 3〜5 回起きた（2026-09-22 実機。
// 切ると 8 回に 1 回）。電池の持ちと引き換えなので、ゲームの画面が開いている間と、GAS とやり取りする間だけにする
static volatile bool s_want_low_latency = false;
static bool s_low_latency = false;

static SemaphoreHandle_t s_gas_lock = nullptr;
static volatile GasSlot s_gas_slot = GasSlot::Idle;
// 投げっぱなしの依頼（返事を取りに来ない）。送り終えたら箱をすぐ空ける
static bool s_gas_detached = false;
static uint32_t s_gas_req = 0;
static uint32_t s_gas_started_ms = 0;
static char s_gas_body[kGasRequestMax];        // 預かった依頼（LVGL タスクが書く）
static char s_gas_sending[kGasRequestMax];     // 送信用に写したもの（loop だけが触る）
static GasResult s_gas_result;

void begin()
{
    s_reports = xQueueCreate(kReportQueueLen, sizeof(Report));
    s_gas_lock = xSemaphoreCreateMutex();
    s_boot_id = esp_random();
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char id[16];
    snprintf(id, sizeof(id), "coffee-%02x%02x%02x", mac[3], mac[4], mac[5]);
    s_device_id = id;

    WiFi.mode(WIFI_STA);
    // 再接続は poll() の WiFiMulti に任せる。ドライバーの自動再接続を有効にすると、つながらない間ずっと
    // 裏で接続を試み続け、その間のスキャンが「0 件」になって他の登録先を見つけられなくなる
    WiFi.setAutoReconnect(false);
    // 切断・接続失敗の理由コードを記録する（Wi-Fi 名は出さない）。
    // 主な値: 2=認証期限切れ 15=4way ハンドシェイク失敗(パスワード違いが多い) 201=見つからない 202=認証失敗 205=接続失敗
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
        Serial.printf("[NET] disconnected, reason %d\n", (int)info.wifi_sta_disconnected.reason);
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    // 登録済みの Wi-Fi（最大 3 つ）のうち、見つかった電波の強い方へ poll() の WiFiMulti がつなぐ。
    // 2 つ目・3 つ目は自宅やスマホのテザリング用。ESP32 は 2.4GHz 専用なので、iPhone は「互換性を優先」をオンにすること
    s_wifi_multi.addAP(WIFI_SSID, WIFI_PASSWORD);
    int registered = 1;
    if (strlen(WIFI_SSID2) > 0) {
        s_wifi_multi.addAP(WIFI_SSID2, WIFI_PASSWORD2);
        ++registered;
    }
    if (strlen(WIFI_SSID3) > 0) {
        s_wifi_multi.addAP(WIFI_SSID3, WIFI_PASSWORD3);
        ++registered;
    }
    s_registered = registered;
    Serial.printf("[NET] %d Wi-Fi network(s) registered\n", registered);
}

int rssi()
{
    return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
}

int registeredCount()
{
    return s_registered;
}

bool ntpSynced()
{
    return s_ntp_ever;
}

void debugScan()
{
    // 周囲の Wi-Fi 名は記録に残さず、登録済みの Wi-Fi が見えるかと電波の強さだけを出す
    WiFi.scanDelete();
    int n = WiFi.scanNetworks(false, true);
    // 自動接続のスキャンと重なると -1（実行中）が返るので、完了まで待つ
    const uint32_t start = millis();
    while (n < 0 && millis() - start < 10000) {
        delay(100);
        n = WiFi.scanComplete();
    }
    Serial.printf("[SCAN] %d networks found\n", n);
    for (int i = 0; i < n; ++i) {
        const String ssid = WiFi.SSID(i);
        const char *tag = ssid == WIFI_SSID ? "WIFI_SSID (1)"
                        : (strlen(WIFI_SSID2) > 0 && ssid == WIFI_SSID2) ? "WIFI_SSID2 (2)"
                        : (strlen(WIFI_SSID3) > 0 && ssid == WIFI_SSID3) ? "WIFI_SSID3 (3)" : "other";
        Serial.printf("[SCAN] %-14s ch=%2d rssi=%d\n", tag, WiFi.channel(i), WiFi.RSSI(i));
        // 端末のすぐ近く（-65dBm より強い）にあるのに登録外の電波は、自分のスマホのテザリングで名前が
        // 食い違っている可能性が高い。照合できるよう、その名前と文字数だけは表示する
        if (strcmp(tag, "other") == 0 && WiFi.RSSI(i) > -65) {
            Serial.printf("[SCAN]   nearby unregistered name: \"%s\" (%u bytes)\n", ssid.c_str(), (unsigned)ssid.length());
        }
    }
    WiFi.scanDelete();
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
    // 端末側で変えられるようになったので、そのときの上限も送る（GAS 側は Code.gs の
    // MAX_CUPS ではなく、届いた max を使うように直すこと）
    r.max = settings::maxCups();
    if (xQueueSend(s_reports, &r, 0) != pdTRUE) {
        Serial.println("[GAS] queue full, event dropped");
    }
}

static bool sendReportInner(const Report &r);

// コーヒーの記録も同じ作り（POST → 転送先を GET）なので、送る間だけ Wi-Fi の省電力を切る
static bool sendReport(const Report &r)
{
    WiFi.setSleep(false);
    const bool ok = sendReportInner(r);
    WiFi.setSleep(!s_want_low_latency);
    return ok;
}

static bool sendReportInner(const Report &r)
{
    JsonDocument doc;
    doc["token"] = GAS_TOKEN;
    doc["device"] = s_device_id;
    doc["event"] = r.event;
    doc["id"] = r.id;
    doc["taken"] = r.taken;
    doc["left"] = r.left;
    doc["prev"] = r.prev;
    doc["max"] = r.max;
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
        // 1 本目の TLS（内蔵メモリを約 35KB 使う）を先に手放す。end() だけでは keep-alive で握ったままになり、
        // 2 本目の接続でメモリが足りず失敗する（2026-09-22 実機: tls=-16、空き 62KB / 最大の連続 31KB）
        client.stop();
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

// ---------------------------------------------------------------------------
// GAS への「1 件だけ」の依頼（AI DUEL の予測など）
//
// 送り方は sendReport とまったく同じ（POST → 302 は新しい接続で GET）。
// **URL・合言葉・本文はログに出さない**。出すのは依頼番号と HTTP の番号と所要時間だけ。
//
// GAS は Jev の呼び出しとシートへの書き込みを**全部終えてから** 302 を返すので、
// POST 側に時間がかかる（実測 4.0〜5.7 秒・端末の電波が弱いと 5.5 秒）。
// 2026-09-22 の実機試験では POST 4.5 秒で毎回間に合わず、全ラウンドが統計 AI に
// 落ちていた。そこで POST 6.5 秒 + 転送先の GET 5 秒 = 往復 11.5 秒までを上限にする。
// ゲーム側はさらに 9 秒で見切って端末内の統計 AI に切り替えるので、
// 遅れて届いた返事は依頼番号で捨てられる。
// ---------------------------------------------------------------------------
static bool sendGas(const char *body, GasResult &out)
{
    // 依頼の本文（`{ … }`）に token と device を足して 1 つの JSON にする
    const char *start = strchr(body, '{');
    const char *end = strrchr(body, '}');
    if (start == nullptr || end == nullptr || end <= start) {
        return false;
    }
    String payload;
    payload.reserve(strlen(body) + 96);
    payload += "{\"token\":\"";
    payload += GAS_TOKEN;
    payload += "\",\"device\":\"";
    payload += s_device_id;
    payload += "\"";
    for (const char *q = start + 1; q < end; ++q) {
        if (q == start + 1) {
            // 中身が空（`{}`）なら何も足さない
            const char *probe = q;
            while (probe < end && (*probe == ' ' || *probe == '\n' || *probe == '\t')) {
                ++probe;
            }
            if (probe >= end) {
                break;
            }
            payload += ',';
        }
        payload += *q;
    }
    payload += '}';

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    // setTimeout は**読み取りの待ち時間だけ**。つなぐところで止まらないよう別に上限を付ける
    http.setConnectTimeout(5000);   // 3 秒だと iPhone のテザリングで TLS の接続が間に合わないことが多かった（2026-09-23 実機: 電波 -52dBm でも 2 回に 1 回）
    http.setTimeout(6500);      // GAS は Jev とシート書き込みを終えてから 302 を返す
    if (!http.begin(client, GAS_URL)) {
        return false;
    }
    const char *collect[] = {"Location"};
    http.collectHeaders(collect, 1);
    http.addHeader("Content-Type", "application/json");
    int status = http.POST(payload);
    if (status < 0) {
        // つながらなかった理由の手がかり（TLS のエラー番号と、内蔵メモリの空き / 最大の連続領域）。
        // TLS は内蔵メモリを数十 KB 使うので、空きが足りないと接続の段階で失敗する
        char tls[64] = "";
        const int tls_code = client.lastError(tls, sizeof(tls));
        Serial.printf("[GAS] req=%lu connect failed: tls=%d heap=%u/%u rssi=%d\n", (unsigned long)out.req,
                      tls_code, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), (int)WiFi.RSSI());
    }
    String resp;
    if (status == HTTP_CODE_FOUND || status == HTTP_CODE_MOVED_PERMANENTLY ||
        status == HTTP_CODE_SEE_OTHER) {
        const String location = http.header("Location");
        http.end();
        // 1 本目の TLS（内蔵メモリを約 35KB 使う）を先に手放す。end() だけでは keep-alive で握ったままになり、
        // 2 本目の接続でメモリが足りず失敗する（2026-09-22 実機: tls=-16、空き 62KB / 最大の連続 31KB）
        client.stop();
        if (location.isEmpty()) {
            Serial.printf("[GAS] req=%lu redirect unusable (no location)\n", (unsigned long)out.req);
            return false;
        }
        // 転送先（結果の置き場）を読む。ここだけ返事が来ない失敗 (-11) がときどき起きる（省電力を切っても 8 回に 1 回）。
        // GAS 側の処理は POST の時点で済んでいるので、POST からやり直さずに**読み取りだけ**を新しい接続でもう 1 回試す。
        // ふだんの読み取りは 1.5 秒以内に終わるので、1 回目の待ちは 3 秒に詰めてある（合計の上限は変えない）
        static constexpr uint32_t kGetReadMs[2] = {3000, 4000};
        for (int attempt = 0; attempt < 2; ++attempt) {
            WiFiClientSecure client2;
            client2.setInsecure();
            HTTPClient http2;
            http2.setConnectTimeout(5000);
            http2.setTimeout(kGetReadMs[attempt]);
            if (!http2.begin(client2, location)) {
                Serial.printf("[GAS] req=%lu redirect unusable (location %u bytes)\n", (unsigned long)out.req,
                              (unsigned)location.length());
                return false;
            }
            status = http2.GET();
            if (status < 0) {
                char tls[64] = "";
                const int tls_code = client2.lastError(tls, sizeof(tls));
                Serial.printf("[GAS] req=%lu redirect GET failed (try %d): http=%d tls=%d heap=%u/%u rssi=%d\n",
                              (unsigned long)out.req, attempt + 1, status, tls_code,
                              (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), (int)WiFi.RSSI());
            }
            resp = status > 0 ? http2.getString() : String();
            http2.end();
            client2.stop();
            if (status > 0) {
                break;
            }
        }
    } else {
        resp = status > 0 ? http.getString() : String();
        http.end();
    }
    out.status = status;
    out.ok = status == HTTP_CODE_OK && resp.length() > 0;
    strlcpy(out.body, resp.c_str(), sizeof(out.body));
    Serial.printf("[GAS] req=%lu -> HTTP %d (%u bytes)\n", (unsigned long)out.req, status,
                  (unsigned)resp.length());
    return out.ok;
}

void setLowLatency(bool on)
{
    s_want_low_latency = on;
}

bool gasReady()
{
    return strlen(GAS_URL) > 0 && WiFi.status() == WL_CONNECTED;
}

bool gasRequest(uint32_t req, const char *body_json, bool detached)
{
    if (s_gas_lock == nullptr || body_json == nullptr || !gasReady()) {
        return false;
    }
    const size_t length = strlen(body_json);
    if (length == 0 || length >= kGasRequestMax) {
        return false;
    }
    // LVGL のコールバックから呼ばれるので**絶対に待たない**。取れなければ見送る
    if (xSemaphoreTake(s_gas_lock, 0) != pdTRUE) {
        return false;
    }
    bool accepted = false;
    if (s_gas_slot == GasSlot::Idle) {
        memcpy(s_gas_body, body_json, length + 1);
        s_gas_req = req;
        s_gas_started_ms = millis();
        s_gas_detached = detached;
        s_gas_slot = GasSlot::Pending;
        accepted = true;
    }
    xSemaphoreGive(s_gas_lock);
    return accepted;
}

bool gasTakeResult(GasResult &out)
{
    if (s_gas_lock == nullptr || xSemaphoreTake(s_gas_lock, 0) != pdTRUE) {
        return false;
    }
    bool got = false;
    if (s_gas_slot == GasSlot::Done) {
        out = s_gas_result;
        s_gas_slot = GasSlot::Idle;
        got = true;
    }
    xSemaphoreGive(s_gas_lock);
    return got;
}

bool gasCancel()
{
    if (s_gas_lock == nullptr) {
        return false;
    }
    // 錠を握るのは memcpy の間だけなので 2ms あれば十分取れる。
    // 取れなかったときは false を返し、呼び出し側が「捨てた」と決めつけないようにする
    if (xSemaphoreTake(s_gas_lock, pdMS_TO_TICKS(2)) != pdTRUE) {
        return false;
    }
    if (s_gas_slot == GasSlot::Pending || s_gas_slot == GasSlot::Done) {
        s_gas_slot = GasSlot::Idle;
    } else if (s_gas_slot == GasSlot::Sending) {
        s_gas_slot = GasSlot::Cancelled;
    }
    xSemaphoreGive(s_gas_lock);
    return true;
}

static void pollGasRequest()
{
    if (s_gas_lock == nullptr || strlen(GAS_URL) == 0) {
        return;
    }
    if (xSemaphoreTake(s_gas_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (s_gas_slot != GasSlot::Pending) {
        xSemaphoreGive(s_gas_lock);
        return;
    }
    // 送信中は錠を離す（LVGL 側が待ち時間 0 で錠を取れるようにするため）
    strlcpy(s_gas_sending, s_gas_body, sizeof(s_gas_sending));
    GasResult result;
    result.req = s_gas_req;
    const uint32_t started = s_gas_started_ms;
    const bool detached = s_gas_detached;
    s_gas_slot = GasSlot::Sending;
    xSemaphoreGive(s_gas_lock);

    // やり取りの間だけ Wi-Fi の省電力（受信の間引き）を切る。入れたままだと、GAS の転送先からの返事が
    // 5 秒待っても届かない失敗 (-11) が 10 回に 3〜5 回起きた（2026-09-22 実機。電波は -52dBm で十分だった）
    WiFi.setSleep(false);
    sendGas(s_gas_sending, result);
    WiFi.setSleep(!s_want_low_latency);     // ゲーム中（setLowLatency(true)）なら切ったままにする
    result.elapsed_ms = millis() - started;

    xSemaphoreTake(s_gas_lock, portMAX_DELAY);
    if (s_gas_slot != GasSlot::Sending) {
        s_gas_slot = GasSlot::Idle;     // 待っている間に画面が閉じた。返事は捨てる
    } else if (detached) {
        // 投げっぱなしの依頼。誰も取りに来ないので、ここで結果を 1 行出して箱を空ける
        s_gas_slot = GasSlot::Idle;
        Serial.printf("[GAS] req=%lu detached %s %lums\n", (unsigned long)result.req,
                      result.ok ? "ok" : "failed", (unsigned long)result.elapsed_ms);
    } else {
        s_gas_result = result;
        s_gas_slot = GasSlot::Done;
    }
    xSemaphoreGive(s_gas_lock);
}

bool poll(Weather &out)
{
    if (s_low_latency != s_want_low_latency && WiFi.status() == WL_CONNECTED) {
        s_low_latency = s_want_low_latency;
        WiFi.setSleep(!s_low_latency);
        Serial.printf("[NET] Wi-Fi power save %s\n", s_low_latency ? "off (game)" : "on");
    }
    // 未接続なら 10 秒ごとに周囲をスキャンして、登録済みの Wi-Fi に接続を試みる
    if (WiFi.status() != WL_CONNECTED && (int32_t)(millis() - s_next_wifi_try_ms) >= 0) {
        s_next_wifi_try_ms = millis() + 10000;
        // スマホのテザリングは認証に時間がかかることがあるので 10 秒待つ（この間 loop() は止まる）
        s_wifi_multi.run(10000);
    }
    if (s_ntp_synced) {
        s_ntp_synced = false;
        rtc::saveSystemTime();
    }

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
        // NTP で時刻が合うたびに（起動後と、以後約 1 時間ごと）時計チップへ保存する
        sntp_set_time_sync_notification_cb([](struct timeval *) { s_ntp_synced = true; s_ntp_ever = true; });
        configTzTime("JST-9", "ntp.nict.jp", "time.google.com", "pool.ntp.org");
        s_time_configured = true;
    }

    // コーヒーの記録が先。ゲームの依頼は残りの時間で行う（記録の再送は妨げない）
    pollReports();
    pollGasRequest();

    if ((int32_t)(millis() - s_next_weather_ms) < 0) {
        return false;
    }
    const bool ok = fetchWeather(out);
    s_next_weather_ms = millis() + (ok ? kWeatherIntervalMs : kWeatherRetryMs);
    return ok;
}

}  // namespace net
