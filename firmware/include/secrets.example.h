// secrets.h のひな形。secrets.h にコピーして値を入れる（secrets.h は Git に含めない）
#pragma once

#define WIFI_SSID     "ここにWi-Fiの名前(SSID)"
#define WIFI_PASSWORD "ここにWi-Fiのパスワード"

// Google Apps Script のウェブアプリ URL と合言葉（gas/README.md 参照）。空なら送信しない
#define GAS_URL   ""
#define GAS_TOKEN ""

// 2 つ目の Wi-Fi（自宅など・任意）。使わないなら空のままでよい
#define WIFI_SSID2     ""
#define WIFI_PASSWORD2 ""

// 3 つ目の Wi-Fi（スマホのテザリングなど・任意）。ESP32 は 2.4GHz 専用。
// iPhone は「インターネット共有」の「互換性を優先」をオンにし、iPhone の名前は半角英数字にしておくと確実
#define WIFI_SSID3     ""
#define WIFI_PASSWORD3 ""
