#!/usr/bin/env bash
# LVGL 用フォントを生成する（Git Bash で実行）。
# 日本語は firmware/src/app/fonts/ct_font_jp_symbols.txt にある文字だけを含める。
# 画面に新しい日本語を足したら、そのファイルに文字を追加してから再実行すること。
#
# 注意: ct_font_22 / ct_font_30 は HOME 画面（と共有 UI 部品）が既に参照しており、
# ここに文字を足すと常時リンクされるフォントデータが膨らみ、xtensa の call8 相対
# 分岐の到達範囲を超えてリンクエラーになることを確認済み。そのためゲーム画面用の
# 大きな文字集合（ct_font_jp_game_symbols.txt）は ct_font_jp_<size> という別名の
# フォントに分離し、HOME 側の ct_font_22 / ct_font_30 の中身は変えない。
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
GAME_JP="$(tr -d '\r\n' < "$OUT/ct_font_jp_game_symbols.txt")"

# 時計の数字
$CONV --size 104 --font Montserrat-Light.ttf -r 0x20,0x2D,0x30-0x3A \
    -o "$OUT/ct_font_time_104.c" --lv-font-name ct_font_time_104

# 人狼ゲームの大きいタイマー表示（数字のみ、時計の数字と同じ字形）
$CONV --size 64 --font Montserrat-Light.ttf -r 0x20,0x2D,0x30-0x3A \
    -o "$OUT/ct_font_time_64.c" --lv-font-name ct_font_time_64

# 英数字は Montserrat、日本語は Zen Maru Gothic（HOME 画面用: 小さい文字集合）
for sz in 22 30; do
    $CONV --size $sz --font Montserrat-Medium.ttf -r 0x20-0x7E,0xB0 \
        --font ZenMaruGothic-Medium.ttf --symbols "$JP" \
        -o "$OUT/ct_font_$sz.c" --lv-font-name ct_font_$sz
done

# ゲーム画面用（人狼など）: 文字数が多いので ct_font_jp_<size> として分離。
# 20: 補助文字/タイトル, 22: 本文/ボタン, 40: 役職名表示
for sz in 20 22 40; do
    $CONV --size $sz --font Montserrat-Medium.ttf -r 0x20-0x7E,0xB0 \
        --font ZenMaruGothic-Medium.ttf --symbols "$GAME_JP" \
        -o "$OUT/ct_font_jp_$sz.c" --lv-font-name ct_font_jp_$sz
done
# HOME の天気マーク（Weather Icons by Erik Flowers, SIL OFL 1.1）。使う 11 個だけを収録する。
# 晴れ F00D / 晴れ時々くもり F002 / くもり F013 / 霧 F014 / 霧雨 F01C / 雨 F019 / 雪 F01B / にわか雨 F01A / 雷雨 F01E
# 夜の晴れ F02E / 夜の晴れ時々くもり F086（対応は HomeScreen.cpp の weatherIcon()）
dl weathericons.ttf https://github.com/erikflowers/weather-icons/raw/master/font/weathericons-regular-webfont.ttf
$CONV --size 44 --font weathericons.ttf \
    -r 0xF00D,0xF002,0xF013,0xF014,0xF01C,0xF019,0xF01B,0xF01A,0xF01E,0xF02E,0xF086 \
    -o "$OUT/ct_font_weather_44.c" --lv-font-name ct_font_weather_44

echo "fonts written to $OUT"
