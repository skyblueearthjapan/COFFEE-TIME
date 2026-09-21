"""ビルド時に Git の版を `CT_GIT_REV` としてファームへ埋め込む（PlatformIO の pre スクリプト）。

`firmware/platformio.ini` の `extra_scripts = pre:../tools/git_rev.py` から呼ばれる。
「システム情報」画面の「ソフトの版」に出る文字列を作る。

- `git rev-parse --short HEAD` の結果を使い、作業ツリーに変更があれば末尾に `+` を付ける
- **git が無い・リポジトリでない・失敗した場合でもビルドは止めない**（"unknown" にする）
- シェルを経由せず git を直接呼ぶ（この PC では Git Bash / msys の sh が壊れているため）
"""
import os
import subprocess

Import("env")   # noqa: F821  （PlatformIO が注入する）


def git(args, cwd):
    # shell=False。見つからない・失敗したときは None を返すだけにする
    try:
        out = subprocess.run(["git"] + args, cwd=cwd, capture_output=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    if out.returncode != 0:
        return None
    return out.stdout.decode("utf-8", "replace").strip()


def revision():
    # このスクリプトは PlatformIO に exec されるので __file__ が無い。
    # プロジェクト（firmware/）の 1 つ上がリポジトリの根
    root = os.path.dirname(os.path.abspath(env.subst("$PROJECT_DIR")))   # noqa: F821
    short = git(["rev-parse", "--short", "HEAD"], root)
    if not short:
        return "unknown"
    dirty = git(["status", "--porcelain", "--untracked-files=no"], root)
    return short + "+" if dirty else short


rev = revision()
print("CT_GIT_REV = %s" % rev)
env.Append(CPPDEFINES=[("CT_GIT_REV", env.StringifyMacro(rev))])   # noqa: F821
