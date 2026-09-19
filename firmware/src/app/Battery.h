#pragma once

#include <stdint.h>

/**
 * LiPo 電池電圧の測定。
 * 回路図: BAT —200kΩ— GPIO4(BAT_ADC) —100kΩ— GND なので、電池電圧 = ADC 電圧 × 3。
 * 充電 IC (ETA6098) の状態ピンは LED にしかつながっておらず、充電中かどうかは直接読めない。
 */
namespace battery {

void begin();
void update();              // loop から 1 秒程度ごとに呼ぶ（LVGL ロック不要）
uint32_t millivolts();      // 平滑化した電池電圧 (mV)。未測定なら 0
int percent();              // 電圧からの目安 (0-100)

}  // namespace battery
