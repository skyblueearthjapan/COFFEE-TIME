# COFFEE TIME 引き継ぎ書

- 最終更新: 2026-09-24（**第 7 版・全面整理**に、9/23 夜の「残り 0 の ＋1 トースト」と 9/24 の「**Wi-Fi 越しの作業（遠隔コンソール・無線更新）**」を追記。第 6 版までの内容をすべて含む）。最新コミットは `git log`（この文書に固定の番号は書かない）
- リポジトリ: https://github.com/skyblueearthjapan/COFFEE-TIME （ブランチ `main`。作業ツリーは `C:\Users\imaizumi.LINEWORKS-NET\Documents\COFFEE TIME`）
- **この文書だけ読めば「何のアプリか・どこまでできているか・次に何をするか」が分かり、作業を再開できる**ように書いてある。詳細は関連文書へ

> ⚠️ **このリポジトリは PUBLIC。** Wi-Fi のパスワード・合言葉 (TOKEN)・GAS の URL・デプロイ ID・API キー・スマホの名前を、
> このファイルやコミットに書かないこと。秘密情報は Git 管理外のファイル（§9）と GAS のスクリプト プロパティにだけ存在する。

| 関連文書 | 中身 |
|---|---|
| [STORAGE_POLICY.md](STORAGE_POLICY.md) | フラッシュ / 内蔵ファイル領域 / microSD の使い分け、SD の配線と試験、操作ログ、背景画像の方針 |
| [MENU_DESIGN.md](MENU_DESIGN.md) | メニュー（今日の状況・履歴・設定）の設計と NVS のデータ形式 |
| [WEREWOLF_TODO.md](WEREWOLF_TODO.md) / [WEREWOLF_STANDARD_RULES.md](WEREWOLF_STANDARD_RULES.md) | 人狼ワンナイト版の実装メモ / 通常ルールの仕様（ユーザー確定） |
| [AI_GAMES_DIGEST.md](AI_GAMES_DIGEST.md) | エスパー対決・AI DUEL の設計要点、Jev API の疎通試験の結果（2026-09-21。計画は下の各 PLAN が新しい） |
| [AI_DUEL_PLAN.md](AI_DUEL_PLAN.md) | AI DUEL の実装計画（設計書から何を変えたか・画面・保存・端末⇔GAS の約束） |
| [REVERSI_PLAN.md](REVERSI_PLAN.md) | JEV REVERSI の実装計画 |
| [POKER_TABLE_PLAN.md](POKER_TABLE_PLAN.md) | POKER TABLE（トランプ 4 種）の実装計画。**§5b にテキサス・ホールデムのルール** |
| [ESPER_STAGE2_PLAN.md](ESPER_STAGE2_PLAN.md) | エスパー対決 第 2 段階（Jev）の実装計画 |
| `参考データ/` | 企画書・UI イメージ・ゲームの設計書と付属パッケージ（zip）。**設計書由来の JSON・コアは書き換えない** |

---

## 1. 最初にやること（5 分）

1. ESP32 の COM ポートを確認（これまで **COM8** = native USB、COM9 = UART 側）
   ```powershell
   Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match 'COM\d+' } | Select Name
   ```
2. 実機の画面を見る: `python tools/snapshot.py COM8 now.png`
3. **書き込む前に必ず、ユーザーが遊んでいないか確かめる**: `python tools/serlog.py COM8 10 send=GDVUJK`（6 ゲームぶんの状態が出る）。
   `[WOLF] … phase=0`、`[DET] … case=-`、`[ESP] view=0`、`[DUEL] view=-`、`[REV] view=-`、`[CARDS] view=-`（または `paused`）なら待機中
4. **Wi-Fi につながっているときは、+1・補充・シリアルの `T`/`R` が本物の記録になり、「残り」が 3 / 0 になった瞬間に登録者へメールが飛ぶ。** 試験で杯数を動かさない。
   自動操作（`tools/uiwalk.py`）は必ず `expect:` / `expect:Q:depth=2` で画面を確かめてからタップする（§10-11）。偽装タップ（`P`）からの ＋1 はファームが記録しないが、それに頼らない
5. ビルド・書き込みは **PowerShell から**。Git Bash（msys）はこの PC でときどき起動不能になる（§10-2）
6. ユーザーへの説明は日本語・平易に。作業は自分で進め、ユーザーには目視・タッチ・秘密情報の入力・Google の承認だけを頼む。**質問は選択式で出す**と答えやすい（2026-09-23 に要望）
7. §4 の残タスクと §12 の未回答事項を確認する

## 2. プロジェクトとユーザー

社内カフェに置く**持ち運びできる**タッチ式コーヒーカウンター端末「COFFEE TIME」。コーヒーを 1 杯取ったら「+1」。今日の杯数・残り杯数を表示し、
残り 3 杯（通常）と 0 杯（至急）で担当者にメール（Google Apps Script 経由）。休憩時間に遊べる **6 つのゲーム**（AI DUEL・エスパー対決・リバーシ・POKER TABLE・喫茶「余白」の事件簿・閉店後の人狼会）を載せる。
AI を使うゲーム（AI DUEL・エスパー・リバーシ・POKER TABLE）は TypeSafe **Jev** を GAS 経由で呼び、通信できないときは端末内の AI/ルール方式で最後まで遊べる。

- ハード: **Waveshare ESP32-S3-Touch-LCD-2.8C**（480×480 丸型 / ST7701 RGB / GT911 タッチ / Flash 16MB / PSRAM 8MB / microSD 32GB / LiPo 1500mAh / RTC PCF85063 / ブザー）
- ユーザー（今泉さん）: 組み込み・デプロイは初めての非エンジニア。「理解を深めながら、作業はできる限りエージェント側で」。ゲームは「ディレクターとして完了まで」エージェントに一任する方針。
  どこに保存され、どこでビルドされ、どこにデプロイされたかを知りたがる。**画面の見本（画像）を見せると判断が早い**（デスクトップに PNG を置いて開く）
- アカウント: Google は会社の Workspace、GitHub は `skyblueearthjapan`

## 3. 現在の状態（2026-09-23 夜）

| 分野 | 状態 |
|---|---|
| カフェ機能（+1・補充・日付リセット・NVS 保存） | ✅ 運用できる状態。HOME の見出しは「今日」「残り」、天気はマーク＋気温、補充の表示は「補充しました：n 杯」。**残り 0 で ＋1 すると 2 秒のトースト**（9/23 夜。ユーザーが実機で表示を確認済み） |
| 通知（GAS・メール） | ✅ デプロイ **@14**（URL 不変。@7〜@14 はゲーム用の受け口の追加だけで、コーヒー側は無変更）。ゲージは端末から届く `max` に連動、文面は「『残り』の数字を長押し」。0 杯の【至急】は見本のみ確認（実機 1→0 は未確認） |
| Wi-Fi | ✅ 3 件まで登録（WiFiMulti）。自宅では **iPhone のテザリングを端末のすぐ横に置けば接続**（-47〜-69dBm）。自宅の Wi-Fi にも弱く（-86〜-90dBm）つながる。ゲーム中は省電力を切る（§10-13）。受信感度の弱さ（ケースの金属リングが原因と推定）は未対策 |
| 時計 | ✅ NTP + RTC。RTC は**電源が完全に切れると時刻を失う**（電池スイッチ OFF + USB 抜き）。設定画面と `tools/settime.py` で合わせられる |
| microSD | ✅ 操作ログ（起動理由・+1・補充・日付変更・時刻設定・ゲーム 1 回・取り消し）。シリアル `L` で読める |
| メニュー（今日の状況・履歴・設定） | ✅ 実装・全画面を実機で確認。履歴の週は**月〜日**、ゲームの回数つき（6 行）。設定 7 項目。未確認: 自動の暗転、再起動をはさんだ保存、操作音（ブザーの極性） |
| 人狼 ワンナイト / 通常ルール | ✅ 実装・PC 上の試験 全合格（20 万局）。席ごとのキャラクター、物語の導入、役職の絵。**ユーザーの指での確認待ち**（秘密の画面は自動操作で開けない） |
| 探偵（ひとり推理） | ✅ 完成形（Jev は入れない＝ユーザー決定）。第 1 話を実機で通し確認。第 2・3 話の通しは未実施 |
| エスパー対決 第 1 段階 + **第 2 段階（Jev）** | ✅ 4 モード（ミニ/レギュラー/フル/超難関）。Jev が「次の質問」と「最終予想」を選ぶ。実機で Jev 対戦（レギュラー・フル・ミニ）と、届かないときに「ルール対戦」へ落ちる経路を確認。**きろくが毎回消えていた不具合を 9/23 に修正**（§7.3） |
| AI DUEL | ✅ じゃんけん 10 回勝負。8 アイコン + ゲスト、記録は端末、Jev は GAS 経由（統計 AI に自動で落ちる）、相手別（Jev / 統計AI）の勝敗。実機で多数回確認 |
| JEV REVERSI | ✅ 6×6 / 8×8、JEV / JEV PRO / CASUAL / 端末AI、保存・再開、投了。実機で各モード 1 局以上完走 |
| POKER TABLE（名前はユーザー指定） | ✅ 4 卓（POKER = **テキサス・ホールデム（既定）/ 5 カードドロー**、GOPS 7/13、THIRTY-ONE、BACCARAT OPEN/CLASSIC）。8 アイコン + ゲスト。初回だけの説明ページ。実機で 4 卓とも端末 AI と Jev の両方で試合を完走 |
| 「考え中」の演出 | ✅ Jev 待ちの共通部品 `ui/Thinking`（回る印 + 言葉の切り替え）を 4 ゲームに導入。実機で確認 |
| Wi-Fi 越しの作業 | ✅ 9/24。**USB なしで**ログ・状態・スクショ・自動操作（`net`）と書き換え（`tools/ota.py`、4.5MB を約 22 秒）。会社の Wi-Fi で PC から端末に届くことを確認。合言葉つき（§8「遠隔コンソール」） |
| フラッシュ / RAM | 約 68.2%（アプリ領域 6.25MB 中 約 4.47MB）/ 静的 RAM 27.6%。ゲーム用フォント 899 字。内蔵メモリの空きは約 60〜110KB（TLS 1 本で約 35KB。§4「気になる点」） |

## 3b. これまでにやったこと（時系列。詳細は各節）

- 〜9/20: 基板の初期化・HOME（時計・天気・杯数）・GAS 通知・NVS 保存・Wi-Fi・RTC・人狼ワンナイト。
- 9/21: microSD の操作ログ、メニュー 3 画面、人狼の通常ルール、探偵、エスパー第 1 段階（4 モードの調整）、Jev の疎通試験（GAS @6）、引き継ぎ書 第 6 版。
- 9/22: **AI DUEL**（設計書を端末 1 台向けに軽量化して実装。GAS @7〜@8）、通信の不具合 2 つを修正（TLS のメモリ不足 / 省電力の取りこぼし）、**JEV REVERSI**（GAS @9）。自動操作の誤タップで本物の ＋1 が 1 件入り、道具に画面確認を追加。
- 9/23: **POKER TABLE**（GAS @10〜@11）、**エスパー第 2 段階**（GAS @12〜@13）、ゲーム一覧の並び替え（ユーザー指定）、AI DUEL の相手別勝敗、2 度目の誤 ＋1 → 偽装タップからの記録をファームで禁止、
  誤記録の削除（シリアル `Y!undo` / `Y!hist`）、POKER TABLE の初回説明、「考え中」の演出、**テキサス・ホールデム**（GAS @14）、エスパーのきろくが消える不具合の修正。すべて独立レビュー → 反映 → コミット済み。
  夜: シートの誤記録 4 行を jev-hands で削除。**残り 0 の ＋1 トースト**を実装（下記）。
- **9/23 夜（別セッション）の作業内容**:
  1. 引き継ぎ書を読み、§4 の 0 番を実装。`HomeScreen.cpp` の `addOneCup()` で、HOME 表示中かつ `prev == 0` のときだけ `showToast("残りは 0 のままです。\n補充したら『残り』を長押し")`。記録・GAS 送信・SD ログは無変更
  2. HOME の `showToast` のタイマーを 1 本の使い回し（`s_toast_timer` + `lv_timer_reset`）にした。前は出すたびにタイマーを作っていたので、続けて出すと前のタイマーで新しいトーストが早く消えた。トーストの文字は中央ぞろえ（2 行になるため）
  3. HOME 用フォント `ct_font_22.c` / `ct_font_30.c` に 11 字（。『』すでのはらを押長）を追加して作り直し（PowerShell から `lv_font_conv`。§10-3）。`collect_ui_chars.py --check` 合格
  4. ビルド（6 分）→ `serlog.py … send=GDVUJKQ` で全ゲーム待機中・HOME（depth=1）を確認 → 書き込み → スクリーンショットで HOME の正常表示を確認（今日 0・残り 0・電池 4.17V）
  5. 独立レビュー（code-reviewer）: 指摘なしで承認。低の注意 1 件 = トーストの幅を固定していないので、将来長い文言を入れると丸い画面の端に届く（今の文言は 322×68px で余裕あり）
  6. 見本画像をデスクトップ `残り0のトースト見本.png` に置いた（ユーザー了承済み）。秘密の検査 → コミット `70f85ba` → push

- **9/24 の作業内容**: 「残り 3 のメールが来ない」→ GAS・メール送信は正常（見本メールで確認）、ユーザーの受信箱に遅れて届いていた（原因は未特定。端末の再送 30 秒ごと or Google の配信遅延）。
  続けて**遠隔コンソール（TCP 2323）と無線更新（TCP 2324）**を実装（executor）→ USB で最後の書き込み → Wi-Fi 越しに状態・スクショ・uiwalk・無線更新・合言葉違いの拒否・ゲーム中の更新拒否を確認 →
  独立レビュー（承認・修正 2 件: 無線更新後の再起動が「見張り」になっていた → 再起動前に `WiFi.disconnect(true)` で「software」に / `s_game_active` を volatile）→ 無線更新で反映・確認 → コミット

## 4. 残タスク（優先順）

### すぐ着手できるもの
0. ✅ **残りが 0 のときの「+1」**（ユーザー決定 9/23 夜: **押せるまま + トースト**。9/23 21 時台に実装・書き込み・独立レビュー済み）: 「今日」+1、「残り」は 0 のまま、GAS には `left=0` で送る（メールは出ない）。
   HOME が表示中で `prev == 0` のとき 2 秒のトースト「残りは 0 のままです。／補充したら『残り』を長押し」（2 行・中央ぞろえ。ゲームのカフェ画面からの ＋1 では出さない）。
   トーストのタイマーは 1 本を使い回す（続けて出しても早く消えない）。HOME 用フォント `ct_font_22/30` に 11 字を追加。
   実機での表示はユーザーが確認済み（9/23 夜）。見本はデスクトップの `残り0のトースト見本.png`。朝は必ず残り 0 から始まるので、補充前の最初の ＋1 で毎朝出る
0b. ✅ **残り 0 のトーストの実機確認**: 9/23 夜に**ユーザーが実機で表示を確認済み**。私は本物の ＋1 を押していない（シートに 1 行残るため）。
   確認点: 2 行が中央にそろって出る・2 秒で消える・「今日」が +1 され「残り」は 0 のまま・メールが出ない。ゲームのカフェ画面からの ＋1 では出ないこと。
   自動操作で確かめたい場合は、ユーザーの了承を得てからシリアル `T`（本物の記録）→ `Y!undo` で端末側を戻す（シートの行は残る）
1. **ユーザーの試遊と感想**（全部の新しいゲーム）。特に: ホールデムが「ブラインド無し・参加点 1」で物足りないか、Jev の待ち（1 手 3〜7 秒）が演出で許せるか、8×8 リバーシの 39px マスが指で押せるか、
   AI DUEL のアイコン方式（了承済み）、POKER TABLE の初回説明が分かりやすいか
2. **ホールデムの残り**: 場の札をめくる演出（180ms ずつ）とバカラの説明の切れは 9/23 夕方に修正・実機確認・コミット済み。残りはユーザーの感想（ブラインド無しで物足りないか）だけ
3. **POKER TABLE の未確認**: GOPS 13 枚版と BACCARAT CLASSIC の通し、120 秒放置のひと休み、「内訳」（Jev の確率）画面、ドローの初回説明が 1 回出ること（`seen` の 5 ビット目）、
   受入試験（原本 124 項目 + UI 67 項目は設計一式の中）。試験データ: アイコン「はぐるま」(p7) に私の試合が多数、`CardsResults` シートに試験行
4. **AI DUEL の未確認**: 3 分放置のひと休み、「記録を消す」長押し（自動操作では長押しできない）、ゲーム中のカフェ画面、オフライン時の表示、20 ラウンド以上たまったあとの「癖」カード。
   「はぐるま」には私の試験のあとにユーザー本人の対戦が入っている（消さない）。`DuelRounds` シートに guest / p7 の試験行
5. **JEV REVERSI の未確認**: 白番・おまかせで 1 局通し、「端末AIへ切り替える」からの続行、保存に失敗したときの表示（起こせていない）、日付が変わったときの `cup_ext` の切り替え、受入試験 88 項目
6. **エスパー第 2 段階の未確認**: `mixed` の経路、取り消し直後の返事が捨てられること、「AI が考え中…」画面の見た目（自動操作では一瞬で撮れなかった）。AI が外れたときの答え合わせ画面、3 分放置のひと休み
7. 探偵の第 2・3 話を自動操作で通す（`tools/uiwalk.py`。終わったら `X` で進み具合を初期化）
8. メニューの未確認 3 点（自動の暗転・再起動後の保存・操作音）。ブザーは EXIO8、極性は `Buzzer.cpp` の定数 1 つ
9. 人狼（両モード）のユーザーによる指での確認（押している間だけ役職が出る／離すと消える／2 本指で出ない／8 秒で自動で隠れる／通常ルールの 2 日目以降）

### 中期
10. 待ち時間そのものを縮める: GAS の往復（4〜7 秒）が主因。GAS の処理を減らす・Jev の返事を控える・先読みできる局面を増やす、など
11. 背景画像の作り直し（朝と昼の見分けがつきにくい・夜を追加）と SD からの読み込み、USB 経由で SD にファイルを送るツール（[STORAGE_POLICY.md](STORAGE_POLICY.md) §9）
12. LINE WORKS 通知、省電力・筐体、遊び要素（達成アニメ・季節テーマ）、受入試験（人狼 99 項目・探偵 72 項目）
13. ネットワーク処理を別タスクにする（今はメインループが GAS のやり取りで最大 14.5 秒止まり、シリアルコマンドと NVS 保存がその間遅れる）

### 気になる点（未解決）
- **残り 0 のトーストは毎朝出る**（朝は必ず残り 0 から始まるため、補充前の最初の ＋1 で出る）。仕様どおりだが、うるさいと感じるかはユーザーの感想待ち
- **補充の誤操作**: 「残り」の 1.5 秒長押しは、持ち運びで右端に指がかかると誤って働く（実際に 1 回起きた）。3 秒長押し＋進み具合の表示 / 取り消しを提案済み、ユーザーは「とりあえず大丈夫」
- 送信キューは RAM のみ（長い Wi-Fi 断＋再起動で未送信分が消える）。内蔵ファイル領域へ逃がす案あり
- HTTPS は `setInsecure()`（証明書検証なし）。個人情報（名前・PIN）を持たない作りにしたので当面このまま
- **内蔵メモリの余裕が小さい**: TLS 1 本で約 35KB。空き約 60〜110KB・最大の連続 31〜51KB。大きな静的配列を足すときは PSRAM に置く（各ゲームの試合の状態は PSRAM）
- GAS の返事が 10〜20 秒かかる瞬間や、Google の一時エラーの HTML が返る瞬間がある。端末側は自動のやり直しで吸収するが、連続すると「通信の状態」画面（リバーシ・POKER TABLE）／ルール方式（エスパー・AI DUEL）になる
- 人狼 7 人以上は手がかりが薄い（配役は確定済みだが、遊んで物足りなければ騎士・霊媒師を検討）
- エスパーで使わない候補は無い（超難関が 128 こ全部を使う）が、レギュラー・フルは各分野の先頭から採っているだけ。入れ替えたければ `tools/gen_esper_data.py` の `PLAY_MODES`
- スプレッドシート「ログ」に試験データが入っている。9/23 に clasp のログイン切れに気づかず試験を 2 回送り、旧デプロイがコーヒーの記録として扱って **`cards` という行が 2 行**入った（杯数は空・メール無し）。
  誤タップの `take` 2 行（9/22 01:24・9/23 10:05）と合わせて、**ユーザーが手で削除する**ことになっている（依頼済み・未確認）
- 誤記録の端末側は消した（`Y!undo` / `Y!hist`）が、HOME の「最後の 1 杯 10:05」の表示だけは 9/23 中は残る
- エスパーのきろくは 9/23 以前のぶんが残っていない（不具合で毎回消えていた。ユーザーの 9/21 の 4 局も含む）
- GAS の `Object.hasOwn` は原本のコードが使う ES2022 の関数。Apps Script の V8 に無い場合に備えて `Cards.gs` に代替を置いた（実機では問題なく動いている）
- GAS の 1 日の Jev 呼び出し上限（スクリプト プロパティ `JEV_DAILY_MAX`、無ければ 1500）は 4 ゲーム共通で数える。契約上の本当の上限は未確認

## 5. ユーザーと決めたこと（変えないこと）

- **保存場所**: カフェ機能と人狼は SD なしで 100% 動く。大きなゲーム素材はまず内蔵ファイル領域、SD はログと「無くても動く大きなもの」。区画は変えない（OTA の余地を残す）
- **朝は必ず 0 杯から**（夜の残りは配るかアイスコーヒー）。日付が変わったら「残り」= 0 で固定、設定項目にもしない
- **探偵に Jev は入れない**。AI（TypeSafe Jev）を使うのはエスパー対決・AI DUEL・リバーシ・POKER TABLE。API キーは端末に置かず GAS のスクリプト プロパティ `JEV_API_KEY`
- **人狼**: ワンナイトと通常ルールを開始時に選ぶ。通常ルールは 村人・人狼・占い師のみ、初日は襲撃なし、抜けた人の役職は終了まで非公開。
  配役（確定）: 4〜6 人 = 人狼 1・占い師 1 / 7〜8 人 = 人狼 2・占い師 1 / 9〜10 人 = 人狼 2・占い師 2。**どの画面でも「いま何をする場面か」を見出しに書く**。通常ルールの導入文はワンナイトの語り口を正として書く
- **エスパー対決の 4 モード**（AI の勝率はユーザー指定）: ミニ 10 こ・3 問 = 80% / レギュラー 20 こ・4 問 = 70% / フル 64 こ・5 問 = 50% / 超難関 128 こ・5 問 = 25%。
  選んだものは「メモ」として質問の画面にいつも出す（画面表示だけ。AI には渡さない）
- **メニュー**: アイコン 4 つ（今日の状況・履歴・設定・ゲーム）。履歴の週は月曜はじまり。ゲームは回数だけを数える（役職・勝敗・正誤は記録しない）。「お知らせ」画面と Wi-Fi のパスワード入力は入れない
- HOME のメニューボタンは右斜め下。日付は日本語、COFFEE TIME のロゴは英語のまま
- **ゲーム一覧の並び順**（2026-09-23 指定）: **AI DUEL → エスパー対決 → リバーシ → POKER TABLE → 喫茶「余白」の事件簿 → 閉店後の人狼会**。履歴のゲームの表も同じ順
- **AI ゲームの共通の作り**（ユーザーから「ディレクターとして完了まで」と一任されて決めた。理由は各 PLAN の §2）: 記録の正本は端末（NVS）、シートは写し。本人の選択は **8 つのアイコン + ゲスト（名前・PIN なし。2026-09-23 に「このままでよい」と回答）**。
  相手は Jev がつながれば Jev、だめなら端末内の AI/ルール方式に自動で落ちる。PIN・管理画面・多数のシート・コミットハッシュは作らない
- **待ち時間**（2026-09-23 回答「長い。演出で紛らわせたい」）→ 共通の「考え中」表示。偽の％や「AI 理解度」のような表示は出さない
- **POKER TABLE**（2026-09-23）: 名前は「POKER TABLE」。ホールデムは必須（ドローも残す）。初めての人向けの説明を卓ごとに付け、2 回目以降はスキップできる
- **誤記録**（2026-09-23 回答）: 消す

## 6. 全体構成とデータの流れ

```
【PC】COFFEE TIME フォルダー ──git push──▶【GitHub】COFFEE-TIME
   │ PlatformIO でビルド → USB(COM8) で書き込み          │ clasp -u work push / update-deployment
   ▼                                                     ▼
【ESP32】画面・杯数・統計・設定(NVS)・操作ログ(SD)   【Google Apps Script】（スプレッドシートに紐づけ・デプロイ @14）
   ├─ Wi-Fi → NTP / Open-Meteo 天気（千葉市・15 分ごと）  ├─ 「ログ」シートに 1 行追記
   └─ +1・補充・日付変更 ──HTTPS POST──▶ Web アプリ       ├─ 残りが 3 / 0 に減った瞬間 →「設定」シートの宛先へ HTML メール
   └─ ゲームの依頼箱 net::gasRequest ──HTTPS POST──▶      ├─ event=jevtest → TypeSafe Jev の疎通（`gas/Jev.gs`）
                                                          ├─ event=duel    → Jev に次の手の予測 / `DuelRounds` シート（`gas/Duel.gs`）
                                                          ├─ event=reversi → 棋譜から局面を再計算して Jev に 1 手 / `ReversiResults`（`gas/Reversi.gs` + `ReversiShared.gs`）
                                                          ├─ event=cards   → 観測を検査して Jev に 1 手 / `CardsResults`（`gas/Cards.gs` + `CardsContract.gs` `CardsGate.gs` `CardsHoldem.gs`）
                                                          └─ event=esper   → 候補と質問の ID を英文に置き換えて Jev に予想と次の質問（`gas/Esper.gs` + 生成した `EsperCatalog.gs`）
```

### ESP32 → GAS の送信内容（コーヒー。JSON）
```json
{"token":"…","device":"coffee-xxxxxx","event":"take|refill|newday","id":"<起動乱数>-<連番>",
 "taken":7,"left":3,"prev":4,"max":10,"rssi":-80,"ts":1789800000}
```
- 通知条件: `event=="take" && prev>left && (left==3 || left==0)`。`id` で 6 時間の重複排除。失敗時は 30 秒ごとに再送（RAM キュー 32 件）
- GAS の応答は 302。転送先は**ヘッダーを引き継がない新しい GET** で読む（端末も PC の道具も同じ）。1 本目の TLS を `stop()` してから 2 本目（§10-12）
- 「ログ」シートの列: 受信日時 / 端末日時 / 端末ID / イベント / 今日の杯数 / 残り杯数 / Wi-Fi強度 / イベントID

### ゲームの依頼箱（`NetService.h`）
- `net::gasRequest(req, body_json, detached)` … LVGL タスクから 1 件だけ預ける（待たない）。`gasTakeResult` で返事を受け取る。`detached=true` は送りっぱなし（記録用。返事は捨てて箱を空ける）。`gasCancel` は bool
- 送受信はメインループ側の `net::poll`。POST 接続 5 秒 + 読み取り 6.5 秒、転送先 GET 接続 5 秒 + 読み取り 5 秒（読み取り失敗は 1 回だけ内側でやり直す）。GAS の返事は 768 バイトまで
- 各ゲームは自分の待ち時間で見切る（AI DUEL 9 秒 / リバーシ 12 秒 / POKER TABLE 12 秒 / エスパー 10 秒・全体 13 秒）。失敗は 1 回だけ同じ要求を自動で送り直す（GAS は控えを返すので Jev を呼び直さない）
- ログは 1 行で理由まで出る（`[GAS] req=12 -> HTTP 200 (96 bytes)` / `connect failed: tls=48 heap=…` / `[DUEL] req=5 -> jev 4094ms` / `-> stats 7391ms (http -11)`）。URL・合言葉・本文は出さない

### 端末内の保存（NVS。合計 20KB の区画）
| 名前空間 / キー | 中身 |
|---|---|
| `cup` / `ymd` `taken` `left` | 杯数（最重要。ほかの保存が壊れても触らない） |
| `cup` / `today` | 今日の時間帯別の杯数・補充回数・最後の 1 杯 / 補充の時刻・ゲーム 0〜3 の回数（48B・版と CRC。**形は変えない**） |
| `cup_hist` / `days` | 直近 35 日の輪（杯数・補充・ゲーム 0〜3 の回数）＋累計（588B。**形は変えない**） |
| `cup_ext` / `today` `days` | **ゲーム 4 番以降**（リバーシ・POKER TABLE）を遊んだ回数（今日 16B / 35 日の輪と累計 308B。4 枠あり）。壊れていても杯数には影響しない |
| `cfg` / `v1` | 明るさ・暗くするまでの時間・操作音・1 回に作る杯数（12B） |
| `ct_wolf` / `meta` `mode` | 人狼の公開メタ（20B）と前回のモード。**秘密は RAM のみ** |
| `ct_det` / `prog` | 探偵の進み具合（初回の結果・ヒント・読了・記念品。20B） |
| `ct_esp` / `stat` | エスパーのきろく（4 モード × AI 勝 / 人 勝 / 最少問数。40B。CRC は末尾 4 バイト＝9/23 修正） |
| `ct_duel` / `p0`〜`p7` `meta` | AI DUEL の 1 人ぶんの統計（第 2 版 512B。第 1 版 488B も読めて次の保存で第 2 版に。手の回数・遷移・直近 50 手・予測の成績・相手別の勝敗）× 8 と対戦の通し番号 |
| `ct_rev` / `game` `stats` | リバーシの進行中の 1 局（`REV1` 形式・294B 以内。1 手確定ごとに保存）と、きろく（盤 2 × 相手区分 5 × 勝・負・分。68B） |
| `ct_cards` / `stats` `seen` | POKER TABLE のきろく（8 人 × 4 卓 × 勝・負・分・中止 + バカラの的中。296B）と、初回説明を見たかの 5 ビット（8B）。**試合の状態は PSRAM だけ**（電源が切れると消える） |

SD: `/coffee_time/log_YYYYMM.csv`（時刻不明の間は `log_nodate.csv`）。カードが無くても全部動く。フォーマットは絶対にしない。

## 7. ゲーム

共通: `ui::ScreenManager` に 1 画面を載せ、局面ごとに中身を作り直す方式。破棄時に静的ポインタとタイマーを片付ける（`lv_event_get_target(e) != s_screen` の見張りつき）。
「カフェ / 一時停止」の画面からコーヒー＋1（本物の記録）。無操作でひと休み（人狼・探偵・エスパー・AI DUEL は 3 分、リバーシ・POKER TABLE は 2 分）。
ゲーム中は自動の暗転をせず Wi-Fi の省電力も切る（`display::setGameActive`）。終わったら `cup::stats::gamePlayed(GameId, note)` で 1 回と数える。
本人（AI DUEL・POKER TABLE）は 8 つのアイコン（カップ/まめ/ほし/つき/たいよう/はっぱ/くも/はぐるま = p0〜p7）+ ゲスト。試験には p7「はぐるま」を使ってきた。

### 7.1 人狼（`firmware/src/app/games/werewolf/`）
- ワンナイト: 設計書のコア `core/*.hpp` を**無改変**で使用。通常ルール: 新しいコア `core_std/werewolf_std_core.hpp`。PC 上の試験 `python tools/run_wolf_std_checks.py`（20 万局）
- 画面と進行 `WerewolfGame.cpp`、ハード接続 `WerewolfPort.*`、文言は `data/content.ja.json`（書き換えない）＋ 追加分 `data/content.local.ja.json` → `python tools/gen_game_data.py` → `WerewolfContent.*`
- **覗き見防止**: 秘密は押している間だけラベルに入れ、離したら空にして `lv_refr_now` → 40ms → 次へ。判定はタッチの生データ（`lvgl_v8_port.*`）。固まったら見張りタスクがバックライトを切る。秘密は NVS・SD・シリアルに出さない。自動タップ（`P`）では開けない
- ゲーム一覧（`WerewolfUI.cpp` にある。歴史的な置き場所）: 6 段 300×44・間隔 52。タップ位置 AI DUEL `240,110` / エスパー `240,162` / リバーシ `240,214` / POKER TABLE `240,266` / 探偵 `240,318` / 人狼 `240,370` / もどる `240,428`

### 7.2 探偵（`games/detective/`）
- 脚本は `data/catalog.author.json`（書き換えない）。`python tools/gen_detective_data.py` が 13 文字 × 6 行で折り返し・ページ分け。3 話。証拠 3 つ → 3 択 → 答え合わせ → 解説 → 結末 → 記念品。進み具合は NVS `ct_det/prog`。試験で初見を消費したらシリアル `X` で初期化

### 7.3 エスパー対決（`games/esper/`）
- カタログ `data/catalog.json`（128 候補・146 質問。書き換えない）→ `python tools/gen_esper_data.py` → `EsperContent.*`。コア `core/esper_core.hpp`（モード表の 0〜2 番は設計書のモード、3〜6 番が遊び用の 4 モード）
- PC 上の試験 `python tools/run_esper_checks.py`（手本 168 局と完全一致 + 4 モードの AI 勝率 80.0/70.0/46.9/24.4% + 第 2 段階の `applyAdvice`・要求 JSON の検査）
- メモ: 「決めた」ものを `s_memo` に持ち、質問の画面に「メモ：○○」で出す。**画面表示だけ。AI には絶対に渡さない**（要求 JSON は ID と ASCII だけ、と検査で強制）
- **第 2 段階（Jev）**: 回答のあと候補が 2 つ以上あれば `event:"esper"`（セッション ID・revision・モード・履歴・残った候補と安全な質問の **ID だけ**・残り問数）を送り「AI が考え中…」を最大 10 秒（全体 13 秒）。
  GAS が `EsperCatalog.gs`（`tools/gen_esper_gas.py` が catalog.json から生成）の英文に置き換えて Jev に「最終予想」と「次の質問」を 1 回で聞く（候補 17 こ以上の質問の途中では最終予想を聞かない）。
  返事は検証して `Engine::applyAdvice`（現在の shortlist 内の質問・候補内の予想・revision 一致のときだけ）。だめなら黙って基準（ルール方式）。時間切れのあとは 15 秒ほど聞かない。処理系 `jev` / `mixed` / `rule` を結果画面と SD ログに出す。
  Jev に聞く回数は少なめが正常（安全な質問が 2 問以上のときだけ「次の質問」を聞く）
- **きろくが消えていた不具合**（9/23 修正）: 4 モード化のとき CRC の位置が 28〜31 バイト目のままで、読み込みのたびに壊れていると判定されていた → 末尾 4 バイトへ。`static_assert` で固定
- 状態表示 `V`: `[ESP] view=6 mode=regular phase=1 q=Q124 asked=3/4 skip=0 undo=0 remain=2 guess=- reason=0 verdict=0 engine=mixed pending=1 try=1`（view: 0 モード 1 一覧 2 カード 3 じゅんび 4 考え中(端末) 5 AI 待ち 6 質問 7 ヘルプ 8 やめる確認 9 予想 10 結果 … 15 カフェ 16 ひと休み 17 メモ）
- 自動操作 `python tools/esper_autoplay.py COM8 <dir>`（じゅんび画面から 1 局）。タップ: モード `240,140/196/252/308`、一覧の 1 件目 `240,165`（2 件目 `240,215`）、カードの決めた `318,329`、はじめる `240,356`、はい `158,314` いいえ `322,314`、予想の 正解 `158,326` ちがう `322,326`、結果の もう一度 `240,343`（モード画面へ戻る）、きろく `290,367`

### 7.4 AI DUEL（`games/duel/`）
- 計画 [AI_DUEL_PLAN.md](AI_DUEL_PLAN.md)。コア `core/duel_core.hpp`（勝敗・統計・統計 AI の式・最善手・癖カード・保存形式・Jev に渡す集計）、保存 `DuelStore.*`、画面 `DuelGame.*`。PC 上の試験 `python tools/run_duel_checks.py`（参考ロジックの手本 539 ラウンドと 1e-9 以内で一致）
- 1 ラウンド: 結果を出した瞬間に次の回の予測を先読み → 端末内の統計 AI は必ず計算 → 記録が 1 回以上あれば GAS 経由で Jev → 9 秒以内に届けば Jev の確率で「期待得点差が最大の手」。いちど決めた手は変えない。決まるまでボタンは無効
- 記録のシート送信は対戦の終わりに 1 回（最大 10 件）。Jev に渡すのは集計だけ。相手別（Jev / 統計AI）の勝敗をラウンド単位で記録（プロフィールと結果画面）。混ざった対戦の見出しは「相手の勝ち！」
- 実測 Jev 1 回 3.7〜7.3 秒。状態表示 `U`（相手の手は公開後だけ `ai=`。末尾 `vsjev=勝-敗-分 vsstats=…`）
- タップ: はじめる `240,266` → ゲスト `240,319`（p0〜p3 は y=146 の x=112/197/283/368、p4〜p7 は y=242）→ 対戦する `240,243` → はじめる `240,300` → グー `120,264` チョキ `240,264` パー `360,264` → 次へ `240,345`／やめる `301,402` → 確認のやめる `240,284`。
  結果画面: もう一度 `240,283` 癖を見る `240,337` おわる `240,389`。プロフィールの もどる `240,401`、入口の ゲーム一覧へ `240,387`。**カフェ画面・ひと休み画面の「＋1杯」（`240,241` / `240,305`）は本物の記録**

### 7.5 JEV REVERSI（`games/reversi/`）
- 計画 [REVERSI_PLAN.md](REVERSI_PLAN.md)。設計一式のコア `core/reversi_core.hpp` `reversi_session.hpp` は**無改変**（`run_reversi_checks.py` が SHA-256 で確かめる）。追加分 `core/reversi_extra.hpp`。文言は `data/content.ja.json` ＋ `content.local.ja.json` → `python tools/gen_reversi_data.py`
- ルール・反転・パス・勝敗は端末。Jev は合法手から 1 手を選ぶだけ（JEV / JEV PRO / CASUAL / 端末AI）。合法手 1 つ・パスは GAS に聞かない。返ってきた手は端末でも検証。通信失敗 → 内側の読み直し → 自動で同じ棋譜を再送（GAS の控え）→「通信の状態」画面（同じ手の結果を確認 / 端末AIへ切り替える / 保存して後で続ける）。端末 AI に切り替えた局は「混在」
- 盤 312×312・左上 (84,84)。6×6 `x=110+52×列, y=110+52×行`、8×8 `x=103+39×列, y=103+39×行`。マスを選んで「置く」`240,428`。候補 `324,422`（1 枚目 `180,169`）、操作 `156,422`。結果: もう一局 `240,323`、**カフェへ `240,382` は HOME に戻る**
- 状態表示 `J`: `[REV] view=board phase=wait n=6 mode=jev human=B ply=7 side=W B=5 W=4 closure=active local=0 pending=1 try=1 saveerr=0 last=C2:H`
- 再起動後の「つづきから」は一時停止画面に入る（そこの「＋1杯」`240,305` とカフェ画面の `240,241` は本物の記録）

### 7.6 POKER TABLE（`games/cards/`。原本の名前は CAFE CARDS）
- 計画 [POKER_TABLE_PLAN.md](POKER_TABLE_PLAN.md)。原本のコア `core/cards_core.hpp` `local_policy.hpp` は**無改変**（`run_cards_checks.py` が確かめる）。追加分 `core/cards_extra.hpp`（4 卓 + ホールデムの進行・観測 JSON・公開イベント・不変条件・端末 AI）。画面 `CardsGame.cpp`、保存 `CardsStore.*`
- 4 卓: **POKER = テキサス・ホールデム（既定。持ち点 200・手札 2 + 場 5・単位 2/2/4/4・上乗せ各段階 2 回・5 ハンド。§5b）/ 5 カードドロー（持ち点 100）**、GOPS（1〜7 or 1〜13 の札で得点札を競る。同点は消滅）、
  THIRTY-ONE（場の 3 枚と 1 枚ずつ交換・3 ハンド・20 手上限・通常ターンにパス無し・ノックで相手に最後の 1 手）、BACCARAT（OPEN / CLASSIC・5 回の予想。PLAYER/BANKER は札の側の名前）。相手は JEV か 端末AI
- 同時選択（GOPS の入札・ポーカーの交換・バカラの予想）は AI の選択を先に確定してから人間の確定ボタンが有効になる。ログは `sealed` としか出さない。**選ぶ → 確認画面 → 決定**の二段階
- Jev: 端末が原本 `buildObservation` の形（ホールデムは §5b の形）で観測を作り、GAS が原本の gate / `CardsHoldem.gs` で検査して Jev に選ばせる。実測 1 手 3.4〜9 秒。失敗は 1 回自動でやり直し →「通信の状態」画面
- **初回説明**: 卓ごと（ホールデムとドローは別）に最初だけ「この卓の流れ」＋遊び方のページ。前へ `170,379` / 次へ・はじめる `310,379` / スキップ `240,427` / 最後のページの「次回から表示しない：オン/オフ」`240,331`。入口の「説明をもう一度」`240,360` で全部消す。初回の 1 ハンド目だけ卓に一言のヒント
- 別の卓を始めるとき、途中の試合があれば「いまの試合を終了して新しく始めます」の確認（決定 `310,376`）
- 状態表示 `K`: `[CARDS] view=table game=poker variant=holdem slot=p7 unit=2/5 phase=flop rev=9 score=197-199 provider=jev pending=0 try=0 local=0 seen=10010`（seen: ホールデム/gops/31/bac/ドロー）
- 自動操作 `python tools/cards_autoplay.py COM8 <dir>`（卓の状態を読みながら 1 試合。ホールデム対応）。入口: POKER `166,144` GOPS `314,144` THIRTY-ONE `166,238` BACCARAT `314,238` / つづきから `170,312` 遊び方 `310,312` / もどる `240,432`。
  プレイヤーは AI DUEL と同じ / きろくの「〜ではじめる」`240,318` / 種類 上 `240,161` 下 `240,247` / 相手 JEV `240,161` 端末AI `240,247` / 卓の 2 ボタン `170,376` `310,376`・3 ボタン `136/240/344,376`・カフェへ `240,436` /
  確認の決定 `310,376`・選び直す `170,376` / 結果 きろく `170,329` もう一度 `310,329` ゲーム一覧へ `240,389`。ドローの手札 `104+68i,300`、GOPS の候補 `130+76i,295`（4 枚ずつページ）、31 の場 `156+84i,195`・手札 `156+84i,303`。ホールデムの札は押せない
- 手札が出ている間はシリアル `S` と画面切替を受け付けない（持ち主の確認用にシリアル `O` でスクリーンショットだけ許可できる。起動時は禁止）

### 7.7 AI（Jev）と GAS
- `gas/Jev.gs`: `jevChoice_(state, instructions, criteria, model)`。モデル `jev-1.13.0`、キーはスクリプト プロパティ `JEV_API_KEY`。疎通試験 `python tools/gas_call.py jevtest`
- PC からの試験（URL・合言葉・キーは表示されない。シートには書かない）: `gas_call.py duel` / `reversi [mode=jev_pro|casual] [same=1]` / `cards [game=gops|holdem] [same=1]` / `esper [same=1]` / `ping-unauthorized`
- GAS 側の各受け口は Node の模擬試験で確かめてからデプロイした（スクラッチのスクリプト。再現するなら `gas/*.gs` を連結して Google のサービスを差し替える）
- **GAS に権限を足すときは、デプロイを更新する前にユーザーがエディタで承認すること**（`jevAuthorize()`）。先に更新すると、承認が済むまで doPost 全体が止まる
- 原本由来の `CardsContract.gs` `CardsGate.gs` `ReversiShared.gs` は無改変。グローバル名（`bounded` `faceId` など）が増えているので、新しい .gs を足すときは名前の衝突に注意

## 8. 開発環境と手順

- Windows 11、Python 3.14（pyserial / pillow / esptool / platformio / ziglang）、Node.js 24、Git、gh、clasp 3.2.0
- PlatformIO core は **`C:/pio`**（パス長制限のため）。pioarduino 55.03.311（Arduino-ESP32 3.3.11）/ ESP32_Display_Panel 1.0.4 / LVGL 8.4.0 / ArduinoJson 7

```powershell
cd firmware
python -m platformio run -e app -t upload --upload-port COM8      # 本体（ビルド 1 分・書き込み 20 秒）
python -m platformio run -e sdtest -t upload --upload-port COM8   # microSD の読み書き試験
python -m platformio run -e hwtest / wifitest / app_uart          # LCD・タッチ試験 / Wi-Fi 受信だけ / UART 側(COM9)
```

| 道具 | 内容 |
|---|---|
| `python tools/snapshot.py COM8 out.png` | 実機のスクリーンショット |
| `python tools/uiwalk.py COM8 <dir> key:2 expect:Q:depth=2 tap:240,266 wait:1 expect:K:view=entry snap:name …` | **画面を自動で操作して順に撮る**。`expect:<状態命令>:<文言>` = 違えば中止、`until:J:phase=idle:45` = その状態まで待つ、`watch:10` = ログ表示。ゲームと通信の 1 行ログは待ち時間中の分も表示 |
| `python tools/cards_autoplay.py` / `esper_autoplay.py COM8 <dir>` | 卓の状態を読みながら 1 試合 / 1 局を打ち切る（タップの直前に画面を確かめる。＋1 は押さない） |
| `python tools/serlog.py COM8 30 [send=GD]` | ログ表示（＋開発コマンド送信。1 文字ずつ 1.5 秒間隔なので複数文字の命令は `uiwalk.py key:` で） |
| `COM8` の代わりに `net` | **Wi-Fi の遠隔コンソール**（USB なしで同じ操作）。`serlog` `send` `snapshot` `settime` `uiwalk` `cards_autoplay` `esper_autoplay` の最初の引数に `net`（= `coffee-time.local`）か端末の IP を書く。例 `python tools/serlog.py net 30 send=GDVUJK`。中身は `tools/ctport.py`（下の「遠隔コンソール」） |
| `python tools/ota.py [net または IP] [--bin …]` | **Wi-Fi 越しのソフト更新**。先に `python -m platformio run -e app` でビルドし、できた `firmware.bin` を送る。進み具合（10% ごと）と最後の 1 行（`[OTA] ok, rebooting` / `[OTA] failed: …` / `[OTA] busy: …`）を表示。失敗なら終了コード 1 |
| `python tools/settime.py COM8` | PC の時計を端末と RTC に設定 |
| `python tools/collect_ui_chars.py [--check]` | 日本語の文字一覧を更新 / **一覧とフォントの実体の両方を検査** |
| `bash tools/gen_fonts.sh` | フォント再生成（Git Bash が動かないときは同じ `npx -y lv_font_conv@1.5.3 …` を PowerShell から。§10-3） |
| `python tools/gen_game_data.py` / `gen_detective_data.py` / `gen_esper_data.py` / `gen_reversi_data.py` / `gen_esper_gas.py` / `gen_duel_golden.py` | 設計データを C++ / GAS に変換（手で編集しない生成物: `*Content.*`, `EsperCatalog.gs`, `golden_cases.json`） |
| `python tools/run_wolf_std_checks.py` / `run_esper_checks.py` / `run_duel_checks.py` / `run_reversi_checks.py` / `run_cards_checks.py` | PC 上のロジック試験（zig でコンパイル。コミット前に全部通す） |
| `python tools/gas_preview.py 3 12` / `gas_call.py …` | 見本メール（所有者だけ）/ GAS の各受け口の試験 |

シリアル開発コマンド: `S`=スクショ / `M` `N` `E` `A`=背景固定・自動 / `T`=+1 / `R`=補充（**本物の記録になる**）/ `W`=Wi-Fi スキャン / `C<UNIX秒>`=時刻設定 / `L`=SD ログの末尾 / `P<x>,<y>`=タップ（偽装。＋1 は記録されない）/
`0`=HOME / `1`=メニュー / `2`=ゲーム一覧 / `3`=人狼 / `4`=探偵 / `5`〜`9`=今日の状況・時間ごと・履歴・日ごと・設定 / `B`=明るさ / `F`=時刻合わせ / `I`=システム情報（これらは `[KEY]` の返事あり）/
`G`=人狼の公開状態 / `D`=探偵 / `V`=エスパー / `U`=AI DUEL / `J`=リバーシ / `K`=POKER TABLE の状態 / `Q`=画面の重なりの数（1 = HOME）/ `O`=手札のスクショ許可の切り替え /
`Y!undo`=今日の 1 杯を取り消す / `Y!hist,YYYYMMDD,cups`=履歴の 1 日の杯数を直す（合言葉付き・GAS には送らない）/ `X`=探偵の進み具合を初期化。秘密が画面に出ている間は `S` と画面切替を受け付けない

### 遠隔コンソールと Wi-Fi 越しの更新（USB なしで作業する）
- 端末は Wi-Fi につながると `[NET] remote console coffee-time.local / <IP> :2323 (update :2324)` を出す。名前は mDNS の `coffee-time.local`（DHCP のホスト名も `coffee-time`）。
  名前が引けないときは「設定 → システム情報」の **IP アドレス**の行（シリアル `I` でも開ける）を使う
- **コンソール = TCP 2323**。USB のシリアルとまったく同じ（上の開発コマンドもログも全部）。ファームは `Serial` を `ct_serial_tee.h`（`build_src_flags` の `-include`）で差し替えているだけで、ほかのソースは変えていない。
  最初の 1 行が `AUTH <REMOTE_PASSWORD>` で、返事は `[CON] ok` / `[CON] denied`。**3 回続けて違うと 60 秒は誰も入れない**。合言葉を待つのは 10 秒
- **相手は 1 人だけ**。新しい接続が合言葉を通ると前の相手は `[CON] replaced by a new connection` で切られる（眠った PC の古い接続に居座られないため）。つないでいる間は Wi-Fi の省電力を切る
- 画面を切り替える命令とタップは**USB と同じ注意**（§10-11。`expect:Q:depth=2` など）。遠隔でも ＋1 の誤記録は起こりうる
- **更新 = TCP 2324**（`tools/ota.py`）。`AUTH …` → `OTA <バイト数> <md5>` → `[OTA] ready` → 本体 → `[OTA] ok, rebooting`。端末は更新中「ソフトを更新しています」と % を全画面に出す。
  **断る条件**: ゲームの画面が開いている / コーヒーの記録が GAS に未送信 / 秘密が画面に出ている / 大きすぎる（`[OTA] busy: …`）。失敗しても今のソフトのまま動く（アプリ領域が 2 つ = app0/app1）
- 更新が終わると杯数を保存し SD に `ota` を書いてから、**Wi-Fi を止めて**再起動する（止めないと再起動の理由が「見張り」になった。9/24）。書き換え中は画面が乱れることがあるが、再起動で直る
- 通信は暗号化していない（社内 LAN 用）。合言葉は同じネットワークで盗み見られうる
- `REMOTE_PASSWORD` が空なら両方とも開かない（`[CON] disabled (no REMOTE_PASSWORD)`）。ArduinoOTA（espota）は PC 側に受け口が要り Windows のファイアウォールで止まるので使っていない

### GAS
- 必ず `clasp -u work …`（既定ユーザーは個人 Gmail）。反映は `cd gas; clasp -u work push -f` → `list-deployments` で **@数字** の ID → `update-deployment <ID> --description "…"`（URL 不変）。`create-deployment` は URL が変わるので使わない。デプロイ ID は画面に出さない（伏せ字）
- `invalid_grant (invalid_rapt)` で失敗したら Workspace の再ログインが必要 → ユーザーに `! clasp -u work login` を実行してもらう。**ログインが切れているとコードが送られないまま「デプロイ 0 件」になる**ので、更新後は必ず `gas_call.py ping-unauthorized` と各受け口で疎通を確かめる
- 更新の直後 1 回だけ Google の一時エラー（HTML）が返ることがある。少し待って再試行

### 並行作業
- 大きな実装は実装担当エージェント（executor, opus）に仕様（各 PLAN）を渡して任せ、**エージェントはビルドまで・書き込みと実機確認は自分**。終わったら別のエージェント（code-reviewer）で独立レビュー → 反映 → 実機で再確認 → コミット
- 同時に 2 つ進めるときは git worktree **`C:\ctw`** を使う（`git -C C:\ctw checkout --detach <commit>`。`secrets.h` はコピー済み）。終わったら `git -C C:\ctw diff` をパッチにして本体へ `git apply`（CRLF/LF に注意。生成物は本体で作り直す）
- 同じ作業ツリーで 2 つのエージェントが同時にビルドすると壊れる。ビルドは順番に
- エージェントが利用上限で止まることがある。作業ツリーに途中の成果が残るので、`git status` と試験・ビルドで状態を確かめて引き継ぐ

## 9. 秘密情報の置き場所（すべて Git 管理外）

| 場所 | 中身 |
|---|---|
| `firmware/include/secrets.h` | `WIFI_SSID` `WIFI_PASSWORD`（会社）/ `…2`（自宅）/ `…3`（テザリング）、`GAS_URL`、`GAS_TOKEN`、`REMOTE_PASSWORD`（遠隔コンソールと Wi-Fi 越しの更新の合言葉。英数字 20 文字を乱数で作って追記済み。`ctport.py` `ota.py` もここから読む）。**表示しない**。名前と文字数だけ確かめる。ユーザーに入力してもらうときはメモ帳で開く |
| `gas/Secret.gs` | `TOKEN`（`GAS_TOKEN` と同じ値） |
| `gas/.clasp.json` | scriptId / parentId |
| GAS のスクリプト プロパティ | `JEV_API_KEY`（ユーザー本人が設定）、任意で `JEV_MODEL`、`JEV_DAILY_MAX` |
| `backup/factory_firmware_16MB.bin` | 工場出荷ファームのバックアップ |

コミット前に秘密の値が混ざっていないか検索する（scratchpad の `secret_scan.py` 相当: secrets.h の値・`AKfycb`・`macros/s/`・`ghp_`・`Bearer ` を対象）。

## 10. ハマりどころ

1. Windows のパス長制限 → PlatformIO core を `C:/pio` に
2. **Git Bash（msys）が起動不能になることがある**（`add_item … failed, errno 1`）。ビルドは元から PowerShell。push の認証は `gh auth token` を
   `GIT_CONFIG_COUNT=1` `GIT_CONFIG_KEY_0=http.https://github.com/.extraheader` `GIT_CONFIG_VALUE_0="AUTHORIZATION: basic <x-access-token:トークン の base64>"` で渡し `git -c credential.helper= push origin main`（トークンは表示しない）
3. **フォント**: HOME 用（`ct_font_22/30`）とゲーム用（`ct_font_jp_20/22/40`）は統合しない（リンクエラー）。文字一覧だけ更新して `.c` を作り直し忘れると □ になる → `collect_ui_chars.py --check`。
   PowerShell での作り直し: 文字一覧を `Get-Content -Raw -Encoding UTF8` で読み改行を除いて `--symbols` に渡す。フォントの元は `%LOCALAPPDATA%\Temp\coffee_time_fonts`。アイコン: Material Icons Round（`ct_font_icons_*`）、天気（Weather Icons）、手（Font Awesome Free Solid の 3 字）
4. LVGL: ボタンは `pad_all` 0 で座標配置 / 日本語は自動折り返しされない（データ側で改行）/ 空で作ったラベルに後から複数行を入れると位置がずれる / 画面遷移中に `lv_obj_del` しない / LVGL タスクのスタックは 8KB / NVS 書き込み中は画面が一瞬止まる。UTF-8 のバイト列でアイコンを書くときはコードポイントと突き合わせる（U+EFEF を `\xEE\xBE\xAF` と書き間違えて □ になった）
5. シェルのヒアドキュメント経由でソースを書くと `\n` が本物の改行になる（Bash ツールに渡した `python - <<'EOF'` の中の `'\\0'` も NUL 文字 1 つになった。9/24） → スクリプトをファイルに書いて実行するか Edit を使う。PowerShell の 1 行コマンドに日本語やバッククォートを含む文字列を入れると構文エラーになりやすい → Python の小さなスクリプトに逃がす
6. Open-Meteo は `useHTTP10(true)`、GAS の 302 は新しい接続で GET、curl は `-d` だけ
7. `serlog.py … reset` は native USB ではリセットがかからない → `python -m esptool --chip esp32s3 --port COM8 chip_id` の直後に `serlog.py`
8. SD は GPIO1・2 を LCD の初期化線と共用 → `board->begin()` の後にマウント
9. iPhone のテザリング: 「互換性を優先」をオン（2.4GHz）、名前は `iPhone (n)` のように**半角スペース入り**になることがある、共有の画面を開いたまま端末のすぐ横に置く。失敗理由は `[NET] disconnected, reason N`（202 = 認証失敗、201 = 見つからない）
10. `uiwalk.py` の自動操作中にユーザーが端末を触ると進行がずれる。ユーザーが遊んでいそうなときは自動操作をしない
11. **シリアルポートは `serial.Serial()` を作ってから `dtr=False; rts=False` を設定し、そのあと `open()` する**。直接開くと端末が再起動し、続けて送ったタップが HOME の「＋1」に当たった（9/22）。
    画面切替の命令には `[KEY] <文字>` の返事があり `uiwalk.py` はそれを待つ。**一覧をタップする前は `expect:Q:depth=2`**（`view=-` は HOME かもしれない。9/23 に 2 度目の誤 ＋1）。
    ゲーム内のタップの前は `expect:K:view=table` のように画面を確かめる。今は偽装タップからの ＋1 はファームが記録しない（`home::addOneCup` が `lvgl_port_debug_tap_recent()` を見る）が、道具側の確認も続ける。本物の記録の試験はシリアル `T`
12. GAS の 302 を追うとき、1 本目の `WiFiClientSecure` を `stop()` してから 2 本目をつなぐ。`http.end()` だけでは keep-alive で TLS のメモリ（約 35KB）を握ったままになり、2 本目が `tls=-16` → `HTTP -1` で落ちる
13. **ゲーム中は Wi-Fi の省電力を切る**（`display::setGameActive` → `net::setLowLatency`）。入れたままだと GAS の転送先からの返事が届かない失敗 (-11) が 10 回に 3〜5 回起きた（切ると 8 回に 1 回）
14. **GAS への TLS 接続は 5 秒待つ**。3 秒だとテザリングで 2 回に 1 回「connect failed: tls=48」になった（電波 -52dBm でも）
15. 各ゲームの状態表示（`U` `J` `K` `V`）の直後に通信の 1 行ログが混ざる。`uiwalk.py` の `expect:` は ` view=` を含む行だけを返事として扱う
16. ゲームの記録の blob に版と CRC を付けるとき、**CRC の位置は必ず末尾 = `kBlobBytes - 4`** にして `static_assert` で配置を固定する（エスパーで 2 日間きろくが消えていた）
17. **push の認証に使う環境変数は 3 つとも消す**（`GIT_CONFIG_COUNT` `GIT_CONFIG_KEY_0` `GIT_CONFIG_VALUE_0`）。9/23 夜、`VALUE_0` だけ消したら後続の git が `missing config value GIT_CONFIG_VALUE_0` で失敗した（push 自体は成功）。
    PowerShell の `$env:` は同じ 1 回のコマンドの中だけ有効なので、push と後片付けは同じコマンドに書く
18. Bash ツールで `cat > ファイル` のように入力の無いコマンドを書くと、標準入力を待って 2 分で時間切れになる（9/23 夜に 1 回）。ファイルは Write / Edit ツールで作る
19. **`firmware/src/app/ct_serial_tee.h`（`Serial` の差し替え。`build_src_flags` の `-include`）を変えたら `firmware/.pio/build/app/src`（と `app_uart/src`）を消してからビルドする**。SCons は強制インクルードの変更を追わないので、古い .o が残って `undefined reference` になった（9/24）
20. `ct_serial_tee.h` で `Arduino.h` を読み込むと、人狼のコア（`werewolf_core.hpp` の `bit()`）が Arduino の `bit` マクロとぶつかる。読み込むのは `Stream.h` だけにし、`NO_GLOBAL_SERIAL` で HardwareSerial.h の `#define Serial` を止めている。差し替え先の名前空間は `ct_tee`（`cards` に `ct` という名前空間があり `ct::con` が化けた）

## 11. 仕様の要点（カフェ機能）

- HOME: 日付・時刻（104px）・天気マーク＋気温・「今日」・+1・「残り」（3 以下オレンジ / 0 赤）・ロゴ・SD / Wi-Fi / 電池電圧。背景は 5–11 時 朝 / 11–16 時 昼 / それ以外 夕方（時刻不明の間は夕方）
- +1 は `LV_EVENT_CLICKED` のみ。「残り」を 1.5 秒長押しで「1 回に作る杯数」（設定 5〜15、既定 10）まで補充
- メール: 宛先は「設定」シート A 列（現在 2 人、最終 4 人）。差出人表示 `CaféTamu`。3 杯 = ブラウン系 / 0 杯 = 【至急】赤基調。PC 幅 680px の 2 カラム

## 12. ユーザーの回答待ち・依頼中

- [x] 「ログ」シートの 4 行（9/22 01:24 `take`、9/23 `cards` ×2、9/23 10:05 `take`）は 9/23 夜に**私が PC の画面操作（jev-hands）で削除した**（名前ボックスに `24:26` / `22:22` → Ctrl+Alt+−）。
  残っている試験データ: 9/19 の `coffee-face3f` の 12 行、9/21 の `jevtest` 2 行と `coffee-010000` の 5 行。本番前に消すかは未定
- [x] 残りが 0 のときの「+1」: **押せるまま + トースト**（9/23 夜に回答。§4 の 0 番。同日 21 時台に実装済み）
- [ ] 手違いで Drive にできた空のスプレッドシート「COFFEE TIME 記録」の削除可否
- [ ] 通知先の残り 2 人の登録（ユーザーが「設定」シートに入力）
- [ ] Jev の契約上の 1 日の上限（設計書の 500 / 1,500 回は設計上の目安）
- [ ] 新しいゲームの感想（§4-1）
- [x] 残り 0 のトーストの目視確認（9/23 夜にユーザーが確認）\n- [ ] 毎朝出ることがうるさくないか（しばらく使ってからの感想）
- [x] AI DUEL / POKER TABLE の本人の選び方は 8 アイコン + ゲストのまま（9/23）／誤記録は消す（9/23。端末側は消した）／待ち時間は「長い。演出で」（9/23。演出を入れた）／POKER = ホールデム必須（9/23。入れた）

## 13. 作業の進め方

- 変更 → PowerShell でビルド → **ユーザーが遊んでいないことを確かめて**書き込み → `snapshot.py` / `uiwalk.py` / `*_autoplay.py` で確認 → 独立レビュー → 引き継ぎ書 → コミット・push
- ユーザーは AI ゲームについて「ディレクターとして完了まで」を一任している。それでも **メール・外部送信・GAS のデプロイなど外に影響する操作は事前に伝える**（デプロイは「@n に更新します。コーヒー側は無変更」と一言添えてから）
- 設計書由来のデータ（JSON・コア）は書き換えず、表示や調整は生成ツール側・追加ファイル側で差し替える。設計書と端末の実情が合わないところは PLAN 文書に「ディレクター判断」として理由つきで記録する
- 新しい画面や文言は、実装の前に**見本の画像**を作ってユーザーに見せると早い。ユーザーへの質問は選択式で
- 起きた事故（誤記録など）はすぐ報告し、打ち消しの偽記録を重ねない。再発防止は道具とファームの両方に入れる
- コミットの末尾に `Co-Authored-By:`（そのときのモデル名）を付ける
