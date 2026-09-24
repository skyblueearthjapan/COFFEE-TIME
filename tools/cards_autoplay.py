"""POKER TABLE を実機で自動で遊ぶ（開発用）。画面の状態表示 `K` を読みながら、局面に合わせてタップする。

使い方: python tools/cards_autoplay.py COM8 <出力フォルダー> [最大秒数=300]
  すでに卓（view=table）が開いている状態から始める。試合が終わる（match_over）か時間切れまで進め、
  局面ごとに最初の 1 枚だけスクリーンショットを撮り、[CARDS] / [GAS] のログを表示する。
  カフェ画面・ひと休み画面の「＋1杯」は絶対に押さない（本物の記録になる）。想定外の画面なら止まる。
"""
import pathlib
import re
import sys
import time

import ctport
from PIL import Image

port, out_dir = sys.argv[1], pathlib.Path(sys.argv[2])
limit = float(sys.argv[3]) if len(sys.argv) > 3 else 300.0
out_dir.mkdir(parents=True, exist_ok=True)

s = ctport.open_port(port, timeout=3)     # COM8 / net（Wi-Fi の遠隔コンソール）


def show(text: str) -> None:
    if text.startswith(("[CARDS]", "[GAS]", "[DEV]")) and " view=" not in text:
        print(time.strftime("%H:%M:%S"), text)


def drain() -> None:
    data = s.read(s.in_waiting) if s.in_waiting else b""
    for raw in data.split(b"\n"):
        show(raw.decode("utf-8", "replace").strip())


def state() -> dict:
    drain()
    s.write(b"K")
    deadline = time.time() + 30
    while time.time() < deadline:
        line = s.readline().decode("utf-8", "replace").strip()
        if line.startswith("[CARDS]") and " view=" in line:
            return dict(re.findall(r"(\w+)=(\S+)", line))
        show(line)
    sys.exit("no state answer")


def tap(x: int, y: int) -> None:
    # タップの直前にもう一度画面を確かめる。ひと休み・カフェ画面の「＋1杯」の位置に卓のタップが重なるため
    # （ファーム側でも偽装タップからの ＋1 は記録しないが、二重に守る）
    now = state()
    if now.get("view") in ("cafe", "paused") or now.get("view") == "-":
        sys.exit(f"refusing to tap on view={now.get('view')}: {now}")
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
        print("snapshot refused (a hand is on screen)")
        return
    w, h, size = header
    data = bytearray()
    while len(data) < size and time.time() < deadline + 15:
        data += s.read(size - len(data))
    Image.frombuffer("RGB", (w, h), bytes(data), "raw", "BGR;16", 0, 1).copy().save(out_dir / f"{name}.png")
    print("snap", name)


CONFIRM = (310, 376)
seen = set()
alt = 0
started = time.time()
while time.time() - started < limit:
    st = state()
    view, game, phase = st.get("view"), st.get("game"), st.get("phase")
    tag = f"{game}_{phase}" if view == "table" else view
    if tag not in seen:
        seen.add(tag)
        snap(tag)
    if view == "confirm":
        tap(*CONFIRM)
    elif view == "table":
        if st.get("pending") == "1":
            time.sleep(1.0)
            continue
        if phase in ("bet_pre", "bet_post", "preflop", "flop", "turn", "river"):
            # 2 つのボタンなら「続ける / 同じ額」、3 つなら「降りる」に当たる（どちらも確認画面へ進む）
            tap(170, 376)
            if state().get("view") != "confirm":
                tap(310, 376)
        elif phase == "draw":
            tap(104, 300)
            tap(310, 376)
        elif phase == "bid":
            tap(130 + 76 * (alt % 4), 295)
            alt += 1
            tap(310, 376)
        elif phase in ("turn", "last_reply"):
            if phase == "turn" and alt % 5 == 4:
                tap(310, 376)           # ここで勝負（ノック）
            else:
                tap(156 + 84 * (alt % 3), 195)
                tap(156 + 84 * ((alt // 3) % 3), 303)
                tap(170, 376)           # 交換する
            alt += 1
        elif phase == "predict":
            tap(136 + 104 * (alt % 3), 376)
            alt += 1
        elif phase == "unit_result":
            tap(310, 376)               # 次のハンド / 結果へ
        elif phase == "match_over":
            snap("match_over")
            print("match over:", st)
            break
        else:
            print("waiting on phase", phase)
            time.sleep(1.0)
    elif view == "result":
        snap("result")
        print("match result:", st)
        break
    elif view == "network":
        print("network view:", st)
        retries += 1
        if retries > 3:
            sys.exit("network view: gave up after 3 retries (Jev の呼び出しを浪費しないため)")
        tap(240, 223)                   # もう一度
    else:
        sys.exit(f"unexpected view: {st}")
else:
    print("time limit reached:", state())
