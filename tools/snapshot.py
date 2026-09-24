"""ESP32 の画面をシリアル経由で取得して PNG に保存する（開発用）。

使い方: python tools/snapshot.py COM8 out.png
"""
import sys
import time

import ctport
from PIL import Image

port, out = sys.argv[1], sys.argv[2]

s = ctport.open_port(port, timeout=5)     # COM8 / net（Wi-Fi の遠隔コンソール）
s.reset_input_buffer()
s.write(b"S")

deadline = time.time() + 15
header = None
while time.time() < deadline:
    line = s.readline()
    if line.startswith(b"[SNAP] ") and not line.startswith(b"[SNAP] END"):
        parts = line.decode(errors="replace").split()
        if len(parts) == 4:
            header = (int(parts[1]), int(parts[2]), int(parts[3]))
            break
        sys.exit(f"snapshot error: {line!r}")
if header is None:
    sys.exit("no snapshot header received")

w, h, size = header
data = bytearray()
while len(data) < size and time.time() < deadline + 15:
    data += s.read(size - len(data))
if len(data) < size:
    sys.exit(f"incomplete snapshot: {len(data)}/{size}")

# LVGL 8 TRUE_COLOR (16bit, LV_COLOR_16_SWAP=0) = RGB565 リトルエンディアン
img = Image.frombuffer("RGB", (w, h), bytes(data), "raw", "BGR;16", 0, 1)
img.save(out)
print(f"saved {out} ({w}x{h})")
