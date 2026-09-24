#include "RemoteConsole.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdarg.h>

#include "CupState.h"
#include "Display.h"
#include "NetService.h"
#include "SdLog.h"
#include "lvgl_v8_port.h"
#include "secrets.h"
#include "games/cards/CardsGame.h"
#include "games/werewolf/WerewolfGame.h"
#include "ui/UpdateOverlay.h"

// 遠隔コンソールとソフトの更新の合言葉（secrets.h）。無い・空なら両方とも開かない
#ifndef REMOTE_PASSWORD
#define REMOTE_PASSWORD ""
#endif

// 差し替える前の本物のポート。ct_serial_tee.h が NO_GLOBAL_SERIAL を立てているので、
// HardwareSerial.h の `Serial` の定義と Serial0 の宣言は無い。ここで同じ選び方をする
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
static HWCDC &ctRealSerial()
{
    return HWCDCSerial;         // env app: 基板の native USB（COM8）
}
#elif ARDUINO_USB_CDC_ON_BOOT
static USBCDC &ctRealSerial()
{
    return USBSerial;
}
#else
extern HardwareSerial Serial0;
static HardwareSerial &ctRealSerial()
{
    return Serial0;             // env app_uart: 基板の USB TO UART（COM9）
}
#endif

namespace {

constexpr uint16_t kConsolePort = 2323;
constexpr uint16_t kOtaPort = 2324;
constexpr const char *kHostName = "coffee-time";    // coffee-time.local（DHCP の名前は NetService が付ける）

constexpr size_t kOutRingBytes = 16 * 1024;     // ほかのタスクのログを loop が送るまで預かる（PSRAM）
constexpr uint32_t kAuthTimeoutMs = 10000;      // 合言葉（と更新の依頼の行）を待つ時間
constexpr int kMaxFails = 3;                    // 続けて違えば
constexpr uint32_t kLockoutMs = 60000;          // この間は誰も受け付けない
constexpr uint32_t kOtaReadTimeoutMs = 10000;   // 更新のデータが途切れたら見切る
constexpr size_t kOtaChunk = 4096;

TaskHandle_t s_loop_task = nullptr;
bool s_enabled = false;

// --- 合言葉を通った相手（コンソール）-------------------------------------------
// WiFiClient に触ってよいのは loop タスクだけ。ほかのタスクの書き込みはリングに積む
WiFiClient s_client;
volatile bool s_client_live = false;
bool s_in_client_write = false;     // 送信中にもう一度 write が来たらリングへ回す（再入防止）
bool s_ota_running = false;

portMUX_TYPE s_ring_mux = portMUX_INITIALIZER_UNLOCKED;
uint8_t *s_ring = nullptr;
size_t s_ring_head = 0;             // 次に書く位置
size_t s_ring_count = 0;            // たまっているバイト数
size_t s_ring_dropped = 0;          // あふれて捨てたバイト数（あとで 1 行で知らせる）

// --- 合言葉を待っている接続（ポートごとに 1 つ）----------------------------------
struct Pending {
    WiFiClient c;
    bool active = false;
    bool authed = false;            // 更新用: 合言葉は通った。次は OTA の行
    uint32_t since = 0;
    char line[96] = {};
    size_t len = 0;
    bool overflow = false;
};

WiFiServer s_con_server(kConsolePort);
WiFiServer s_ota_server(kOtaPort);
Pending s_con_pending;
Pending s_ota_pending;
bool s_servers_started = false;
bool s_was_connected = false;
bool s_mdns_started = false;

int s_fails = 0;
bool s_locked = false;
uint32_t s_lock_until = 0;

bool onLoopTask()
{
    return s_loop_task != nullptr && xTaskGetCurrentTaskHandle() == s_loop_task;
}

// どちらかの相手がいる間は Wi-Fi の省電力を切る
void updateAwake()
{
    net::holdAwake(s_client_live || s_ota_running);
}

void closeClient(const char *why)
{
    if (!s_client_live) {
        return;
    }
    s_client_live = false;      // 先に落とす（下の printf が相手へ行かないように）
    s_client.stop();
    portENTER_CRITICAL(&s_ring_mux);
    s_ring_count = 0;
    s_ring_dropped = 0;
    portEXIT_CRITICAL(&s_ring_mux);
    updateAwake();
    Serial.printf("[CON] client %s\n", why);
}

// loop タスクから、認証済みの相手へそのまま送る。送り切れなければ切る
void sendToClient(const uint8_t *buf, size_t n)
{
    if (!s_client_live || n == 0) {
        return;
    }
    s_in_client_write = true;
    const size_t sent = s_client.write(buf, n);
    s_in_client_write = false;
    if (sent != n) {
        closeClient("dropped (send failed)");
    }
}

// ほかのタスクの書き込みを預かる。あふれた分は捨てて数えておく
void pushRing(const uint8_t *buf, size_t n)
{
    if (s_ring == nullptr) {
        return;
    }
    portENTER_CRITICAL(&s_ring_mux);
    const size_t room = kOutRingBytes - s_ring_count;
    const size_t take = n < room ? n : room;
    s_ring_dropped += n - take;
    const size_t first = take < kOutRingBytes - s_ring_head ? take : kOutRingBytes - s_ring_head;
    memcpy(s_ring + s_ring_head, buf, first);
    memcpy(s_ring, buf + first, take - first);
    s_ring_head = (s_ring_head + take) % kOutRingBytes;
    s_ring_count += take;
    portEXIT_CRITICAL(&s_ring_mux);
}

// 預かった分を相手へ送る（loop タスクだけ）。読む側は loop だけなので、送り終えてから数を減らす
void drainRing()
{
    while (s_client_live && s_ring != nullptr) {
        portENTER_CRITICAL(&s_ring_mux);
        const size_t count = s_ring_count;
        const size_t tail = (s_ring_head + kOutRingBytes - count) % kOutRingBytes;
        const size_t dropped = count == 0 ? s_ring_dropped : 0;
        if (count == 0) {
            s_ring_dropped = 0;
        }
        portEXIT_CRITICAL(&s_ring_mux);

        if (count == 0) {
            if (dropped > 0) {
                char msg[48];
                const int len = snprintf(msg, sizeof(msg), "[CON] dropped %u bytes\n", (unsigned)dropped);
                sendToClient((const uint8_t *)msg, (size_t)len);
            }
            return;
        }
        const size_t piece = count < kOutRingBytes - tail ? count : kOutRingBytes - tail;
        sendToClient(s_ring + tail, piece);
        if (!s_client_live) {
            return;     // closeClient が数を 0 にした
        }
        portENTER_CRITICAL(&s_ring_mux);
        s_ring_count -= piece;
        portEXIT_CRITICAL(&s_ring_mux);
    }
}

// --- 合言葉 --------------------------------------------------------------------

// 時間で中身を推し量られないよう、比べる時間は合言葉の長さだけで決まる
bool passwordMatches(const char *given)
{
    const char *want = REMOTE_PASSWORD;
    const size_t lg = strlen(given);
    const size_t lw = strlen(want);
    uint8_t diff = lg != lw ? 1 : 0;
    for (size_t i = 0; i < lw; ++i) {
        diff |= (uint8_t)want[i] ^ (uint8_t)(i < lg ? given[i] : 0);
    }
    return diff == 0;
}

bool lockedOut()
{
    if (s_locked && (int32_t)(millis() - s_lock_until) >= 0) {
        s_locked = false;
    }
    return s_locked;
}

// 合言葉の行を確かめる。違えば数え、3 回で 60 秒締め出す
bool checkAuth(Pending &p, const char *tag)
{
    const bool ok = strncmp(p.line, "AUTH ", 5) == 0 && passwordMatches(p.line + 5);
    if (ok) {
        s_fails = 0;
        return true;
    }
    Serial.printf("[%s] wrong password from %s\n", tag, p.c.remoteIP().toString().c_str());
    if (++s_fails >= kMaxFails) {
        s_fails = 0;
        s_locked = true;
        s_lock_until = millis() + kLockoutMs;
        Serial.printf("[CON] %d wrong passwords, locked for %lu s\n", kMaxFails,
                      (unsigned long)(kLockoutMs / 1000));
    }
    p.c.print("[CON] denied\n");
    return false;
}

void dropPending(Pending &p)
{
    p.c.stop();
    p = Pending();
}

void acceptInto(WiFiServer &server, Pending &p)
{
    if (!server.hasClient()) {
        return;
    }
    WiFiClient c = server.accept();
    if (!c) {
        return;
    }
    // 締め出し中・合言葉待ちが既にいるときは、何も返さずに切る
    if (lockedOut() || p.active) {
        c.stop();
        return;
    }
    p = Pending();
    p.c = c;
    p.active = true;
    p.since = millis();
}

// 1 行（\n まで）がそろったら true。行の外（改行の後ろ）は読まずに残す
bool readLine(Pending &p)
{
    while (p.c.available() > 0) {
        const int ch = p.c.read();
        if (ch < 0) {
            break;
        }
        if (ch == '\n') {
            if (p.len > 0 && p.line[p.len - 1] == '\r') {
                --p.len;
            }
            p.line[p.len] = '\0';
            if (p.overflow) {
                p.line[0] = '\0';       // 長すぎる行は無効（合言葉としても通さない）
            }
            p.len = 0;
            p.overflow = false;
            return true;
        }
        if (p.len + 1 < sizeof(p.line)) {
            p.line[p.len++] = (char)ch;
        } else {
            p.overflow = true;
        }
    }
    return false;
}

bool pendingExpired(Pending &p)
{
    return !p.c.connected() || millis() - p.since > kAuthTimeoutMs;
}

// --- コンソール ------------------------------------------------------------------

void pollConsolePending()
{
    Pending &p = s_con_pending;
    if (!p.active) {
        return;
    }
    if (pendingExpired(p)) {
        dropPending(p);
        return;
    }
    if (!readLine(p)) {
        return;
    }
    if (!checkAuth(p, "CON")) {
        dropPending(p);
        return;
    }
    // 合言葉が通った。前の相手がいれば入れ替える（眠った PC の古い接続に居座られないため）
    if (s_client_live) {
        static const char kBye[] = "[CON] replaced by a new connection\n";
        sendToClient((const uint8_t *)kBye, sizeof(kBye) - 1);
        closeClient("replaced");
    }
    s_client = p.c;
    p = Pending();
    s_client.setNoDelay(true);
    s_client.print("[CON] ok\n");
    portENTER_CRITICAL(&s_ring_mux);
    s_ring_head = 0;
    s_ring_count = 0;
    s_ring_dropped = 0;
    portEXIT_CRITICAL(&s_ring_mux);
    s_client_live = true;
    updateAwake();
    Serial.printf("[CON] client %s connected\n", s_client.remoteIP().toString().c_str());
}

// --- ソフトの更新 ----------------------------------------------------------------

// 相手へ 1 行返し、同じ行をログ（シリアルとコンソール）にも出す
void otaSay(WiFiClient &c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void otaSay(WiFiClient &c, const char *fmt, ...)
{
    char line[112];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    c.print(line);
    c.print("\n");
    Serial.println(line);
}

bool isHex32(const char *s)
{
    if (strlen(s) != 32) {
        return false;
    }
    for (const char *q = s; *q != '\0'; ++q) {
        if (!isxdigit((unsigned char)*q)) {
            return false;
        }
    }
    return true;
}

// 更新してよい状態か。だめなら理由を out に書いて false
bool readyForUpdate(size_t size, const esp_partition_t *part, char *out, size_t cap)
{
    if (part == nullptr) {
        snprintf(out, cap, "no update partition");
        return false;
    }
    if (size == 0 || size > part->size) {
        snprintf(out, cap, "size %u does not fit the app partition (%u)", (unsigned)size, (unsigned)part->size);
        return false;
    }
    if (display::gameActive()) {
        snprintf(out, cap, "a game screen is open");
        return false;
    }
    const int queued = net::pendingReports();
    if (queued > 0) {
        snprintf(out, cap, "%d coffee report(s) not sent to GAS yet", queued);
        return false;
    }
    bool secret = true;     // ロックが取れなければ「見えているかもしれない」として断る
    if (lvgl_port_lock(1000)) {
        secret = werewolf::secretOnScreen() || cards::privateOnScreen();
        lvgl_port_unlock();
    }
    if (secret) {
        snprintf(out, cap, "a secret is on screen");
        return false;
    }
    return true;
}

void overlayProgress(int percent)
{
    if (lvgl_port_lock(200)) {
        ui::setUpdateProgress(percent);
        lvgl_port_unlock();
    }
}

// 更新の本体。loop タスクで最後まで（数十秒）止まって受け取る
void runOta(WiFiClient &c, const char *line)
{
    unsigned long size = 0;
    char md5[40] = {};
    if (sscanf(line, "OTA %lu %39s", &size, md5) != 2 || !isHex32(md5)) {
        otaSay(c, "[OTA] failed: bad request (use: OTA <size> <md5hex>)");
        return;
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(nullptr);
    char why[80];
    if (!readyForUpdate(size, part, why, sizeof(why))) {
        otaSay(c, "[OTA] busy: %s", why);
        return;
    }
    uint8_t *buf = (uint8_t *)heap_caps_malloc(kOtaChunk, MALLOC_CAP_SPIRAM);
    if (buf == nullptr) {
        otaSay(c, "[OTA] failed: no memory");
        return;
    }
    if (!Update.begin(size)) {
        otaSay(c, "[OTA] failed: begin: %s", Update.errorString());
        Update.abort();
        heap_caps_free(buf);
        return;
    }
    if (!Update.setMD5(md5)) {
        Update.abort();
        otaSay(c, "[OTA] failed: bad md5");
        heap_caps_free(buf);
        return;
    }

    s_ota_running = true;
    updateAwake();
    if (lvgl_port_lock(-1)) {
        display::noteActivity();    // 暗くなっていたら戻す
        ui::showUpdateOverlay();
        lvgl_port_unlock();
    }
    otaSay(c, "[OTA] ready (%lu bytes)", size);

    size_t got = 0;
    int shown = 0;                  // 知らせた 10% の段
    uint32_t last_rx = millis();
    const char *err = nullptr;
    while (got < size) {
        const int avail = c.available();
        if (avail > 0) {
            size_t want = size - got;
            if (want > kOtaChunk) {
                want = kOtaChunk;
            }
            if (want > (size_t)avail) {
                want = (size_t)avail;
            }
            const int n = c.read(buf, want);
            if (n > 0) {
                if (Update.write(buf, (size_t)n) != (size_t)n) {
                    err = Update.errorString();
                    break;
                }
                got += (size_t)n;
                last_rx = millis();
                const int step = (int)((uint64_t)got * 10 / size);
                if (step > shown) {
                    shown = step;
                    otaSay(c, "[OTA] %d%%", step * 10);
                    overlayProgress(step * 10);
                }
                continue;
            }
        }
        if (!c.connected()) {
            err = "connection lost";
            break;
        }
        if (millis() - last_rx > kOtaReadTimeoutMs) {
            err = "timeout (no data for 10 s)";
            break;
        }
        delay(2);
    }
    heap_caps_free(buf);
    if (err == nullptr && !Update.end(true)) {
        err = Update.errorString();     // MD5 が合わないときもここ
    }

    if (err != nullptr) {
        Update.abort();
        otaSay(c, "[OTA] failed: %s (the running firmware stays)", err);
        if (lvgl_port_lock(-1)) {
            ui::setUpdateFailed();
            lvgl_port_unlock();
        }
        delay(2000);
        if (lvgl_port_lock(-1)) {
            ui::hideUpdateOverlay();
            lvgl_port_unlock();
        }
        s_ota_running = false;
        updateAwake();
        return;
    }

    otaSay(c, "[OTA] ok, rebooting");
    // 念のため、杯数と操作ログを書き出してから再起動する
    if (lvgl_port_lock(-1)) {
        cup::saveIfDirty();
        lvgl_port_unlock();
    }
    sdlog::event("ota", cup::taken(), cup::remaining(), cup::remaining(), "remote update");
    sdlog::poll();
    Serial.flush();
    c.flush();
    delay(500);
    c.stop();
    if (s_client_live) {
        s_client_live = false;
        s_client.stop();
    }
    // Wi-Fi を先に止めてから再起動する。止めずに再起動すると、閉じかけの接続の後始末が間に合わず
    // 見張り（ウォッチドッグ）での再起動になった（2026-09-24 実機。コードレビューの指摘）
    WiFi.disconnect(true);
    delay(100);
    ESP.restart();
}

void pollOtaPending()
{
    Pending &p = s_ota_pending;
    if (!p.active) {
        return;
    }
    if (pendingExpired(p)) {
        dropPending(p);
        return;
    }
    if (!readLine(p)) {
        return;
    }
    if (!p.authed) {
        if (!checkAuth(p, "OTA")) {
            dropPending(p);
            return;
        }
        p.authed = true;
        p.since = millis();         // OTA の行を待つ時間を数え直す
        p.c.setNoDelay(true);
        p.c.print("[CON] ok\n");
        return;
    }
    runOta(p.c, p.line);
    dropPending(p);
}

// --- 名前（mDNS）とサーバーの開始 ---------------------------------------------------

void pollDiscovery()
{
    const bool connected = net::wifiConnected();
    if (connected == s_was_connected) {
        return;
    }
    s_was_connected = connected;
    if (!connected) {
        return;
    }
    // つながるたびに名乗り直す（アクセスポイントが変わると IP も変わるため）
    if (s_mdns_started) {
        MDNS.end();
    }
    s_mdns_started = MDNS.begin(kHostName);
    if (!s_servers_started) {
        s_con_server.begin();
        s_ota_server.begin();
        s_servers_started = true;
    }
    Serial.printf("[NET] remote console %s.local / %s :%u (update :%u)%s\n", kHostName,
                  WiFi.localIP().toString().c_str(), (unsigned)kConsolePort, (unsigned)kOtaPort,
                  s_mdns_started ? "" : " (mDNS failed)");
}

}  // namespace

// --- Serial の差し替え（ct_serial_tee.h）------------------------------------------

namespace ct_tee {

Console con;

void Console::begin(unsigned long baud)
{
    ctRealSerial().begin(baud);
}

size_t Console::write(uint8_t c)
{
    return write(&c, 1);
}

size_t Console::write(const uint8_t *buffer, size_t size)
{
    ctRealSerial().write(buffer, size);
    if (s_client_live) {
        if (onLoopTask() && !s_in_client_write) {
            // 預かっている分（ほかのタスクのログ）はここでは送らず、poll() と flush() でまとめて送る。
            // スクリーンショット（見出しの行 → 画像の生データ）の間に別のログが割り込まないようにするため
            sendToClient(buffer, size);
        } else {
            pushRing(buffer, size);
        }
    }
    return size;
}

int Console::available()
{
    int n = ctRealSerial().available();
    if (s_client_live && onLoopTask()) {
        n += s_client.available();
    }
    return n;
}

int Console::read()
{
    if (ctRealSerial().available() > 0) {
        return ctRealSerial().read();
    }
    if (s_client_live && onLoopTask() && s_client.available() > 0) {
        return s_client.read();
    }
    return -1;
}

int Console::peek()
{
    if (ctRealSerial().available() > 0) {
        return ctRealSerial().peek();
    }
    if (s_client_live && onLoopTask() && s_client.available() > 0) {
        return s_client.peek();
    }
    return -1;
}

void Console::flush()
{
    ctRealSerial().flush();
    if (onLoopTask()) {
        drainRing();
    }
}

}  // namespace ct_tee

// --- 公開 ------------------------------------------------------------------------

namespace remote {

void begin()
{
    s_loop_task = xTaskGetCurrentTaskHandle();
    if (strlen(REMOTE_PASSWORD) == 0) {
        Serial.println("[CON] disabled (no REMOTE_PASSWORD)");
        return;
    }
    s_ring = (uint8_t *)heap_caps_malloc(kOutRingBytes, MALLOC_CAP_SPIRAM);
    s_enabled = true;
}

void poll()
{
    if (!s_enabled) {
        return;
    }
    pollDiscovery();
    if (!s_servers_started) {
        return;
    }
    acceptInto(s_con_server, s_con_pending);
    acceptInto(s_ota_server, s_ota_pending);
    pollConsolePending();
    if (s_client_live && !s_client.connected()) {
        closeClient("disconnected");
    }
    drainRing();
    pollOtaPending();
}

}  // namespace remote
