#include "RtcClock.h"

#include <Arduino.h>
#include <driver/i2c.h>
#include <sys/time.h>
#include <time.h>

namespace rtc {

static constexpr i2c_port_t kPort = I2C_NUM_0;
static constexpr uint8_t kAddr = 0x51;
static constexpr uint8_t kRegControl1 = 0x00;
static constexpr uint8_t kRegSeconds = 0x04;     // 0x04〜0x0A: 秒 分 時 日 曜日 月 年
static constexpr TickType_t kTimeout = pdMS_TO_TICKS(50);

static uint8_t toBcd(int v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static int fromBcd(uint8_t v)
{
    return (v >> 4) * 10 + (v & 0x0F);
}

bool restoreSystemTime()
{
    const uint8_t reg = kRegSeconds;
    uint8_t d[7];
    if (i2c_master_write_read_device(kPort, kAddr, &reg, 1, d, sizeof(d), kTimeout) != ESP_OK) {
        Serial.println("[RTC] read failed");
        return false;
    }
    // 秒レジスタの bit7 (OS) が立っていれば、電源断などで時刻が失われている
    if (d[0] & 0x80) {
        Serial.println("[RTC] no valid time (oscillator stopped)");
        return false;
    }
    struct tm tm = {};
    tm.tm_sec = fromBcd(d[0] & 0x7F);
    tm.tm_min = fromBcd(d[1] & 0x7F);
    tm.tm_hour = fromBcd(d[2] & 0x3F);
    tm.tm_mday = fromBcd(d[3] & 0x3F);
    tm.tm_mon = fromBcd(d[5] & 0x1F) - 1;
    tm.tm_year = fromBcd(d[6]) + 100;   // 2000 年起点
    if (tm.tm_year < 124) {             // 2024 年より前なら未設定とみなす
        Serial.println("[RTC] time not set");
        return false;
    }
    // RTC は UTC で保存しているので、TZ の影響を受けないよう UTC として秒に変換する
    const int y = tm.tm_year + 1900, m = tm.tm_mon + 1;
    const int a = (14 - m) / 12, yy = y + 4800 - a, mm = m + 12 * a - 3;
    const long days = tm.tm_mday + (153 * mm + 2) / 5 + 365L * yy + yy / 4 - yy / 100 + yy / 400 - 32045 - 2440588;
    const time_t t = (time_t)days * 86400 + tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;

    struct timeval tv = {t, 0};
    settimeofday(&tv, nullptr);
    Serial.printf("[RTC] restored %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                  y, m, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return true;
}

bool saveSystemTime()
{
    const time_t now = time(nullptr);
    struct tm tm;
    gmtime_r(&now, &tm);
    const uint8_t buf[8] = {
        kRegSeconds,
        toBcd(tm.tm_sec),               // bit7 (OS) = 0 で「時刻有効」になる
        toBcd(tm.tm_min),
        toBcd(tm.tm_hour),
        toBcd(tm.tm_mday),
        (uint8_t)tm.tm_wday,
        toBcd(tm.tm_mon + 1),
        toBcd(tm.tm_year - 100),
    };
    // 念のため時計を動作状態に（Control_1 の STOP ビットを 0）
    const uint8_t ctrl[2] = {kRegControl1, 0x00};
    if (i2c_master_write_to_device(kPort, kAddr, ctrl, sizeof(ctrl), kTimeout) != ESP_OK ||
        i2c_master_write_to_device(kPort, kAddr, buf, sizeof(buf), kTimeout) != ESP_OK) {
        Serial.println("[RTC] write failed");
        return false;
    }
    Serial.printf("[RTC] saved %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return true;
}

}  // namespace rtc
