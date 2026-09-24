#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * Wi-Fi 越しの開発用コンソールとソフトの更新（USB をつながずに社内の Wi-Fi から作業するため）。
 *
 * - コンソール … TCP 2323。USB のシリアルとまったく同じ（開発コマンドもログも）。
 *   `Serial` は ct_serial_tee.h で差し替えてあるので、ほかのソースは何も変えていない。
 * - ソフトの更新 … TCP 2324。PC の tools/ota.py が firmware.bin を送る。
 * - どちらも最初の 1 行が `AUTH <合言葉>`（secrets.h の REMOTE_PASSWORD）。違えば `[CON] denied` で切る。
 *   3 回続けて違えば 60 秒は誰も受け付けない。合言葉が空ならどちらも開かない。
 * - つなげる相手は 1 人だけ。新しい接続が合言葉を通ると、前の相手は切られる
 *   （PC が眠って切れ目が分からない古い接続に居座られないため）。
 * - 名前は `coffee-time.local`（mDNS）。DHCP のホスト名も coffee-time。
 *
 * すべて loop() タスクから poll() で動く。LVGL のロックは取っていない状態で呼ぶこと。
 */
namespace remote {

void begin();       // setup() の最後（net::begin の後）で 1 回。loop タスクを覚える
void poll();        // loop() から毎回（net::poll の隣）

}  // namespace remote
