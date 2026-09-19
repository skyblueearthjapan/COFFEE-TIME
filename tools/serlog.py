"""ESP32 のシリアルログを一定時間取得する。

使い方: python tools/serlog.py COM8 20 [reset]
"""
import sys
import time

import serial

port, secs = sys.argv[1], float(sys.argv[2])
reset = len(sys.argv) > 3 and sys.argv[3] == "reset"

s = serial.Serial()
s.port = port
s.baudrate = 115200
s.timeout = 0.2
s.dtr = False
s.rts = False
s.open()
if reset:
    s.rts = True
    time.sleep(0.1)
    s.rts = False
    s.close()
    time.sleep(1.5)
    s.open()

end = time.time() + secs
while time.time() < end:
    try:
        d = s.read(4096)
    except serial.SerialException:
        time.sleep(0.5)
        try:
            s.close()
            s.open()
        except serial.SerialException:
            pass
        continue
    if d:
        sys.stdout.write(d.decode("utf-8", "replace"))
        sys.stdout.flush()
