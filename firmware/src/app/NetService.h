#pragma once

#include <stdint.h>

/**
 * Wi-Fi 接続・時刻同期 (NTP)・天気取得 (Open-Meteo)・記録サーバー (GAS) への送信。
 * loop() から net::poll() を呼び続ける。LVGL は触らない。
 */
namespace net {

struct Weather {
    bool valid = false;
    float temperature = 0;  // 現在気温 (°C)
    int code = -1;          // WMO 天気コード
};

void begin();
// 新しい天気を取得したら true を返し、out に格納する
bool poll(Weather &out);
bool wifiConnected();
bool timeSynced();

// 杯数イベントを送信キューに積む（どのタスクからでも呼べる）。
// event: "take" / "refill" / "newday"
void reportEvent(const char *event, uint32_t taken, uint32_t left);

}  // namespace net
