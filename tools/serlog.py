"""ESP32 のシリアルログを一定時間取得する。

使い方: python tools/serlog.py COM8 20 [reset] [send=RTT]
  send=... を付けると、接続直後にその文字を 1.5 秒間隔で送る（開発用コマンド）
  COM8 の代わりに net（= coffee-time.local）や IP を書くと Wi-Fi の遠隔コンソールで読む（reset は効かない）
"""
import sys
import time

import serial

import ctport

port, secs = sys.argv[1], float(sys.argv[2])
reset = "reset" in sys.argv[3:]
send = next((a[5:] for a in sys.argv[3:] if a.startswith("send=")), "")

s = ctport.open_port(port, timeout=0.2)
if reset and ctport.is_serial_port(port):
    s.rts = True
    time.sleep(0.1)
    s.rts = False
    s.close()
    time.sleep(1.5)
    s.open()

pending = list(send)
next_send = time.time() + 1.0
start = time.time()
end = start + secs
line_start = True
while time.time() < end:
    if pending and time.time() >= next_send:
        s.write(pending.pop(0).encode())
        next_send = time.time() + 1.5
    try:
        d = s.read(4096)
    except serial.SerialException:
        time.sleep(0.5)
        try:
            s.close()
            if ctport.is_serial_port(port):
                s.open()
            else:
                s = ctport.open_port(port, timeout=0.2)     # 遠隔コンソールは合言葉からつなぎ直す
        except (serial.SerialException, OSError):
            pass
        continue
    if d:
        # 各行の先頭に PC 側の時刻を付け、操作とログを照合できるようにする
        for ch in d.decode("utf-8", "replace"):
            if line_start:
                sys.stdout.write(time.strftime("%H:%M:%S "))
                line_start = False
            sys.stdout.write(ch)
            if ch == "\n":
                line_start = True
        sys.stdout.flush()
