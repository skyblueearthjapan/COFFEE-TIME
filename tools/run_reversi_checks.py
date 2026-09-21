"""JEV REVERSI のロジック試験を、この PC（実機なし）で組み立てて実行する。

使い方（PowerShell から）:
    python tools/run_reversi_checks.py
    python tools/run_reversi_checks.py --keep       # 組み立てた実行ファイルを消さない

コンパイラーは pip で入れた zig を使う（Visual Studio も MinGW も要らない）:
    python -m pip install --user ziglang
期待値づくりと棋譜の検証には Node.js を使う（gas/ReversiShared.gs をそのまま読む）。
どちらも無ければこのスクリプトが入れ方を案内して止まる。

この試験は 4 段階:
  0. firmware/src/app/games/reversi/core/reversi_core.hpp と reversi_session.hpp が
     設計一式の zip の中身と **1 バイトも違わない**ことを SHA-256 で確かめる
  1. Node が gas/ReversiShared.gs（GAS と共通の JS）で「端末 AI の手」の期待値を
     6×6 / 8×8 の多数の局面ぶん作る
  2. zig で tools/reversi_checks.cpp を組み立てて実行する
     （設計一式の golden_positions.json 41 件・保存形式・端末 AI の一致・多数の対局）
  3. その C++ が書き出した棋譜を Node の CTReversi.replay に通し、局面が一致するか見る

試験の中身は tools/reversi_checks.cpp を参照。
"""
import argparse
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
GAME_DIR = ROOT / "firmware" / "src" / "app" / "games" / "reversi"
SOURCE = ROOT / "tools" / "reversi_checks.cpp"
CORE = GAME_DIR / "core" / "reversi_core.hpp"
SESSION = GAME_DIR / "core" / "reversi_session.hpp"
EXTRA = GAME_DIR / "core" / "reversi_extra.hpp"
SHARED_JS = ROOT / "gas" / "ReversiShared.gs"
ZIP = ROOT / "参考データ" / "ゲーム4部作" / "COFFEE_TIME_JEV_REVERSI_Handoff_v1.0.zip"

# zip の中の「正本」と、ファームに置いた写し
UNMODIFIED = ((CORE, "src/reversi_core.hpp"), (SESSION, "src/reversi_session.hpp"))

LOCAL_CASES_PER_SIZE = 900        # 端末 AI の手を JS と突き合わせる局面の数（サイズごと）
RUN_TIMEOUT_S = 300               # コンパイル・実行・Node の上限（固まったら落とす）


def sha256(data: bytes) -> str:
    import hashlib
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


def zip_member(name_suffix: str) -> bytes:
    with zipfile.ZipFile(ZIP) as z:
        name = next((n for n in z.namelist() if n.endswith(name_suffix)), None)
        if name is None:
            raise SystemExit("zip の中に %s がありません" % name_suffix)
        return z.read(name)


def check_unmodified() -> bool:
    """設計一式のコアを書き換えていないことを確かめる（計画 §2 の「守るもの」）。"""
    ok = True
    for path, member in UNMODIFIED:
        ours = sha256(path.read_bytes())
        theirs = sha256(zip_member(member))
        mark = "一致" if ours == theirs else "**違う**"
        print("  %-22s %s  %s" % (path.name, ours[:16], mark))
        if ours != theirs:
            print("     設計一式: %s" % theirs[:16])
            ok = False
    return ok


# ---------------------------------------------------------------------------
# Node 側（gas/ReversiShared.gs をそのまま読む）
# ---------------------------------------------------------------------------
NODE_SCRIPT = r"""
'use strict';
const fs = require('fs');
const path = require('path');
const CT = require(path.join(__dirname, 'ReversiShared.js'));

// 再現できるでたらめ（mulberry32）。試験の中だけで使う
function rng(seed) {
  let a = seed >>> 0;
  return function () {
    a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// 「端末 AI の手」の期待値を集める。局面は 6×6 / 8×8 の実戦から拾う
function generate(limitPerSize) {
  const out = [];
  for (const n of [6, 8]) {
    let kept = 0;
    for (let seed = 1; seed <= 6000 && kept < limitPerSize; seed++) {
      const r = rng(seed * 7919 + n);
      let p = CT.initial(n);
      for (let guard = 0; guard < 128; guard++) {
        if (CT.terminal(p)) break;
        const l = CT.legal(p);
        if (kept < limitPerSize && r() < 0.30) {
          out.push({ n: n, cells: p.cells, side: p.side, ply: p.ply, local: CT.localMove(p) });
          kept++;
        }
        if (!l.length) { p = CT.step(p, -1); continue; }
        // 「端末 AI 同士」と「でたらめ相手」を混ぜて、偏った局面だけにならないようにする
        const move = (seed % 2 === 0 || r() < 0.5) ? l[Math.floor(r() * l.length)] : CT.localMove(p);
        p = CT.step(p, move);
      }
    }
  }
  return out;
}

// C++ が書き出した棋譜を replay に通す（設計一式の検証器が受け取る形かどうか）
function verify(file) {
  const rows = JSON.parse(fs.readFileSync(file, 'utf8'));
  let bad = 0;
  const modes = {};
  for (const row of rows) {
    modes[row.snapshot.mode] = (modes[row.snapshot.mode] || 0) + 1;
    let p;
    try {
      p = CT.replay(row.snapshot);
    } catch (err) {
      if (bad < 10) console.log('  replay に断られました: ' + err.message);
      bad++;
      continue;
    }
    if (p.ply !== row.ply || p.side !== row.side || p.cells !== row.cells) {
      if (bad < 10) console.log('  局面が違います (ply ' + row.ply + ')');
      bad++;
      continue;
    }
    try {
      CT.identity(row.snapshot);
    } catch (err) {
      if (bad < 10) console.log('  identity に断られました: ' + err.message);
      bad++;
    }
  }
  console.log('  棋譜 ' + rows.length + ' 件を CTReversi.replay に通しました（不合格 ' + bad + ' 件）');
  console.log('  モードの内訳: ' + JSON.stringify(modes));
  // 4 つのモードが 1 つでも欠けていたら、その経路を試していないことになる
  const missing = ['jev', 'jev_pro', 'casual', 'local'].filter(function (m) { return !modes[m]; });
  if (missing.length) {
    console.log('  **試していないモード: ' + missing.join(', ') + '**');
    return false;
  }
  return bad === 0 && rows.length > 0;
}

const mode = process.argv[2];
if (mode === 'gen') {
  const limit = Number(process.argv[4]);
  fs.writeFileSync(process.argv[3], JSON.stringify(generate(limit)), 'utf8');
  process.exit(0);
} else if (mode === 'verify') {
  process.exit(verify(process.argv[3]) ? 0 : 1);
}
console.log('usage: node rev_js.js gen <out.json> <limit> | verify <snapshots.json>');
process.exit(2);
"""


def write_node_tools(out_dir: pathlib.Path) -> pathlib.Path:
    # .gs のままでは require できないので、同じ中身を .js として置く（書き換えない）
    (out_dir / "ReversiShared.js").write_bytes(SHARED_JS.read_bytes())
    script = out_dir / "rev_js.js"
    script.write_text(NODE_SCRIPT, encoding="utf-8", newline="\n")
    return script


# ---------------------------------------------------------------------------
# C++ から読める形に落とす
# ---------------------------------------------------------------------------
def u8_array(name: str, values) -> str:
    body = ",".join(str(int(v)) for v in values) if values else "0"
    return "static const uint8_t %s[] = {%s};" % (name, body)


def write_golden_inc(path: pathlib.Path, local_cases) -> tuple:
    golden = json.loads(zip_member("data/golden_positions.json").decode("utf-8"))

    out = []
    a = out.append
    a("// AUTO-GENERATED by tools/run_reversi_checks.py — 触らないこと")
    a("// 元データ: 設計一式 zip の data/golden_positions.json")
    a("//           gas/ReversiShared.gs（Node）が作った端末 AI の手の期待値")
    a("")
    a("struct GoldenCase {")
    a("    uint8_t n; const char *cells; char side; uint16_t ply; int move;")
    a("    const uint8_t *legal; uint8_t legal_count;")
    a("    const uint8_t *flips; uint8_t flip_count;")
    a("    uint8_t next_n; const char *next_cells; char next_side; uint16_t next_ply;")
    a("    const char *error; bool terminal; char winner;")
    a("};")
    a("")
    for i, case in enumerate(golden):
        a(u8_array("kGoldenLegal%d" % i, case["expected"]["legal"]))
        a(u8_array("kGoldenFlips%d" % i, case["expected"]["flips"]))
    a("")
    a("static const GoldenCase kGolden[] = {")
    for i, case in enumerate(golden):
        src, exp, nxt = case["input"], case["expected"], case["expected"]["next"]
        a('    {%d, "%s", \'%s\', %d, %d, kGoldenLegal%d, %d, kGoldenFlips%d, %d,'
          % (src["n"], src["cells"], src["side"], src["ply"], src["move"],
             i, len(exp["legal"]), i, len(exp["flips"])))
        a('     %d, "%s", \'%s\', %d, "%s", %s, \'%s\'},'
          % (nxt["n"], nxt["cells"], nxt["side"], nxt["ply"],
             exp["error"] or "", "true" if exp["terminal"] else "false", exp["winner"]))
    a("};")
    a("static const int kGoldenCount = %d;" % len(golden))
    a("")
    a("struct LocalCase { uint8_t n; const char *cells; char side; uint16_t ply; int expected; };")
    a("static const LocalCase kLocal[] = {")
    for c in local_cases:
        a('    {%d, "%s", \'%s\', %d, %d},' % (c["n"], c["cells"], c["side"], c["ply"], c["local"]))
    a("};")
    a("static const int kLocalCount = %d;" % len(local_cases))
    a("")
    path.write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
    return len(golden), len(local_cases)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--keep", action="store_true", help="組み立てた実行ファイルを消さない")
    args = ap.parse_args()

    for path in (SOURCE, CORE, SESSION, EXTRA, SHARED_JS, ZIP):
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
        print("Node.js が見つかりません（gas/ReversiShared.gs を動かすのに必要です）")
        return 2
    print("zig %s / Node %s で試験します\n" % (version, node))

    print("[0] 設計一式のコアを書き換えていないか（SHA-256）")
    if not check_unmodified():
        print("  **設計一式のヘッダーと違います。無改変で使うきまりです（計画 §2）**")
        return 1
    print()

    out_dir = pathlib.Path(tempfile.mkdtemp(prefix="reversi_checks_"))
    script = write_node_tools(out_dir)

    print("[1] gas/ReversiShared.gs で端末 AI の手の期待値を作る")
    expect_json = out_dir / "local_expect.json"
    try:
        gen = subprocess.run([shutil.which("node"), str(script), "gen", str(expect_json),
                              str(LOCAL_CASES_PER_SIZE)], capture_output=True, text=True,
                             timeout=RUN_TIMEOUT_S)
    except subprocess.TimeoutExpired:
        print("  Node が %d 秒で終わりませんでした" % RUN_TIMEOUT_S)
        return 1
    if gen.returncode != 0:
        print(gen.stdout, gen.stderr)
        print("  Node での期待値づくりに失敗しました")
        return gen.returncode
    local_cases = json.loads(expect_json.read_text(encoding="utf-8"))
    print("  %d 局面（6×6 %d / 8×8 %d）"
          % (len(local_cases), sum(1 for c in local_cases if c["n"] == 6),
             sum(1 for c in local_cases if c["n"] == 8)))

    inc = out_dir / "reversi_golden.inc"
    n_golden, n_local = write_golden_inc(inc, local_cases)
    print("  golden_positions.json %d 件と合わせて %s に書き出しました\n" % (n_golden, inc.name))

    print("[2] zig で組み立てて実行する")
    exe = out_dir / ("checks.exe" if os.name == "nt" else "checks")
    cmd = [sys.executable, "-m", "ziglang", "c++", "-std=c++17", "-O2",
           "-Wall", "-Wextra", "-Wno-unused-parameter",
           "-I", str(out_dir), str(SOURCE), "-o", str(exe)]
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

    snapshots = out_dir / "snapshots.json"
    t1 = time.time()
    # 試験そのものが固まったら（対局が終わらない等）落とす。黙って待ち続けない
    try:
        run_rc = subprocess.run([str(exe), str(snapshots)], text=True,
                                timeout=RUN_TIMEOUT_S).returncode
    except subprocess.TimeoutExpired:
        print("\n  **試験が %d 秒で終わりませんでした**" % RUN_TIMEOUT_S)
        run_rc = 1
    print("\n実行 %.1f 秒\n" % (time.time() - t1))
    rc = run_rc

    print("[3] 書き出した棋譜を Node の CTReversi.replay に通す")
    if snapshots.is_file():
        try:
            verify_rc = subprocess.run([shutil.which("node"), str(script), "verify",
                                        str(snapshots)], text=True,
                                       timeout=RUN_TIMEOUT_S).returncode
        except subprocess.TimeoutExpired:
            print("  Node が %d 秒で終わりませんでした" % RUN_TIMEOUT_S)
            verify_rc = 1
        if verify_rc != 0:
            print("  **棋譜が設計一式の検証器に通りませんでした**")
            rc = rc or verify_rc
    else:
        print("  棋譜が書き出されていません")
        rc = rc or 1

    if not args.keep:
        shutil.rmtree(out_dir, ignore_errors=True)
    else:
        print("\n残したファイル:", out_dir)
    return rc


if __name__ == "__main__":
    sys.exit(main())
