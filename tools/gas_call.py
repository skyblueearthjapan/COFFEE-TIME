"""GAS のウェブアプリを PC から呼ぶ（開発用）。URL と合言葉は secrets.h から読み、表示しない。

使い方:
  python tools/gas_call.py jevtest                 Jev（AI）への疎通試験。結果の JSON と所要時間を表示
  python tools/gas_call.py jevtest model=jev-1.13.0
  python tools/gas_call.py duel                    AI DUEL の予測を 1 回頼む（架空の集計。シートには書かない）
  python tools/gas_call.py duel withlog=1          上に加えて DuelRounds シートへ guest の試験行を 1 行書く
  python tools/gas_call.py reversi [mode=jev_pro|casual] [same=1]   JEV REVERSI の 1 手を頼む（シートには書かない）
  python tools/gas_call.py esper [same=1]                            エスパー対決の 1 局面を Jev に聞く（シートには書かない）
  python tools/gas_call.py cards [game=gops] [same=1]              POKER TABLE の 1 手を頼む（シートには書かない）
  python tools/gas_call.py ping-unauthorized    わざと違う合言葉で呼び、doPost が動いているかだけ確かめる
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

if event == "reversi":
    # 6×6 で人間（黒）が C2 に置いた直後の局面を聞く。シートには書かない。
    # 対局 ID を毎回変えるので毎回 Jev を呼ぶ（same=1 なら固定 ID = 2 回目からは GAS の控えが返る）
    import secrets as _secrets
    mode = str(payload.pop("mode", "jev"))
    gid = "0" * 32 if payload.pop("same", 0) else _secrets.token_hex(16)
    payload["req"] = 1
    payload["snapshot"] = {"game_id": gid, "n": 6, "human": "B", "mode": mode, "history": ["C2:H"]}

if event == "esper":
    # エスパー対決: 「食べ物か飲み物 → はい」のあと、飲み物 4 つが残り、安全な質問が 2 問ある局面。シートには書かない
    import secrets as _secrets
    sid = "0" * 32 if payload.pop("same", 0) else _secrets.token_hex(16)
    payload.update({"req": 1, "session": sid, "rev": 2, "mode": "mini",
                    "history": [{"q": "Q001", "a": "yes"}],
                    "candidates": ["D01", "D04", "D15", "D20"],
                    "shortlist": ["Q002", "Q003"], "remaining": 2})

if event == "cards":
    # POKER TABLE: バカラ（OPEN。P1=S7, B1=C2）の予想を 1 回頼む。手札の要らないゲームなので試験に向く。シートには書かない。
    # game=gops で GOPS（7 枚版・1 回目・得点札 4）も試せる。same=1 なら固定 ID = 2 回目からは GAS の控えが返る
    import secrets as _secrets
    game = str(payload.pop("game", "baccarat"))
    mid = "0" * 32 if payload.pop("same", 0) else _secrets.token_hex(16)
    payload.update({"req": 1, "match": mid, "rev": 3})
    if game == "holdem":
        # ホールデム: フロップで相手が 2 点出してきた局面（自分は A♠K♠、場 A♥7♦2♣）
        payload["observation"] = {"game": "holdem", "variant": "holdem", "phase": "flop", "rules_version": "1.0.0",
                                  "own_cards": ["SA", "SK"], "board": ["HA", "D7", "C2"], "hand_no": 2, "max_hands": 5,
                                  "stacks": {"self": 195, "opponent": 197}, "pot": 8,
                                  "contribution": {"self": 0, "opponent": 2}, "unit": 2, "raises_left": 2, "dealer": "SELF",
                                  "public_actions": [{"actor": "SELF", "type": "CHECK"}, {"actor": "OPPONENT", "type": "BET", "amount": 2}],
                                  "statistics": {"sample_n": 0}}
        payload["legal"] = ["CALL", "FOLD", "RAISE"]
    elif game == "gops":
        payload["observation"] = {"game": "gops", "variant": "quick7", "phase": "bid", "rules_version": "1.0.0",
                                  "n": 7, "round_no": 1, "prize": 4, "own_remaining": [1, 2, 3, 4, 5, 6, 7],
                                  "opponent_remaining": [1, 2, 3, 4, 5, 6, 7], "scores": {"self": 0, "opponent": 0},
                                  "history": [], "previous_match_history": []}
        payload["legal"] = ["PLAY:%d" % i for i in range(1, 8)]
    else:
        payload["observation"] = {"game": "baccarat", "variant": "open", "phase": "predict", "rules_version": "1.0.0",
                                  "decks": 8, "fresh_deck_each_round": True,
                                  "public_first_cards": {"PLAYER": "S7", "BANKER": "C2"}}
        payload["legal"] = ["BANKER", "PLAYER", "TIE"]

if event == "duel":
    # 「グーが多く、負けた次はパーに変えがち」な架空の人の集計。シートには書かない（withlog=1 のときだけ guest で 1 行書く）
    payload["req"] = 1
    payload["state"] = {
        "game": "rock_paper_scissors", "round_no": 4, "history_rounds": 83,
        "overall_counts": {"ROCK": 36, "SCISSORS": 22, "PAPER": 25},
        "recent20_counts": {"ROCK": 10, "SCISSORS": 4, "PAPER": 6},
        "first_hand_counts": {"ROCK": 5, "SCISSORS": 1, "PAPER": 2},
        "previous_round": {"player_hand": "ROCK", "ai_hand": "PAPER", "player_result": "ai_win"},
        "conditional_next_counts": {
            "by_hand": {"sample_n": 30, "ROCK": 8, "SCISSORS": 5, "PAPER": 17},
            "by_hand_result": {"sample_n": 12, "ROCK": 2, "SCISSORS": 1, "PAPER": 9}},
        "recent_sequence": [
            {"new_match": True, "history_truncated": False, "round_no": 1,
             "player_hand": "SCISSORS", "ai_hand": "PAPER", "player_result": "human_win"},
            {"new_match": False, "history_truncated": False, "round_no": 2,
             "player_hand": "ROCK", "ai_hand": "SCISSORS", "player_result": "human_win"},
            {"new_match": False, "history_truncated": False, "round_no": 3,
             "player_hand": "ROCK", "ai_hand": "PAPER", "player_result": "ai_win"}],
        "same_hand_streak": 2,
        "stats_baseline": {"ROCK": 0.30, "SCISSORS": 0.17, "PAPER": 0.53},
        "notice": "Counts are observations, not certainties. The current hand is not present.",
    }
    if payload.pop("withlog", 0):
        # 実機と同じく「記録だけ」の要求にする（予測の依頼と一緒には送らない）
        del payload["state"]
        payload["logs"] = [{"player": "guest", "match": 0, "round": 3, "you": "ROCK", "ai": "PAPER",
                            "result": "ai_win", "provider": "stats"}]

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
