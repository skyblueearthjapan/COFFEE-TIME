#pragma once

/**
 * 基板の時計チップ PCF85063 (I2C 0x51)。
 * Wi-Fi が無くても・電源を入れ直しても時刻を保つために使う。
 * I2C バス (port 0, SDA=15 / SCL=7) はタッチパネル・IO 拡張と共有しており、
 * ESP32_Display_Panel が従来型ドライバーで初期化済みのものをそのまま使う（board->begin() の後に呼ぶ）。
 * チップには UTC を保存する。
 */
namespace rtc {

bool restoreSystemTime();   // RTC の時刻を ESP32 のシステム時刻に設定。有効な時刻が無ければ false
bool saveSystemTime();      // 現在のシステム時刻（NTP 同期後）を RTC に書き込む

}  // namespace rtc
