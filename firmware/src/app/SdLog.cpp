#include "SdLog.h"

#include <FS.h>
#include <SD_MMC.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <time.h>

#include "Battery.h"
#include "NetService.h"

namespace sdlog {

// 配線は docs/STORAGE_POLICY.md §7。D1・D2 は未接続なので 1 bit モードで使う
static constexpr int kPinClk = 2;
static constexpr int kPinCmd = 1;
static constexpr int kPinD0 = 42;
static constexpr uint8_t kExpanderSdD3 = 3;     // EXIO4（エキスパンダーのピン番号は 0 始まり）

static constexpr const char *kDir = "/coffee_time";
static constexpr const char *kHeader = "datetime,uptime_s,event,taken,left,prev,bat_mv,wifi,note\n";
static constexpr int kQueueLen = 32;
static constexpr uint32_t kRemountRetryMs = 60 * 1000;

struct Entry {
    uint32_t ts;            // UNIX 秒。時刻が分からなければ 0
    uint32_t uptime_s;
    char name[10];
    uint32_t taken;
    uint32_t left;
    uint32_t prev;
    uint16_t bat_mv;
    bool wifi;
    char note[40];
};

static QueueHandle_t s_queue = nullptr;
static esp_panel::board::Board *s_board = nullptr;
static bool s_mounted = false;
static uint32_t s_next_mount_ms = 0;
// 空き容量はマウントしたときに 1 回だけ数えておく。FAT の使用量の集計は数百 ms かかることが
// あるので、「システム情報」画面を作るたびに数え直さない
static uint64_t s_free_bytes = 0;

static bool mount()
{
    if (s_board == nullptr) {
        return false;
    }
    auto expander = s_board->getIO_Expander();
    if (expander == nullptr || expander->getBase() == nullptr) {
        return false;
    }
    expander->getBase()->pinMode(kExpanderSdD3, OUTPUT);
    expander->getBase()->digitalWrite(kExpanderSdD3, HIGH);

    // カードが読めなくても初期化（フォーマット）はしない
    if (!SD_MMC.setPins(kPinClk, kPinCmd, kPinD0) || !SD_MMC.begin("/sdcard", true)) {
        SD_MMC.end();
        return false;
    }
    if (!SD_MMC.exists(kDir)) {
        SD_MMC.mkdir(kDir);
    }
    s_free_bytes = SD_MMC.totalBytes() - SD_MMC.usedBytes();
    return true;
}

static void unmount()
{
    SD_MMC.end();
    s_mounted = false;
    s_free_bytes = 0;
    s_next_mount_ms = millis() + kRemountRetryMs;
}

// 月ごとに 1 ファイル。時刻が分からない間の記録は log_nodate.csv に入れる
static void logPath(uint32_t ts, char *out, size_t size)
{
    if (ts == 0) {
        snprintf(out, size, "%s/log_nodate.csv", kDir);
        return;
    }
    const time_t t = (time_t)ts;
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(out, size, "%s/log_%04d%02d.csv", kDir, tm.tm_year + 1900, tm.tm_mon + 1);
}

static bool writeEntry(const Entry &e)
{
    char path[48];
    logPath(e.ts, path, sizeof(path));
    // 電源断に備え、1 行ごとに「開く → 追記 → 閉じる」（実測 平均 5 ms）
    File f = SD_MMC.open(path, FILE_APPEND);
    if (!f) {
        return false;
    }
    if (f.size() == 0) {
        f.print(kHeader);
    }
    char when[24] = "";
    if (e.ts != 0) {
        const time_t t = (time_t)e.ts;
        struct tm tm;
        localtime_r(&t, &tm);
        strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &tm);
    }
    const size_t n = f.printf("%s,%lu,%s,%lu,%lu,%lu,%u,%d,%s\n", when, (unsigned long)e.uptime_s, e.name,
                              (unsigned long)e.taken, (unsigned long)e.left, (unsigned long)e.prev,
                              (unsigned)e.bat_mv, e.wifi ? 1 : 0, e.note);
    f.close();
    return n > 0;
}

void begin(esp_panel::board::Board *board)
{
    s_board = board;
    s_queue = xQueueCreate(kQueueLen, sizeof(Entry));
    s_mounted = mount();
    if (s_mounted) {
        Serial.printf("[SD] mounted, %llu MB free\n", s_free_bytes / (1024ULL * 1024ULL));
    } else {
        Serial.println("[SD] no card (logging disabled until a card is found)");
        s_next_mount_ms = millis() + kRemountRetryMs;
    }
}

void event(const char *name, uint32_t taken, uint32_t left, uint32_t prev, const char *note)
{
    if (s_queue == nullptr) {
        return;
    }
    Entry e = {};
    e.ts = net::timeSynced() ? (uint32_t)time(nullptr) : 0;
    e.uptime_s = millis() / 1000;
    strlcpy(e.name, name, sizeof(e.name));
    e.taken = taken;
    e.left = left;
    e.prev = prev;
    e.bat_mv = (uint16_t)battery::millivolts();
    e.wifi = net::wifiConnected();
    strlcpy(e.note, note != nullptr ? note : "", sizeof(e.note));
    for (char *p = e.note; *p != '\0'; ++p) {   // CSV の列を壊す文字は空白にする
        if (*p == ',' || *p == '\n' || *p == '\r') {
            *p = ' ';
        }
    }
    xQueueSend(s_queue, &e, 0);     // 満杯なら捨てる
}

void poll()
{
    if (s_queue == nullptr || uxQueueMessagesWaiting(s_queue) == 0) {
        return;
    }
    if (!s_mounted) {
        if ((int32_t)(millis() - s_next_mount_ms) >= 0) {
            s_mounted = mount();
            if (s_mounted) {
                Serial.println("[SD] card found, logging resumed");
            } else {
                s_next_mount_ms = millis() + kRemountRetryMs;
            }
        }
        if (!s_mounted) {
            xQueueReset(s_queue);   // 書けない記録は捨てる（本体の動作を優先）
            return;
        }
    }
    Entry e;
    while (xQueueReceive(s_queue, &e, 0) == pdTRUE) {
        if (!writeEntry(e)) {
            Serial.println("[SD] write failed, logging paused");
            unmount();
            return;
        }
    }
}

bool mounted()
{
    return s_mounted;
}

uint64_t freeBytes()
{
    return s_mounted ? s_free_bytes : 0;
}

void dumpTail(Stream &out, size_t max_bytes)
{
    if (!s_mounted) {
        out.println("[SDLOG] no card");
        return;
    }
    char path[48];
    logPath(net::timeSynced() ? (uint32_t)time(nullptr) : 0, path, sizeof(path));
    File f = SD_MMC.open(path, FILE_READ);
    if (!f) {
        out.printf("[SDLOG] %s not found\n", path);
        return;
    }
    const size_t size = f.size();
    if (size > max_bytes) {
        f.seek(size - max_bytes);
        f.readStringUntil('\n');    // 途中から始まる行は読み飛ばす
    }
    out.printf("[SDLOG] %s (%u bytes)\n", path, (unsigned)size);
    uint8_t buf[256];
    for (size_t n = f.read(buf, sizeof(buf)); n > 0; n = f.read(buf, sizeof(buf))) {
        out.write(buf, n);
    }
    f.close();
    out.println("[SDLOG] END");
}

}  // namespace sdlog
