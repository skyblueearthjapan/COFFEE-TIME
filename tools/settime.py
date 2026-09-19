"""PC の時計を ESP32 に設定する（Wi-Fi が使えない場所用）。時計チップにも保存される。

使い方: python tools/settime.py COM8
"""
import sys
import time

import serial

port = sys.argv[1]

s = serial.Serial()
s.port = port
s.baudrate = 115200
s.timeout = 0.3
s.dtr = False
s.rts = False
s.open()
s.reset_input_buffer()

# UNIX 秒（UTC 基準）を送る。ESP32 側で日本時間として表示される
s.write(f"C{int(time.time())}\n".encode())
s.flush()

end = time.time() + 6
while time.time() < end:
    line = s.readline()
    if not line:
        continue
    text = line.decode("utf-8", "replace").rstrip()
    if "[TIME]" in text or "[RTC]" in text or "[CUP]" in text:
        print(text)
s.close()
