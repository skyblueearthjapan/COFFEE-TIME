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
