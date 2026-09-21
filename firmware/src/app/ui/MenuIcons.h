#pragma once

/**
 * メニューの中身（今日の状況・履歴・設定）で使うマーク。
 *
 * Material Icons Round（Google, Apache License 2.0）の 1 文字。符号位置は
 * MaterialIconsRound-Regular.codepoints で確認済み（2026-09-21）。
 * 収録する文字は tools/gen_fonts.sh の ICONS_36 / ICONS_54 に必ず足すこと。
 * 日本語フォント（ct_font_jp_*）には絶対に混ぜない（リンクエラーの原因になる）。
 */
namespace ui {
namespace icon {

// ct_font_icons_54（メニューのます目・明るさ画面の大きなマーク）
constexpr const char *kLocalCafe = "\xEE\x95\x81";       // U+E541 local_cafe
constexpr const char *kBarChart = "\xEE\x89\xAB";        // U+E26B bar_chart
constexpr const char *kSettings = "\xEE\xA2\xB8";        // U+E8B8 settings
constexpr const char *kGames = "\xEE\xA8\xA8";           // U+EA28 sports_esports

// ct_font_icons_36（設定の行・システム情報・時刻合わせ）
constexpr const char *kBrightnessMid = "\xEE\x86\xAE";   // U+E1AE brightness_medium
constexpr const char *kBrightnessLow = "\xEE\x86\xAD";   // U+E1AD brightness_low
constexpr const char *kBrightnessHigh = "\xEE\x86\xAC";  // U+E1AC brightness_high
constexpr const char *kBedtime = "\xEE\xBD\x84";         // U+EF44 bedtime
constexpr const char *kSchedule = "\xEE\xA2\xB5";        // U+E8B5 schedule
constexpr const char *kVolumeUp = "\xEE\x81\x90";        // U+E050 volume_up
constexpr const char *kSunny = "\xEE\x90\xB0";           // U+E430 wb_sunny
constexpr const char *kWifi = "\xEE\x98\xBE";            // U+E63E wifi
constexpr const char *kInfo = "\xEE\xA2\x8E";            // U+E88E info
constexpr const char *kChevronRight = "\xEE\x97\x8C";    // U+E5CC chevron_right
constexpr const char *kArrowUp = "\xEE\x97\x87";         // U+E5C7 arrow_drop_up
constexpr const char *kArrowDown = "\xEE\x97\x85";       // U+E5C5 arrow_drop_down
constexpr const char *kBattery = "\xEE\x86\xA4";         // U+E1A4 battery_full
constexpr const char *kSdCard = "\xEE\x98\xA3";          // U+E623 sd_card
constexpr const char *kRestart = "\xEF\x81\x93";         // U+F053 restart_alt

}  // namespace icon
}  // namespace ui
