"""GAS のウェブアプリを PC から呼ぶ（開発用）。URL と合言葉は secrets.h から読み、表示しない。

使い方:
  python tools/gas_call.py jevtest                 Jev（AI）への疎通試験。結果の JSON と所要時間を表示
  python tools/gas_call.py jevtest model=jev-1.13.0
  python tools/gas_call.py ping-unauthorized       わざと違う合言葉で呼び、doPost が動いているかだけ確かめる
                                                   （{"ok":false,"error":"unauthorized"} が返れば正常。何も記録されない）

GAS は POST を処理したあと 302 で別ホストへ渡す。転送先は「ヘッダーを引き継がない新しい GET」で読む
（引き継ぐと 400/404 になる。docs/HANDOFF.md §8-4）。
"""
import http.client
import json
import pathlib
import re
import sys
import time
import urllib.parse
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
src = (ROOT / "firmware" / "include" / "secrets.h").read_text(encoding="utf-8", errors="replace")


def define(name):
    m = re.findall(r'^[ \t]*#define[ \t]+' + name + r'[ \t]+"(.*)"', src, re.M)
    return m[-1] if m else ""


url, token = define("GAS_URL"), define("GAS_TOKEN")
if not url or not token:
    sys.exit("GAS_URL / GAS_TOKEN not set in secrets.h")
if len(sys.argv) < 2:
    sys.exit(__doc__)

event = sys.argv[1]
payload = {"token": token, "event": event}
if event == "ping-unauthorized":
    payload = {"token": "wrong-token-on-purpose", "event": "take"}
for arg in sys.argv[2:]:
    k, _, v = arg.partition("=")
    payload[k] = int(v) if v.lstrip("-").isdigit() else v

u = urllib.parse.urlsplit(url)
started = time.time()
conn = http.client.HTTPSConnection(u.netloc, timeout=90)
conn.request("POST", u.path + ("?" + u.query if u.query else ""), body=json.dumps(payload).encode(),
             headers={"Content-Type": "application/json"})
resp = conn.getresponse()
resp.read()
location = resp.getheader("Location")
conn.close()
print("POST ->", resp.status)
if resp.status in (301, 302, 303) and location:
    try:
        with urllib.request.urlopen(urllib.request.Request(location), timeout=90) as r:
            text = r.read(4000).decode("utf-8", "replace")
    except Exception as e:      # URL を含むメッセージは出さない
        sys.exit("GET  -> failed: " + type(e).__name__)
    print("round trip %.1f s" % (time.time() - started))
    try:
        print(json.dumps(json.loads(text), ensure_ascii=False, indent=2))
    except ValueError:
        print(text[:600])
