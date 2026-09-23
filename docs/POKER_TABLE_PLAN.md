# POKER TABLE（CAFE CARDS）実装計画（v1・2026-09-23）

設計一式: `参考データ/ゲーム4部作/COFFEE_TIME_CAFE_CARDS_UI_Handoff_v1.1.zip`（UI v1.1 + 中に固定原本 `baseline/CAFE_CARDS_Design_v1.0_READONLY.md`
〔SHA-256 `7fe570ff…` を照合済み〕と原本の一式 `baseline/CAFE_CARDS_Handoff_v1.0_READONLY.zip`）。見本は `…_Preview_v1.1.html`（27 場面）。
**ゲームの名前は「POKER TABLE」**（ユーザー指定 2026-09-23）。内部 ID は原本どおり `app_id=cafe_cards`、`game_id=poker|gops|thirty_one|baccarat`。
ユーザーからは AI DUEL・リバーシと同じく「ディレクターとして完了まで」の進め方を続ける。

## 1. ひとことで

喫茶「余白」の小さなテーブルで、人間 1 人 vs AI（Jev または端末 AI）のトランプ 4 種を 1 つの入口から遊ぶ。
POKER（5 カードドロー・5 ハンド・持ち点 100）/ GOPS（1〜7 または 1〜13 の札で得点札を競る）/ THIRTY-ONE（場の 3 枚と交換・3 ハンド）/ BACCARAT（配布結果を予想・5 回）。
ルール・配布・秘密はすべて端末。Jev は「合法な行動の一覧から 1 つ選ぶ」だけ。現金・課金・賭けの要素は一切無い（原本どおり）。

## 2. 原本から変えるところ（ディレクター判断）

| 項目 | 原本 | v1 の実装 | 理由 |
|---|---|---|---|
| GAS | 7 操作・端末認証・決定予約・冪等台帳・5 種のシート | **`event:"cards"` の 1 操作**（AI DUEL・リバーシと同じ）。端末が作った観測を原本の `observation_gate.js` で検査 → Jev → 検証 → 行動を返す。同じ観測の聞き直しには控え（10 分）を返す。終わった試合は `CardsResults` シートに 1 行 | 端末 1 台・往復 4〜7 秒。往復回数を最小に |
| 本人 | AI DUEL の個人登録（PIN）を共用 | **AI DUEL と同じ 8 つのアイコン + ゲスト**（名前・PIN なし）。きろくはアイコンごと・ゲームごと | 端末に PIN が無い（AI DUEL の決定） |
| 途中の試合の保存 | 登録者は NVS 2 スロット各 32KiB、再認証で再開 | **RAM（PSRAM）だけ**。カフェへ戻る・他の画面を開いても同じ電源セッション内なら再開できる。電源が切れたら消える。手札は NVS・SD・シリアルに書かない | 20KB の NVS に 32KiB は入らない。手札は私的情報なので、PIN の無いこの端末では電源をまたいで残さない方が安全 |
| 個人の行動傾向（Jev に渡す統計） | 公開行動 200 件の窓 | v1 では **`sample_n:0`（傾向なし）** を送る。累計の勝敗だけを NVS に持つ | 第 1 段階では不要。後から足せる（原本 I13 の形） |
| 待ち | 決定予約 → ポーリング | AI DUEL・リバーシと同じ依頼箱（`net::gasRequest`）で 1 往復。12 秒で見切り → 通信の状態画面（もう一度 / 端末AIで続行 / カフェ） | |

守るもの（原本どおり）: 原本の C++ コア `cards_core.hpp` と端末 AI `local_policy.hpp` を**無改変**で使う／4 ゲームのルール（P1〜B7）を実装途中で変えない／
同時選択（GOPS の入札・ポーカーの交換・バカラの予想）は **AI を先に確定・保存してから**人間の確定ボタンを有効にし、人間の仮選択は送らない／
Jev に渡すのは `buildObservation` の許可項目だけ（相手の手札・山札・未来の札・人間の仮選択は渡さない）／フォールドで終わったハンドの手札と確率内訳は見せない／
31 の通常ターンに STAND は無い（SWAP か KNOCK。20 手目 KNOCK なら 21 手目の応答）／GOPS の同点は得点札を破棄／バカラの PLAYER・BANKER は席の名前で人間・Jev ではない／
UI は v1.1 の `data/theme.json`・`data/screens.json`（テーブルの木枠・深緑のフェルト・大きいカード・「選ぶ → 確認 → 確定」）／秘密の画面はカフェへ戻る前に不透明に隠す／
確定操作は `LV_EVENT_CLICKED` 1 回、同じ revision の二重確定を防ぐ。

## 3. 段階

1. **第 1 段階（通信なし）**: コアの組み込みと PC 上の検査（原本の `tests/` 相当 + 4 ゲームを端末 AI 同士で多数完走）、共通 UI 部品（TableFrame・CardView・ActionButtonRow・ConfirmSheet・PrivacyCover）、
   4 テーブルの全段階の画面、遊び方（`tutorials.ja.json` 22 ページ）、RAM での中断・再開、カフェ復帰、きろく（アイコン別・ゲーム別の勝敗）、回数の記録（`GameId::Cards = 5`、`cup_ext`）、
   ゲーム一覧 6 件、履歴の表 6 行。この段階では相手は端末 AI だけ（JEV のボタンは「準備中」）。
2. **第 2 段階（Jev）**: `gas/Cards.gs`（原本の `jev_contract.js` と `observation_gate.js` を無改変で取り込み）と端末側の観測づくり・依頼・検証。
   端末 AI に切り替えた試合は Jev に戻らず「混合」。DETAILS（行動の内訳。フォールドのハンドでは出さない）。
3. 独立レビュー → 引き継ぎ書 → コミット。

## 4. 端末のデータ

- NVS `ct_cards`: `stats`（アイコン 8 + ゲスト無し × ゲーム 4 × 勝・負・分・中止、バカラは的中数も。版と CRC）。試合の状態は **NVS に書かない**。
- 試合の状態（山札の順列・手札・イベント）は画面を開いたときに PSRAM に確保し、ゲームの画面を閉じても保持する（`cafe_cards::session` として RAM 常駐。約 2KB）。
  電源が切れたら消える。「再開できる試合があります」は同じ電源セッションの中だけ。
- 乱数は `esp_random()`（Wi-Fi 稼働中はハード乱数）。配布順は試合開始時に 1 回決めて保存し、再送・再開で引き直さない。

## 5. 端末 ⇔ GAS の約束（第 2 段階。`event:"cards"`）

```json
要求: { "event":"cards", "req":12, "match":"<32hex>", "rev":7, "observation": { …buildObservation の形… }, "legal": ["CHECK","BET"] }
応答: { "ok":true, "req":12, "status":"ready", "action":"BET", "p":{"CHECK":0.4,"BET":0.6}, "confidence":0.3, "ms":300, "cached":false }
      { "ok":true, "req":12, "status":"failed", "reason":"NO_KEY|PROVIDER_HTTP_429|DAILY_LIMIT|UNKNOWN_OR_MISSING_KEY|…" }
結果: { "event":"cards", "req":40, "result": { "game":"poker","variant":"fixed5","opponent":"jev|local|mixed","player":"p3|guest",
                                             "human_score":110,"opponent_score":90,"winner":"H|A|D","completed_units":5,"end_reason":"completed|aborted" } }   ← 送りっぱなし
```

- GAS は `validateObservation` で観測を検査し、合法 ID が一致しなければ断る。返ってきた行動が合法かは端末でも必ず確かめる。
- 控えの目印は `match + rev + 観測のハッシュ`。同じ局面の聞き直しは Jev を呼ばない。
- 1 日の Jev 呼び出しは AI DUEL・リバーシと共通の上限（`JEV_DAILY_MAX`）。

## 5b. テキサス・ホールデム（2026-09-23 ユーザー要望「ホールデムは必須」）

原本の POKER は 5 カードドロー。ユーザーはホールデムしか知らないので、**POKER 卓で「ホールデム（既定）／ドロー」を選ぶ**（GOPS の 7/13 と同じ選び方）。
ホールデムは原本に無いので、ここで決める（ディレクター判断。「持ち点が足りない」を構造的に防ぐ原本 P2 の考え方を引き継ぐ）。

| 条件 | 確定値 |
|---|---|
| ID / variant | `poker / holdem`（ドローは `fixed5` のまま） |
| 参加者 | 人間 H と AI の 2 人（ヘッズアップ） |
| 試合の長さ | 5 ハンド。持ち点は**双方 200** から（ドローは 100） |
| 各ハンドの開始 | 新しくシャッフルした 52 枚。双方 1 点の参加点（ブラインドは使わない = ドローと同じ作法） |
| 配る枚数 | 手札 2 枚ずつ（非ディーラーから交互）。場に共通カード: フロップ 3 枚 → ターン 1 枚 → リバー 1 枚（バーンカードなし） |
| ベット段階 | プリフロップ・フロップ・ターン・リバーの 4 回。先手は毎回非ディーラー |
| 単位 | プリフロップ・フロップ 2 点、ターン・リバー 4 点。上乗せは各段階 2 回まで（原本 P3 と同じ行動 CHECK / BET / CALL / RAISE / FOLD） |
| 拠出の上限 | 1 + 2×3 + 2×3 + 4×3 + 4×3 = **37 点/ハンド**。5 ハンドで 185 < 200 なので必ず払える |
| 役 | 手札 2 + 場 5 の 7 枚から最良の 5 枚（21 通りを `poker_value` で比べる）。同キーは引き分けで pot を折半 |
| フォールド | どの段階でも。相手が場を獲得し、以降の段階は作らない |
| 不変条件 | `Hの残点 + Aの残点 + pot == 400`、`pot <= 74` |
| 画面 | 上: 相手の裏札 2 枚。中: 場の 5 枚（未公開は裏・未配布は無し）。下: 自分の 2 枚 + 役名。段階名（プリフロップ/フロップ/ターン/リバー）と単位を表示 |
| Jev に渡す観測 | `game:"holdem"`: own_cards(2), board(0/3/4/5), phase(preflop/flop/turn/river), hand_no, stacks, pot, contribution, unit, raises_left, dealer, public_actions, statistics。相手の手札・山札・バーンは渡さない |
| 端末 AI | 役の強さ（プリフロップは手札 2 枚の格付け、以降は 7 枚の役）と保存済みの乱数で BET / CALL / FOLD を決める固定方針。強くなくてよい |
| 記録 | `stats` の POKER の欄はドロー・ホールデム共通（勝・負・分・中止）。シートの `variant` に `holdem` |

GAS: 観測の検査は原本の gate（`CardsGate.gs`）に無いので、`gas/CardsHoldem.gs` に同じ厳しさの検査と英語の規則文・候補説明を書く（gate と同じ `exactKeys` 方式・合法 ID の一致・stacks+pot==400・unit と段階の対応）。

## 6. 検査

- `tools/cards_checks.cpp` + `tools/run_cards_checks.py`（zig）: 原本の `tests/core_test.cpp`・`local_test.cpp` 相当をそのまま通す + 4 ゲームを端末 AI 同士で各 400 試合完走
  （不変条件: ポーカーの合計 200・pot ≤ 38、GOPS の得点 + burned = 公開済み得点札の合計、31 の 21 行動以内・31 で即終了、バカラの追加札の表）。
- 観測づくりは `gas/CardsGate.gs`（= `observation_gate.js`）に Node で通して合格すること（端末の JSON をそのまま検査）。
- 実機は `tools/uiwalk.py`（`expect:` で画面確認。カフェ画面の「＋1杯」は押さない）。
