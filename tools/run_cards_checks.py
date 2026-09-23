"""POKER TABLE（CAFE CARDS）のロジック試験を、この PC（実機なし）で組み立てて実行する。

使い方（PowerShell から）:
    python tools/run_cards_checks.py
    python tools/run_cards_checks.py --keep       # 組み立てた実行ファイルを消さない

コンパイラーは pip で入れた zig を使う（Visual Studio も MinGW も要らない）:
    python -m pip install --user ziglang
観測の検査には Node.js を使う（gas/CardsGate.gs をそのまま読む）。
どちらも無ければこのスクリプトが入れ方を案内して止まる。

この試験は 3 段階:
  0. firmware/src/app/games/cards/core/cards_core.hpp と local_policy.hpp が
     設計一式の原本と **1 バイトも違わない**ことを SHA-256 で確かめる
     （gas/CardsContract.gs・gas/CardsGate.gs が原本の JS と同一であることも合わせて見る）
  1. zig で tools/cards_checks.cpp を組み立てて実行する
     （原本 tests/core_test.cpp・local_test.cpp 相当、3 枚 22,100 通り、バカラの表、
       4 ゲーム × 端末AI同士 / でたらめ対端末AI を各 400 試合）
  2. その C++ が書き出した観測 JSON を、Node で gas/CardsGate.gs
     （＝原本 observation_gate.js・無改変）の validateObservation / requestFromObservation に
     通し、**1 件も断られない**ことを確かめる

試験の中身は tools/cards_checks.cpp を参照。
"""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
GAME_DIR = ROOT / "firmware" / "src" / "app" / "games" / "cards"
SOURCE = ROOT / "tools" / "cards_checks.cpp"
CORE = GAME_DIR / "core" / "cards_core.hpp"
LOCAL = GAME_DIR / "core" / "local_policy.hpp"
EXTRA = GAME_DIR / "core" / "cards_extra.hpp"
GATE_GS = ROOT / "gas" / "CardsGate.gs"
CONTRACT_GS = ROOT / "gas" / "CardsContract.gs"
ZIP = ROOT / "参考データ" / "ゲーム4部作" / "COFFEE_TIME_CAFE_CARDS_UI_Handoff_v1.1.zip"
INNER_ZIP_SUFFIX = "baseline/CAFE_CARDS_Handoff_v1.0_READONLY.zip"

# 原本（zip in zip）と、このリポジトリに置いた写し。**無改変**が条件（計画 §2）
UNMODIFIED = (
    (CORE, "core/cards_core.hpp"),
    (LOCAL, "reference/local_policy.hpp"),
    (CONTRACT_GS, "reference/jev_contract.js"),
    (GATE_GS, "reference/observation_gate.js"),
)

RUN_TIMEOUT_S = 900


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def zig_version() -> str:
    try:
        out = subprocess.run([sys.executable, "-m", "ziglang", "version"],
                             capture_output=True, text=True, timeout=120)
    except Exception:
        return ""
    return out.stdout.strip() if out.returncode == 0 else ""


def node_version() -> str:
    exe = shutil.which("node")
    if exe is None:
        return ""
    try:
        out = subprocess.run([exe, "--version"], capture_output=True, text=True, timeout=120)
    except Exception:
        return ""
    return out.stdout.strip() if out.returncode == 0 else ""


_INNER = {}


def baseline_member(name_suffix: str) -> bytes:
    """設計一式 zip の中の baseline zip から 1 ファイル読む。"""
    if not _INNER:
        with zipfile.ZipFile(ZIP) as outer:
            name = next((n for n in outer.namelist() if n.endswith(INNER_ZIP_SUFFIX)), None)
            if name is None:
                raise SystemExit("zip の中に %s がありません" % INNER_ZIP_SUFFIX)
            import io
            with zipfile.ZipFile(io.BytesIO(outer.read(name))) as inner:
                for n in inner.namelist():
                    _INNER[n] = inner.read(n)
    for key, data in _INNER.items():
        if key.endswith(name_suffix):
            return data
    raise SystemExit("原本の中に %s がありません" % name_suffix)


def check_unmodified() -> bool:
    ok = True
    for path, member in UNMODIFIED:
        ours = sha256(path.read_bytes())
        theirs = sha256(baseline_member(member))
        mark = "一致" if ours == theirs else "**違う**"
        print("  %-22s %s  %s" % (path.name, ours[:16], mark))
        if ours != theirs:
            print("     原本: %s" % theirs[:16])
            ok = False
    return ok


# ---------------------------------------------------------------------------
# Node 側（gas/CardsGate.gs をそのまま読む）
# ---------------------------------------------------------------------------
NODE_SCRIPT = r"""
'use strict';
const fs = require('fs');
const path = require('path');
// gas/CardsGate.gs（= 原本 observation_gate.js・無改変）。中で ./jev_contract.js を require する
const Gate = require(path.join(__dirname, 'observation_gate.js'));
// gas/CardsContract.gs（= 原本 jev_contract.js・無改変）。役のキーの正本
const Contract = require(path.join(__dirname, 'jev_contract.js'));

let bad = 0;

// --- 観測を入力ゲートに通す -------------------------------------------------
const rows = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const seen = {};
for (const row of rows) {
  const key = row.game + '/' + row.phase;
  seen[key] = (seen[key] || 0) + 1;
  try {
    const req = Gate.requestFromObservation(row.observation, row.legal);
    const keys = Object.keys(req.questions.action.criteria).sort();
    if (JSON.stringify(keys) !== JSON.stringify(row.legal)) {
      throw new Error('CRITERIA_MISMATCH');
    }
  } catch (err) {
    if (bad < 10) {
      console.log('  断られました (' + key + '): ' + err.message);
      console.log('    ' + JSON.stringify(row.observation).slice(0, 300));
    }
    bad++;
  }
}
console.log('  観測 ' + rows.length + ' 件を CardsGate に通しました（不合格 ' + bad + ' 件）');
console.log('  内訳: ' + JSON.stringify(seen));
const want = ['poker/bet_pre', 'poker/draw', 'poker/bet_post', 'gops/bid',
              'thirty_one/turn', 'thirty_one/last_reply', 'baccarat/predict'];
const missing = want.filter(function (k) { return !seen[k]; });
if (missing.length) {
  console.log('  **試していない段階: ' + missing.join(', ') + '**');
  bad++;
}

// --- 役のキーが C++ と JS で一致するか --------------------------------------
// 端末（cards_core.hpp の poker_value）と GAS（CardsContract.gs の pokerKey）が
// 別々に書かれているので、**両方が同じ 6 つの数字を出すこと**をここで確かめる。
// 観測の own_poker_key はゲートがこの pokerKey と突き合わせるため、ここがずれると
// 実機のポーカーだけが POKER_KEY で全部断られる
function faceId(s) {
  const m = /^([CDHS])([2-9]|10|J|Q|K|A)$/.exec(s);
  if (!m) throw new Error('FACE ' + s);
  const r = { J: 11, Q: 12, K: 13, A: 14 }[m[2]] || Number(m[2]);
  return 'CDHS'.indexOf(m[1]) * 13 + r - 2;
}
const keyRows = JSON.parse(fs.readFileSync(process.argv[3], 'utf8'));
let keyBad = 0;
const categories = {};
for (const row of keyRows) {
  let mine;
  try {
    mine = Contract.pokerKey(row.cards.map(faceId));
  } catch (err) {
    if (keyBad < 10) console.log('  pokerKey が断りました: ' + row.cards.join(',') + ' — ' + err.message);
    keyBad++;
    continue;
  }
  categories[mine[0]] = (categories[mine[0]] || 0) + 1;
  if (JSON.stringify(mine) !== JSON.stringify(row.key)) {
    if (keyBad < 10) {
      console.log('  役のキーが違います ' + row.cards.join(',') +
                  ' — C++ ' + JSON.stringify(row.key) + ' / JS ' + JSON.stringify(mine));
    }
    keyBad++;
  }
}
console.log('  役のキー ' + keyRows.length + ' 件を CardsContract.pokerKey と照合しました（不合格 ' +
            keyBad + ' 件）');
console.log('  役の内訳（0=ハイカード〜8=ストレートフラッシュ）: ' + JSON.stringify(categories));
for (let c = 0; c <= 8; c++) {
  if (!categories[c]) {
    console.log('  **役 ' + c + ' を 1 件も試していません**');
    keyBad++;
  }
}
bad += keyBad;

process.exit(bad === 0 && rows.length > 0 && keyRows.length > 0 ? 0 : 1);
"""


def write_node_tools(out_dir: pathlib.Path) -> pathlib.Path:
    # .gs のままでは require できないので、同じ中身を .js として置く（書き換えない）
    (out_dir / "jev_contract.js").write_bytes(CONTRACT_GS.read_bytes())
    (out_dir / "observation_gate.js").write_bytes(GATE_GS.read_bytes())
    script = out_dir / "cards_js.js"
    script.write_text(NODE_SCRIPT, encoding="utf-8", newline="\n")
    return script


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--keep", action="store_true", help="組み立てた実行ファイルを消さない")
    args = ap.parse_args()

    for path in (SOURCE, CORE, LOCAL, EXTRA, GATE_GS, CONTRACT_GS, ZIP):
        if not path.is_file():
            print("試験の元ファイルが見つかりません:", path)
            return 2

    version = zig_version()
    if not version:
        print("zig が見つかりません。次のコマンドで入れてください:")
        print("    python -m pip install --user ziglang")
        return 2
    node = node_version()
    if not node:
        print("Node.js が見つかりません（gas/CardsGate.gs を動かすのに必要です）")
        return 2
    print("zig %s / Node %s で試験します\n" % (version, node))

    print("[0] 原本を書き換えていないか（SHA-256）")
    if not check_unmodified():
        print("  **原本と違います。無改変で使うきまりです（計画 §2）**")
        return 1
    print()

    out_dir = pathlib.Path(tempfile.mkdtemp(prefix="cards_checks_"))
    script = write_node_tools(out_dir)

    print("[1] zig で組み立てて実行する")
    exe = out_dir / ("checks.exe" if os.name == "nt" else "checks")
    # zig は -O2 のとき NDEBUG を付けるので、assert を残すために外す
    cmd = [sys.executable, "-m", "ziglang", "c++", "-std=c++17", "-O2", "-UNDEBUG",
           "-Wall", "-Wextra", "-Wno-unused-parameter", str(SOURCE), "-o", str(exe)]
    t0 = time.time()
    try:
        build = subprocess.run(cmd, capture_output=True, text=True, timeout=RUN_TIMEOUT_S)
    except subprocess.TimeoutExpired:
        print("  コンパイルが %d 秒で終わりませんでした" % RUN_TIMEOUT_S)
        return 1
    if build.stdout.strip():
        print(build.stdout)
    if build.stderr.strip():
        print(build.stderr)
    if build.returncode != 0:
        print("  コンパイルに失敗しました")
        return build.returncode
    print("  コンパイル %.1f 秒\n" % (time.time() - t0))

    samples = out_dir / "observations.json"
    keys = out_dir / "poker_keys.json"
    t1 = time.time()
    try:
        rc = subprocess.run([str(exe), str(samples), str(keys)], text=True,
                            timeout=RUN_TIMEOUT_S).returncode
    except subprocess.TimeoutExpired:
        print("\n  **試験が %d 秒で終わりませんでした**" % RUN_TIMEOUT_S)
        rc = 1
    print("\n実行 %.1f 秒\n" % (time.time() - t1))

    print("[2] 観測と役のキーを gas/CardsGate.gs・gas/CardsContract.gs（原本）に通す")
    if samples.is_file() and keys.is_file():
        try:
            verify_rc = subprocess.run([shutil.which("node"), str(script), str(samples),
                                        str(keys)], text=True, timeout=RUN_TIMEOUT_S).returncode
        except subprocess.TimeoutExpired:
            print("  Node が %d 秒で終わりませんでした" % RUN_TIMEOUT_S)
            verify_rc = 1
        if verify_rc != 0:
            print("  **観測または役のキーが原本と合いませんでした**")
            rc = rc or verify_rc
    else:
        print("  観測または役のキーが書き出されていません")
        rc = rc or 1

    if not args.keep:
        shutil.rmtree(out_dir, ignore_errors=True)
    else:
        print("\n残したファイル:", out_dir)
    return rc


if __name__ == "__main__":
    sys.exit(main())
