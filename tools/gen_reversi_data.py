"""JEV REVERSI の文言（JSON）を ESP32 ファーム用の C++ ソースへ変換する。

使い方:
  python tools/gen_reversi_data.py

入力（設計一式 COFFEE_TIME_JEV_REVERSI_Handoff_v1.0.zip の検証済みデータ）:
  firmware/src/app/games/reversi/data/content.ja.json        文言 105 件（**書き換えない**）
  firmware/src/app/games/reversi/data/tutorial.ja.json       遊び方 8 ページ（**書き換えない**）
  firmware/src/app/games/reversi/data/content.local.ja.json  追加分と、枠に収めるための改行。
      同じ鍵は正本を上書きする（findText がこちらを先に見る）

出力（手で編集しない）:
  firmware/src/app/games/reversi/ReversiContent.h
  firmware/src/app/games/reversi/ReversiContent.cpp

遊び方の本文だけは、丸い画面の枠（312px・ct_font_jp_22）に合わせてここで折り返す。
全角 1 文字 = 2 単位・半角 1 文字 = 1 単位で数え、1 行 26 単位まで。
行頭に置けない文字（、。）」など）と、カタカナ語・英数字の途中では折り返さない。

このスクリプトは何度実行しても同じ出力になる（冪等）。
"""
import hashlib
import json
import pathlib
import unicodedata

ROOT = pathlib.Path(__file__).resolve().parent.parent
GAME_DIR = ROOT / "firmware" / "src" / "app" / "games" / "reversi"
DATA = GAME_DIR / "data"
CONTENT = DATA / "content.ja.json"
TUTORIAL = DATA / "tutorial.ja.json"
LOCAL = DATA / "content.local.ja.json"
HEADER = GAME_DIR / "ReversiContent.h"
SOURCE = GAME_DIR / "ReversiContent.cpp"

# 1 行の幅（全角 12 文字）。禁則の「ぶら下げ」で 1 文字はみ出しても
# 全角 13 文字 = 286px に収まり、本文の枠 312px を超えない
BODY_UNITS = 24
BODY_LINES = 7         # 1 ページの最大行数

# 行頭に置かない文字（禁則）
NO_LINE_START = "、。，．・：；？！）」』】〉》〕｝”’ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮーヵヶ%％"
# 行末に置かない文字
NO_LINE_END = "（「『【〈《〔｛“‘"


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
            # 16 進エスケープは後ろの文字まで飲み込む（C++ の maximal munch）。
            # 桁数の決まっている 8 進 3 桁を使う
            out.append("\\%03o" % ord(ch))
        else:
            out.append(ch)
    return "".join(out)


def lit(s: str) -> str:
    return '"' + esc(s) + '"'


def units(ch: str) -> int:
    return 2 if unicodedata.east_asian_width(ch) in ("W", "F", "A") else 1


def is_word_char(ch: str) -> bool:
    """途中で切りたくない文字（カタカナ語・英数字・記号つきの語）。"""
    if ch.isascii():
        return ch.isalnum() or ch in "+-.%/×"
    return unicodedata.name(ch, "").startswith("KATAKANA") or ch == "ー"


def wrap(text: str, width: int = BODY_UNITS) -> str:
    """全角 1 文字 = 2 単位で数えて折り返す（禁則・語の途中で切らない）。"""
    lines = []
    for paragraph in text.split("\n"):
        line = ""
        used = 0
        for i, ch in enumerate(paragraph):
            w = units(ch)
            if used + w > width and line:
                # 行頭に置けない文字は前の行にぶら下げる
                if ch in NO_LINE_START:
                    line += ch
                    lines.append(line)
                    line, used = "", 0
                    continue
                # 語（カタカナ・英数字）の途中なら、語の先頭まで戻して次の行へ送る
                back = 0
                if is_word_char(ch):
                    while back < len(line) and is_word_char(line[-1 - back]):
                        back += 1
                    if back >= len(line) or back * 2 >= width:
                        back = 0       # 1 行まるごと 1 語なら、あきらめて切る
                moved = line[len(line) - back:] if back else ""
                lines.append(line[:len(line) - back] if back else line)
                line = moved + ch
                used = sum(units(c) for c in line)
                continue
            # 行末に置けない文字が行のおしりに来たら、次の行へ送る
            if used + w == width and ch in NO_LINE_END and line:
                lines.append(line)
                line, used = ch, w
                continue
            line += ch
            used += w
        lines.append(line)
    return "\n".join(lines)


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_if_changed(path: pathlib.Path, text: str) -> bool:
    if path.exists() and path.read_text(encoding="utf-8", newline="") == text:
        return False
    path.write_text(text, encoding="utf-8", newline="\n")
    return True


def check_strings(name: str, table: dict) -> dict:
    """文言の表は「鍵も値も文字列」でなければならない。黙って通さない。"""
    for key, value in table.items():
        if not isinstance(key, str) or not isinstance(value, str):
            raise SystemExit("%s: 鍵と値は文字列でなければなりません: %r -> %r"
                             % (name, key, value))
    return table


def main() -> int:
    content = check_strings(CONTENT.name, json.loads(CONTENT.read_text(encoding="utf-8")))
    tutorial = json.loads(TUTORIAL.read_text(encoding="utf-8"))
    local = check_strings(LOCAL.name,
                          {k: v for k, v in json.loads(LOCAL.read_text(encoding="utf-8")).items()
                           if not k.startswith("_")})

    if not isinstance(tutorial, list):
        raise SystemExit("%s: 配列でなければなりません" % TUTORIAL.name)
    pages = []
    for page in tutorial:
        if (not isinstance(page, dict) or not isinstance(page.get("title"), str)
                or not isinstance(page.get("body"), str)):
            raise SystemExit("%s: title と body の文字列が要ります: %r" % (TUTORIAL.name, page))
        body = wrap(page["body"])
        if body.count("\n") + 1 > BODY_LINES:
            raise SystemExit("遊び方の本文が %d 行を超えました: %s" % (BODY_LINES, page["title"]))
        pages.append((page["title"], body))

    head = []
    a = head.append
    a("// AUTO-GENERATED FILE — DO NOT EDIT BY HAND.")
    a("// Generated by tools/gen_reversi_data.py from:")
    a("//   data/content.ja.json       SHA-256 %s" % sha256(CONTENT))
    a("//   data/tutorial.ja.json      SHA-256 %s" % sha256(TUTORIAL))
    a("//   data/content.local.ja.json SHA-256 %s" % sha256(LOCAL))
    a("// Re-run: python tools/gen_reversi_data.py")
    a("//")
    a("// 設計一式（COFFEE_TIME_JEV_REVERSI_Handoff_v1.0.zip）の文言をそのまま持つ。")
    a("// 枠に収まらない文言と、設計一式に無い画面の文言は content.local.ja.json 側にあり、")
    a("// 同じ鍵なら local が勝つ（findText が local を先に見る）。")
    a("//")
    a("// 遊び方の本文は生成時に折り返し済み（1 行 全角 %d 文字・1 ページ 最大 %d 行）。"
      % (BODY_UNITS // 2, BODY_LINES))
    a("// LVGL 側は LV_LABEL_LONG_CLIP で幅・高さを指定してそのまま貼るだけでよい。")
    a("#pragma once")
    a("")
    a("#include <cstddef>")
    a("")
    a("namespace coffee { namespace rev { namespace content {")
    a("")
    a("// 鍵と文言の組（線形検索。件数が少ないので十分速い）")
    a("struct TextEntry { const char *key; const char *value; };")
    a("extern const TextEntry kTexts[];          // data/content.ja.json（設計一式）")
    a("extern const size_t kTextCount;")
    a("extern const TextEntry kLocalTexts[];     // data/content.local.ja.json（追加・上書き）")
    a("extern const size_t kLocalTextCount;")
    a("")
    a("// 見つからなければ鍵をそのまま返す（画面で気付けるよう nullptr にしない）")
    a("const char *findText(const char *key);")
    a("")
    a("// {coord} と {count} を差し替える（設計一式の board.selected / board.preview 用）。")
    a("// 置き換えるものが無ければ元の文言をそのまま写す")
    a("void formatText(const char *key, const char *coord, unsigned count, char *out, size_t size);")
    a("")
    a("// 遊び方（設計一式 tutorial.ja.json）。本文は折り返し済み")
    a("struct TutorialPage { const char *title; const char *body; };")
    a("extern const TutorialPage kTutorial[];")
    a("constexpr size_t kTutorialCount = %d;" % len(pages))
    a("")
    a("constexpr const char *kRulesVersion = \"1.0.0\";")
    a("constexpr int kBodyCellsPerLine = %d;    // 生成時の折り返し幅（全角の文字数）" % (BODY_UNITS // 2))
    a("")
    a("}}} // namespace coffee::rev::content")
    a("")

    src = []
    b = src.append
    b("// AUTO-GENERATED FILE — DO NOT EDIT BY HAND.")
    b("// Generated by tools/gen_reversi_data.py — Re-run: python tools/gen_reversi_data.py")
    b('#include "ReversiContent.h"')
    b("")
    b("#include <cstdio>")
    b("#include <cstring>")
    b("")
    b("namespace coffee { namespace rev { namespace content {")
    b("")
    b("const TextEntry kTexts[] = {")
    for key in content:
        b("    {%s, %s}," % (lit(key), lit(content[key])))
    b("};")
    b("const size_t kTextCount = %d;" % len(content))
    b("")
    b("const TextEntry kLocalTexts[] = {")
    for key in local:
        b("    {%s, %s}," % (lit(key), lit(local[key])))
    b("};")
    b("const size_t kLocalTextCount = %d;" % len(local))
    b("")
    b("const TutorialPage kTutorial[] = {")
    for title, body in pages:
        b("    {%s," % lit(title))
        b("     %s}," % lit(body))
    b("};")
    b("")
    b("const char *findText(const char *key)")
    b("{")
    b("    if (key == nullptr) {")
    b("        return \"\";")
    b("    }")
    b("    for (size_t i = 0; i < kLocalTextCount; ++i) {")
    b("        if (std::strcmp(kLocalTexts[i].key, key) == 0) {")
    b("            return kLocalTexts[i].value;")
    b("        }")
    b("    }")
    b("    for (size_t i = 0; i < kTextCount; ++i) {")
    b("        if (std::strcmp(kTexts[i].key, key) == 0) {")
    b("            return kTexts[i].value;")
    b("        }")
    b("    }")
    b("    return key;")
    b("}")
    b("")
    b("void formatText(const char *key, const char *coord, unsigned count, char *out, size_t size)")
    b("{")
    b("    if (out == nullptr || size == 0) {")
    b("        return;")
    b("    }")
    b("    char number[12];")
    b("    std::snprintf(number, sizeof(number), \"%u\", count);")
    b("    const char *src = findText(key);")
    b("    size_t at = 0;")
    b("    while (*src != '\\0' && at + 1 < size) {")
    b("        const char *fill = nullptr;")
    b("        size_t skip = 0;")
    b("        if (std::strncmp(src, \"{coord}\", 7) == 0) {")
    b("            fill = coord != nullptr ? coord : \"\";")
    b("            skip = 7;")
    b("        } else if (std::strncmp(src, \"{count}\", 7) == 0) {")
    b("            fill = number;")
    b("            skip = 7;")
    b("        }")
    b("        if (fill == nullptr) {")
    b("            out[at++] = *src++;")
    b("            continue;")
    b("        }")
    b("        for (const char *p = fill; *p != '\\0' && at + 1 < size; ++p) {")
    b("            out[at++] = *p;")
    b("        }")
    b("        src += skip;")
    b("    }")
    b("    out[at] = '\\0';")
    b("}")
    b("")
    b("}}} // namespace coffee::rev::content")
    b("")

    header_changed = write_if_changed(HEADER, "\n".join(head))
    source_changed = write_if_changed(SOURCE, "\n".join(src))
    print("文言 %d 件（うち追加・上書き %d 件）・遊び方 %d ページ"
          % (len(content), len(local), len(pages)))
    print("  %s %s" % (HEADER.name, "更新" if header_changed else "変更なし"))
    print("  %s %s" % (SOURCE.name, "更新" if source_changed else "変更なし"))
    for title, body in pages:
        widest = max(sum(units(c) for c in line) for line in body.split("\n"))
        print("  遊び方: %-14s %d 行・最長 %d 単位" % (title, body.count("\n") + 1, widest))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
