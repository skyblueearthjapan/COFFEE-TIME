#include "Battery.h"

#include <Arduino.h>

namespace battery {

static constexpr int kPin = 4;              // BAT_ADC
static constexpr uint32_t kDividerRatio = 3;  // (200k + 100k) / 100k

static volatile uint32_t s_mv = 0;

void begin()
{
    analogReadResolution(12);
    analogSetPinAttenuation(kPin, ADC_11db);
}

void update()
{
    uint32_t sum = 0;
    for (int i = 0; i < 8; ++i) {
        sum += analogReadMilliVolts(kPin);
    }
    const uint32_t mv = sum / 8 * kDividerRatio;
    // 表示がちらつかないよう指数移動平均で平滑化
    s_mv = (s_mv == 0) ? mv : (s_mv * 7 + mv) / 8;
}

uint32_t millivolts()
{
    return s_mv;
}

int percent()
{
    // LiPo の目安: 3.30V = 0%、4.15V 以上 = 100%（負荷や温度で変わるので概算）
    const int mv = (int)s_mv;
    if (mv <= 3300) {
        return 0;
    }
    if (mv >= 4150) {
        return 100;
    }
    return (mv - 3300) * 100 / (4150 - 3300);
}

}  // namespace battery
