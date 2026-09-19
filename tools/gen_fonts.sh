#!/usr/bin/env bash
# LVGL 用フォントを生成する（Git Bash で実行）。
# 日本語は firmware/src/app/fonts/ct_font_jp_symbols.txt にある文字だけを含める。
# 画面に新しい日本語を足したら、そのファイルに文字を追加してから再実行すること。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/firmware/src/app/fonts"
WORK="${TMPDIR:-/tmp}/coffee_time_fonts"
mkdir -p "$WORK"
cd "$WORK"

dl() { [ -f "$1" ] || curl -sSL -o "$1" "$2"; }
dl Montserrat-Light.ttf  https://github.com/JulietaUla/Montserrat/raw/master/fonts/ttf/Montserrat-Light.ttf
dl Montserrat-Medium.ttf https://github.com/JulietaUla/Montserrat/raw/master/fonts/ttf/Montserrat-Medium.ttf
dl ZenMaruGothic-Medium.ttf https://github.com/google/fonts/raw/main/ofl/zenmarugothic/ZenMaruGothic-Medium.ttf

CONV="npx -y lv_font_conv@1.5.3 --bpp 4 --format lvgl --no-compress"
JP="$(tr -d '\r\n' < "$OUT/ct_font_jp_symbols.txt")"

# 時計の数字
$CONV --size 104 --font Montserrat-Light.ttf -r 0x20,0x2D,0x30-0x3A \
    -o "$OUT/ct_font_time_104.c" --lv-font-name ct_font_time_104

# 英数字は Montserrat、日本語は Zen Maru Gothic
for sz in 22 30; do
    $CONV --size $sz --font Montserrat-Medium.ttf -r 0x20-0x7E,0xB0 \
        --font ZenMaruGothic-Medium.ttf --symbols "$JP" \
        -o "$OUT/ct_font_$sz.c" --lv-font-name ct_font_$sz
done
echo "fonts written to $OUT"
