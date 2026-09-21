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
void debugScan();          // 開発用：周囲の Wi-Fi をスキャンしてログに出す
bool timeSynced();

// 「設定 → Wi-Fi / システム情報」の表示用（Wi-Fi の名前とパスワードは出さない）
int rssi();                // 接続中の電波の強さ (dBm)。未接続なら 0
int registeredCount();     // secrets.h に登録された Wi-Fi の数
bool ntpSynced();          // 起動後に一度でも NTP で時刻が合ったか

// 杯数イベントを送信キューに積む（どのタスクからでも呼べる）。
// event: "take" / "refill" / "newday"。prev はイベント前の残り杯数（通知の重複防止に使う）
void reportEvent(const char *event, uint32_t taken, uint32_t left, uint32_t prev);

}  // namespace net
