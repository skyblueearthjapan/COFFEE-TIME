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

# 人狼ゲームの大きいタイマー表示と、設定画面の大きな数値（数字と % のみ、時計の数字と同じ字形）
$CONV --size 64 --font Montserrat-Light.ttf -r 0x20,0x25,0x2D,0x30-0x3A \
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

# マーク（Material Icons Round by Google, Apache License 2.0）。符号位置は同じフォルダーの
# MaterialIconsRound-Regular.codepoints で確認済み（2026-09-21）。
# 日本語フォント（ct_font_jp_*）には絶対に混ぜない（リンクエラーの原因になる）。
#
# 大きさごとに収録する文字を変えている（88 は人狼だけが使うので増やさない＝ファームが太らない）:
#   88: 人狼の手渡し画面の大きなマークだけ（17 個・下の ICONS_WOLF）
#   36: 人狼の席のマーク＋メニュー／設定の行のマーク（35 個）
#   54: メニューのます目 4 つと、明るさ画面の大きなマークだけ（5 個）
#
#   人狼・席のキャラクター（公開情報 / data/content.local.ja.json の characters）:
#     EFEF coffee(カップ)  EA53 bakery_dining(パン) F00C flatware(スプーン)
#     EFF0 coffee_maker(ポット) EAAC cookie(クッキー) E838 star(ほし)
#     EA35 eco(はっぱ)     E7F4 notifications(ベル)  EA19 menu_book(ほん)
#     E51C dark_mode(つき)
#   人狼・役職（秘密。表示の出し入れは WerewolfGame.cpp が制御する）:
#     E91D pets(人狼)      E8F4 visibility(占い師)   E88A home(村人)
#   人狼・世界観のお話（story）:
#     EF5E nightlight_round  F233 groups  E666 auto_stories  E541 local_cafe
#   メニューのます目（ui/MainMenu.cpp・ui/MenuIcons.h）:
#     E541 local_cafe(今日の状況) E26B bar_chart(履歴) E8B8 settings(設定)
#     EA28 sports_esports(ゲーム)
#   設定の行・システム情報・時刻合わせ（ui/MenuIcons.h）:
#     E1AE brightness_medium  E1AD brightness_low  E1AC brightness_high
#     EF44 bedtime  E8B5 schedule  E050 volume_up  E430 wb_sunny  E63E wifi
#     E88E info  E5CC chevron_right  E5C7 arrow_drop_up  E5C5 arrow_drop_down
#     E1A4 battery_full  E623 sd_card  F053 restart_alt
dl MaterialIconsRound-Regular.otf \
    https://github.com/google/material-design-icons/raw/master/font/MaterialIconsRound-Regular.otf
ICONS_WOLF="0xE51C,0xE541,0xE666,0xE7F4,0xE838,0xE88A,0xE8F4,0xE91D,0xEA19,0xEA35,0xEA53,0xEAAC,0xEF5E,0xEFEF,0xEFF0,0xF00C,0xF233"
ICONS_MENU="0xE050,0xE1A4,0xE1AC,0xE1AD,0xE1AE,0xE26B,0xE430,0xE5C5,0xE5C7,0xE5CC,0xE623,0xE63E,0xE88E,0xE8B5,0xE8B8,0xEA28,0xEF44,0xF053"
ICONS_TILE="0xE1AE,0xE26B,0xE541,0xE8B8,0xEA28"
# lv_font_conv が OTF を読めない場合は同じフォルダーの MaterialIcons-Regular.ttf に差し替える
ICON_FONT=MaterialIconsRound-Regular.otf
$CONV --size 36 --font "$ICON_FONT" -r "$ICONS_WOLF,$ICONS_MENU" \
    -o "$OUT/ct_font_icons_36.c" --lv-font-name ct_font_icons_36
$CONV --size 54 --font "$ICON_FONT" -r "$ICONS_TILE" \
    -o "$OUT/ct_font_icons_54.c" --lv-font-name ct_font_icons_54
$CONV --size 88 --font "$ICON_FONT" -r "$ICONS_WOLF" \
    -o "$OUT/ct_font_icons_88.c" --lv-font-name ct_font_icons_88

echo "fonts written to $OUT"
