"""GAS に見本メール（所有者だけに届く・ログに残らない）を依頼する。URL と合言葉は表示しない。

使い方: python tools/gas_preview.py [残り杯数=3] [1 回に作る杯数=12]

GAS は POST を処理したあと 302 で別ホストへ渡す。転送先は「ヘッダーを引き継がない新しい GET」で読むこと
（引き継ぐと 400/404 になる。docs/HANDOFF.md §8-4）。
"""
import http.client
import json
import pathlib
import re
import sys
import urllib.parse
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
src = (ROOT / "firmware" / "include" / "secrets.h").read_text(encoding="utf-8", errors="replace")


def define(name):
    m = re.findall(r'^[ \t]*#define[ \t]+' + name + r'[ \t]+"(.*)"', src, re.M)
    return m[-1] if m else ""


url, token = define("GAS_URL"), define("GAS_TOKEN")
if not url or not token:
    sys.exit("GAS_URL / GAS_TOKEN not set")

left = int(sys.argv[1]) if len(sys.argv) > 1 else 3
cups = int(sys.argv[2]) if len(sys.argv) > 2 else 12
body = json.dumps({"token": token, "event": "preview", "left": left, "max": cups}).encode()

u = urllib.parse.urlsplit(url)
conn = http.client.HTTPSConnection(u.netloc, timeout=60)
conn.request("POST", u.path + ("?" + u.query if u.query else ""), body=body,
             headers={"Content-Type": "application/json"})
resp = conn.getresponse()
resp.read()
print("POST ->", resp.status)
location = resp.getheader("Location")
conn.close()
if resp.status in (301, 302, 303) and location:
    try:
        with urllib.request.urlopen(urllib.request.Request(location), timeout=60) as r:
            print("GET  ->", r.status, r.read(200).decode("utf-8", "replace"))
    except Exception as e:      # URL を含むメッセージは出さない
        print("GET  -> failed:", type(e).__name__)
