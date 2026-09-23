"""実機の画面をシリアルから操作して、順にスクリーンショットを撮る（開発用）。

使い方: python tools/uiwalk.py COM8 <出力フォルダー> <手順> [<手順> ...]
  手順:
    key:<文字>     開発用の 1 文字コマンドを送る（例 key:3 = 人狼のロビーを開く、key:0 = HOME へ）
    tap:<x>,<y>    その座標をタップする（ファームの P コマンド。秘密の表示はできない）
    wait:<秒>      待つ
    snap:<名前>    画面を <名前>.png に保存する
    expect:<文字>:<文言>  状態表示コマンド（U=DUEL など）を送り、返事に <文言> が無ければ **そこで中止**する。
                   ゲーム内のタップの前に必ず入れる（例 expect:U:view=result）。HOME でのタップは本物の「＋1」になる
    watch:<秒>     その間の [DUEL] / [GAS] のログを表示する
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


def show(raw: bytes) -> None:
    """待っている間に流れてきた、ゲームと通信の 1 行ログを表示する（数え漏れを防ぐ）。"""
    text = raw.decode("utf-8", "replace").strip()
    if text.startswith(("[DUEL]", "[REV]", "[CARDS]", "[ESP]", "[GAS]", "[DEV]")) and " view=" not in text:
        print(time.strftime("%H:%M:%S"), text)


def drain() -> None:
    """受信済みのデータを捨てる前に、ゲームと通信の 1 行ログだけは表示する（待ち時間中に届いた分の数え漏れ防止）。"""
    data = s.read(s.in_waiting) if s.in_waiting else b""
    for raw in data.split(b"\n"):
        show(raw)


def snapshot(path: pathlib.Path) -> Image.Image:
    drain()
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
        drain()
        s.write(arg.encode())
        if arg and arg in "0123456789BFI":
            # 画面を切り替える命令は受領の返事 [KEY] を待つ。待たずにタップを送ると、端末が忙しい間に
            # 「切替 → タップ」が続けて処理され、タップが作りかけの画面や前の画面（HOME の ＋1）に当たる
            deadline = time.time() + 45
            while time.time() < deadline:
                line = s.readline()
                if line.startswith(b"[KEY]"):
                    break
                show(line)
            else:
                sys.exit(f"no key ack: {arg}（古いファームには [KEY] の返事が無い）")
            time.sleep(0.8)     # 画面を作って最初の描画が終わるまで
        else:
            time.sleep(0.3)
    elif kind == "tap":
        # 端末はメインループが数秒止まることがある（Wi-Fi の再接続）。受領の返事を待ってから次へ進む
        drain()
        s.write(f"P{arg}\n".encode())
        sent = time.time()
        deadline = sent + 45            # 通信（GAS への送信など）でメインループが数十秒止まることがある
        while time.time() < deadline:
            line = s.readline()
            if line.startswith(b"[TAP]"):
                break
            show(line)
        else:
            sys.exit(f"no tap ack: {arg}")
        if time.time() - sent > 1.0:    # 端末が止まっていた時間の目安（画面の処理が重い・通信中など）
            print(f"slow tap ack {time.time() - sent:.1f}s: {arg}")
        time.sleep(0.6)     # 押下 120ms + 離す + 画面の作り直し
    elif kind == "wait":
        time.sleep(float(arg))
    elif kind == "expect":
        # いまの画面が想定どおりかを端末に聞く。違えば **それ以降のタップを送らずに止める**
        # （端末が再起動して HOME に戻っていると、ゲーム用のタップが「＋1」に当たって本物の記録になる）
        key, _, want = arg.partition(":")
        drain()
        s.write(key.encode())
        deadline = time.time() + 45
        seen = ""
        while time.time() < deadline:
            seen = s.readline().decode("utf-8", "replace").strip()
            # 状態表示の行（view= を含む）だけを返事として扱う。通信の 1 行ログ（[DUEL] req=…）は読み飛ばす
            if " view=" not in seen:
                if seen.startswith(("[DUEL]", "[REV]", "[CARDS]", "[ESP]", "[GAS]", "[DEV]")):
                    print(time.strftime("%H:%M:%S"), seen)
                continue
            if want in seen:
                break
            if seen.startswith("["):      # どのゲームの状態表示でも、想定と違えばすぐ止める
                sys.exit(f"expect failed: wanted '{want}', device says: {seen}")
        else:
            sys.exit(f"expect failed: no answer for key {key}")
    elif kind == "until":
        # until:<文字>:<文言>[:<秒>]  状態表示に <文言> が出るまで 0.5 秒おきに聞き直す（既定 30 秒）。
        # 相手の番・パスの表示・通信待ちなど、長さが決まらない待ちに使う。出なければ中止する
        parts = arg.split(":")
        key, want = parts[0], parts[1]
        limit = float(parts[2]) if len(parts) > 2 else 30.0
        deadline = time.time() + limit
        seen = ""
        ok = False
        while time.time() < deadline and not ok:
            drain()
            s.write(key.encode())
            inner = time.time() + 3
            while time.time() < inner:
                seen = s.readline().decode("utf-8", "replace").strip()
                if " view=" in seen:
                    ok = want in seen
                    break
                if seen.startswith(("[DUEL]", "[REV]", "[CARDS]", "[ESP]", "[GAS]", "[DEV]")):
                    print(time.strftime("%H:%M:%S"), seen)
            if not ok:
                time.sleep(0.5)
        if not ok:
            sys.exit(f"until timed out: wanted '{want}', last: {seen}")
    elif kind == "watch":
        # <秒> の間、ゲームと通信の 1 行ログだけを表示する（Wi-Fi の名前が出る行は通さない）
        deadline = time.time() + float(arg)
        s.timeout = 0.5
        while time.time() < deadline:
            line = s.readline().decode("utf-8", "replace").strip()
            if line.startswith(("[DUEL]", "[REV]", "[CARDS]", "[ESP]", "[GAS]", "[DEV]")):
                print(time.strftime("%H:%M:%S"), line)
        s.timeout = 5
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
