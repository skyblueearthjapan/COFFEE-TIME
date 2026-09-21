"""AI DUEL の「正解データ」を設計書の参考実装から作る。

使い方（PowerShell から）:
    python tools/gen_duel_golden.py            # golden_cases.json を作り直す
    python tools/gen_duel_golden.py --cases 8  # 局数を減らして様子を見る

設計書付属の zip（参考データ/ゲーム4部作/COFFEE_TIME_AI_DUEL_Handoff_v1.0.zip）から
`duel_reference.py` を一時フォルダーへ取り出して import し、乱数で作ったラウンド列を
そのまま通して、各ラウンドの

  - そのラウンドを出す前の統計 AI の確率 (baseline)
  - その確率での最善手の集合 (optimal)
  - 実際に採用した確率とそこから選んだ AI の手
  - 1 局ぶんを通し終えたあとの統計まるごと

を firmware/src/app/games/duel/data/golden_cases.json に書き出す。
この JSON を tools/run_duel_checks.py が C++ 側と 1e-9 以内で突き合わせる。

**設計書のデータ（zip の中身）は絶対に書き換えない。** ここは読むだけ。
"""
import argparse
import importlib.util
import json
import pathlib
import random
import sys
import tempfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
ZIP = ROOT / "参考データ" / "ゲーム4部作" / "COFFEE_TIME_AI_DUEL_Handoff_v1.0.zip"
OUT = ROOT / "firmware" / "src" / "app" / "games" / "duel" / "data" / "golden_cases.json"

HANDS = ("ROCK", "SCISSORS", "PAPER")
RESULTS = ("human_win", "ai_win", "draw")
PROVIDERS = ("jev", "stats")


def load_reference():
    """zip の中の duel_reference.py を一時フォルダーへ出して import する。"""
    if not ZIP.is_file():
        print("設計書の zip が見つかりません:", ZIP)
        sys.exit(2)
    work = pathlib.Path(tempfile.mkdtemp(prefix="duel_ref_"))
    with zipfile.ZipFile(ZIP) as z:
        name = next((n for n in z.namelist() if n.endswith("duel_reference.py")), None)
        if name is None:
            print("zip の中に duel_reference.py がありません")
            sys.exit(2)
        target = work / "duel_reference.py"
        target.write_bytes(z.read(name))
    spec = importlib.util.spec_from_file_location("duel_reference", target)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module, name


def random_probabilities(rng) -> dict:
    """Jev の返事に見立てた、検証を通る確率分布を作る。"""
    raw = [rng.random() + 0.05 for _ in HANDS]
    total = sum(raw)
    p = {h: raw[i] / total for i, h in enumerate(HANDS)}
    # 参考実装の検証（合計 1±0.001）を必ず通る形にしておく
    return p


def pick_player_hand(rng, previous_hand, previous_result):
    """人の手。まったくの一様乱数だと癖も遷移も育たないので、少し偏らせる。

    偏らせ方そのものは試験の対象ではない（対象は、その列を通したときの統計と確率）。
    """
    if previous_hand is None:
        return rng.choice(HANDS)
    if previous_result == "human_win":
        # 勝った次は同じ手を出しがち
        return previous_hand if rng.random() < 0.62 else rng.choice(HANDS)
    if previous_result == "ai_win":
        # 負けた次は変えがち
        if rng.random() < 0.68:
            return rng.choice([h for h in HANDS if h != previous_hand])
        return previous_hand
    return previous_hand if rng.random() < 0.55 else rng.choice(HANDS)


def stats_snapshot(stats: dict) -> dict:
    """C++ 側の Stats と突き合わせる形に整える（round_id は端末に無いので落とす）。"""
    return {
        "rounds": stats["rounds"],
        "hand_counts": stats["hand_counts"],
        "outcome_counts": stats["outcome_counts"],
        "first_hand_counts": stats["first_hand_counts"],
        "transition_by_hand": stats["transition_by_hand"],
        "transition_by_hand_result": stats["transition_by_hand_result"],
        "repeat_after_result": stats["repeat_after_result"],
        "tail": [{"match_id": int(t["match_id"]), "round_no": t["round_no"],
                  "player_hand": t["player_hand"], "ai_hand": t["ai_hand"],
                  "player_result": t["player_result"]} for t in stats["tail"]],
        "prediction": {k: {"n": v["n"], "hits": v["hits"],
                           "brier_sum": v["brier_sum"], "nll_sum": v["nll_sum"]}
                       for k, v in stats["prediction"].items()},
        "match_counts": stats["match_counts"],
    }


def build_case(ref, case_index: int, seed: int, match_seq: int):
    rng = random.Random(seed * 1000003 + case_index)
    stats = ref.zero_stats()
    steps = []
    matches = []
    n_matches = rng.randint(2, 4)

    for _ in range(n_matches):
        match_seq += 1
        match_id = match_seq                    # 端末内の対戦通し番号 (uint16)
        # 4 局に 1 局くらいは途中でやめる（計画 §4 の aborted）
        aborted = rng.random() < 0.25
        planned = rng.randint(2, 9) if aborted else 10
        score = {"human_win": 0, "ai_win": 0, "draw": 0}
        previous_hand, previous_result = None, None

        for round_no in range(1, planned + 1):
            baseline = ref.stats_prediction(stats, str(match_id), round_no)
            baseline_optimal = ref.optimal_actions(baseline)

            provider = PROVIDERS[rng.randrange(len(PROVIDERS))]
            used = random_probabilities(rng) if provider == "jev" else baseline
            used = ref.normalize_probabilities(used)
            used_optimal = ref.optimal_actions(used)
            # 実機は esp_random() で等確率に選ぶ。手本では並びの中から乱数で 1 つに決める
            ai_hand = used_optimal[rng.randrange(len(used_optimal))]

            # 同じ対戦の中だけ「前の手」を引き継ぐ（対戦をまたいだら None に戻す）
            player_hand = pick_player_hand(rng, previous_hand, previous_result)
            player_result = ref.result(player_hand, ai_hand)

            stats = ref.append_resolved(stats, {
                "round_id": "%08x%08x" % (case_index * 100000 + match_id, round_no),
                "match_id": str(match_id),
                "round_no": round_no,
                "player_hand": player_hand,
                "ai_hand": ai_hand,
                "player_result": player_result,
                "provider_used": provider,
                "probabilities": used,
            })
            score[player_result] += 1
            previous_hand, previous_result = player_hand, player_result

            steps.append({
                "match_id": match_id,
                "round_no": round_no,
                "baseline": [baseline[h] for h in HANDS],
                "baseline_optimal": baseline_optimal,
                "provider": provider,
                "probabilities": [used[h] for h in HANDS],
                "optimal": used_optimal,
                "ai_hand": ai_hand,
                "player_hand": player_hand,
                "player_result": player_result,
            })

        if planned == 10:
            winner = ("human_win" if score["human_win"] > score["ai_win"]
                      else "ai_win" if score["human_win"] < score["ai_win"] else "draw")
            stats["match_counts"]["completed"] += 1
            stats["match_counts"][winner] += 1
        else:
            # 計画 §4：途中終了は aborted を 1 回だけ増やす（勝敗には数えない）
            stats["match_counts"]["aborted"] += 1
        matches.append({"match_id": match_id, "rounds": planned, "aborted": planned != 10})

    return {"case": case_index, "matches": matches, "steps": steps,
            "final_stats": stats_snapshot(stats)}, match_seq


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cases", type=int, default=20, help="作る局数（既定 20）")
    ap.add_argument("--seed", type=int, default=20260922, help="乱数の種")
    args = ap.parse_args()

    ref, zip_member = load_reference()
    print("参考実装を読み込みました:", zip_member)

    cases = []
    match_seq = 0
    for i in range(args.cases):
        case, match_seq = build_case(ref, i, args.seed, match_seq)
        cases.append(case)

    rounds = sum(len(c["steps"]) for c in cases)
    payload = {
        "generator": "tools/gen_duel_golden.py",
        "reference": "duel_reference.py (COFFEE_TIME_AI_DUEL_Handoff_v1.0.zip)",
        "seed": args.seed,
        "case_count": len(cases),
        "round_count": rounds,
        "note": "手で編集しないこと。作り直しは python tools/gen_duel_golden.py",
        "cases": cases,
    }
    OUT.parent.mkdir(parents=True, exist_ok=True)
    # 1 局 1 行の詰めた JSON にする（float を 1 行ずつ並べると 3 倍近く太るため）。
    # 差分は「どの局が変わったか」の粒度で読めれば十分
    head = {k: v for k, v in payload.items() if k != "cases"}
    body = ",\n".join(json.dumps(c, ensure_ascii=False, separators=(",", ":")) for c in cases)
    text = json.dumps(head, ensure_ascii=False, separators=(",", ":"))[:-1] + \
        ',"cases":[\n' + body + "\n]}\n"
    OUT.write_text(text, encoding="utf-8", newline="\n")
    print("%s: %d 局 / %d ラウンド / %.0f KB"
          % (OUT.name, len(cases), rounds, OUT.stat().st_size / 1024))
    return 0


if __name__ == "__main__":
    sys.exit(main())
