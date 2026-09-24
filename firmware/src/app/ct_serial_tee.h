/**
 * アプリ本体（src/app）のすべての C++ ソースの先頭に強制的に読み込まれるヘッダー
 * （platformio.ini の [env:app] の build_src_flags = -include …）。
 *
 * `Serial` を「本物のシリアル ＋ Wi-Fi の遠隔コンソール」に差し替える。
 * ソース側の `Serial.printf(...)` や `Serial.read()` は 1 文字も書き換えずに、
 * USB のシリアルと同じ出力・入力が遠隔コンソール（TCP 2323。RemoteConsole.h）でも使える。
 * 本物のポート（env app は HWCDCSerial、app_uart は Serial0）は RemoteConsole.cpp だけが触る。
 * ライブラリ（画面・LVGL など）とほかの env（hwtest 等）には効かない。
 *
 * ここで Arduino.h は読み込まない。設計書由来のゲームのコア（werewolf_core.hpp など）は Arduino.h
 * の無い所で書かれていて、`bit` などのマクロとぶつかる。読み込むのは Stream.h（Print.h・WString.h）だけ。
 * Arduino.h（の中の HardwareSerial.h）があとから読み込まれても `Serial` を定義し直さないよう、
 * NO_GLOBAL_SERIAL を立てておく（HardwareSerial.h の `#define Serial …` とその下の宣言を飛ばす）。
 */
#pragma once

#ifdef __cplusplus

#ifndef NO_GLOBAL_SERIAL
#define NO_GLOBAL_SERIAL 1
#endif

#include <Stream.h>

namespace ct_tee {

class Console : public Stream {
public:
    void begin(unsigned long baud);

    // 書き込み: 本物のシリアルへ。遠隔コンソールが認証済みならそちらへも（RemoteConsole.cpp）
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    using Print::write;

    // 読み取り: 本物のシリアルを先に、無ければ遠隔コンソールの入力（loop タスクからだけ）
    int available() override;
    int read() override;
    int peek() override;
    void flush() override;

    operator bool() const { return true; }
};

extern Console con;

}  // namespace ct_tee

#define Serial ::ct_tee::con

#endif  // __cplusplus
