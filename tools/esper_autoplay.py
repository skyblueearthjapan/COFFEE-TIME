"""エスパー対決を実機で自動で 1 局遊ぶ（開発用）。状態表示 `V` を読みながら、局面に合わせてタップする。

使い方: python tools/esper_autoplay.py COM8 <出力フォルダー> [最大秒数=240]
  「じゅんび」画面（view=3）か質問画面から始める。はい／いいえを交互に押し、最終予想には「当たり」と答えて結果まで進める
  （答え合わせの内容は試験用なので本当かどうかは問わない）。画面ごとに最初の 1 枚だけスクリーンショットを撮り、
  [ESP] / [GAS] のログを表示する。カフェ画面・ひと休み画面では止まる（「＋1杯」は押さない）。
"""
import pathlib
import re
import sys
import time

import ctport
from PIL import Image

port, out_dir = sys.argv[1], pathlib.Path(sys.argv[2])
limit = float(sys.argv[3]) if len(sys.argv) > 3 else 240.0
out_dir.mkdir(parents=True, exist_ok=True)

s = ctport.open_port(port, timeout=3)     # COM8 / net（Wi-Fi の遠隔コンソール）

VIEW = {"0": "mode", "1": "catalog", "2": "card", "3": "ready", "4": "thinking", "5": "waiting", "6": "question",
        "7": "help", "8": "quit", "9": "guess", "10": "result", "11": "reveal_pick", "12": "reveal_card",
        "13": "stats", "14": "error", "15": "cafe", "16": "paused", "17": "memo"}


def show(text: str) -> None:
    if text.startswith(("[ESP]", "[GAS]", "[DEV]")) and " view=" not in text:
        print(time.strftime("%H:%M:%S"), text)


def drain() -> None:
    data = s.read(s.in_waiting) if s.in_waiting else b""
    for raw in data.split(b"\n"):
        show(raw.decode("utf-8", "replace").strip())


def state() -> dict:
    drain()
    s.write(b"V")
    deadline = time.time() + 30
    while time.time() < deadline:
        line = s.readline().decode("utf-8", "replace").strip()
        if line.startswith("[ESP]") and " view=" in line:
            d = dict(re.findall(r"(\w+)=(\S+)", line))
            d["name"] = VIEW.get(d.get("view", ""), d.get("view", ""))
            return d
        show(line)
    sys.exit("no state answer")


def tap(x: int, y: int, expect: str) -> None:
    # 押す直前に画面を確かめ、想定の画面でなければ止める（ひと休み画面の「＋1杯」と同じ場所を押す座標があるため）
    now = state()
    if now["name"] != expect:
        sys.exit(f"refusing to tap: expected {expect}, device is on {now['name']}: {now}")
    drain()
    s.write(f"P{x},{y}\n".encode())
    deadline = time.time() + 45
    while time.time() < deadline:
        line = s.readline()
        if line.startswith(b"[TAP]"):
            break
        show(line.decode("utf-8", "replace").strip())
    else:
        sys.exit(f"no tap ack {x},{y}")
    time.sleep(0.7)


def snap(name: str) -> None:
    drain()
    s.write(b"S")
    header = None
    deadline = time.time() + 15
    while time.time() < deadline:
        line = s.readline()
        if line.startswith(b"[SNAP] ") and not line.startswith(b"[SNAP] END"):
            parts = line.decode(errors="replace").split()
            if len(parts) == 4:
                header = (int(parts[1]), int(parts[2]), int(parts[3]))
                break
    if header is None:
        print("snapshot refused")
        return
    w, h, size = header
    data = bytearray()
    while len(data) < size and time.time() < deadline + 15:
        data += s.read(size - len(data))
    Image.frombuffer("RGB", (w, h), bytes(data), "raw", "BGR;16", 0, 1).copy().save(out_dir / f"{name}.png")
    print("snap", name)


seen = set()
turn = 0
started = time.time()
while time.time() - started < limit:
    st = state()
    name = st["name"]
    tag = name if name != "question" else f"question_{st.get('asked', '')}".replace("/", "of")
    if tag not in seen:
        seen.add(tag)
        snap(tag)
    if name == "ready":
        tap(240, 356, "ready")              # はじめる
    elif name in ("thinking", "waiting"):
        time.sleep(0.8)
    elif name == "question":
        tap(158, 314, "question") if turn % 2 == 0 else tap(322, 314, "question")   # はい / いいえ
        turn += 1
    elif name == "guess":
        tap(158, 326, "guess")              # 当たり（試験用）
    elif name == "result":
        print("result:", st)
        break
    else:
        sys.exit(f"unexpected view: {st}")
else:
    print("time limit reached:", state())
