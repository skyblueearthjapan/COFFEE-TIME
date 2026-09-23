"""エスパー対決の GAS 側カタログ（gas/EsperCatalog.gs）を catalog.json から作る。

端末は ID（D01 / Q001）だけを送り、GAS がここで英語名・日本語の定義・質問の英語の意味に置き換えて Jev に渡す
（docs/ESPER_STAGE2_PLAN.md §2）。catalog.json は設計書のデータなので書き換えない。

使い方: python tools/gen_esper_gas.py
"""
import hashlib
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "firmware" / "src" / "app" / "games" / "esper" / "data" / "catalog.json"
DST = ROOT / "gas" / "EsperCatalog.gs"

raw = SRC.read_bytes()
cat = json.loads(raw.decode("utf-8"))
items = {i["id"]: {"n": i["name_en"], "d": i["definition_ja"]} for i in cat["items"]}
questions = {q["id"]: q["meaning_en"] for q in cat["questions"]}
modes = {k: {"items": v["item_ids"], "max_questions": v["max_questions"]} for k, v in cat["modes"].items()}

out = [
    "/**",
    " * COFFEE TIME — エスパー対決のカタログ（GAS 側）。tools/gen_esper_gas.py が catalog.json から生成する。**手で編集しない**。",
    " * 端末は ID だけを送り、ここで Jev に渡す英語名・定義・質問の意味に置き換える。秘密の答えはどこにも無い。",
    " */",
    "const ESPER_CATALOG_VERSION = %s;" % json.dumps(cat["catalog_version"]),
    "const ESPER_CATALOG_SHA256 = %s;" % json.dumps(hashlib.sha256(raw).hexdigest()),
    "const ESPER_RULES_VERSION = '1.0.0';",
    "const ESPER_ITEMS = %s;" % json.dumps(items, ensure_ascii=False, separators=(",", ":")),
    "const ESPER_QUESTIONS = %s;" % json.dumps(questions, ensure_ascii=False, separators=(",", ":")),
    "const ESPER_DESIGN_MODES = %s;" % json.dumps(modes, ensure_ascii=False, separators=(",", ":")),
    "",
]
DST.write_text("\n".join(out), encoding="utf-8", newline="\n")
print(f"{DST.name}: {len(items)} items, {len(questions)} questions, {DST.stat().st_size} bytes")
