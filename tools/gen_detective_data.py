"""探偵ゲーム「喫茶『余白』の事件簿」の脚本 JSON を ESP32 ファーム用の C++ ソースへ変換する。

使い方:
  python tools/gen_detective_data.py
  python tools/gen_detective_data.py --catalog <catalog.author.json> --strings <ui_strings.json> \
      --config <config.json> --out-dir <出力先>

入力（既定値はリポジトリに取り込んだ検証済みデータを指す）:
  --catalog : data/catalog.author.json （設計書 6.1 の著作用パック。3 話の全脚本・正解・解説）
  --strings : data/ui_strings.json     （画面共通の文言）
  --config  : data/config.json         （表示寸法・点数・無操作時間）

出力:
  <out-dir>/DetectiveContent.h
  <out-dir>/DetectiveContent.cpp

■ これは「solo パック」である（設計書 6.2）
  端末には正解・誤答フィードバック・解説・結末・記念品まで全部を焼き込む。
  オフラインで採点と物語表示を完結させるために必要だからで、
  分解した端末から答えを読み取られることは技術的に防げない（設計書 6.2 の明記事項）。
  社内の気軽なゲームとして運用し、賞金のかかった競技には使わないこと。

■ 事前の折り返しとページ分割
  日本語は LVGL が自動折り返ししてくれない（半角空白が無いため）ので、
  本文はこのスクリプトが行単位まで整形してから C++ 文字列にする。
    - 本文欄は 300px 幅・22px の字 → 全角 13 文字（＝半角 26 個分）で 1 行
    - 1 ページは最大 6 行（設計書 5.1）。あふれたら字を小さくせずページを足す
    - 行頭禁則（、。」）など）・行末禁則（「（など）は「追い出し」で処理する
  元データの "\\n" は作者が意図した改行なので必ず残し、その各行をさらに折り返す。

■ 生成時の検査（失敗したらファイルを書かずに止まる）
    - answer_id が 3 つの選択肢 id のどれかであること
    - すべての発言者 id が characters に居ること
    - 各話が証拠 3 件・ヒント 2 段階ちょうどであること
    - 整形後のどの行も 26 半角分を超えず、どのページも 6 行以内であること

文字列は const char* としてそのままバイナリに埋め込まれる（ESP32 では .rodata が
flash にマップされてそのまま読めるので PROGMEM は不要）。
このスクリプトは何度実行しても同じ出力になる（冪等）。
"""
import argparse
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME_DIR = os.path.join(ROOT, "firmware", "src", "app", "games", "detective")
DATA_DIR = os.path.join(GAME_DIR, "data")

DEFAULT_CATALOG = os.path.join(DATA_DIR, "catalog.author.json")
DEFAULT_STRINGS = os.path.join(DATA_DIR, "ui_strings.json")
DEFAULT_CONFIG = os.path.join(DATA_DIR, "config.json")
DEFAULT_OUT_DIR = GAME_DIR

HEADER_NAME = "DetectiveContent.h"
SOURCE_NAME = "DetectiveContent.cpp"

# 本文欄の寸法（設計書 5.1）。1 全角 = 半角 2 つ分として数える
BODY_CELLS_PER_LINE = 13          # 全角 13 文字
BODY_UNITS_PER_LINE = BODY_CELLS_PER_LINE * 2
BODY_LINES_PER_PAGE = 6

# 質問文は人物名/状態の欄（幅 230px・20px の字）に 2 行で出す
QUESTION_UNITS_PER_LINE = 20
QUESTION_MAX_LINES = 2

# 行頭に来てはいけない文字（追い出しで前の行の末尾へ送る）
NO_LINE_START = (
    "、。，．・：；？！゛゜ー～〜ゝゞ々〻）」』】〕〉》｝］’”"
    "ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ"
    ",.!?:;)]}’”"
)
# 行末に来てはいけない文字（次の行の先頭へ送る）
NO_LINE_END = "（「『【〔〈《｛［‘“([{‘“"

# 途中で折り返してはいけない半角の並び（時刻「17:48」・数値・英字など）
ASCII_RUN_CHARS = set(
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    ":.-/%+"
)

# 意味の切れ目。限界の少し手前にこれがあれば、そこで改行したほうが読みやすい
SENTENCE_BREAK = "。、！？．，!?"
SENTENCE_LOOKBACK_CELLS = 6     # 限界から何文字ぶんさかのぼって切れ目を探すか

# 最終行がこれ以下しか残らなかったら、直前の行と割り直す（「…ではな／い。」を避ける）
ORPHAN_MAX_UNITS = 6            # 全角 3 文字ぶん

SPEAKER_ENUM = {
    "NARRATOR": "Speaker::Narrator",
    "LATTE": "Speaker::Latte",
    "MOCHA": "Speaker::Mocha",
    "CHAI": "Speaker::Chai",
    "COCOA": "Speaker::Cocoa",
}
EMOTION_ENUM = {
    "neutral": "Emotion::Neutral",
    "puzzled": "Emotion::Puzzled",
    "soft": "Emotion::Soft",
    "smile": "Emotion::Smile",
}


class GenError(Exception):
    """生成時の検査に落ちたときに投げる（出力ファイルは書かない）。"""


def esc(s: str) -> str:
    """C++ 文字列リテラル用にエスケープする（UTF-8 の日本語はそのまま出す）。"""
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


def units(s: str) -> int:
    """半角いくつ分か（ASCII=1 / それ以外=2）。ct_font_jp_22 の送り幅 11px/22px に合わせた見積り。"""
    return sum(1 if ord(ch) < 0x80 else 2 for ch in s)


def atomize(text: str) -> list:
    """折り返しの最小単位に割る。日本語は 1 文字ずつだが、
    「17:48」のような半角の並びは途中で切ると読めなくなるのでひとかたまりにする。"""
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
    """atoms を k の手前で切ってよいか（行頭禁則・行末禁則）。"""
    if k <= 0 or k >= len(atoms):
        return False
    return atoms[k][0] not in NO_LINE_START and atoms[k - 1][-1] not in NO_LINE_END


def rebalance(prev: str, last: str, limit: int):
    """短すぎる最終行を、直前の行と合わせて半分ずつに割り直す。
    割れなければ None を返して元のままにする。"""
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
            score -= 4      # 句読点の直後で割れるなら少し優遇する
        if best is None or score < best[0]:
            best = (score, [first, second])
    return best[1] if best is not None else None


def wrap_line(text: str, limit: int) -> list:
    """1 行を limit（半角個数）で折り返す。禁則は追い出しで処理する。"""
    atoms = atomize(text)
    out = []
    i = 0
    n = len(atoms)
    while i < n:
        # limit に収まる最大の位置 j を探す（1 個だけはみ出す場合はそのまま 1 行にする）
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

        # 限界の少し手前に「。」「、」があれば、そこで切ると文の切れ目で改行できる
        k = None
        for p in range(j - 1, max(i, j - SENTENCE_LOOKBACK_CELLS) - 1, -1):
            if atoms[p] in SENTENCE_BREAK and p + 1 > i and breakable(atoms, p + 1):
                k = p + 1
                break
        if k is None:
            k = j
            # 行頭禁則: 次の行の先頭が「、」などになるなら、今の行の末尾を落とす（追い出し）
            while k > i + 1 and atoms[k][0] in NO_LINE_START:
                k -= 1
            # 行末禁則: 今の行の末尾が「（」などなら、その文字を次の行の先頭へ送る
            while k > i + 1 and atoms[k - 1][-1] in NO_LINE_END:
                k -= 1
        out.append("".join(atoms[i:k]))
        i = k

    # 最後の行が 1〜3 文字しか残らなかったら、直前の行と割り直して座りを良くする
    if len(out) >= 2 and units(out[-1]) <= ORPHAN_MAX_UNITS:
        merged = rebalance(out[-2], out[-1], limit)
        if merged is not None:
            out[-2:] = merged
    # 「。 今回は」のように文の区切りに空白がある文言では、折り返した行の頭に
    # 空白が残ってしまうので落とす（行末の空白も幅の無駄なので落とす）
    out = [out[0].rstrip(" 　")] + [s.strip(" 　") for s in out[1:]]
    return out if out else [""]


def wrap(text: str, limit: int = BODY_UNITS_PER_LINE) -> list:
    """作者の改行を保ったまま、各行を折り返して行のリストにする。"""
    lines = []
    for raw in text.split("\n"):
        if raw == "":
            lines.append("")
            continue
        lines.extend(wrap_line(raw, limit))
    return lines


def wrapped_text(text: str, limit: int = BODY_UNITS_PER_LINE) -> str:
    return "\n".join(wrap(text, limit))


def paginate(text: str) -> list:
    """本文 1 件を「6 行以内のページ」の並びへ分ける。"""
    lines = wrap(text)
    pages = []
    for start in range(0, len(lines), BODY_LINES_PER_PAGE):
        pages.append("\n".join(lines[start:start + BODY_LINES_PER_PAGE]))
    return pages if pages else [""]


class Emitter:
    """ページ配列を重複しない名前で .cpp へ積んでいく。"""

    def __init__(self):
        self.blocks = []     # 生成した static 配列の行
        self.page_total = 0

    def speeches(self, name: str, speeches: list) -> str:
        """[{speaker, emotion, text}, ...] を Page[] にして PageRun の初期化子を返す。"""
        rows = []
        for sp in speeches:
            speaker = SPEAKER_ENUM[sp["speaker"]]
            emotion = EMOTION_ENUM[sp.get("emotion", "neutral")]
            for page in paginate(sp["text"]):
                rows.append((speaker, emotion, page))
        return self._emit(name, rows)

    def narration(self, name: str, text: str) -> str:
        """語り（手帳）1 本の長文を Page[] にする。"""
        rows = [(SPEAKER_ENUM["NARRATOR"], EMOTION_ENUM["neutral"], page)
                for page in paginate(text)]
        return self._emit(name, rows)

    def _emit(self, name: str, rows: list) -> str:
        if not rows:
            return "{nullptr, 0}"
        self.page_total += len(rows)
        a = self.blocks.append
        a("static const Page %s[] = {" % name)
        for speaker, emotion, text in rows:
            a("    {%s, %s, %s}," % (speaker, emotion, lit(text)))
        a("};")
        a("")
        return "{%s, %d}" % (name, len(rows))


def check_pages(where: str, text: str, limit: int = BODY_UNITS_PER_LINE,
                max_lines: int = BODY_LINES_PER_PAGE):
    lines = text.split("\n")
    if len(lines) > max_lines:
        raise GenError("%s: %d 行あり %d 行に収まらない" % (where, len(lines), max_lines))
    for line in lines:
        if units(line) > limit:
            raise GenError("%s: 行が幅を超えている(%d>%d): %s" % (where, units(line), limit, line))


def validate(catalog: dict):
    """設計書どおりのデータかを生成時に確かめる（落ちたら書き出さない）。"""
    char_ids = {c["id"] for c in catalog["characters"]}
    for missing in ("NARRATOR", "LATTE", "MOCHA", "CHAI", "COCOA"):
        if missing not in char_ids:
            raise GenError("characters に %s が無い" % missing)
    for cid in char_ids:
        if cid not in SPEAKER_ENUM:
            raise GenError("未知の人物 id: %s" % cid)

    episodes = catalog["episodes"]
    if len(episodes) != 3:
        raise GenError("第一章は 3 話でなければならない（%d 話ある）" % len(episodes))

    for ep in episodes:
        case_id = ep["case_id"]
        pub, priv = ep["public"], ep["private"]

        option_ids = [o["id"] for o in pub["options"]]
        if len(option_ids) != 3:
            raise GenError("%s: 選択肢が 3 つでない" % case_id)
        if len(set(option_ids)) != 3:
            raise GenError("%s: 選択肢 id が重複している" % case_id)
        if priv["answer_id"] not in option_ids:
            raise GenError("%s: answer_id %s が選択肢に無い" % (case_id, priv["answer_id"]))
        for oid in option_ids:
            if oid not in char_ids:
                raise GenError("%s: 選択肢 %s が characters に無い" % (case_id, oid))

        if len(pub["clues"]) != 3:
            raise GenError("%s: 証拠が 3 件でない（%d 件）" % (case_id, len(pub["clues"])))
        if len(pub["hints"]) != 2:
            raise GenError("%s: ヒントが 2 段階でない（%d 段階）" % (case_id, len(pub["hints"])))
        for want, hint in enumerate(pub["hints"], start=1):
            if int(hint["level"]) != want:
                raise GenError("%s: ヒントの level が 1,2 の順でない" % case_id)

        # 誤答フィードバックは「正解でない 2 人」ちょうど
        wrong = priv["wrong_feedback"]
        expect = {o for o in option_ids if o != priv["answer_id"]}
        if set(wrong.keys()) != expect:
            raise GenError("%s: wrong_feedback の宛先が不正解の 2 人と一致しない" % case_id)

        # 発言者がすべて実在すること
        def walk(node, where):
            if isinstance(node, dict):
                if "speaker" in node and "text" in node:
                    if node["speaker"] not in char_ids:
                        raise GenError("%s/%s: 未知の発言者 %s" % (case_id, where, node["speaker"]))
                    if node.get("emotion", "neutral") not in EMOTION_ENUM:
                        raise GenError("%s/%s: 未知の表情 %s" % (case_id, where, node["emotion"]))
                for k, v in node.items():
                    walk(v, where + "/" + str(k))
            elif isinstance(node, list):
                for i, v in enumerate(node):
                    walk(v, "%s[%d]" % (where, i))

        walk(pub, "public")
        walk(priv, "private")


def gen_header(catalog: dict, config: dict, strings: dict) -> str:
    lines = []
    a = lines.append
    a("// AUTO-GENERATED FILE — DO NOT EDIT BY HAND.")
    a("// Generated by tools/gen_detective_data.py from:")
    a("//   catalog: data/catalog.author.json (schema_version %s / content_version %s)"
      % (catalog.get("schema_version"), catalog.get("content_version")))
    a("//   strings: data/ui_strings.json")
    a("//   config:  data/config.json (schema_version %s)" % config.get("schema_version"))
    a("// Re-run: python tools/gen_detective_data.py")
    a("//")
    a("// ここに入っているのは設計書 6.2 の「solo パック」＝正解・誤答文・解説・結末まで")
    a("// 全部を含む配布物である。オフラインで採点と物語表示を完結させるために必要で、")
    a("// 端末を分解すれば答えを読み取れることは設計書が明記している（暗号化はしていない）。")
    a("// 社内の気軽なゲームとして運用し、賞金のかかった競技には使わないこと。")
    a("//")
    a("// 本文は生成時に整形済み: 1 行は全角 %d 文字以内、1 ページは %d 行以内。"
      % (BODY_CELLS_PER_LINE, BODY_LINES_PER_PAGE))
    a("// LVGL 側は LV_LABEL_LONG_CLIP で幅・高さを指定してそのまま貼るだけでよい。")
    a("#pragma once")
    a("")
    a("#include <cstddef>")
    a("#include <cstdint>")
    a("")
    a("namespace coffee { namespace det { namespace content {")
    a("")
    a("// 人物（設計書 2.2）。COCOA は第3話の結末にだけ出るので三択には入らない。")
    a("enum class Speaker : uint8_t { Narrator = 0, Latte, Mocha, Chai, Cocoa };")
    a("")
    a("// 表情は 4 種だけ（設計書 2.3）。プレイヤーの選択や正誤で変えてはいけない。")
    a("enum class Emotion : uint8_t { Neutral = 0, Puzzled, Soft, Smile };")
    a("")
    a("struct Character {")
    a("    const char *id;")
    a("    const char *name;")
    a("    Speaker speaker;")
    a("    uint32_t color;      // 設計書 2.4 の初期パレット（0xRRGGBB）")
    a("};")
    a("extern const Character kCharacters[];")
    a("extern const size_t kCharacterCount;")
    a("// 見つからなければ nullptr。")
    a("const Character *findCharacter(Speaker speaker);")
    a("")
    a("// 本文 1 ページ。text は整形済みで、そのまま lv_label_set_text に渡せる。")
    a("struct Page {")
    a("    Speaker speaker;")
    a("    Emotion emotion;")
    a("    const char *text;")
    a("};")
    a("struct PageRun {")
    a("    const Page *pages;")
    a("    uint16_t count;")
    a("};")
    a("")
    a("struct Option { const char *id; const char *label; };")
    a("struct Clue {")
    a("    const char *id;")
    a("    const char *label;")
    a("    PageRun pages;       // 最後のページの「読んだ」で既読になる")
    a("    PageRun summary;     // 手帳に並べる要約")
    a("};")
    a("struct Ambient { const char *id; const char *label; PageRun pages; };")
    a("struct Collectible { const char *id; const char *label; const char *text; };")
    a("")
    a("constexpr size_t kClueCount = 3;")
    a("constexpr size_t kOptionCount = 3;")
    a("constexpr size_t kHintLevels = 2;")
    a("")
    a("struct Episode {")
    a("    const char *case_id;")
    a("    const char *chapter;        // 第1話")
    a("    const char *title;          // 長い題名（整形済み）")
    a("    const char *short_title;    // 一覧・見出し用の短い題名")
    a("    const char *difficulty;")
    a("    const char *teaser;         // 表紙の短い導入コピー（整形済み）")
    a("    const char *question;       // 三択の問い（整形済み・2 行以内）")
    a("    Option options[kOptionCount];")
    a("    PageRun intro;")
    a("    PageRun premises;           // 今回の約束")
    a("    Clue clues[kClueCount];")
    a("    PageRun hints[kHintLevels]; // 段階を飛ばさない。2 段目の最後に結論を足してある")
    a("    const Ambient *ambient;")
    a("    uint16_t ambient_count;")
    a("    uint8_t answer_index;                    // options の添字")
    a("    PageRun wrong_feedback[kOptionCount];    // 正解の枠は count == 0")
    a("    PageRun explanation;")
    a("    PageRun epilogue;")
    a("    PageRun recap;              // 章のおわりに並べる 1 行まとめ")
    a("    Collectible collectible;")
    a("};")
    a("extern const Episode kEpisodes[];")
    a("extern const size_t kEpisodeCount;")
    a("")
    a("// data/ui_strings.json。見つからなければ nullptr。")
    a("// kStrings は元のまま（ボタンの見出しなど 1 行で使うもの）。")
    a("// kWrappedStrings は本文欄と同じ規則で折り返したもの（複数文の案内を本文として出すとき）。")
    a("struct StringEntry { const char *key; const char *value; };")
    a("extern const StringEntry kStrings[];")
    a("extern const size_t kStringCount;")
    a("const char *findString(const char *key);")
    a("extern const StringEntry kWrappedStrings[];")
    a("extern const size_t kWrappedStringCount;")
    a("const char *findWrappedString(const char *key);")
    a("")
    a("constexpr const char *kPackId = %s;" % lit(catalog.get("pack_id", "")))
    a("constexpr const char *kPackTitle = %s;" % lit(strings.get("pack_title", "")))
    a("constexpr const char *kWorldTitle = %s;" % lit(strings.get("world_title", "")))
    a("constexpr const char *kGameTitle = %s;" % lit(strings.get("game_title", "")))
    a("constexpr const char *kContentVersion = %s;" % lit(catalog.get("content_version", "")))
    a("")
    disp = config["display"]
    a("// 設計書 5.1 の本文欄。生成時の整形もこの値で行っている。")
    a("constexpr int16_t kBodyCellsPerLine = %d;" % BODY_CELLS_PER_LINE)
    a("constexpr int16_t kBodyLinesPerPage = %d;" % BODY_LINES_PER_PAGE)
    a("constexpr int16_t kBodyFontPx = %d;" % int(disp["body_font_px"]))
    a("constexpr int16_t kBodyLineHeightPx = %d;" % int(disp["body_line_height_px"]))
    a("")
    a("// 読書中の無操作でお話の一覧へ戻るまで（config.json の page_idle_pause_ms）。")
    a("constexpr uint32_t kPageIdlePauseMs = %d;" % int(config["page_idle_pause_ms"]))
    a("")
    pts = config["score_points"]
    a("// 初見の結果にだけ付く点（復習は加算しない）。")
    a("constexpr uint8_t kPointsUnassistedCorrect = %d;" % int(pts["unassisted_correct"]))
    a("constexpr uint8_t kPointsAssistedCorrect = %d;" % int(pts["assisted_correct"]))
    a("constexpr uint8_t kPointsIncorrect = %d;" % int(pts["incorrect_or_give_up"]))
    a("")
    a("}}} // namespace coffee::det::content")
    a("")
    return "\n".join(lines)


def gen_source(catalog: dict, strings: dict) -> tuple:
    em = Emitter()
    lines = []
    a = lines.append
    a('#include "%s"' % HEADER_NAME)
    a("")
    a("#include <cstring>")
    a("")
    a("namespace coffee { namespace det { namespace content {")
    a("")

    # --- 人物 ---------------------------------------------------------------
    char_rows = []
    for c in catalog["characters"]:
        color = int(c["color"].lstrip("#"), 16)
        char_rows.append("    {%s, %s, %s, 0x%06Xu}," % (lit(c["id"]), lit(c["name"]),
                                                         SPEAKER_ENUM[c["id"]], color))

    # --- 話ごとのページ配列 --------------------------------------------------
    episode_rows = []
    stats = []
    for ep in catalog["episodes"]:
        case_id = ep["case_id"]
        tag = case_id
        pub, priv = ep["public"], ep["private"]
        option_ids = [o["id"] for o in pub["options"]]

        intro = em.speeches("kPg_%s_intro" % tag, pub["intro"])
        premises = em.speeches("kPg_%s_premises" % tag, pub["premises"])

        clue_inits = []
        for i, clue in enumerate(pub["clues"]):
            pages = em.speeches("kPg_%s_clue%d" % (tag, i), clue["pages"])
            summary = em.narration("kPg_%s_sum%d" % (tag, i), "\n".join(clue["summary"]))
            clue_inits.append("        {%s, %s, %s, %s},"
                              % (lit(clue["id"]), lit(clue["label"]), pages, summary))

        hint_inits = []
        for hint in pub["hints"]:
            level = int(hint["level"])
            speeches = list(hint["pages"])
            if level == 2:
                # 2 段目の最後に「どう考えれば決まるか」の結論を足す（設計書 3.5 の一歩進んだ整理）
                speeches = speeches + [{"speaker": "NARRATOR", "emotion": "neutral",
                                        "text": priv["hint_conclusion"]}]
            hint_inits.append(em.speeches("kPg_%s_hint%d" % (tag, level), speeches))

        ambient_inits = []
        for i, amb in enumerate(pub["ambient"]):
            pages = em.speeches("kPg_%s_amb%d" % (tag, i), amb["pages"])
            ambient_inits.append("    {%s, %s, %s}," % (lit(amb["id"]), lit(amb["label"]), pages))
        amb_name = "kAmbient_%s" % tag
        em.blocks.append("static const Ambient %s[] = {" % amb_name)
        em.blocks.extend(ambient_inits)
        em.blocks.append("};")
        em.blocks.append("")

        wrong_inits = []
        for oid in option_ids:
            if oid == priv["answer_id"]:
                wrong_inits.append("{nullptr, 0}")
            else:
                wrong_inits.append(em.narration("kPg_%s_wrong_%s" % (tag, oid),
                                                priv["wrong_feedback"][oid]))

        explanation = em.speeches("kPg_%s_expl" % tag, priv["explanation"])
        epilogue = em.speeches("kPg_%s_epi" % tag, priv["epilogue"])
        recap = em.speeches("kPg_%s_recap" % tag, priv["recap"])

        title = wrapped_text(pub["title"])
        teaser = wrapped_text(pub["menu_teaser"])
        question = wrapped_text(pub["question"], QUESTION_UNITS_PER_LINE)
        collectible_text = wrapped_text(priv["collectible"]["text"])

        check_pages("%s/title" % case_id, title)
        check_pages("%s/teaser" % case_id, teaser)
        check_pages("%s/question" % case_id, question, QUESTION_UNITS_PER_LINE, QUESTION_MAX_LINES)
        # 表紙は 題名 + 空行 + コピー を 1 ページに出すので合計 6 行以内であること
        check_pages("%s/cover" % case_id, title + "\n\n" + teaser)
        # 記念品は 名前 + 空行 + 一言 を 1 ページに出す
        check_pages("%s/collectible" % case_id,
                    priv["collectible"]["label"] + "\n\n" + collectible_text)

        episode_rows.append("    {")
        episode_rows.append("        %s, %s," % (lit(case_id), lit(pub["chapter"])))
        episode_rows.append("        %s," % lit(title))
        episode_rows.append("        %s, %s," % (lit(pub["short_title"]), lit(pub["difficulty"])))
        episode_rows.append("        %s," % lit(teaser))
        episode_rows.append("        %s," % lit(question))
        episode_rows.append("        {%s}," % ", ".join(
            "{%s, %s}" % (lit(o["id"]), lit(o["label"])) for o in pub["options"]))
        episode_rows.append("        %s," % intro)
        episode_rows.append("        %s," % premises)
        episode_rows.append("        {")
        episode_rows.extend(clue_inits)
        episode_rows.append("        },")
        episode_rows.append("        {%s}," % ", ".join(hint_inits))
        episode_rows.append("        %s, %d," % (amb_name, len(ambient_inits)))
        episode_rows.append("        %d," % option_ids.index(priv["answer_id"]))
        episode_rows.append("        {%s}," % ", ".join(wrong_inits))
        episode_rows.append("        %s," % explanation)
        episode_rows.append("        %s," % epilogue)
        episode_rows.append("        %s," % recap)
        episode_rows.append("        {%s, %s, %s}," % (lit(priv["collectible"]["id"]),
                                                       lit(priv["collectible"]["label"]),
                                                       lit(collectible_text)))
        episode_rows.append("    },")

        stats.append((case_id, pub["short_title"], priv["answer_id"]))

    lines.extend(em.blocks)

    a("const Character kCharacters[] = {")
    lines.extend(char_rows)
    a("};")
    a("const size_t kCharacterCount = sizeof(kCharacters) / sizeof(kCharacters[0]);")
    a("")
    a("const Character *findCharacter(Speaker speaker) {")
    a("    for (size_t i = 0; i < kCharacterCount; ++i) {")
    a("        if (kCharacters[i].speaker == speaker) return &kCharacters[i];")
    a("    }")
    a("    return nullptr;")
    a("}")
    a("")
    a("const Episode kEpisodes[] = {")
    lines.extend(episode_rows)
    a("};")
    a("const size_t kEpisodeCount = sizeof(kEpisodes) / sizeof(kEpisodes[0]);")
    a("")
    a("const StringEntry kStrings[] = {")
    for key, value in strings.items():
        a("    {%s, %s}," % (lit(key), lit(value)))
    a("};")
    a("const size_t kStringCount = sizeof(kStrings) / sizeof(kStrings[0]);")
    a("")
    a("const char *findString(const char *key) {")
    a("    for (size_t i = 0; i < kStringCount; ++i) {")
    a("        if (std::strcmp(kStrings[i].key, key) == 0) return kStrings[i].value;")
    a("    }")
    a("    return nullptr;")
    a("}")
    a("")
    a("const StringEntry kWrappedStrings[] = {")
    for key, value in strings.items():
        wrapped = wrapped_text(value)
        check_pages("ui_strings/%s" % key, wrapped)
        a("    {%s, %s}," % (lit(key), lit(wrapped)))
    a("};")
    a("const size_t kWrappedStringCount = "
      "sizeof(kWrappedStrings) / sizeof(kWrappedStrings[0]);")
    a("")
    a("const char *findWrappedString(const char *key) {")
    a("    for (size_t i = 0; i < kWrappedStringCount; ++i) {")
    a("        if (std::strcmp(kWrappedStrings[i].key, key) == 0) return kWrappedStrings[i].value;")
    a("    }")
    a("    return nullptr;")
    a("}")
    a("")
    a("}}} // namespace coffee::det::content")
    a("")
    return "\n".join(lines), em, stats


def write_if_changed(path: str, text: str) -> bool:
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8", newline="") as f:
            if f.read() == text:
                return False
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return True


def load_json(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def page_report(catalog: dict) -> list:
    """話ごとのページ数を数えて表にする（納品報告用）。"""
    report = []
    for ep in catalog["episodes"]:
        pub, priv = ep["public"], ep["private"]

        def n_speeches(speeches):
            return sum(len(paginate(s["text"])) for s in speeches)

        intro = n_speeches(pub["intro"])
        premises = n_speeches(pub["premises"])
        clues = [n_speeches(c["pages"]) for c in pub["clues"]]
        summaries = sum(len(paginate("\n".join(c["summary"]))) for c in pub["clues"])
        hints = []
        for hint in pub["hints"]:
            speeches = list(hint["pages"])
            if int(hint["level"]) == 2:
                speeches = speeches + [{"speaker": "NARRATOR", "text": priv["hint_conclusion"]}]
            hints.append(n_speeches(speeches))
        ambient = sum(n_speeches(a["pages"]) for a in pub["ambient"])
        wrong = sum(len(paginate(t)) for t in priv["wrong_feedback"].values())
        explanation = n_speeches(priv["explanation"])
        epilogue = n_speeches(priv["epilogue"])
        recap = n_speeches(priv["recap"])
        total = (intro + premises + sum(clues) + summaries + sum(hints) + ambient +
                 wrong + explanation + epilogue + recap)
        report.append({
            "case_id": ep["case_id"], "intro": intro, "premises": premises,
            "clues": clues, "summaries": summaries, "hints": hints, "ambient": ambient,
            "wrong": wrong, "explanation": explanation, "epilogue": epilogue,
            "recap": recap, "total": total,
        })
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--catalog", default=DEFAULT_CATALOG)
    ap.add_argument("--strings", default=DEFAULT_STRINGS)
    ap.add_argument("--config", default=DEFAULT_CONFIG)
    ap.add_argument("--out-dir", default=DEFAULT_OUT_DIR)
    args = ap.parse_args()

    catalog = load_json(args.catalog)
    strings = load_json(args.strings)
    config = load_json(args.config)

    validate(catalog)

    header_text = gen_header(catalog, config, strings)
    source_text, em, _ = gen_source(catalog, strings)

    os.makedirs(args.out_dir, exist_ok=True)
    header_path = os.path.join(args.out_dir, HEADER_NAME)
    source_path = os.path.join(args.out_dir, SOURCE_NAME)
    header_changed = write_if_changed(header_path, header_text)
    source_changed = write_if_changed(source_path, source_text)

    print("wrote %s (%s)" % (header_path, "changed" if header_changed else "unchanged"))
    print("wrote %s (%s)" % (source_path, "changed" if source_changed else "unchanged"))
    print("pages: %d 枚（全話合計。1 ページ = 最大 %d 行 x 全角 %d 文字）"
          % (em.page_total, BODY_LINES_PER_PAGE, BODY_CELLS_PER_LINE))
    for r in page_report(catalog):
        print("  %s: 導入%d 約束%d 証拠%s(=%d) 要約%d ヒント%s 小話%d 誤答%d 解説%d 結末%d まとめ%d / 計%d"
              % (r["case_id"], r["intro"], r["premises"], r["clues"], sum(r["clues"]),
                 r["summaries"], r["hints"], r["ambient"], r["wrong"], r["explanation"],
                 r["epilogue"], r["recap"], r["total"]))
    print("strings: %d entries" % len(strings))
    print("source file size: %d bytes" % os.path.getsize(source_path))


if __name__ == "__main__":
    main()
