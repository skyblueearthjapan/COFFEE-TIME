"""実機の画面をシリアルから操作して、順にスクリーンショットを撮る（開発用）。

使い方: python tools/uiwalk.py COM8 <出力フォルダー> <手順> [<手順> ...]
  手順:
    key:<文字>     開発用の 1 文字コマンドを送る（例 key:3 = 人狼のロビーを開く、key:0 = HOME へ）
    tap:<x>,<y>    その座標をタップする（ファームの P コマンド。秘密の表示はできない）
    wait:<秒>      待つ
    snap:<名前>    画面を <名前>.png に保存する
  最後に、撮った画面を並べた sheet.png を作る。

例: python tools/uiwalk.py COM8 out key:3 wait:1 snap:lobby tap:240,367 wait:0.6 snap:brief1
"""
import pathlib
import sys
import time

import serial
from PIL import Image

port, out_dir, steps = sys.argv[1], pathlib.Path(sys.argv[2]), sys.argv[3:]
out_dir.mkdir(parents=True, exist_ok=True)

s = serial.Serial()
s.port = port
s.baudrate = 115200
s.timeout = 5
s.dtr = False
s.rts = False
s.open()


def snapshot(path: pathlib.Path) -> Image.Image:
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
    if header is None:
        sys.exit("no snapshot header received")
    w, h, size = header
    data = bytearray()
    while len(data) < size and time.time() < deadline + 15:
        data += s.read(size - len(data))
    if len(data) < size:
        sys.exit(f"incomplete snapshot: {len(data)}/{size}")
    img = Image.frombuffer("RGB", (w, h), bytes(data), "raw", "BGR;16", 0, 1).copy()
    img.save(path)
    return img


shots = []
for step in steps:
    kind, _, arg = step.partition(":")
    if kind == "key":
        s.write(arg.encode())
        time.sleep(0.3)
    elif kind == "tap":
        # 端末はメインループが数秒止まることがある（Wi-Fi の再接続）。受領の返事を待ってから次へ進む
        s.reset_input_buffer()
        s.write(f"P{arg}\n".encode())
        deadline = time.time() + 12
        while time.time() < deadline:
            if s.readline().startswith(b"[TAP]"):
                break
        else:
            sys.exit(f"no tap ack: {arg}")
        time.sleep(0.6)     # 押下 120ms + 離す + 画面の作り直し
    elif kind == "wait":
        time.sleep(float(arg))
    elif kind == "snap":
        shots.append((arg, snapshot(out_dir / f"{arg}.png")))
        print("snap", arg)
    else:
        sys.exit(f"unknown step: {step}")

if shots:
    cols = min(4, len(shots))
    rows = (len(shots) + cols - 1) // cols
    cell = 360
    sheet = Image.new("RGB", (cols * cell, rows * cell), (40, 40, 40))
    for i, (_, img) in enumerate(shots):
        sheet.paste(img.resize((cell, cell)), ((i % cols) * cell, (i // cols) * cell))
    sheet.save(out_dir / "sheet.png")
    print("sheet", out_dir / "sheet.png")
