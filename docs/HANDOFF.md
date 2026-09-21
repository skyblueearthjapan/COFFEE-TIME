# COFFEE TIME 引き継ぎ書

- 最終更新: 2026-09-22 未明（第 6 版 + **AI DUEL を追加**）。最新コミットは `git log` を参照（この文書に固定の番号は書かない）
- リポジトリ: https://github.com/skyblueearthjapan/COFFEE-TIME （ブランチ `main`）
- **この文書だけ読めば作業を再開できる**ように書いてある。詳細は下の関連文書へ

> ⚠️ **このリポジトリは PUBLIC。** Wi-Fi のパスワード・合言葉 (TOKEN)・GAS の URL・デプロイ ID・API キー・スマホの名前を、
> このファイルやコミットに書かないこと。秘密情報は Git 管理外のファイル（§9）と GAS のスクリプト プロパティにだけ存在する。

| 関連文書 | 中身 |
|---|---|
| [STORAGE_POLICY.md](STORAGE_POLICY.md) | フラッシュ / 内蔵ファイル領域 / microSD の使い分け、SD の配線と試験、操作ログ、背景画像の方針 |
| [MENU_DESIGN.md](MENU_DESIGN.md) | メニュー（今日の状況・履歴・設定）の設計と NVS のデータ形式 |
| [WEREWOLF_TODO.md](WEREWOLF_TODO.md) | 人狼ワンナイト版の実装メモ・落とし穴 |
| [WEREWOLF_STANDARD_RULES.md](WEREWOLF_STANDARD_RULES.md) | 人狼「通常ルール」の仕様（ユーザー確定）と実装メモ |
| [AI_GAMES_DIGEST.md](AI_GAMES_DIGEST.md) | エスパー対決・AI DUEL の設計要点、Jev API の疎通試験の結果 |
| [AI_DUEL_PLAN.md](AI_DUEL_PLAN.md) | **AI DUEL の実装計画**（設計書から何を変えたか・画面・保存・端末⇔GAS の約束） |
| `参考データ/` | 企画書・UI イメージ・ゲーム 4 部作の設計書と付属パッケージ（zip） |

---

## 1. 最初にやること（5 分）

1. ESP32 の COM ポートを確認（これまで **COM8** = native USB、COM9 = UART 側）
   ```powershell
   Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match 'COM\d+' } | Select Name
   ```
2. 実機の画面を見る: `python tools/snapshot.py COM8 now.png`
3. **書き込む前に必ず、ユーザーが遊んでいないか確かめる**: `python tools/serlog.py COM8 5 send=GD` で
   `send=GDVU` にすると 4 ゲームぶん出る。`[WOLF] … phase=0`、`[DET] … case=-`、`[ESP] view=0`、`[DUEL] view=-`（または `paused`）なら待機中
4. **Wi-Fi につながっているとき（ユーザー宅では iPhone のテザリング）は、+1・補充・シリアルの `T`/`R` が本物の記録になり、
   「残り」が 3 / 0 になった瞬間に登録者へメールが飛ぶ。** 試験で杯数を動かさない。
   **ゲームの中のタップを送る前に、必ず `uiwalk.py` の `expect:` でいまの画面を確かめる**（§10-11。端末が再起動して HOME に戻っていると、タップが「＋1」に当たる）
5. ビルド・書き込みは **PowerShell から**。Git Bash（msys）はこの PC でときどき起動不能になる（§10）
6. ユーザーへの説明は日本語・平易に。作業は自分で進め、ユーザーには目視・タッチ・秘密情報の入力・Google の承認だけを頼む
7. §4 の残タスクと §12 の未回答事項を確認する

## 2. プロジェクトとユーザー

社内カフェに置く**持ち運びできる**タッチ式コーヒーカウンター端末。コーヒーを 1 杯取ったら「+1」。今日の杯数・残り杯数を表示し、
残り 3 杯（通常）と 0 杯（至急）で担当者にメール。休憩時間に遊べるゲーム 4 部作（人狼・探偵・エスパー対決・AI DUEL）を載せる。

- ハード: **Waveshare ESP32-S3-Touch-LCD-2.8C**（480×480 丸型 / ST7701 RGB / GT911 タッチ / Flash 16MB / PSRAM 8MB / microSD 32GB / LiPo 1500mAh / RTC PCF85063 / ブザー）
- ユーザー（今泉さん）: 組み込み・デプロイは初めての非エンジニア。「理解を深めながら、作業はできる限りエージェント側で」。
  どこに保存され、どこでビルドされ、どこにデプロイされたかを知りたがる。**画面の見本（画像）を見せると判断が早い**（デスクトップに PNG を置いて開く）
- アカウント: Google は会社の Workspace、GitHub は `skyblueearthjapan`

## 3. 現在の状態（2026-09-22 未明）

| 分野 | 状態 |
|---|---|
| カフェ機能（+1・補充・日付リセット・NVS 保存） | ✅ 運用できる状態。HOME の見出しは「今日」「残り」、天気はマーク＋気温、補充の表示は「補充しました：n 杯」 |
| 通知（GAS・メール） | ✅ デプロイ **@8**（URL 不変。@7・@8 は AI DUEL の受け口の追加だけで、コーヒー側は無変更）。ゲージは端末から届く `max` に連動、文面は「『残り』の数字を長押し」。0 杯の【至急】は見本のみ確認（実機 1→0 は未確認） |
| Wi-Fi | ✅ 3 件まで登録（WiFiMulti）。自宅では **iPhone のテザリングを端末のすぐ横に置けば接続**（-47dBm）。受信感度の弱さ（ケースの金属リングが原因と推定）は未対策 |
| 時計 | ✅ NTP + RTC。RTC は**電源が完全に切れると時刻を失う**（電池スイッチ OFF + USB 抜き）。設定画面と `tools/settime.py` で合わせられる |
| microSD | ✅ 操作ログ（起動理由・+1・補充・日付変更・時刻設定・ゲーム 1 回）。シリアル `L` で読める |
| メニュー（今日の状況・履歴・設定） | ✅ 実装・全画面を実機で表示確認。履歴の週は**月〜日**、ゲームの回数つき。設定 7 項目。未確認: 自動の暗転、再起動をはさんだ保存、操作音（ブザーの極性） |
| 人狼 ワンナイト | ✅ 実装。席ごとのキャラクター、物語の導入、役職の絵。ユーザーが指で遊んで確認中（秘密の本文がボタンに重なる不具合は修正済み） |
| 人狼 通常ルール | 🚧 実装・PC 上の試験 全合格（20 万局）。1 日目だけ役職確認、2 日目以降は 1 画面少ない。ユーザーが指で確認中 |
| 探偵（ひとり推理） | ✅ 完成形（Jev は入れない＝ユーザー決定）。第 1 話を実機で通し確認。第 2・3 話の通しは未実施 |
| エスパー対決 第 1 段階（端末だけ） | ✅ 実装・PC 上の試験 全合格。4 モード（§7.3）。ユーザーが遊んで「ミニ・レギュラー・フルで負け、超難関で勝ち」と確認。未確認は §4 |
| エスパー対決 第 2 段階（Jev） | 未着手。**Jev API の疎通は確認済み**（GAS 経由・0.2〜0.5 秒/回） |
| AI DUEL | ✅ 実装・PC 上の試験 全合格（参考ロジックと 539 ラウンド一致）・実機で Jev 対戦を 10 回通し確認（§7.4）。アイコン 8 人 + ゲスト、記録は端末、Jev は GAS 経由。未確認は §4 |
| フラッシュ使用率 | 約 65%（アプリ領域 6.25MB 中 約 4.24MB）。RAM 25.7%。ゲーム用フォント 848 字 |

## 4. 残タスク（優先順）

### これからやる作業
1. **AI DUEL の仕上げ**（実装は済み。§7.4・[AI_DUEL_PLAN.md](AI_DUEL_PLAN.md)）:
   ユーザーに指で遊んでもらって感想を聞く（手のアイコンの見え方、8 つのアイコン方式でよいか、待ち時間）／
   未確認 = 3 分放置のひと休み、「記録を消す」長押し（自動操作では長押しできない）、ゲーム中のカフェ画面、オフライン時の表示、20 ラウンド以上たまったあとの「癖」カード／
   **試験データの片付け**: アイコン「はぐるま」に 2 ラウンド（途中終了 1）が残っている → プロフィールの「記録を消す（長押し）」。`DuelRounds` シートに guest / p7 の試験行あり
2. **エスパー対決 第 2 段階**: 残った候補の順位付けと次の質問選びを Jev に任せる。端末側の差し込み口は `games/esper/core/esper_core.hpp` の `Advisor`。
   GAS には `gas/Jev.gs` の `jevChoice_` がある。**端末 → GAS の通信は AI DUEL で作った依頼箱 `net::gasRequest / gasTakeResult`（ゲーム非依存）をそのまま使える**。**選んだもののメモ（`s_memo`）はサーバーへ絶対に送らない**。
   Jev にすると AI の勝率が上がるはずなので、上がりすぎたら候補の数（`tools/gen_esper_data.py` の `PLAY_MODES`）で調整
3. エスパーの未確認: AI が外れたときの答え合わせ画面、3 分放置のひと休み、独立したコードレビュー（実装担当が利用上限で最終報告の前に止まったため未実施）
4. 探偵の第 2・3 話を自動操作で通す（`tools/uiwalk.py`。終わったら `X` で進み具合を初期化）
5. メニューの未確認 3 点（自動の暗転・再起動後の保存・操作音）。ブザーは EXIO8、極性は `Buzzer.cpp` の定数 1 つ
6. 背景画像の作り直し（朝と昼の見分けがつきにくい・夜を追加）と SD からの読み込み、USB 経由で SD にファイルを送るツール（[STORAGE_POLICY.md](STORAGE_POLICY.md) §9）
7. LINE WORKS 通知、省電力・筐体、遊び要素（達成アニメ・季節テーマ）

### ユーザーに確認してもらうこと（依頼済み・結果待ち）
- 人狼（両モード）を指で通す: 押している間だけ役職が出る／離すと消える・残像なし／2 本指で出ない／8 秒で自動で隠れる、通常ルールの 2 日目以降の流れ、分かりにくい画面
- 探偵のプレビュー HTML（`参考データ/ゲーム4部作/COFFEE_TIME_CAFE_DETECTIVE_Preview.html`）の感想

### 気になる点（未解決）
- **補充の誤操作**: 「残り」の 1.5 秒長押しは、持ち運びで右端に指がかかると誤って働く（実際に 1 回起きた）。3 秒長押し＋進み具合の表示 / 取り消しを提案済み、ユーザーは「とりあえず大丈夫」
- 送信キューは RAM のみ（長い Wi-Fi 断＋再起動で未送信分が消える）。内蔵ファイル領域へ逃がす案あり
- HTTPS は `setInsecure()`（証明書検証なし）。AI DUEL は名前も PIN も持たない作りにしたので当面このまま
- **内蔵メモリの余裕が小さい**: TLS 1 本で約 35KB。空き約 62KB・最大の連続 31KB（DUEL 画面表示中の実測）。GAS の転送先を読む前に 1 本目を `client.stop()` で手放している（§10-12）。
  大きな静的配列を足すときは PSRAM に置くことを検討する（AI DUEL で静的 RAM が +18.9KB 増えた）
- 2026-09-22 01:24 に**試験の誤タップで本物の「＋1」が 1 件**入った（今日の杯数 0→1、ログシートに `take` 1 行、メールは条件外で未送信）。ユーザーに報告済み、消すかどうかは未回答
- メインループは Wi-Fi の再接続（最大 10 秒）や GAS への送信（最大 15 秒×2）、AI DUEL の予測の依頼（最大 11.5 秒）で止まる。シリアルコマンドと NVS 保存がその間遅れる。ネットワーク処理を別タスクにするのが本筋
- 人狼 7 人以上は手がかりが薄い（配役は確定済みだが、遊んで物足りなければ騎士・霊媒師を検討）
- エスパーで使わない候補は無い（超難関が 128 こ全部を使う）が、レギュラー・フルは各分野の先頭から採っているだけ。遊んでみて入れ替えたければ `PLAY_MODES`
- スプレッドシート「ログ」に試験データが入っている（本番前に消すか相談）
- 受入試験（人狼 99 項目・探偵 72 項目）は未実施

## 5. ユーザーと決めたこと（変えないこと）

- **保存場所**: カフェ機能と人狼は SD なしで 100% 動く。大きなゲーム素材はまず内蔵ファイル領域、SD はログと「無くても動く大きなもの」。区画は変えない（OTA の余地を残す）
- **朝は必ず 0 杯から**（夜の残りは配るかアイスコーヒー）。日付が変わったら「残り」= 0 で固定、設定項目にもしない
- **探偵に Jev は入れない**。AI（TypeSafe Jev）を使うのはエスパー対決と AI DUEL だけ。API キーは端末に置かず GAS のスクリプト プロパティ `JEV_API_KEY`
- **人狼**: ワンナイトと通常ルールを開始時に選ぶ。通常ルールは 村人・人狼・占い師のみ、初日は襲撃なし、抜けた人の役職は終了まで非公開。
  配役（確定）: 4〜6 人 = 人狼 1・占い師 1 / 7〜8 人 = 人狼 2・占い師 1 / 9〜10 人 = 人狼 2・占い師 2。
  **どの画面でも「いま何をする場面か」を見出しに書く**（投票は「人狼だと思う人を選ぼう」）。通常ルールの導入文はワンナイトの語り口を正として書く
- **エスパー対決の 4 モード**（AI の勝率はユーザー指定）: ミニ 10 こ・3 問 = 80% / レギュラー 20 こ・4 問 = 70% / フル 64 こ・5 問 = 50% / 超難関 128 こ・5 問 = 25%。
  選んだものは「メモ」として質問の画面にいつも出す（画面表示だけ。AI には渡さない）
- **メニュー**: アイコン 4 つ（今日の状況・履歴・設定・ゲーム）。履歴の週は月曜はじまり。ゲームは回数だけを数える（役職・勝敗・正誤は記録しない）。「お知らせ」画面と Wi-Fi のパスワード入力は入れない
- HOME のメニューボタンは右斜め下。日付は日本語、COFFEE TIME のロゴは英語のまま
- **ゲーム一覧の並び順**（2026-09-21 指定）: AI DUEL → エスパー対決 → 喫茶「余白」の事件簿 → 閉店後の人狼会。履歴のゲームの表も同じ順。
  自動操作のタップ位置: AI DUEL `240,136` / エスパー `240,206` / 探偵 `240,276` / 人狼 `240,346`
- **AI DUEL の作り**（2026-09-22、ユーザーから「ディレクターとして完了まで」と一任されて決めた。理由は [AI_DUEL_PLAN.md](AI_DUEL_PLAN.md) §2）:
  個人の記録の正本は端末（NVS）、シートは写し。本人の選択は 8 つのアイコン + ゲスト（名前・PIN なし）。相手は自動（つながれば Jev、だめなら統計 AI）。
  PIN・管理画面・9 枚のシート・コミットハッシュは作らない

## 6. 全体構成とデータの流れ

```
【PC】COFFEE TIME フォルダー ──git push──▶【GitHub】COFFEE-TIME
   │ PlatformIO でビルド → USB(COM8) で書き込み          │ clasp -u work push / update-deployment
   ▼                                                     ▼
【ESP32】画面・杯数・統計・設定(NVS)・操作ログ(SD)   【Google Apps Script】（スプレッドシートに紐づけ・デプロイ @8）
   ├─ Wi-Fi → NTP / Open-Meteo 天気（千葉市・15 分ごと）  ├─ 「ログ」シートに 1 行追記
   └─ +1・補充・日付変更 ──HTTPS POST──▶ Web アプリ       ├─ 残りが 3 / 0 に減った瞬間 →「設定」シートの宛先へ HTML メール
                                                          ├─ event=jevtest → TypeSafe Jev（`gas/Jev.gs`。AI ゲーム用）
                                                          └─ event=duel → Jev に次の手の予測を中継 / `DuelRounds` シートに対戦の記録（`gas/Duel.gs`）
```

### ESP32 → GAS の送信内容（JSON）
```json
{"token":"…","device":"coffee-xxxxxx","event":"take|refill|newday","id":"<起動乱数>-<連番>",
 "taken":7,"left":3,"prev":4,"max":10,"rssi":-80,"ts":1789800000}
```
- 通知条件: `event=="take" && prev>left && (left==3 || left==0)`。`id` で 6 時間の重複排除。失敗時は 30 秒ごとに再送（RAM キュー 32 件）
- GAS の応答は 302。転送先は**ヘッダーを引き継がない新しい GET** で読む（端末も PC の道具も同じ）
- 「ログ」シートの列: 受信日時 / 端末日時 / 端末ID / イベント / 今日の杯数 / 残り杯数 / Wi-Fi強度 / イベントID

### 端末内の保存（NVS）
| 名前空間 / キー | 中身 |
|---|---|
| `cup` / `ymd` `taken` `left` | 杯数（最重要。ほかの保存が壊れても触らない） |
| `cup` / `today` | 今日の時間帯別の杯数・補充回数・最後の 1 杯 / 補充の時刻・ゲームの回数（48B・版と CRC） |
| `cup_hist` / `days` | 直近 35 日の輪（杯数・補充・ゲームの回数）＋ゲームの累計（588B） |
| `cfg` / `v1` | 明るさ・暗くするまでの時間・操作音・1 回に作る杯数（12B） |
| `ct_wolf` / `meta` `mode` | 人狼の公開メタ（20B・設計書の形式）と前回のモード。**秘密は RAM のみ** |
| `ct_det` / `prog` | 探偵の進み具合（初回の結果・ヒント・読了・記念品。20B） |
| `ct_esp` | エスパーのきろく（4 モード × AI 勝 / 人 勝 / 最少問数。40B） |
| `ct_duel` / `p0`〜`p7` `meta` | AI DUEL の 1 人ぶんの統計（488B・版と CRC。手の回数・遷移・直近 50 手・予測の成績）× 8 と、対戦の通し番号。1 ラウンドごとに保存 |

SD: `/coffee_time/log_YYYYMM.csv`（時刻不明の間は `log_nodate.csv`）。カードが無くても全部動く。フォーマットは絶対にしない。

## 7. ゲーム

共通: `ui::ScreenManager` に 1 画面を載せ、局面ごとに中身を作り直す方式。破棄時に静的ポインタとタイマーを片付ける
（`lv_event_get_target(e) != s_screen` の見張りつき）。左上の「カフェ / 一時停止」からコーヒー＋1。3 分放置でひと休み。
ゲーム中は自動の暗転をしない（`display::setGameActive`）。終わったら `cup::stats::gamePlayed(GameId, note)` で 1 回と数える。

### 7.1 人狼（`firmware/src/app/games/werewolf/`）
- ワンナイト: 設計書のコア `core/*.hpp` を**無改変**で使用（検証済み）。通常ルール: 新しいコア `core_std/werewolf_std_core.hpp`
- 画面と進行 `WerewolfGame.cpp`、ハード接続 `WerewolfPort.*`、文言は設計書由来の `data/content.ja.json`（書き換えない）＋
  追加分 `data/content.local.ja.json`（同じキーは上書き）→ `python tools/gen_game_data.py` → `WerewolfContent.*`（手で編集しない）
- **覗き見防止**: 秘密（役職・夜の結果・投票先）は押している間だけラベルに入れ、離したら空にして `lv_refr_now` → 40ms 待ち → 次へ。
  判定は LVGL ではなくタッチの生データ（`lvgl_v8_port.*` が保存。2 点以上・200ms より古い値は無効）。固まったら見張りタスクがバックライトを切る（`display::privacyCut/Restore`）。
  秘密は NVS・SD・シリアルに出さない。開発用の自動タップ（`P`）では秘密は開けない
- PC 上の試験: `python tools/run_wolf_std_checks.py`（zig でコンパイル。20 万局）
- キャラクター: カップ / パン / スプーン / ポット / クッキー / ほし / はっぱ / ベル / ほん / つき（`ct_font_icons_36/54/88` = Material Icons Round）

### 7.2 探偵（`games/detective/`）
- 脚本は `data/catalog.author.json`（書き換えない）。`python tools/gen_detective_data.py` が 13 文字 × 6 行で折り返し・ページ分けして `DetectiveContent.*` を作る
- 3 話。証拠 3 つ → 3 択 → 答え合わせ → 解説 → 結末 → 記念品。間違えても降参しても結末まで読める。進み具合は NVS `ct_det/prog`
- 試験で初見の記録を消費したら、シリアル `X` で初期化すること

### 7.3 エスパー対決（`games/esper/`）
- カタログ `data/catalog.json`（128 候補・146 質問。書き換えない）。`python tools/gen_esper_data.py` →`EsperContent.*`。カタカナ語の途中では折り返さない
- コア `core/esper_core.hpp`。モード表の **0〜2 番は設計書のモード（手本データの照合用）、3〜6 番が遊び用の 4 モード**（`PLAY_MODES`）。画面は遊び用だけを使う
- PC 上の試験: `python tools/run_esper_checks.py`（手本 168 局と完全一致 + 遊び用 4 モードの AI の勝率を 40 通りの種で実測: 80.0 / 70.0 / 46.9 / 24.4%）
- メモ: カードを開いて「決めた」を押したものを `s_memo` に持ち、質問の画面に「メモ：○○」のボタンで出す。**画面表示だけ**で推論には渡さない

### 7.4 AI DUEL（`games/duel/`）
- 計画と約束ごとは [AI_DUEL_PLAN.md](AI_DUEL_PLAN.md)。コア `core/duel_core.hpp`（勝敗・統計・統計 AI の式・最善手・癖カード・保存形式・Jev に渡す集計）、保存 `DuelStore.*`、画面 `DuelGame.*`
- 1 ラウンドの流れ: 結果を出した瞬間に次の回の予測を先読み → 端末内の統計 AI の確率は必ず計算 → Wi-Fi があり記録が 1 回以上あれば GAS 経由で Jev に依頼 →
  **9 秒以内**に届けば Jev の確率、だめなら統計 AI の確率で「期待得点差が最大の手」に決める。いちど決めた手は変えない。決まるまで手のボタンは無効
- 記録のシート送信は**対戦の終わりに 1 回**（`logs` 最大 10 件）。送れなければ捨てる。Jev に渡すのは集計だけ（アイコン・端末名・杯数・今回の手は入れない）
- PC 上の試験: `python tools/run_duel_checks.py`（設計書の参考ロジック `duel_reference.py` で作った手本 20 局 539 ラウンドと 1e-9 以内で一致。手本の作り直しは `tools/gen_duel_golden.py`）
- 実機の実測: Jev 1 回 **3.7〜7.3 秒**（Jev 自体は 0.2〜0.3 秒。残りは TLS 2 回と GAS の往復で、GAS が遅い時間帯は 6 秒を超えて POST の 6.5 秒待ちに当たり統計 AI に落ちる。
  10 回中 Jev が 3〜8 回というばらつき）。プレイヤーは「次へ」のあと「相手が考え中…」を数秒見ることがある。**待ちを縮めるなら GAS の往復そのものを減らす工夫が要る**（未着手）。ログの 1 行で理由まで分かる:
  `[DUEL] req=5 -> jev 4094ms` / `-> stats 7391ms (http -11)` / `(none:DAILY_LIMIT)` / `(not sent: no history)` / `[DUEL] logs=10 sent req=11`
- 1 日の Jev 呼び出し上限: スクリプト プロパティ `JEV_DAILY_MAX`（無ければ 1500。Jev の呼び出しが失敗した回も 1 回と数える）
- 独立レビュー（2026-09-22）: 重大なし。要修正 2 件（コアが断ったラウンドを画面だけ進めない／先読みの返事で結果画面を作り直さない）と改善 6 件を反映済み。
  残した継ぎ目: NVS への保存が 2 回続けて失敗すると保存済みの内容を読み直すので、その 1 ラウンドは画面・シートと端末の記録が食い違う（探偵・エスパーと同じ方針）。
  9 秒で見切った直後のラウンドは、前の通信がまだ終わっていないと `not sent: busy` で統計 AI になる
- 依頼箱 `net::gasRequest(req, body, detached)`: ふつうは `gasTakeResult` で返事を受け取る。`detached=true` は送りっぱなし（記録の送信用。返事は捨てて箱を空ける）
- 手のアイコンは Font Awesome Free Solid（OFL）の 3 字だけを `ct_font_hands_54` に、アイコン 8 人 + ゲストは Material Icons Round を `ct_font_icons_54` に追加
- 自動操作のタップ位置は `AI_DUEL_PLAN.md` ではなくここ: はじめる `240,266` → ゲスト `240,319`（p0〜p3 は y=146 の x=112/197/283/368、p4〜p7 は y=242）→ 対戦する `240,243` → はじめる `240,300` →
  グー `120,264` チョキ `240,264` パー `360,264` → 次へ `240,345`／やめる `301,402` → 確認のやめる `240,284`。**カフェ画面とひと休み画面の「＋1杯」（`240,241` / `240,305`）は本物の記録になるので押さない**

### 7.5 AI（Jev）
- `gas/Jev.gs`: `jevChoice_(state, instructions, criteria, model)` が選択式の質問を 1 つ投げる。モデル `jev-1.13.0`、キーはスクリプト プロパティ `JEV_API_KEY`
- 疎通試験: `python tools/gas_call.py jevtest`（URL・合言葉・キーは表示されない）。AI DUEL の受け口は `gas_call.py duel`（シートに書かない）/ `duel withlog=1`（guest の試験行を 1 行書く）
- **GAS に権限を足すときは、デプロイを更新する前にユーザーがエディタで承認すること**（`jevAuthorize()`）。先に更新すると、承認が済むまでコーヒーの記録を含む doPost 全体が止まる

## 8. 開発環境と手順

- Windows 11、Python 3.14（pyserial / pillow / esptool / platformio / ziglang）、Node.js、Git、gh、clasp 3.2.0、Codex CLI
- PlatformIO core は **`C:/pio`**（パス長制限のため）。pioarduino 55.03.311（Arduino-ESP32 3.3.11）/ ESP32_Display_Panel 1.0.4 / LVGL 8.4.0 / ArduinoJson 7

```powershell
cd firmware
python -m platformio run -e app -t upload --upload-port COM8      # 本体
python -m platformio run -e sdtest -t upload --upload-port COM8   # microSD の読み書き試験
python -m platformio run -e hwtest / wifitest / app_uart          # LCD・タッチ試験 / Wi-Fi 受信だけ / UART 側(COM9)
```

| 道具 | 内容 |
|---|---|
| `python tools/snapshot.py COM8 out.png` | 実機のスクリーンショット |
| `python tools/uiwalk.py COM8 <dir> key:3 tap:240,367 wait:1 snap:name …` | **画面を自動で操作して順に撮る**（タップは端末の受領の返事を待つ。秘密は開けない）。`expect:U:view=result` = 画面が違えば中止、`watch:10` = ログ表示 |
| `python tools/serlog.py COM8 30 [send=GD]` | ログ表示（＋開発コマンド送信） |
| `python tools/settime.py COM8` | PC の時計を端末と RTC に設定 |
| `python tools/collect_ui_chars.py [--check]` | 日本語の文字一覧を更新 / **一覧とフォントの実体の両方を検査** |
| `bash tools/gen_fonts.sh` | フォント再生成（Git Bash が動かないときは同じ `npx lv_font_conv` を PowerShell から。§10） |
| `python tools/gen_game_data.py` / `gen_detective_data.py` / `gen_esper_data.py` | ゲームのデータを C++ に変換 |
| `python tools/run_wolf_std_checks.py` / `run_esper_checks.py` / `run_duel_checks.py` | PC 上のロジック試験 |
| `python tools/gas_preview.py 3 12` / `gas_call.py jevtest` | 見本メール（所有者だけ）/ GAS 経由の Jev 疎通試験 |

シリアル開発コマンド: `S`=スクショ / `M` `N` `E` `A`=背景固定・自動 / `T`=+1 / `R`=補充（**本物の記録になる**）/ `W`=Wi-Fi スキャン /
`C<UNIX秒>`=時刻設定 / `L`=SD ログの末尾 / `P<x>,<y>`=タップ / `0`=HOME / `1`=メニュー / `2`=ゲーム一覧 / `3`=人狼 / `4`=探偵 /
`5`〜`9`=今日の状況・時間ごと・履歴・日ごと・設定 / `B`=明るさ / `F`=時刻合わせ / `I`=システム情報 / `G`=人狼の公開状態 / `D`=探偵の状態 / `V`=エスパーの状態 / `U`=AI DUEL の状態（相手の手は公開後だけ出る）/ `X`=探偵の進み具合を初期化。
秘密が画面に出ている間は `S` と画面切替を受け付けない。

### GAS
- 必ず `clasp -u work …`（既定ユーザーは個人 Gmail）。反映は `cd gas; clasp -u work push -f` → `list-deployments` で **@数字** の ID → `update-deployment <ID> --description "…"`（URL 不変）。`create-deployment` は URL が変わるので使わない
- `invalid_grant (invalid_rapt)` で失敗したら Workspace の再ログインが必要 → ユーザーに `! clasp -u work login` を実行してもらう

### 並行作業
- 大きな実装は実装担当エージェント（executor, opus）に仕様を渡して任せ、**エージェントはビルドまで・書き込みと実機確認は自分**。終わったら別のエージェントでコードレビュー
- 同時に 2 つ進めるときは git worktree **`C:\ctw`** を使う（`git -C C:/ctw checkout --detach main` で最新にし、`secrets.h` をコピー）。
  終わったら変更ファイルを本体へコピーし、文字一覧は和集合にしてフォントを作り直す
- エージェントが利用上限で止まることがある。作業ツリーに途中の成果が残るので、`git status` と試験・ビルドで状態を確かめて引き継ぐ

## 9. 秘密情報の置き場所（すべて Git 管理外）

| 場所 | 中身 |
|---|---|
| `firmware/include/secrets.h` | `WIFI_SSID` `WIFI_PASSWORD`（会社）/ `…2`（自宅）/ `…3`（テザリング）、`GAS_URL`、`GAS_TOKEN`。**表示しない**。値の形式だけ確かめるときは名前と文字数だけを出す。ユーザーに入力してもらうときはメモ帳で開く |
| `gas/Secret.gs` | `TOKEN`（`GAS_TOKEN` と同じ値） |
| `gas/.clasp.json` | scriptId / parentId |
| GAS のスクリプト プロパティ | `JEV_API_KEY`（ユーザー本人が設定）、任意で `JEV_MODEL` |
| `backup/factory_firmware_16MB.bin` | 工場出荷ファームのバックアップ |

コミット前に、変更ファイルへ `[0-9a-f]{32}|AKfycb|macros/s/|ghp_|Bearer ` が混ざっていないか検索する。デプロイ ID は画面にも出さない（伏せ字にして扱う）。

## 10. ハマりどころ

1. Windows のパス長制限 → PlatformIO core を `C:/pio` に
2. **Git Bash（msys）が起動不能になることがある**（`add_item … failed, errno 1`。自然に戻ることもある）。ビルドは元から PowerShell。
   push の認証もこれで失敗するので、`gh auth token` を環境変数 `GIT_CONFIG_COUNT=1` `GIT_CONFIG_KEY_0=http.https://github.com/.extraheader`
   `GIT_CONFIG_VALUE_0="AUTHORIZATION: basic <x-access-token:トークン の base64>"` で渡し `git -c credential.helper= push origin main`（トークンは表示しない）
3. **フォント**: HOME 用（`ct_font_22/30`）とゲーム用（`ct_font_jp_20/22/40`）は統合しない（リンクエラー）。
   文字一覧だけ更新して `.c` を作り直し忘れると画面で □ になる → `collect_ui_chars.py --check` がフォントの実体まで検査する。
   PowerShell での作り直し: 文字一覧を `Get-Content -Raw -Encoding UTF8` で読み改行を除いて `--symbols` に渡す。フォントの元は `%LOCALAPPDATA%\Temp\coffee_time_fonts`
4. LVGL: ボタンは `pad_all` を 0 にして座標で置く / 日本語は自動折り返しされない（データ側で改行、`LV_LABEL_LONG_CLIP`＋幅と高さ）/
   空で作ったラベルに後から複数行を入れると位置がずれる（入れた後に置き直す）/ 画面遷移中に `lv_obj_del` しない / LVGL タスクのスタックは 8KB / NVS 書き込み中は画面が一瞬止まる
5. シェルのヒアドキュメント経由でソースを書くと `\n` が本物の改行になって文字列が壊れる → スクリプトをファイルに書いて実行するか Edit を使う
6. Open-Meteo は `useHTTP10(true)`、GAS の 302 は新しい接続で GET、curl は `-d` だけ（`-X POST` と `-L` を併用しない）
7. `serlog.py … reset` は native USB ではリセットがかからない → `python -m esptool --chip esp32s3 --port COM8 chip_id` の直後に `serlog.py`
8. SD は GPIO1・2 を LCD の初期化線と共用 → `board->begin()` の後にマウント。SD 使用開始後に LCD へコマンドを送らない
9. iPhone のテザリング: 「互換性を優先」をオン（2.4GHz）、名前は自動で `iPhone (n)` のように**半角スペース入り**になることがある、共有の画面を開いたまま端末のすぐ横に置く。
   失敗理由はログの `[NET] disconnected, reason N`（202 = 認証失敗、201 = 見つからない）、近くの未登録の電波名は `W` で出る
10. `uiwalk.py` の自動操作中にユーザーが端末を触ると進行がずれる。ユーザーが遊んでいそうなときは自動操作をしない
11. **シリアルポートは `serial.Serial()` を作ってから `dtr=False; rts=False` を設定し、そのあと `open()` する**（`serlog.py` / `uiwalk.py` の書き方）。
    `serial.Serial(port, …)` で直接開くと端末が再起動し、続けて送ったゲーム用のタップが HOME の「＋1」に当たって**本物の記録になる**（2026-09-22 に実際に起こした）。
    即席のスクリプトを書かず `uiwalk.py` を使い、ゲーム内のタップの前には `expect:U:view=choose` のように画面を確かめる。`watch:<秒>` で `[DUEL]` `[GAS]` のログも見られる
12. GAS の 302 を追うとき、1 本目の `WiFiClientSecure` を `stop()` してから 2 本目をつなぐ。`http.end()` だけでは keep-alive で TLS のメモリ（約 35KB）を握ったままになり、
    2 本目が `tls=-16`（メモリ確保失敗）→ `HTTP -1` で落ちる。失敗時は `[GAS] … failed: tls=… heap=空き/最大の連続` が出る

## 11. 仕様の要点

- HOME: 日付・時刻（104px）・天気マーク＋気温・「今日」・+1・「残り」（3 以下オレンジ / 0 赤）・ロゴ・SD / Wi-Fi / 電池電圧。背景は 5–11 時 朝 / 11–16 時 昼 / それ以外 夕方（時刻不明の間は夕方）
- +1 は `LV_EVENT_CLICKED` のみ。「残り」を 1.5 秒長押しで「1 回に作る杯数」（設定 5〜15、既定 10）まで補充
- メール: 宛先は「設定」シート A 列（現在 2 人、最終 4 人）。差出人表示 `CaféTamu`。3 杯 = ブラウン系 / 0 杯 = 【至急】赤基調。PC 幅 680px の 2 カラム

## 12. ユーザーの回答待ち

- [ ] 手違いで Drive にできた空のスプレッドシート「COFFEE TIME 記録」の削除可否
- [ ] 通知先の残り 2 人の登録（ユーザーが「設定」シートに入力）
- [ ] 「ログ」シートの試験データを本番前に消すか
- [ ] Jev の契約上の 1 日の上限（設計書の 500 / 1,500 回は設計上の目安）
- [ ] AI DUEL: 8 つのアイコン方式（名前・PIN なし）でよいか。9 人以上やニックネームが要るか
- [ ] 2026-09-22 01:24 の誤記録（今日の杯数 +1、ログシートの `take` 1 行）を消すか

## 13. 作業の進め方

- 変更 → PowerShell でビルド → **ユーザーが遊んでいないことを確かめて**書き込み → `snapshot.py` / `uiwalk.py` で確認 → ユーザーに目視・タッチを依頼 → コミット・push
- **コミットと push はユーザーの了承を得てから**。メール・外部送信・GAS のデプロイなど外に影響する操作は事前に伝える（見本は所有者のみ）
- 設計書由来のデータ（JSON・コア）は書き換えず、表示や調整は生成ツール側・追加ファイル側で差し替える
- 新しい画面や文言は、実装の前に**見本の画像**を作ってユーザーに見せると早い
- コミットの末尾に `Co-Authored-By:`（そのときのモデル名）を付ける
