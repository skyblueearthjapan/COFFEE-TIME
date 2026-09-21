"""人狼ゲームの文言・定数・画面配置データ（JSON）を ESP32 ファーム用の C++ ソースへ変換する。

使い方:
  python tools/gen_game_data.py
  python tools/gen_game_data.py --content <content.ja.json> --rules <rules.json> \
      --layout <layout.json> --out-dir <出力先>

入力（既定値は人狼設計パッケージの検証済みデータを指す）:
  --content : data/content.ja.json （schema_version 2.0.0 / 文言・チュートリアル等）
  --rules   : data/rules.json      （schema_version 2.0.0 / タイミング・定数）
  --layout  : data/layout.json     （schema_version 2.0.0 / 部品の絶対座標）
  --local   : data/content.local.ja.json（設計書に無い追加分。無ければ空として扱う）
              席のキャラクター・世界観のお話・追加文言。strings は同じ鍵で正本を上書きする
              （findString がこちらを先に見る）。正本の 3 ファイルは改変しない。

出力:
  <out-dir>/WerewolfContent.h
  <out-dir>/WerewolfContent.cpp

生成されるデータ:
  - content.strings          -> coffee::wolf::content::kStrings[] (key/value のペア, 線形検索)
  - content.tutorial         -> coffee::wolf::content::kTutorial[]
  - content.intro_variants   -> coffee::wolf::content::kIntroVariants[]
  - content.ending_variants  -> coffee::wolf::content::kEndingVariants[]
  - content.mandatory_brief  -> coffee::wolf::content::kMandatoryBrief[]
  - local.strings            -> coffee::wolf::content::kLocalStrings[]（findString が優先）
  - local.characters         -> coffee::wolf::content::kSeatCharacters[]
  - local.story              -> coffee::wolf::content::kStory[]
  - local.role_icons         -> coffee::wolf::content::kIconWolf / kIconSeer / kIconVillager
  - rules.timing_by_players  -> coffee::wolf::rules::kTimingByPlayers[]
  - rules の各種 ms/秒/ページ定数 -> coffee::wolf::rules 名前空間の constexpr
  - layout.rects             -> coffee::wolf::layout::kXxx （Rect{x,y,w,h} の constexpr）
  - layout.font_sizes ほか    -> coffee::wolf::layout 名前空間の constexpr

文字列は const char* の配列としてそのままバイナリに埋め込まれる。ESP32 では
flash にマップされた領域がそのまま読めるため、AVR 向けの PROGMEM マクロは不要
（`static`/`constexpr` な配列・文字列リテラルは自然に .rodata = flash に置かれる）。

このスクリプトは何度実行しても同じ出力になる（冪等）。
"""
import argparse
import json
import pathlib
import os

GAME_DATA = (pathlib.Path(__file__).resolve().parent.parent / "firmware" / "src" / "app" / "games" / "werewolf" / "data")
DEFAULT_CONTENT = str(GAME_DATA / "content.ja.json")
DEFAULT_RULES = str(GAME_DATA / "rules.json")
DEFAULT_LAYOUT = str(GAME_DATA / "layout.json")
DEFAULT_LOCAL = str(GAME_DATA / "content.local.ja.json")
DEFAULT_OUT_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "firmware", "src", "app", "games", "werewolf",
)

HEADER_NAME = "WerewolfContent.h"
SOURCE_NAME = "WerewolfContent.cpp"


def esc(s: str) -> str:
    """C++ 文字列リテラル用にエスケープする（UTF-8 の日本語はそのまま出力する）。"""
    out = []
    for ch in s:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        elif ord(ch) < 0x20:
            out.append("\\x%02x" % ord(ch))
        else:
            out.append(ch)
    return "".join(out)


def lit(s: str) -> str:
    return '"' + esc(s) + '"'


def icon_lit(code_hex) -> str:
    """アイコンの符号位置（16進の文字列 or 整数）を C++ の \\x エスケープ列にする。

    アイコンは Material Icons Round の私用領域にあり、日本語フォントには入っていない。
    そのまま UTF-8 で書くと tools/collect_ui_chars.py が日本語の一覧へ拾ってしまうので、
    必ずエスケープ（= ASCII だけ）で出力する。全バイトがエスケープなので、C++ の
    16進エスケープが後続文字を飲み込む問題も起きない。
    """
    cp = int(str(code_hex), 16)
    return '"' + "".join("\\x%02X" % b for b in chr(cp).encode("utf-8")) + '"'


def load_json(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def camel(name: str) -> str:
    """layout.json の snake_case な部品名を C++ の定数名（kCamelCase）へ変換する。"""
    return "k" + "".join(part[:1].upper() + part[1:] for part in name.split("_"))


def gen_layout(layout: dict) -> list:
    """layout.json の絶対座標を coffee::wolf::layout の constexpr として書き出す。"""
    lines = []
    a = lines.append
    a("namespace coffee { namespace wolf { namespace layout {")
    a("")
    a("// data/layout.json の絶対座標（原点は画面左上）。丸型 480x480 の内側に収まるよう検証済み。")
    a("struct Rect { int16_t x, y, w, h; };")
    a("")
    a(f"constexpr int16_t kScreenWidth = {int(layout['width'])};")
    a(f"constexpr int16_t kScreenHeight = {int(layout['height'])};")
    center = layout["safe_center"]
    a(f"constexpr int16_t kSafeCenterX = {int(center[0])};")
    a(f"constexpr int16_t kSafeCenterY = {int(center[1])};")
    a(f"constexpr int16_t kSafeRadius = {int(layout['safe_radius'])};")
    a("")
    a("// 部品ごとの字の大きさ（px）。ファーム側のフォント名との対応はコード側で決める。")
    for key, value in layout["font_sizes"].items():
        a(f"constexpr int16_t kFontSize{camel(key)[1:]} = {int(value)};")
    a("")
    limit = layout["body_limit"]
    a(f"constexpr int16_t kBodyCellsPerLine = {int(limit['fullwidth_cells_per_line'])};")
    a(f"constexpr int16_t kBodyLinesPerPage = {int(limit['lines_per_page'])};")
    a(f"constexpr uint8_t kSeatPageSize = {int(layout['seat_page_size'])};")
    a("")
    for name, rect in layout["rects"].items():
        a(f"constexpr Rect {camel(name)}{{{rect[0]}, {rect[1]}, {rect[2]}, {rect[3]}}};")
    a("")
    a("// 席のページ送り（4 枠）と結果画面の行（4 行）は添字で回すことが多いのでまとめておく。")
    a("constexpr Rect kSlots[] = {kSlot0, kSlot1, kSlot2, kSlot3};")
    a("constexpr Rect kRows[] = {kRow0, kRow1, kRow2, kRow3};")
    a("")
    a("}}} // namespace coffee::wolf::layout")
    a("")
    return lines


def gen_header(content: dict, rules: dict, layout: dict, local: dict) -> str:
    lines = []
    a = lines.append
    a("// AUTO-GENERATED FILE — DO NOT EDIT BY HAND.")
    a("// Generated by tools/gen_game_data.py from:")
    a(f"//   content: content.ja.json (schema_version {content.get('schema_version')})")
    a(f"//   rules:   rules.json (schema_version {rules.get('schema_version')})")
    a(f"//   layout:  layout.json (schema_version {layout.get('schema_version')})")
    a(f"//   local:   content.local.ja.json (schema_version {local.get('schema_version')})")
    a("// Re-run: python tools/gen_game_data.py")
    a("#pragma once")
    a("")
    a("#include <cstddef>")
    a("#include <cstdint>")
    a("")
    a("namespace coffee { namespace wolf { namespace content {")
    a("")
    a("struct StringEntry { const char *key; const char *value; };")
    a("extern const StringEntry kStrings[];")
    a("extern const size_t kStringCount;")
    a("// content.local.ja.json の重ね書き。同じ鍵があればこちらが勝つ。")
    a("extern const StringEntry kLocalStrings[];")
    a("extern const size_t kLocalStringCount;")
    a("// 線形検索。kLocalStrings を先に見る。見つからなければ nullptr を返す。")
    a("const char *findString(const char *key);")
    a("")
    a("// id/title/body の3項目を持つページ（tutorial, mandatory_brief で共用）。")
    a("struct PagedEntry { const char *id; const char *title; const char *body; };")
    a("")
    a("extern const PagedEntry kTutorial[];")
    a("extern const size_t kTutorialCount;")
    a("")
    a("extern const char *const kIntroVariants[];")
    a("extern const size_t kIntroVariantCount;")
    a("")
    a("struct EndingVariantGroup { const char *outcome; const char *const *lines; size_t count; };")
    a("extern const EndingVariantGroup kEndingVariants[];")
    a("extern const size_t kEndingVariantGroupCount;")
    a("// 見つからなければ nullptr を返す。")
    a("const EndingVariantGroup *findEndingVariants(const char *outcome);")
    a("")
    a("extern const PagedEntry kMandatoryBrief[];")
    a("extern const size_t kMandatoryBriefCount;")
    a("")
    a("// --- content.local.ja.json（設計書に無い追加分）----------------------------")
    a("")
    a("// 席のキャラクター（公開情報）。icon は Material Icons Round の 1 文字（UTF-8）。")
    a("// 文字は日本語フォントには無く、ct_font_icons_36 / ct_font_icons_88 でのみ描ける。")
    a("struct SeatCharacter { const char *name; const char *icon; };")
    a("extern const SeatCharacter kSeatCharacters[];")
    a("extern const size_t kSeatCharacterCount;")
    a("// seat は 0 起点。範囲外なら nullptr。")
    a("const SeatCharacter *findSeatCharacter(int seat);")
    a("")
    a("// 世界観のお話（遊び方の先頭ページにも使う）。icon はアイコンフォントの 1 文字。")
    a("struct StoryPage { const char *id; const char *title; const char *body; const char *icon; };")
    a("extern const StoryPage kStory[];")
    a("extern const size_t kStoryCount;")
    a("")
    a("// 役職のマーク（秘密の画面でのみ使う。表示の制御はファーム側の責任）。")
    a("extern const char *const kIconWolf;")
    a("extern const char *const kIconSeer;")
    a("extern const char *const kIconVillager;")
    a("")
    a("}}} // namespace coffee::wolf::content")
    a("")
    a("namespace coffee { namespace wolf { namespace rules {")
    a("")
    a(f"constexpr const char *kSchemaVersion = {lit(rules.get('schema_version', ''))};")
    a(f"constexpr const char *kGameId = {lit(rules.get('game_id', ''))};")
    a(f"constexpr uint8_t kDefaultPlayers = {int(rules['default_players'])};")
    a("")
    a("// 人数ごとの議論時間・決選時間・配札関連の定数。")
    a("struct TimingByPlayers {")
    a("    uint8_t players;")
    a("    uint16_t discussion_s;")
    a("    uint16_t runoff_s;")
    a("    uint16_t pool_size;")
    a("    uint16_t deal_count;")
    a("    uint8_t wolf_absent_numerator;")
    a("    uint8_t wolf_absent_denominator;")
    a("};")
    a("extern const TimingByPlayers kTimingByPlayers[];")
    a("extern const size_t kTimingByPlayersCount;")
    a("// 見つからなければ nullptr を返す。")
    a("const TimingByPlayers *findTiming(uint8_t players);")
    a("")
    dov = rules["discussion_override"]
    a(f"constexpr uint16_t kDiscussionAutoS = {int(dov['auto'])};")
    a(f"constexpr uint16_t kDiscussionMinS = {int(dov['min'])};")
    a(f"constexpr uint16_t kDiscussionMaxS = {int(dov['max'])};")
    a(f"constexpr uint16_t kDiscussionStepS = {int(dov['step'])};")
    a(f"constexpr uint16_t kDiscussionExtensionS = {int(rules['discussion_extension_s'])};")
    a(f"constexpr uint8_t kMaxExtensions = {int(rules['max_extensions'])};")
    a(f"constexpr uint8_t kMaxRunoffs = {int(rules['max_runoffs'])};")
    a("")
    a(f"constexpr uint32_t kHoldBeforeRevealMs = {int(rules['hold_before_reveal_ms'])};")
    a(f"constexpr uint32_t kMaxVisibleMs = {int(rules['max_visible_ms'])};")
    a(f"constexpr uint32_t kTouchStaleMs = {int(rules['touch_stale_ms'])};")
    a(f"constexpr uint32_t kUiTickMs = {int(rules['ui_tick_ms'])};")
    a(f"constexpr uint32_t kPrivateIdlePauseMs = {int(rules['private_idle_pause_ms'])};")
    a(f"constexpr uint32_t kResultIdleCloseMs = {int(rules['result_idle_close_ms'])};")
    a(f"constexpr uint64_t kAbsoluteGameLimitMs = {int(rules['absolute_game_limit_ms'])}ULL;")
    a(f"constexpr uint32_t kPrivacyHideNormalMaxMs = {int(rules['privacy_hide_normal_max_ms'])};")
    a(f"constexpr uint32_t kPrivacyHideStaleMaxMs = {int(rules['privacy_hide_stale_max_ms'])};")
    a("")
    a(f"constexpr uint8_t kPageSize = {int(rules['page_size'])};")
    a(f"constexpr uint8_t kMandatoryBriefPages = {int(rules['mandatory_brief_pages'])};")
    a("")
    a(f"constexpr const char *kNvsNamespace = {lit(rules['nvs_namespace'])};")
    a(f"constexpr const char *kNvsKey = {lit(rules['nvs_key'])};")
    a(f"constexpr uint8_t kNvsRecordBytes = {int(rules['nvs_record_bytes'])};")
    a(f"constexpr const char *kNvsMagic = {lit(rules['nvs_magic'])};")
    a("")
    a("struct SoundSpec { uint16_t hz; uint16_t ms; };")
    tap = rules["sounds"]["public_tap"]
    boundary = rules["sounds"]["public_boundary"]
    a(f"constexpr SoundSpec kSoundPublicTap = {{{int(tap['hz'])}, {int(tap['ms'])}}};")
    a(f"constexpr SoundSpec kSoundPublicBoundary = {{{int(boundary['hz'])}, {int(boundary['ms'])}}};")
    a("")
    a("}}} // namespace coffee::wolf::rules")
    a("")
    lines.extend(gen_layout(layout))
    return "\n".join(lines)


def gen_source(content: dict, local: dict) -> str:
    lines = []
    a = lines.append
    a(f'#include "{HEADER_NAME}"')
    a("")
    a("#include <cstring>")
    a("")
    a("namespace coffee { namespace wolf { namespace content {")
    a("")
    a("const StringEntry kStrings[] = {")
    for key, value in content["strings"].items():
        a(f"    {{{lit(key)}, {lit(value)}}},")
    a("};")
    a("const size_t kStringCount = sizeof(kStrings) / sizeof(kStrings[0]);")
    a("")
    a("const StringEntry kLocalStrings[] = {")
    local_strings = local.get("strings", {})
    if not local_strings:
        # 要素 0 の配列は C++ では書けないので、引かれることのない空の鍵を 1 つ置く
        a('    {"", ""},')
    for key, value in local_strings.items():
        a(f"    {{{lit(key)}, {lit(value)}}},")
    a("};")
    a("const size_t kLocalStringCount = sizeof(kLocalStrings) / sizeof(kLocalStrings[0]);")
    a("")
    a("const char *findString(const char *key) {")
    a("    // 重ね書き（content.local.ja.json）を先に見る。設計書の正本は無改変のまま。")
    a("    for (size_t i = 0; i < kLocalStringCount; ++i) {")
    a("        if (std::strcmp(kLocalStrings[i].key, key) == 0) return kLocalStrings[i].value;")
    a("    }")
    a("    for (size_t i = 0; i < kStringCount; ++i) {")
    a("        if (std::strcmp(kStrings[i].key, key) == 0) return kStrings[i].value;")
    a("    }")
    a("    return nullptr;")
    a("}")
    a("")
    a("const PagedEntry kTutorial[] = {")
    for entry in content["tutorial"]:
        a(f"    {{{lit(entry['id'])}, {lit(entry['title'])}, {lit(entry['body'])}}},")
    a("};")
    a("const size_t kTutorialCount = sizeof(kTutorial) / sizeof(kTutorial[0]);")
    a("")
    a("const char *const kIntroVariants[] = {")
    for text in content["intro_variants"]:
        a(f"    {lit(text)},")
    a("};")
    a("const size_t kIntroVariantCount = sizeof(kIntroVariants) / sizeof(kIntroVariants[0]);")
    a("")
    ending = content["ending_variants"]
    for outcome, texts in ending.items():
        a(f"static const char *const kEndingLines_{outcome}[] = {{")
        for text in texts:
            a(f"    {lit(text)},")
        a("};")
    a("const EndingVariantGroup kEndingVariants[] = {")
    for outcome, texts in ending.items():
        a(
            f"    {{{lit(outcome)}, kEndingLines_{outcome}, "
            f"sizeof(kEndingLines_{outcome}) / sizeof(kEndingLines_{outcome}[0])}},"
        )
    a("};")
    a(
        "const size_t kEndingVariantGroupCount = "
        "sizeof(kEndingVariants) / sizeof(kEndingVariants[0]);"
    )
    a("")
    a("const EndingVariantGroup *findEndingVariants(const char *outcome) {")
    a("    for (size_t i = 0; i < kEndingVariantGroupCount; ++i) {")
    a("        if (std::strcmp(kEndingVariants[i].outcome, outcome) == 0) return &kEndingVariants[i];")
    a("    }")
    a("    return nullptr;")
    a("}")
    a("")
    a("const PagedEntry kMandatoryBrief[] = {")
    for entry in content["mandatory_brief"]:
        a(f"    {{{lit(entry['id'])}, {lit(entry['title'])}, {lit(entry['body'])}}},")
    a("};")
    a(
        "const size_t kMandatoryBriefCount = "
        "sizeof(kMandatoryBrief) / sizeof(kMandatoryBrief[0]);"
    )
    a("")
    a("const SeatCharacter kSeatCharacters[] = {")
    characters = local.get("characters", [])
    if not characters:
        a('    {"", ""},')
    for entry in characters:
        a(f"    {{{lit(entry['name'])}, {icon_lit(entry['icon'])}}},   // "
          f"{entry.get('seat', '?')} {entry.get('icon_name', '')}")
    a("};")
    a(
        "const size_t kSeatCharacterCount = "
        "sizeof(kSeatCharacters) / sizeof(kSeatCharacters[0]);"
    )
    a("")
    a("const SeatCharacter *findSeatCharacter(int seat) {")
    a("    if (seat < 0 || (size_t)seat >= kSeatCharacterCount) return nullptr;")
    a("    return &kSeatCharacters[seat];")
    a("}")
    a("")
    a("const StoryPage kStory[] = {")
    story = local.get("story", [])
    if not story:
        a('    {"", "", "", ""},')
    for entry in story:
        a(
            f"    {{{lit(entry['id'])}, {lit(entry['title'])}, {lit(entry['body'])}, "
            f"{icon_lit(entry['icon'])}}},   // {entry.get('icon_name', '')}"
        )
    a("};")
    a("const size_t kStoryCount = sizeof(kStory) / sizeof(kStory[0]);")
    a("")
    role_icons = local.get("role_icons", {})
    for cpp_name, json_key in (("kIconWolf", "wolf"), ("kIconSeer", "seer"),
                               ("kIconVillager", "villager")):
        spec = role_icons.get(json_key)
        value = icon_lit(spec["icon"]) if spec else '""'
        comment = f"   // {spec['icon_name']}" if spec and spec.get("icon_name") else ""
        a(f"const char *const {cpp_name} = {value};{comment}")
    a("")
    a("}}} // namespace coffee::wolf::content")
    a("")
    a("namespace coffee { namespace wolf { namespace rules {")
    a("")
    return "\n".join(lines)


def gen_source_rules_tail(rules: dict) -> str:
    lines = []
    a = lines.append
    a("const TimingByPlayers kTimingByPlayers[] = {")
    for t in rules["timing_by_players"]:
        a(
            "    {%d, %d, %d, %d, %d, %d, %d},"
            % (
                t["players"],
                t["discussion_s"],
                t["runoff_s"],
                t["pool_size"],
                t["deal_count"],
                t["wolf_absent_numerator"],
                t["wolf_absent_denominator"],
            )
        )
    a("};")
    a(
        "const size_t kTimingByPlayersCount = "
        "sizeof(kTimingByPlayers) / sizeof(kTimingByPlayers[0]);"
    )
    a("")
    a("const TimingByPlayers *findTiming(uint8_t players) {")
    a("    for (size_t i = 0; i < kTimingByPlayersCount; ++i) {")
    a("        if (kTimingByPlayers[i].players == players) return &kTimingByPlayers[i];")
    a("    }")
    a("    return nullptr;")
    a("}")
    a("")
    a("}}} // namespace coffee::wolf::rules")
    a("")
    return "\n".join(lines)


def write_if_changed(path: str, text: str) -> bool:
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8", newline="") as f:
            if f.read() == text:
                return False
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--content", default=DEFAULT_CONTENT)
    ap.add_argument("--rules", default=DEFAULT_RULES)
    ap.add_argument("--layout", default=DEFAULT_LAYOUT)
    ap.add_argument("--local", default=DEFAULT_LOCAL)
    ap.add_argument("--out-dir", default=DEFAULT_OUT_DIR)
    args = ap.parse_args()

    content = load_json(args.content)
    rules = load_json(args.rules)
    layout = load_json(args.layout)
    local = load_json(args.local) if os.path.exists(args.local) else {}

    os.makedirs(args.out_dir, exist_ok=True)

    header_text = gen_header(content, rules, layout, local)
    source_text = gen_source(content, local) + gen_source_rules_tail(rules)

    header_path = os.path.join(args.out_dir, HEADER_NAME)
    source_path = os.path.join(args.out_dir, SOURCE_NAME)

    header_changed = write_if_changed(header_path, header_text)
    source_changed = write_if_changed(source_path, source_text)

    string_count = len(content["strings"])
    string_bytes = sum(len(k.encode("utf-8")) + len(v.encode("utf-8")) for k, v in content["strings"].items())
    tutorial_count = len(content["tutorial"])
    intro_count = len(content["intro_variants"])
    ending_count = sum(len(v) for v in content["ending_variants"].values())
    brief_count = len(content["mandatory_brief"])
    total_strings = string_count * 1 + tutorial_count * 2 + intro_count + ending_count + brief_count * 2

    print(f"wrote {header_path} ({'changed' if header_changed else 'unchanged'})")
    print(f"wrote {source_path} ({'changed' if source_changed else 'unchanged'})")
    print(f"strings: {string_count} entries, {string_bytes} bytes (key+value, UTF-8)")
    print(f"tutorial: {tutorial_count} entries")
    print(f"intro_variants: {intro_count} entries")
    print(f"ending_variants: {ending_count} lines across {len(content['ending_variants'])} outcomes")
    print(f"mandatory_brief: {brief_count} entries")
    print(f"local strings (overrides+new): {len(local.get('strings', {}))} entries")
    print(f"seat characters: {len(local.get('characters', []))} entries")
    print(f"story pages: {len(local.get('story', []))} entries")
    print(f"layout rects: {len(layout['rects'])} entries")
    print(f"total C string literals emitted (approx): {total_strings}")
    print(f"source file size: {os.path.getsize(source_path)} bytes")


if __name__ == "__main__":
    main()
