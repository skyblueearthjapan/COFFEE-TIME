"""ソースコードとゲームデータから、画面に出る日本語の文字を集めてフォント用の一覧を更新する。

使い方: python tools/collect_ui_chars.py [--check]
  --check を付けると更新せず、不足している文字があれば終了コード 1 で知らせる（CI 用）。

フォントは 2 系統に分かれている（大きな 1 つにまとめるとリンクエラーになるため）:
  - HOME 画面用   : HomeScreen.cpp の文字 -> ct_font_jp_symbols.txt   (ct_font_22 / ct_font_30)
  - メニュー・ゲーム用: ui/ と games/ の文字 -> ct_font_jp_game_symbols.txt (ct_font_jp_20 / 22 / 40)
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "firmware" / "src" / "app"
HOME_SYMBOLS = SRC / "fonts" / "ct_font_jp_symbols.txt"
GAME_SYMBOLS = SRC / "fonts" / "ct_font_jp_game_symbols.txt"

# "..." の中身（エスケープ対応）。コメント内の引用符付き日本語も拾う点に注意
STRING_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')

ap = argparse.ArgumentParser()
ap.add_argument("--check", action="store_true")
args = ap.parse_args()

def collect(paths) -> set:
    found = set()
    for path in paths:
        if path.suffix not in (".cpp", ".h", ".hpp") or not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for literal in STRING_RE.findall(text):
            for ch in literal:
                if ord(ch) > 0x7E:
                    found.add(ch)
    return found


def merge(symbols_file: pathlib.Path, chars: set) -> list:
    existing = set(symbols_file.read_text(encoding="utf-8")) if symbols_file.exists() else set()
    existing.discard(chr(10))
    existing.discard(chr(13))
    missing = sorted(chars - existing)
    if not args.check:
        merged = "".join(sorted(existing | chars)) + chr(10)
        symbols_file.write_text(merged, encoding="utf-8", newline=chr(10))
        print("%s: 追加 %d / 合計 %d 文字" % (symbols_file.name, len(missing), len(existing | chars)))
    return missing


home_chars = collect([SRC / "HomeScreen.cpp"])
game_chars = collect(list((SRC / "ui").rglob("*")) + list((SRC / "games").rglob("*")))

missing = merge(HOME_SYMBOLS, home_chars) + merge(GAME_SYMBOLS, game_chars)

if args.check:
    if missing:
        print("フォント一覧に無い文字:", "".join(missing))
        sys.exit(1)
    print("不足文字なし")
    sys.exit(0)

if missing:
    print("不足していた文字:", "".join(missing))
    print("この後 bash tools/gen_fonts.sh を実行してフォントを作り直すこと")
else:
    print("不足文字なし。フォントの再生成は不要")
