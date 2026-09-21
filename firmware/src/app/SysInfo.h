#pragma once

#include <stdint.h>

/**
 * 「システム情報」画面と操作ログのための、端末そのものの情報。
 * 起動の理由は起動直後にしか正しく読めないので、setup() の最初に begin() で控えておく。
 *
 * 画面に出す日本語はここには置かない（フォントに収録する文字はソースの `ui/` と `games/` から
 * 集めているため。tools/collect_ui_chars.py）。ここは英字の識別子を返すだけにして、
 * 日本語への言い換えは ui/SettingsScreen.cpp で行う。
 */
namespace sysinfo {

void begin();                   // setup() の冒頭で 1 回

// poweron / software / panic / watchdog / brownout / usb / other
const char *resetReasonId();

uint32_t uptimeSeconds();       // 前回の起動からの経過秒

const char *revision();         // ビルドしたときの Git の版（CT_GIT_REV。取れなければ "unknown"）

}  // namespace sysinfo
