/**
 * COFFEE TIME — microSD の動作試験（画面を初期化した後に SD を使えるかを確かめる）
 *
 * 配線（Waveshare ESP32-S3-Touch-LCD-2.8C）:
 *   SD_CLK = GPIO2 / SD_CMD = GPIO1 / SD_D0 = GPIO42 / SD_D3 = IO エキスパンダー EXIO4（D1・D2 は未接続 → 1 bit モード）
 *   GPIO1・GPIO2 は LCD の初期化用 3 線 SPI と共用。LCD の CS（EXIO3）が High の間は LCD 側は無視する。
 *
 * 試験用ファイル /ct_sdtest.bin と /ct_sdtest.log だけを作成・削除し、カード上の他のファイルには触れない。
 */

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <esp_display_panel.hpp>

using namespace esp_panel::board;

static constexpr int kPinClk = 2;
static constexpr int kPinCmd = 1;
static constexpr int kPinD0 = 42;
static constexpr uint8_t kExpanderSdD3 = 3;     // EXIO4（エキスパンダーのピン番号は 0 始まり）

static constexpr const char *kBinPath = "/ct_sdtest.bin";
static constexpr const char *kLogPath = "/ct_sdtest.log";
static constexpr size_t kChunk = 4096;
static constexpr size_t kTotal = 1024 * 1024;   // 1 MB

static uint8_t pattern(size_t i)
{
    return (uint8_t)((i * 31u + (i >> 8)) & 0xFF);
}

static bool speedTest()
{
    static uint8_t buf[kChunk];

    File f = SD_MMC.open(kBinPath, FILE_WRITE);
    if (!f) {
        Serial.println("[SD] FAIL: cannot open file for write");
        return false;
    }
    uint32_t t0 = millis();
    for (size_t off = 0; off < kTotal; off += kChunk) {
        for (size_t i = 0; i < kChunk; ++i) {
            buf[i] = pattern(off + i);
        }
        if (f.write(buf, kChunk) != kChunk) {
            Serial.printf("[SD] FAIL: write error at %u\n", (unsigned)off);
            f.close();
            return false;
        }
    }
    f.close();
    uint32_t ms = millis() - t0;
    Serial.printf("[SD] write 1 MB: %lu ms (%.0f KB/s)\n", (unsigned long)ms, 1024.0f * 1000.0f / ms);

    f = SD_MMC.open(kBinPath, FILE_READ);
    if (!f) {
        Serial.println("[SD] FAIL: cannot open file for read");
        return false;
    }
    t0 = millis();
    size_t bad = 0;
    for (size_t off = 0; off < kTotal; off += kChunk) {
        if (f.read(buf, kChunk) != (int)kChunk) {
            Serial.printf("[SD] FAIL: read error at %u\n", (unsigned)off);
            f.close();
            return false;
        }
        for (size_t i = 0; i < kChunk; ++i) {
            if (buf[i] != pattern(off + i)) {
                ++bad;
            }
        }
    }
    f.close();
    ms = millis() - t0;
    Serial.printf("[SD] read+verify 1 MB: %lu ms (%.0f KB/s), mismatched bytes = %u\n",
                  (unsigned long)ms, 1024.0f * 1000.0f / ms, (unsigned)bad);
    SD_MMC.remove(kBinPath);
    return bad == 0;
}

// 操作ログを想定：1 行ずつ「開く → 追記 → 閉じる」を繰り返したときの所要時間
static bool appendTest()
{
    uint32_t worst = 0, total = 0;
    for (int i = 0; i < 20; ++i) {
        const uint32_t t0 = millis();
        File f = SD_MMC.open(kLogPath, FILE_APPEND);
        if (!f) {
            Serial.println("[SD] FAIL: cannot open log for append");
            return false;
        }
        f.printf("%lu,take,%d,%d\n", (unsigned long)millis(), i, 10 - i % 10);
        f.close();
        const uint32_t dt = millis() - t0;
        total += dt;
        worst = dt > worst ? dt : worst;
    }
    File f = SD_MMC.open(kLogPath, FILE_READ);
    const size_t size = f ? f.size() : 0;
    if (f) {
        f.close();
    }
    SD_MMC.remove(kLogPath);
    Serial.printf("[SD] append 20 lines: avg %lu ms, worst %lu ms, file %u bytes\n",
                  (unsigned long)(total / 20), (unsigned long)worst, (unsigned)size);
    return size > 0;
}

static void runTest(Board *board)
{
    auto expander = board->getIO_Expander();
    if (expander == nullptr || expander->getBase() == nullptr) {
        Serial.println("[SD] FAIL: IO expander not available");
        return;
    }
    expander->getBase()->pinMode(kExpanderSdD3, OUTPUT);
    expander->getBase()->digitalWrite(kExpanderSdD3, HIGH);

    if (!SD_MMC.setPins(kPinClk, kPinCmd, kPinD0)) {
        Serial.println("[SD] FAIL: setPins");
        return;
    }
    if (!SD_MMC.begin("/sdcard", true /* 1 bit */)) {
        Serial.println("[SD] FAIL: mount failed (card missing / not FAT formatted / wiring)");
        return;
    }

    const uint8_t type = SD_MMC.cardType();
    Serial.printf("[SD] mounted. type=%s size=%llu MB, used=%llu MB of %llu MB\n",
                  type == CARD_MMC ? "MMC" : type == CARD_SD ? "SD" : type == CARD_SDHC ? "SDHC" : "unknown",
                  SD_MMC.cardSize() / (1024ULL * 1024ULL),
                  SD_MMC.usedBytes() / (1024ULL * 1024ULL), SD_MMC.totalBytes() / (1024ULL * 1024ULL));

    // カードの中身は件数だけ出す（ファイル名は記録に残さない）
    File root = SD_MMC.open("/");
    int entries = 0;
    for (File e = root.openNextFile(); e; e = root.openNextFile()) {
        ++entries;
        e.close();
    }
    root.close();
    Serial.printf("[SD] root entries: %d\n", entries);

    const bool ok1 = speedTest();
    const bool ok2 = appendTest();
    Serial.printf("[SD] RESULT: %s\n", (ok1 && ok2) ? "PASS" : "FAIL");
    SD_MMC.end();
}

static Board *s_board = nullptr;

void setup()
{
    Serial.begin(115200);
    delay(3000);    // PC 側がポートを開くのを待つ

    Serial.println("[SD] initializing board (LCD first, same order as the app)");
    s_board = new Board();
    s_board->init();
    if (!s_board->begin()) {
        Serial.println("[SD] FAIL: board begin failed");
        return;
    }
    runTest(s_board);
}

void loop()
{
    // 'r' を送ると再試験
    if (Serial.available() > 0 && Serial.read() == 'r' && s_board != nullptr) {
        runTest(s_board);
    }
    delay(200);
}
