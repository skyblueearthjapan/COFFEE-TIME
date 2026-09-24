"""Wi-Fi 越しに COFFEE TIME のソフトを更新する（USB をつながなくてよい）。

使い方: python tools/ota.py [host] [--bin path]
  host  … 既定 coffee-time.local（名前が引けないときは「システム情報」の IP アドレス）
  --bin … 既定 firmware/.pio/build/app/firmware.bin（先に `python -m platformio run -e app` でビルド）

端末は TCP 2324 で待っている。合言葉は firmware/include/secrets.h の REMOTE_PASSWORD（表示しない）。
ゲームの画面が開いている・コーヒーの記録が GAS に未送信・秘密が画面に出ている間は、端末が断る（[OTA] busy: …）。
失敗しても端末は今のソフトのまま動く。成功すると端末が再起動する。終了コード: 成功 0 / 失敗 1
"""
import argparse
import hashlib
import pathlib
import socket
import sys
import time

import ctport

OTA_PORT = 2324


def read_line(sock: socket.socket, buf: bytearray, timeout: float) -> str:
    """\\n までの 1 行を返す（時間切れ・切断は空文字）"""
    deadline = time.time() + timeout
    while b"\n" not in buf:
        left = deadline - time.time()
        if left <= 0:
            return ""
        sock.settimeout(left)
        try:
            chunk = sock.recv(1024)
        except socket.timeout:
            return ""
        if not chunk:
            return ""
        buf += chunk
    line, _, rest = bytes(buf).partition(b"\n")
    buf[:] = rest
    return line.decode("utf-8", "replace").rstrip("\r")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("host", nargs="?", default=ctport.DEFAULT_HOST)
    ap.add_argument("--bin", default=str(ctport.ROOT / "firmware" / ".pio" / "build" / "app" / "firmware.bin"))
    args = ap.parse_args()

    image = pathlib.Path(args.bin).read_bytes()
    md5 = hashlib.md5(image).hexdigest()
    password = ctport.remote_password()
    if not password:
        print("REMOTE_PASSWORD is not set in firmware/include/secrets.h")
        return 1
    host = ctport.host_of(args.host)
    print(f"{args.bin}: {len(image)} bytes md5 {md5} -> {host}:{OTA_PORT}")

    sock = socket.create_connection((host, OTA_PORT), timeout=10)
    buf = bytearray()
    try:
        sock.sendall(f"AUTH {password}\n".encode())
        line = read_line(sock, buf, 10)
        if line != "[CON] ok":
            print(line or "no answer to AUTH (locked out after 3 wrong tries?)")
            return 1
        sock.sendall(f"OTA {len(image)} {md5}\n".encode())
        line = read_line(sock, buf, 15)
        print(line or "no answer to OTA")
        if not line.startswith("[OTA] ready"):
            return 1

        # 送りながら、端末の進み具合（[OTA] 10% …）をそのまま表示する
        sock.settimeout(30)
        view = memoryview(image)
        sent = 0
        chunk = 4096
        while sent < len(image):
            try:
                sock.sendall(view[sent:sent + chunk])
            except OSError as e:
                # 端末が途中で断って切った。理由の行が届いていれば見せる
                print(f"send failed at {sent}/{len(image)} bytes: {e}")
                for text in bytes(buf).decode("utf-8", "replace").splitlines():
                    print(text)
                return 1
            sent += min(chunk, len(image) - sent)
            sock.setblocking(False)
            try:
                data = sock.recv(1024)
                if data:
                    buf += data
                elif data == b"":
                    print("connection closed by the device")
                    return 1
            except BlockingIOError:
                pass
            finally:
                sock.setblocking(True)
                sock.settimeout(30)
            while b"\n" in buf:
                one, _, rest = bytes(buf).partition(b"\n")
                buf[:] = rest
                text = one.decode("utf-8", "replace").rstrip("\r")
                print(text)
                if text.startswith("[OTA] failed"):
                    return 1

        # 最後の行（ok, rebooting / failed: …）を待つ。MD5 の照合と書き込みの仕上げに数秒かかる
        while True:
            line = read_line(sock, buf, 60)
            if not line:
                print("no final answer from the device")
                return 1
            print(line)
            if line.startswith("[OTA] ok"):
                return 0
            if line.startswith("[OTA] failed"):
                return 1
    finally:
        sock.close()


if __name__ == "__main__":
    sys.exit(main())
