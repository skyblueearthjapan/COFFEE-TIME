"""COFFEE TIME の開発用コンソールを開く（USB のシリアル / Wi-Fi の遠隔コンソールのどちらでも）。

各道具（serlog.py・snapshot.py・uiwalk.py など）の最初の引数をそのまま渡す:
  COM8                  … USB のシリアル（従来どおり。dtr/rts を下げてから開く＝端末が再起動しない）
  net                   … Wi-Fi の遠隔コンソール coffee-time.local:2323
  coffee-time.local / IP … 同上（名前が引けないときは「システム情報」の IP アドレスを使う）

遠隔コンソールの合言葉は firmware/include/secrets.h の REMOTE_PASSWORD から読む（表示しない）。
返すのは pyserial の Serial と同じ使い方ができる物（write / read / readline / in_waiting /
reset_input_buffer / close / timeout）。
"""
import pathlib
import re
import time

import serial
from serial.urlhandler import protocol_socket

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_HOST = "coffee-time.local"
CONSOLE_PORT = 2323


class PortError(serial.SerialException):
    """遠隔コンソールにつなげなかった（合言葉違い・応答なし）。SerialException の一種"""


class _SocketSerial(protocol_socket.Serial):
    """pyserial の socket:// は Windows で close が 2 回目（終了時の後片付け）に
    OSError（ハンドルが無効です）を出す。動作には影響しないので黙らせる"""

    def close(self):
        try:
            super().close()
        except OSError:
            pass


def is_serial_port(name: str) -> bool:
    """COM8 や /dev/ttyACM0 のような本物のシリアルポートか"""
    return bool(re.fullmatch(r"(?i)com\d+", name)) or name.startswith("/dev/")


def host_of(name: str) -> str:
    return DEFAULT_HOST if name.lower() in ("net", "wifi") else name


def remote_password() -> str:
    """secrets.h の REMOTE_PASSWORD（gas_call.py の GAS_TOKEN と同じ読み方）。値は表示しない"""
    path = ROOT / "firmware" / "include" / "secrets.h"
    try:
        src = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""
    m = re.findall(r'^[ \t]*#define[ \t]+REMOTE_PASSWORD[ \t]+"(.*)"', src, re.M)
    return m[-1] if m else ""


def open_port(name: str, timeout=None):
    if is_serial_port(name):
        # 直接開くと端末が再起動するので、dtr/rts を下げてから開く（引き継ぎ書 §10-11）
        s = serial.Serial()
        s.port = name
        s.baudrate = 115200
        s.timeout = timeout
        s.dtr = False
        s.rts = False
        s.open()
        return s

    password = remote_password()
    if not password:
        raise PortError("REMOTE_PASSWORD is not set in firmware/include/secrets.h")
    host = host_of(name)
    s = _SocketSerial(f"socket://{host}:{CONSOLE_PORT}", timeout=1)
    s.write(f"AUTH {password}\n".encode())
    deadline = time.time() + 8
    while time.time() < deadline:
        line = s.readline()
        if line.startswith(b"[CON] ok"):
            s.timeout = timeout
            return s
        if line.startswith(b"[CON] denied"):
            s.close()
            raise PortError(f"{host}: password denied (3 wrong tries lock it for 60 s)")
    s.close()
    raise PortError(f"{host}: no answer to AUTH (another login within the last minute may have locked it)")
