"""エスパー対決（AI ESPER）のカタログ JSON を ESP32 ファーム用の C++ ソースへ変換する。

使い方:
  python tools/gen_esper_data.py
  python tools/gen_esper_data.py --catalog <catalog.json> --out-dir <出力先>

入力（既定値はリポジトリに取り込んだ設計書付属の正本データを指す）:
  --catalog : firmware/src/app/games/esper/data/catalog.json
              （設計書 付録A の機械可読カタログ。**書き換えないこと**。
                候補 128・質問 146・18,688 セルの対応・モード 3 種が入っている）

出力:
  <out-dir>/EsperContent.h
  <out-dir>/EsperContent.cpp

■ 何を焼き込むか（設計書 6.3「オフライン方式」）
  端末の中だけで候補を絞り込めるように、候補の表示名・説明、質問文・補足、
  そして「どの候補が Yes か」の 128bit マスクを全部ファームへ入れる。
  英語の `name_en` / `meaning_en` は Jev（段階 2）にサーバー側から渡すものなので
  端末には入れない（設計書 9.1 の「英語のモデル説明は端末では省略可能」）。

■ 候補ビットの並び（設計書 6.3・絶対に変えない）
  catalog.items の配列順そのまま。D01 が bit0、T32 が bit127。
  16 バイト化するときは各バイトの下位 bit から。ここでは uint64 を 2 本
  （lo = bit0..63、hi = bit64..127）にして出力する。
  分野 scope は D/F/O/T を bit0/1/2/3 に割り当てる。

■ 同点整列のためのキー（設計書 3.4 / 付録B の `eligible`）
  情報量が同じ質問は「表示文の長さ（文字数）昇順 → 質問 ID 昇順」で並べる。
  - 表示文の長さは **折り返す前の text_ja の文字数**（バイト数ではない）
  - 質問 ID 順は catalog.questions の配列順とは一致しない（実際にずれている）ので、
    ID を並べ替えた順位 `sort_rank` を別に出力して C++ 側で整数比較できるようにする

■ 事前の折り返し
  日本語は LVGL が自動折り返ししてくれないので、探偵ゲームと同じ規則で
  生成時に行単位まで整形する（1 行 = 全角 13 文字 = 半角 26 個分・禁則・孤立行の割り直し）。
  字を小さくして押し込むことはしない。

■ 生成時の検査（落ちたら 1 文字も書かずに止まる／設計書 13 章 D01〜D06）
    D01 候補 128 件・ID 重複なし・4 分野ちょうど 32 件ずつ
    D02 質問 146 件・ID 重複なし・空文なし
    D03 yes_ids ⊆ scope、0 < |yes| < |scope|（18,688 セルが Y/N/U に解ける）
    D04 128 候補の回答署名がすべて異なる（分離できない組が 0）
    D05 EASY 8 ⊂ NORMAL 32 ⊂ HARD 128
    D06 質問文 32 文字以下・折り返した各行が幅を超えないこと
  さらにカタログの SHA-256 を設計書 16 章の値と突き合わせる。

このスクリプトは何度実行しても同じ出力になる（冪等）。
"""
import argparse
import hashlib
import itertools
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME_DIR = os.path.join(ROOT, "firmware", "src", "app", "games", "esper")
DATA_DIR = os.path.join(GAME_DIR, "data")

DEFAULT_CATALOG = os.path.join(DATA_DIR, "catalog.json")
DEFAULT_OUT_DIR = GAME_DIR

HEADER_NAME = "EsperContent.h"
SOURCE_NAME = "EsperContent.cpp"

# 設計書 16 章に記載されたカタログ（整形 JSON・改行込み）の SHA-256
EXPECTED_SHA256 = "6c0024e43cf76ae82d711c4ee42867c8b482f043294dff768ac062533e6650f3"

# --- 折り返しの寸法（探偵ゲーム tools/gen_detective_data.py と同じ規則）-------
# 1 全角 = 半角 2 つ分として数える。ct_font_jp_22 の送り幅 11px/22px に合わせた見積り
BODY_CELLS_PER_LINE = 13
BODY_UNITS_PER_LINE = BODY_CELLS_PER_LINE * 2

# 画面ごとの上限行数（EsperGame.cpp の枠と合わせてある）
QUESTION_MAX_LINES = 3      # 質問文（枠 300x96・22px）
DEFINITION_MAX_LINES = 4    # 候補の説明（カードの説明欄。句点で改行するので 34 文字でも 4 行になる）
HELP_MAX_LINES = 5          # 質問の補足（HELP 画面の本文欄）
SUMMARY_MAX_LINES = 3       # モードの説明

# 行頭に来てはいけない文字（追い出しで前の行の末尾へ送る）
NO_LINE_START = (
    "、。，．・：；？！゛゜ー～〜ゝゞ々〻）」』】〕〉》｝］’”"
    "ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ"
    ",.!?:;)]}’”"
)
# 行末に来てはいけない文字（次の行の先頭へ送る）
NO_LINE_END = "（「『【〔〈《｛［‘“([{‘“"

# 途中で折り返してはいけない半角の並び（数値・英字など）
ASCII_RUN_CHARS = set(
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    ":.-/%+"
)

# 意味の切れ目。限界の少し手前にこれがあれば、そこで改行したほうが読みやすい
SENTENCE_BREAK = "。、！？．，!?"
SENTENCE_LOOKBACK_CELLS = 6

# 最終行がこれ以下しか残らなかったら、直前の行と割り直す
ORPHAN_MAX_UNITS = 6

# 分野（設計書 2.1）。short は一覧のタブ、name は見出しで使う
GROUPS = [
    ("D", "飲み物", "飲み物"),
    ("F", "食べ物", "カフェの食べ物"),
    ("O", "用品", "カフェ・デスク用品"),
    ("T", "道具", "工場の道具"),
]
GROUP_INDEX = {g[0]: i for i, g in enumerate(GROUPS)}

# モードの日本語見出し（設計書 1.3 の表・5.3 の G10「候補数・最大問数」）。
# 候補数と問数は catalog から数えて埋めるので、ここで手打ちするのは呼び名だけ
MODE_ORDER = ["easy", "normal", "hard"]
# 画面に出す呼び名。カタログの label_ja（EASY / NORMAL / HARD）と「かんたん／むずかしい」は AI にとっての
# 難しさで、人から見ると逆（候補が少ないほど当てられやすい）という指摘を受けて言い換えた（2026-09-21）。
# カタログの JSON は書き換えず、表示だけをここで差し替える
MODE_LABEL = {"easy": "ミニ", "normal": "レギュラー", "hard": "フル"}
MODE_NAME_JA = {"easy": "AIにやさしい", "normal": "いい勝負", "hard": "AIに手ごわい"}
MODE_TIME_JA = {"easy": "20〜45 秒", "normal": "30〜60 秒", "hard": "45〜90 秒"}

# ---------------------------------------------------------------------------
# 画面の本文（生成時に全角 13 文字で折り返してから C++ へ埋め込む）
#
# 値は (本文, その画面の枠に入る最大行数)。1 行に収まる短いボタンの見出しは
# EsperGame.cpp に直接書く。ここに置くのは「複数行になる案内文」だけ。
#
# **どの画面にも「今なにをすればよいか」を必ず書く**（持ち主からの明示の要望）。
# ready_notice は設計書 1.1 の固定文をそのまま使っている（勝手に言い換えない）。
# ---------------------------------------------------------------------------
UI_TEXTS = {
    # G30 READY。設計書 1.1 の固定文をそのまま使う（言い換えない）。
    # 文の切れ目にだけ改行を入れておき、長い文の中での折り返しは下の規則にまかせる
    "ready_notice": (
        "一覧の中から1つだけ、\n頭の中で決めてください。\n答えは入力しません。\n"
        "説明にある温度や材料も、\nそのまま思い浮かべて\nください。\n"
        "途中で別のものに変えず、\n質問に正直に\n答えてください。", 9),
    "mode_lead": ("AI に当てさせる数を\nえらんでください。", 2),
    "catalog_hint": ("名前を押すと 説明が出ます", 1),
    "reveal_hint": ("決めていたものを えらぶ", 1),
    "card_lead": ("これに決めるなら\n「決めた」を押します。", 2),
    "question_guide": ("頭の中のものに\n合っていますか？", 2),
    "thinking": ("考えています…", 1),
    "thinking_sub": ("すこし お待ちください", 1),
    "guess_lead": ("これで 合っていますか？", 1),
    "guess_low_info": ("情報が足りないので\nいちばん近いものです。", 2),
    "result_ai_win": ("AIの勝ち！\n当てられました。", 2),
    "result_human_win": ("あなたの勝ち！\n今回は当てられませんでした。", 3),
    "reveal_none": ("その答えは 最後まで\n候補に残っていました。\n質問の数が\n足りませんでした。", 4),
    "reveal_outside": ("その答えは 今回の一覧に\n入っていません。\n今回は記録しません。", 4),
    "reveal_intro": ("つぎの答えが\n食い違っていました。", 2),
    "quit_confirm": ("とちゅうで やめます。\nこの回は 記録しません。", 3),
    "inconsistent": ("データが合いません。\nこの回はここで終わります。\n記録はしません。", 4),
    "stats_note": ("この端末だけの記録です。\n自己申告なので\n人の名前は残しません。", 4),
    "cafe_note": ("コーヒーの記録は\nいつでもできます。", 2),
    "paused_note": ("ひと休みしています。\nつづきから 遊べます。", 2),
}


class GenError(Exception):
    """生成時の検査に落ちたときに投げる（出力ファイルは書かない）。"""


# ---------------------------------------------------------------------------
# C++ 文字列
# ---------------------------------------------------------------------------
def esc(s: str) -> str:
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


# ---------------------------------------------------------------------------
# 折り返し（探偵ゲームと同じ実装）
# ---------------------------------------------------------------------------
def units(s: str) -> int:
    return sum(1 if ord(ch) < 0x80 else 2 for ch in s)


def atomize(text: str) -> list:
    atoms = []
    i = 0
    n = len(text)
    while i < n:
        if text[i] in ASCII_RUN_CHARS:
            j = i
            while j < n and text[j] in ASCII_RUN_CHARS:
                j += 1
            atoms.append(text[i:j])
            i = j
        else:
            atoms.append(text[i])
            i += 1
    return atoms


def breakable(atoms: list, k: int) -> bool:
    if k <= 0 or k >= len(atoms):
        return False
    return atoms[k][0] not in NO_LINE_START and atoms[k - 1][-1] not in NO_LINE_END


def rebalance(prev: str, last: str, limit: int):
    atoms = atomize(prev + last)
    best = None
    for k in range(1, len(atoms)):
        if not breakable(atoms, k):
            continue
        first = "".join(atoms[:k])
        second = "".join(atoms[k:])
        if units(first) > limit or units(second) > limit:
            continue
        score = abs(units(first) - units(second))
        if atoms[k - 1] in SENTENCE_BREAK:
            score -= 4
        if best is None or score < best[0]:
            best = (score, [first, second])
    return best[1] if best is not None else None


def wrap_line(text: str, limit: int) -> list:
    atoms = atomize(text)
    out = []
    i = 0
    n = len(atoms)
    while i < n:
        j = i
        used = 0
        while j < n:
            w = units(atoms[j])
            if used + w > limit and j > i:
                break
            used += w
            j += 1
        if j >= n:
            out.append("".join(atoms[i:]))
            break

        k = None
        for p in range(j - 1, max(i, j - SENTENCE_LOOKBACK_CELLS) - 1, -1):
            if atoms[p] in SENTENCE_BREAK and p + 1 > i and breakable(atoms, p + 1):
                k = p + 1
                break
        if k is None:
            k = j
            while k > i + 1 and atoms[k][0] in NO_LINE_START:
                k -= 1
            while k > i + 1 and atoms[k - 1][-1] in NO_LINE_END:
                k -= 1
        out.append("".join(atoms[i:k]))
        i = k

    if len(out) >= 2 and units(out[-1]) <= ORPHAN_MAX_UNITS:
        merged = rebalance(out[-2], out[-1], limit)
        if merged is not None:
            out[-2:] = merged
    out = [out[0].rstrip(" 　")] + [s.strip(" 　") for s in out[1:]]
    return out if out else [""]


def wrap(text: str, limit: int = BODY_UNITS_PER_LINE) -> list:
    lines = []
    for raw in text.split("\n"):
        if raw == "":
            lines.append("")
            continue
        lines.extend(wrap_line(raw, limit))
    return lines


def wrapped(text: str, limit: int = BODY_UNITS_PER_LINE) -> str:
    return "\n".join(wrap(text, limit))


def check_lines(where: str, text: str, max_lines: int, limit: int = BODY_UNITS_PER_LINE):
    lines = text.split("\n")
    if len(lines) > max_lines:
        raise GenError("%s: %d 行あり %d 行に収まらない: %s"
                       % (where, len(lines), max_lines, text.replace("\n", "/")))
    for line in lines:
        if units(line) > limit:
            raise GenError("%s: 行が幅を超えている(%d>%d): %s" % (where, units(line), limit, line))


# ---------------------------------------------------------------------------
# 128bit マスク
# ---------------------------------------------------------------------------
def mask_of(indices) -> tuple:
    lo = hi = 0
    for i in indices:
        if i < 64:
            lo |= 1 << i
        else:
            hi |= 1 << (i - 64)
    return lo, hi


def mask_lit(m: tuple) -> str:
    return "{0x%016Xull, 0x%016Xull}" % m


def popcount(m: tuple) -> int:
    return bin(m[0]).count("1") + bin(m[1]).count("1")


# ---------------------------------------------------------------------------
# 検査（設計書 13 章 D01〜D06）
# ---------------------------------------------------------------------------
def validate(catalog: dict, raw: bytes) -> dict:
    items = catalog["items"]
    questions = catalog["questions"]
    ids = [i["id"] for i in items]
    idset = set(ids)

    # D01 候補
    if len(items) != 128 or len(idset) != 128:
        raise GenError("D01: 候補が 128 件・ID 重複なしでない（%d 件 / ID %d 種）"
                       % (len(items), len(idset)))
    for g, _short, _name in GROUPS:
        n = sum(1 for i in items if i["group"] == g)
        if n != 32:
            raise GenError("D01: 分野 %s が 32 件でない（%d 件）" % (g, n))
    for it in items:
        if it["group"] not in GROUP_INDEX:
            raise GenError("D01: 未知の分野 %s（%s）" % (it["group"], it["id"]))
        if not it["name_ja"] or not it["definition_ja"]:
            raise GenError("D01: %s の表示名か説明が空" % it["id"])
        if it["id"][0] != it["group"]:
            raise GenError("D01: %s の ID 接頭辞と分野が食い違う" % it["id"])
    if ids != sorted(ids):
        # ビットの並びは配列順に固定する決まりなので、ID 昇順であることまで確かめる
        raise GenError("D01: items が ID 昇順に並んでいない（ビット割り当てが設計書 6.3 と食い違う）")

    # D02 質問
    qids = [q["id"] for q in questions]
    if len(questions) != 146 or len(set(qids)) != 146:
        raise GenError("D02: 質問が 146 件・ID 重複なしでない（%d 件 / ID %d 種）"
                       % (len(questions), len(set(qids))))
    for q in questions:
        for key in ("text_ja", "meaning_en", "help_ja"):
            if not q.get(key):
                raise GenError("D02: %s の %s が空" % (q["id"], key))
        if len(q["text_ja"]) > 32:
            raise GenError("D06: %s の質問文が 32 文字を超える（%d 文字）"
                           % (q["id"], len(q["text_ja"])))

    # D03 全対応（scope 外 = U、scope 内で yes_ids に無ければ N）
    index_of = {x: n for n, x in enumerate(ids)}
    scope_sets = {}
    yes_sets = {}
    for q in questions:
        groups = set(q["scope_groups"])
        if not groups or not groups <= set(GROUP_INDEX):
            raise GenError("D03: %s の scope_groups が不正: %s" % (q["id"], q["scope_groups"]))
        scope = {x for x in ids if x[0] in groups}
        yes = set(q["yes_ids"])
        if not yes <= scope:
            raise GenError("D03: %s の yes_ids が適用範囲の外を指している" % q["id"])
        if not (0 < len(yes) < len(scope)):
            raise GenError("D03: %s は片側が空で候補を分けられない（yes %d / scope %d）"
                           % (q["id"], len(yes), len(scope)))
        scope_sets[q["id"]] = scope
        yes_sets[q["id"]] = yes

    # D04 対象分離（128 候補の回答署名がすべて異なる）
    signatures = {}
    for x in ids:
        signatures[x] = tuple(
            "U" if x not in scope_sets[q["id"]] else ("Y" if x in yes_sets[q["id"]] else "N")
            for q in questions)
    collisions = [p for p in itertools.combinations(ids, 2)
                  if signatures[p[0]] == signatures[p[1]]]
    if collisions:
        raise GenError("D04: 分離できない候補の組がある: %s" % collisions[:5])

    # D05 モード集合
    modes = catalog["modes"]
    for name in MODE_ORDER:
        if name not in modes:
            raise GenError("D05: モード %s が無い" % name)
    sets = {name: list(modes[name]["item_ids"]) for name in MODE_ORDER}
    for name, want in (("easy", 8), ("normal", 32), ("hard", 128)):
        if len(sets[name]) != want or len(set(sets[name])) != want:
            raise GenError("D05: %s が %d 件でない（%d 件）" % (name, want, len(sets[name])))
        if not set(sets[name]) <= idset:
            raise GenError("D05: %s に未知の候補 ID がある" % name)
    if not (set(sets["easy"]) <= set(sets["normal"]) <= set(sets["hard"])):
        raise GenError("D05: EASY ⊂ NORMAL ⊂ HARD になっていない")
    for name, want in (("easy", 5), ("normal", 7), ("hard", 10)):
        if int(modes[name]["max_questions"]) != want:
            raise GenError("D05: %s の最大問数が %d でない" % (name, want))

    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_SHA256:
        # 正本が差し替わったら止める。新しい版を使うなら EXPECTED_SHA256 を明示的に更新すること
        raise GenError("カタログの SHA-256 が設計書 16 章の値と違う\n  実測 %s\n  期待 %s"
                       % (digest, EXPECTED_SHA256))
    return {"sha256": digest, "index_of": index_of, "scope_sets": scope_sets,
            "yes_sets": yes_sets, "signatures": signatures}


# ---------------------------------------------------------------------------
# 出力
# ---------------------------------------------------------------------------
def gen_header(catalog: dict, info: dict) -> str:
    lines = []
    a = lines.append
    a("// AUTO-GENERATED FILE — DO NOT EDIT BY HAND.")
    a("// Generated by tools/gen_esper_data.py from:")
    a("//   data/catalog.json (schema_version %s / catalog_version %s)"
      % (catalog.get("schema_version"), catalog.get("catalog_version")))
    a("//   SHA-256 %s" % info["sha256"])
    a("// Re-run: python tools/gen_esper_data.py")
    a("//")
    a("// 端末の中だけで候補を絞り込むためのデータ（設計書 6.3 のオフライン方式）。")
    a("// 候補ビットの並びは catalog.items の配列順そのまま＝ D01 が bit0、T32 が bit127。")
    a("// **この並びは保存データにも試験にも効いてくるので絶対に変えない。**")
    a("//")
    a("// 英語の name_en / meaning_en は入れていない。あれは段階 2 で")
    a("// サーバーから Jev へ渡すものなので、端末に持つ必要がない（設計書 9.1）。")
    a("//")
    a("// 本文は生成時に整形済み: 1 行は全角 %d 文字以内。" % BODY_CELLS_PER_LINE)
    a("// LVGL 側は LV_LABEL_LONG_CLIP で幅・高さを指定してそのまま貼るだけでよい。")
    a("#pragma once")
    a("")
    a("#include <cstddef>")
    a("#include <cstdint>")
    a("")
    a("namespace coffee { namespace esp { namespace content {")
    a("")
    a("// 128 候補の集合。lo が bit0..63（D01..F32）、hi が bit64..127（O01..T32）")
    a("struct Mask128 { uint64_t lo; uint64_t hi; };")
    a("")
    a("// 分野（設計書 2.1）。scope_bits の bit0..3 と同じ番号")
    a("enum class Group : uint8_t { Drink = 0, Food = 1, Office = 2, Tool = 3 };")
    a("constexpr uint8_t kGroupCount = %d;" % len(GROUPS))
    a("struct GroupInfo {")
    a("    char letter;          // D / F / O / T")
    a("    const char *tab;      // 一覧のタブに出す短い名前")
    a("    const char *name;     // 見出しに出す名前")
    a("    Mask128 items;        // この分野の候補")
    a("};")
    a("extern const GroupInfo kGroups[kGroupCount];")
    a("")
    a("struct Item {")
    a("    const char *id;          // \"D01\"。画面には出さない（開発用）")
    a("    uint8_t group;           // Group の値")
    a("    const char *name;        // 表示名（1 行に収まる長さ）")
    a("    const char *definition;  // このゲームでの固定の姿（折り返し済み・最大 %d 行）"
      % DEFINITION_MAX_LINES)
    a("};")
    a("extern const Item kItems[];")
    a("constexpr uint16_t kItemCount = %d;" % len(catalog["items"]))
    a("")
    a("struct Question {")
    a("    const char *id;        // \"Q001\"。画面には出さない（開発用）")
    a("    const char *text;      // 質問文（折り返し済み・最大 %d 行）" % QUESTION_MAX_LINES)
    a("    const char *help;      // HELP 画面の補足（折り返し済み・最大 %d 行）" % HELP_MAX_LINES)
    a("    uint16_t text_len;     // 折り返す前の文字数。同点整列の第 2 キー（設計書 3.4）")
    a("    uint16_t sort_rank;    // 質問 ID の昇順での順位。同点整列の第 3 キー")
    a("    uint8_t scope_bits;    // bit0=D bit1=F bit2=O bit3=T")
    a("    Mask128 yes;           // この質問に「はい」となる候補")
    a("    Mask128 scope;         // 適用範囲の候補（この外は U＝適用外）")
    a("};")
    a("extern const Question kQuestions[];")
    a("constexpr uint16_t kQuestionCount = %d;" % len(catalog["questions"]))
    a("")
    a("struct Mode {")
    a("    const char *id;            // \"easy\"")
    a("    const char *label;         // \"EASY\"")
    a("    const char *name_ja;       // \"かんたん\"")
    a("    const char *summary;       // 候補数・問数・目安時間（折り返し済み）")
    a("    Mask128 items;             // 初期候補集合")
    a("    uint16_t item_count;")
    a("    uint8_t max_questions;     // 最終予想の確認は数に入れない")
    a("    uint8_t max_skips;         // 「わからない」の上限")
    a("};")
    a("extern const Mode kModes[];")
    a("constexpr uint8_t kModeCount = %d;" % len(MODE_ORDER))
    a("")
    a("// 画面の本文（折り返し済み）。1 行で済むボタンの見出しは EsperGame.cpp 側に直接ある")
    a("struct TextEntry { const char *key; const char *value; };")
    a("extern const TextEntry kTexts[];")
    a("extern const size_t kTextCount;")
    a("// 見つからなければ鍵をそのまま返す（画面で気付けるようにするため nullptr にしない）")
    a("const char *findText(const char *key);")
    a("")
    a("constexpr const char *kCatalogVersion = %s;" % lit(catalog.get("catalog_version", "")))
    a("constexpr const char *kCatalogSha256 = %s;" % lit(info["sha256"]))
    a("constexpr const char *kRulesVersion = \"1.0.0\";")
    a("")
    a("// 「1 つ戻る」の上限（設計書 1.4）。skip の上限はモードごとに持つ")
    a("constexpr uint8_t kMaxUndo = 2;")
    a("")
    a("// 無操作でひと休みに入るまで（探偵ゲームと同じ 180 秒。設計書 1.4 の 120 秒より")
    a("// 長いのは、この端末では状態を捨てずに重ねるだけで、勝敗を付けずに戻れるため）")
    a("constexpr uint32_t kIdlePauseMs = 180000;")
    a("")
    a("// 生成時の折り返し幅（EsperGame.cpp の枠と合わせてある）")
    a("constexpr int16_t kBodyCellsPerLine = %d;" % BODY_CELLS_PER_LINE)
    a("")
    a("}}} // namespace coffee::esp::content")
    a("")
    return "\n".join(lines)


def gen_source(catalog: dict, info: dict) -> tuple:
    items = catalog["items"]
    questions = catalog["questions"]
    index_of = info["index_of"]
    ids = [i["id"] for i in items]

    sorted_qids = sorted(q["id"] for q in questions)
    rank_of = {x: n for n, x in enumerate(sorted_qids)}

    lines = []
    a = lines.append
    a('#include "%s"' % HEADER_NAME)
    a("")
    a("namespace coffee { namespace esp { namespace content {")
    a("")

    # --- 分野 ---------------------------------------------------------------
    a("const GroupInfo kGroups[kGroupCount] = {")
    for letter, tab, name in GROUPS:
        m = mask_of(index_of[x] for x in ids if x[0] == letter)
        a("    {'%s', %s, %s, %s}," % (letter, lit(tab), lit(name), mask_lit(m)))
    a("};")
    a("")

    # --- 候補 ---------------------------------------------------------------
    stats = {"def_lines": 0}
    a("const Item kItems[kItemCount] = {")
    for it in items:
        definition = wrapped(it["definition_ja"])
        check_lines("%s/definition" % it["id"], definition, DEFINITION_MAX_LINES)
        # 表示名は 1 行に収まること（枠に入りきらない名前をここで見つける）
        check_lines("%s/name" % it["id"], it["name_ja"], 1)
        stats["def_lines"] = max(stats["def_lines"], len(definition.split("\n")))
        a("    {%s, %d, %s, %s},"
          % (lit(it["id"]), GROUP_INDEX[it["group"]], lit(it["name_ja"]), lit(definition)))
    a("};")
    a("")

    # --- 質問 ---------------------------------------------------------------
    stats["q_lines"] = 0
    stats["help_lines"] = 0
    a("const Question kQuestions[kQuestionCount] = {")
    for q in questions:
        text = wrapped(q["text_ja"])
        help_text = wrapped(q["help_ja"])
        check_lines("%s/text" % q["id"], text, QUESTION_MAX_LINES)
        check_lines("%s/help" % q["id"], help_text, HELP_MAX_LINES)
        stats["q_lines"] = max(stats["q_lines"], len(text.split("\n")))
        stats["help_lines"] = max(stats["help_lines"], len(help_text.split("\n")))
        scope_bits = 0
        for g in q["scope_groups"]:
            scope_bits |= 1 << GROUP_INDEX[g]
        yes = mask_of(index_of[x] for x in q["yes_ids"])
        scope = mask_of(index_of[x] for x in ids if x[0] in set(q["scope_groups"]))
        a("    {%s, %s," % (lit(q["id"]), lit(text)))
        a("     %s," % lit(help_text))
        a("     %d, %d, 0x%X, %s, %s},"
          % (len(q["text_ja"]), rank_of[q["id"]], scope_bits, mask_lit(yes), mask_lit(scope)))
    a("};")
    a("")

    # --- モード -------------------------------------------------------------
    a("const Mode kModes[kModeCount] = {")
    mode_report = []
    for name in MODE_ORDER:
        cfg = catalog["modes"][name]
        member = list(cfg["item_ids"])
        m = mask_of(index_of[x] for x in member)
        used_groups = [g for g, _s, _n in GROUPS if any(x[0] == g for x in member)]
        if len(used_groups) == 1:
            scope_ja = dict((g, n) for g, _s, n in GROUPS)[used_groups[0]]
        else:
            scope_ja = "%d 分野ぜんぶ" % len(used_groups)
        summary = wrapped("%s から %d こ。質問は %d 問まで。目安 %s。"
                          % (scope_ja, len(member), int(cfg["max_questions"]),
                             MODE_TIME_JA[name]))
        check_lines("%s/summary" % name, summary, SUMMARY_MAX_LINES)
        a("    {%s, %s, %s," % (lit(name), lit(MODE_LABEL[name]), lit(MODE_NAME_JA[name])))
        a("     %s," % lit(summary))
        a("     %s, %d, %d, %d},"
          % (mask_lit(m), len(member), int(cfg["max_questions"]), int(cfg["max_skips"])))
        mode_report.append((name, len(member), int(cfg["max_questions"]), int(cfg["max_skips"])))
    a("};")
    a("")

    # --- 画面の本文 ---------------------------------------------------------
    stats["text_lines"] = 0
    a("const TextEntry kTexts[] = {")
    for key in sorted(UI_TEXTS):
        body, max_lines = UI_TEXTS[key]
        text = wrapped(body)
        check_lines("text/%s" % key, text, max_lines)
        stats["text_lines"] = max(stats["text_lines"], len(text.split("\n")))
        a("    {%s, %s}," % (lit(key), lit(text)))
    a("};")
    a("const size_t kTextCount = sizeof(kTexts) / sizeof(kTexts[0]);")
    a("")
    a("const char *findText(const char *key) {")
    a("    for (size_t i = 0; i < kTextCount; ++i) {")
    a("        const char *a = kTexts[i].key;")
    a("        const char *b = key;")
    a("        while (*a != '\\0' && *a == *b) { ++a; ++b; }")
    a("        if (*a == '\\0' && *b == '\\0') { return kTexts[i].value; }")
    a("    }")
    a("    return key;")
    a("}")
    a("")
    a("}}} // namespace coffee::esp::content")
    a("")
    return "\n".join(lines), stats, mode_report


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
    ap.add_argument("--catalog", default=DEFAULT_CATALOG)
    ap.add_argument("--out-dir", default=DEFAULT_OUT_DIR)
    args = ap.parse_args()

    with open(args.catalog, "rb") as f:
        raw = f.read()
    catalog = json.loads(raw.decode("utf-8"))

    info = validate(catalog, raw)

    header_text = gen_header(catalog, info)
    source_text, stats, mode_report = gen_source(catalog, info)

    os.makedirs(args.out_dir, exist_ok=True)
    header_path = os.path.join(args.out_dir, HEADER_NAME)
    source_path = os.path.join(args.out_dir, SOURCE_NAME)
    header_changed = write_if_changed(header_path, header_text)
    source_changed = write_if_changed(source_path, source_text)

    print("wrote %s (%s)" % (header_path, "changed" if header_changed else "unchanged"))
    print("wrote %s (%s)" % (source_path, "changed" if source_changed else "unchanged"))
    print("catalog %s  sha256 %s" % (catalog.get("catalog_version"), info["sha256"][:16] + "…"))
    print("検査 D01〜D06 合格: 候補 %d / 質問 %d / 対応 %d セル / 署名 %d 種（衝突 0）"
          % (len(catalog["items"]), len(catalog["questions"]),
             len(catalog["items"]) * len(catalog["questions"]),
             len(set(info["signatures"].values()))))
    for name, n, maxq, maxs in mode_report:
        print("  %-6s 候補 %3d / 最大 %2d 問 / わからない %d 回" % (name, n, maxq, maxs))
    print("折り返し後の最大行数: 候補の説明 %d 行 / 質問文 %d 行 / 補足 %d 行 / 画面の本文 %d 行"
          % (stats["def_lines"], stats["q_lines"], stats["help_lines"], stats["text_lines"]))
    print("画面の本文: %d 件" % len(UI_TEXTS))
    print("source file size: %d bytes" % os.path.getsize(source_path))


if __name__ == "__main__":
    main()
