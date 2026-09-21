"""人狼「通常ルール」コアの試験を、この PC（実機なし）で組み立てて実行する。

使い方（PowerShell から）:
    python tools/run_wolf_std_checks.py
    python tools/run_wolf_std_checks.py --keep      # 実行ファイルを消さない

コンパイラーは pip で入れた zig を使う（Visual Studio も MinGW も要らない）:
    python -m pip install --user ziglang
無ければこのスクリプトが入れ方を案内して止まる。

試験の中身は tools/wolf_std_checks.cpp を参照。対象は
firmware/src/app/games/werewolf/core_std/werewolf_std_core.hpp だけで、
Arduino も LVGL も使っていないのでそのまま PC で動く。
"""
import argparse
import os
import pathlib
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = ROOT / "tools" / "wolf_std_checks.cpp"
CORE = ROOT / "firmware" / "src" / "app" / "games" / "werewolf" / "core_std" / "werewolf_std_core.hpp"


def zig_version() -> str:
    try:
        out = subprocess.run([sys.executable, "-m", "ziglang", "version"],
                             capture_output=True, text=True, timeout=120)
    except Exception:
        return ""
    return out.stdout.strip() if out.returncode == 0 else ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--keep", action="store_true", help="組み立てた実行ファイルを消さない")
    args = ap.parse_args()

    if not SOURCE.is_file() or not CORE.is_file():
        print("試験の元ファイルが見つかりません:", SOURCE, CORE)
        return 2

    version = zig_version()
    if not version:
        print("zig が見つかりません。次のコマンドで入れてください:")
        print("    python -m pip install --user ziglang")
        return 2
    print(f"zig {version} でコンパイルします")

    out_dir = pathlib.Path(tempfile.mkdtemp(prefix="wolf_std_checks_"))
    exe = out_dir / ("checks.exe" if os.name == "nt" else "checks")
    cmd = [sys.executable, "-m", "ziglang", "c++", "-std=c++17", "-O2",
           "-Wall", "-Wextra", "-Wno-unused-parameter",
           str(SOURCE), "-o", str(exe)]
    t0 = time.time()
    build = subprocess.run(cmd, capture_output=True, text=True)
    if build.stdout.strip():
        print(build.stdout)
    if build.stderr.strip():
        print(build.stderr)
    if build.returncode != 0:
        print("コンパイルに失敗しました")
        return build.returncode
    print(f"コンパイル {time.time() - t0:.1f} 秒\n")

    t1 = time.time()
    run = subprocess.run([str(exe)], text=True)
    elapsed = time.time() - t1
    print(f"\n実行 {elapsed:.1f} 秒")

    if not args.keep:
        try:
            exe.unlink()
            out_dir.rmdir()
        except OSError:
            pass
    return run.returncode


if __name__ == "__main__":
    sys.exit(main())
