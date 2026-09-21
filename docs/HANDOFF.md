# COFFEE TIME 引き継ぎ書

- 最終更新: 2026-09-21 夕方（第 5 版）。最新コミットは `git log` を参照（この文書に固定の番号は書かない）
- **ゲーム実装の詳細なやることリストは [WEREWOLF_TODO.md](WEREWOLF_TODO.md) を参照**（次の作業はここから）
- **フラッシュ / SD カードの使い分けは [STORAGE_POLICY.md](STORAGE_POLICY.md)**（2026-09-21 決定）
- リポジトリ: https://github.com/skyblueearthjapan/COFFEE-TIME （ブランチ `main`）

> ⚠️ **このリポジトリは PUBLIC。** Wi-Fi のパスワード・合言葉 (TOKEN)・GAS の URL・デプロイ ID を、
> このファイルやコミットに書かないこと。秘密情報は Git 管理外のファイル（§7）にだけ存在する。

---

## 1. 最初に読むこと（次の担当者向け 5 分チェックリスト）

1. ESP32 が USB でつながっているか確認し、COM ポートを調べる（これまでは **COM8**）
   ```powershell
   Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match 'COM\d+' } | Select Name
   ```
   `USB シリアル デバイス (COMx)` で DeviceID に `VID_303A` を含むものが ESP32。
2. 実機の画面を取得して現状を見る: `python tools/snapshot.py COM8 now.png`
3. ビルド・書き込みは **PowerShell から**（§6）。GAS の操作は **`clasp -u work`**（§6）。
4. ユーザーへの説明は日本語。作業は自分で進め、ユーザーには目視・タッチ・秘密情報の入力・Google の承認だけを頼む。
5. §9 の未解決事項のうち、ユーザーの回答待ちのものを最初に確認する。

## 2. プロジェクト概要

社内カフェに置くタッチ式コーヒーカウンター端末。コーヒーを 1 杯取ったら画面の「+1」を押す。
本日の杯数・残り杯数を表示し、残り 3 杯（通常）と 0 杯（至急）で担当者にメールする。
企画書・UI イメージ・段階 1 の指示書は `参考データ/`。

- ハード: **Waveshare ESP32-S3-Touch-LCD-2.8C**（480×480 丸型 / ST7701 RGB / GT911 タッチ / Flash 16MB / PSRAM 8MB）
- ユーザー: 組み込み・デプロイは初めて。方針は「**理解を深めながら、作業はできる限りエージェント側で進めてほしい**」。
  結果だけでなく「どこに保存され、どこでビルドされ、どこにデプロイされたか」を知りたがる。都度、簡潔に説明する。
- 使用アカウント: Google は会社の Workspace（`imaizumi@lineworks-local.info`）、GitHub は `skyblueearthjapan`

## 3. 進捗

| 段階 | 内容 | 状態 |
|---|---|---|
| 0 | 実機動作確認 | ✅ |
| 1 | +1 カウンター | ✅ タグ `stage1`・`docs/TEST_RECORD_stage1.md` |
| 2 | Wi-Fi・時計(NTP)・天気・残り杯数・時間帯別背景・日本語表示 | ✅ 実機確認済み・タグ `stage2` |
| 3 | GAS へ記録・残り 3 杯でメール通知（HTML・PC レイアウト） | ✅ 実機で押下〜メール受信まで確認済み・タグ `stage3` |
| 3+ | 残り 0 杯で【至急】メール | ✅ 実装・デプロイ済み（見本メールのみ確認。実機 1→0 は未確認） |
| 3+ | 電池電圧表示・RTC で時刻保持・PC から時刻設定 | ✅ 2026-09-20 実機確認済み |
| 4 | 画面追加（今日の状況グラフ・設定・スクリーンセーバー） | メニューの枠だけ実装（中身は準備中） |
| G0 | ゲーム基盤（画面スタック・共通 UI・メニュー・日本語フォント 431 字） | ✅ 2026-09-21 実機確認済み |
| G1 | 人狼 W1（ロジックコア取り込み・人数選択画面） | ✅ 実機確認済み |
| G2 | 人狼 W2（配役・役職確認・議論・投票・結果・覗き見防止） | 🚧 実装済み・公開画面は実機確認済み。**秘密の表示以降は人の指での確認待ち**。[WEREWOLF_TODO.md](WEREWOLF_TODO.md) §1.5 |
| G3 | 探偵「喫茶『余白』の事件簿」段階 B（実機のひとり推理） | ✅ 実装・第 1 話を実機で通し確認（通常・降参の両ルート）。第 2・3 話の通しと人の目での読みやすさ確認が残り。**段階 C（Google 連携）・D（Jev 対戦）は作らない**（2026-09-21 ユーザー決定:「探偵に Jev を入れる必要性はあまりない。AI が要るのはエスパーと AI DUEL」）。探偵はひとり推理を完成形とする |
| G4 | エスパー / AI DUEL（**AI = Jev が必須の 2 作**） | 未着手。最初にやること: Jev API の疎通試験（キーは端末に置かず GAS のスクリプト プロパティへ。設計書の API 記述は「実 API 未接続・未試験」）。エスパーの設計書は新規 Python サーバー前提なので、既存 GAS に寄せるかの判断が要る |
| 4' | メニューの中身（今日の状況・履歴・設定） | ✅ 実装・全画面を実機で表示確認（[MENU_DESIGN.md](MENU_DESIGN.md)）。アイコンのメニュー、今日の状況＋時間ごと、履歴（今週 = 月〜日 / 30 日 / ゲームの回数 / 日ごと）、設定 7 項目。未確認: 自動の暗転、再起動をはさんだ保存、操作音（ブザーの極性が未検証） |
| G2' | 人狼「通常ルール」（決着まで続く・4〜10 人） | 🚧 実装済み（[WEREWOLF_STANDARD_RULES.md](WEREWOLF_STANDARD_RULES.md)）。PC 上の試験 `python tools/run_wolf_std_checks.py` 全合格（20 万局）。公開画面は実機確認済み（モード選択〜最初の手渡し）。**秘密の画面以降は人の指での確認待ち** |
| — | LINE WORKS 通知 | 未着手（GAS に送信先を足す想定） |
| 5 | 省電力・LiPo 1500mAh・3D プリント筐体 | 未着手 |
| 6 | 遊び要素（達成アニメ・季節テーマ等） | 未着手 |

**次の作業: ① 人狼（通常ルール・ワンナイト）の「指での確認」の結果反映 ③ 探偵の第 2・3 話の通し確認 ④ エスパー対決（Jev API の疎通は 2026-09-21 に確認済み。[AI_GAMES_DIGEST.md](AI_GAMES_DIGEST.md)）。**
**GAS は反映済み（2026-09-21 夜・デプロイ @6、URL は不変）**: メールのゲージは端末から届く `max` に連動、文面は「残り」の数字を長押し。見本メールは `python tools/gas_preview.py 3 12`（所有者だけに届く）。`clasp` が `invalid_grant (invalid_rapt)` で失敗したら、Workspace の再ログインが必要 → ユーザーに `! clasp -u work login` を実行してもらう。
探偵の段階 B は 2026-09-21 に実装済み（`firmware/src/app/games/detective/`、脚本データは同 `data/`、変換は `tools/gen_detective_data.py`、進み具合は NVS `ct_det/prog`）。
段階 C（Google 連携）・D（Jev 対戦）は、共通の本人確認と GAS の大きな作り込みが前提なので後回し。人狼 W2 の「指での確認」はユーザーが並行して実施中。
タグ `stage3` は 0 杯通知の前（`3786174`）。0 杯通知を含む最新は `main` の先頭。

### 2026-09-20 に分かったこと・追加したこと

- **電池（LiPo 1500mAh）**: 充電は正常。基板の「Battery Power Control Switch」は**電池 → 本体の給電**を入り切りするもので、
  OFF だと USB を抜いた瞬間に電源が落ちる（ユーザーはこれを「充電されていない」と認識していた）。充電は USB を挿せばスイッチ位置に関係なく行われる
- **電池電圧**: BAT_ADC = GPIO4（200k/100k 分圧なので ×3）。画面下の Wi-Fi アイコン隣に電圧を表示（`Battery.*`）
- **RTC (PCF85063, I2C 0x51)**: 起動時にシステム時刻へ復元し、NTP 同期のたびに保存（`RtcClock.*`）。
  I2C は ESP32_Display_Panel が従来型ドライバーで port 0 に用意済みのものを共用する（`board->begin()` の後に呼ぶこと）
- **TZ は setup の冒頭で `setenv("TZ","JST-9")`**（Wi-Fi 接続前・RTC 復元時刻にも効かせるため）
- **Wi-Fi が無い場所での時刻設定**: `python tools/settime.py COM8`（シリアルで `C<UNIX秒>` を送る）。RTC にも保存される。
  これで日付が変わったときの TODAY リセットが Wi-Fi 無しでも動く（2026-09-20 に実機で確認）
- **Wi-Fi は 2 つまで登録可**: `secrets.h` の `WIFI_SSID2` / `WIFI_PASSWORD2`（空なら未使用）。WiFiMulti で強い方に接続
- **⚠ 受信感度が異常に弱い**: 同じ部屋で PC が 2.4GHz を 4 台・60% で見えるのに、ESP32 は 0〜4 台・**-95dBm**。
  画面を使わない `wifitest` でも同じなのでソフトの問題ではない。基板上端の赤いセラミックアンテナのすぐ横を
  **ケースの金属リングが囲んでいる**のが有力な原因（写真で確認）。U.FL コネクタは実装済みなので外付けアンテナも可。
  ユーザー判断で**当面は保留**（社内でつながれば可）。社内でも 2026-09-19 時点で -88dBm と余裕がない点に注意

### 2026-09-21 に分かったこと・追加したこと（ゲーム着手）

- ゲーム 4 部作の設計書を 4 本の調査エージェントで精査。**人狼だけが AI・通信・費用ゼロで完結**するため最初に着手
- 人狼パッケージのホスト検証をこの PC で再実行し **全 PASS**（`pip install --user ziglang` の zig をコンパイラーに使用）
- **フォントは 1 つに統合できない**: 共有フォントに 431 字を足すとリンクエラー（xtensa の `call8` 到達範囲）。
  HOME 用（`ct_font_22/30`）とゲーム用（`ct_font_jp_20/22/40`）を分離した
- **LVGL のボタンは既定の内側余白が大きく**、配置指定が効かなくなる（`pad_all` を 0 にして座標で置く）
- **日本語は自動折り返しされない**（`LV_LABEL_LONG_CLIP` ＋ 幅指定が必要）
- コードレビューで高リスク 2 件（画面遷移中の解放済みメモリ参照／ゲームデータが再生成不能）を検出・修正済み

### 2026-09-21（午後）に分かったこと・決めたこと

- **microSD（32GB）を追加**。配線・使い方・試験結果と、フラッシュ / SD の使い分けは **[STORAGE_POLICY.md](STORAGE_POLICY.md)**（ユーザー承認済み）。
  試験用ビルド `sdtest` は PASS。アプリ本体は**操作ログ**を SD に書く（`SdLog.*`。起動理由・+1・補充・日付変更・時刻設定。
  シリアル `L` で読める。実機確認済み）
- **RTC は電源が完全に切れると時刻を失う**（電池スイッチ OFF ＋ USB 抜き）。起動ログに `[RTC] no valid time (oscillator stopped)`。
  リセットだけなら保持される。失ったら `python tools/settime.py COM8`
- **保存（NVS）は正常**: +1 → 再起動で値が残ることを実機で確認。「LEFT が 9 → 10 に戻った」というユーザー報告は、
  LEFT の 1.5 秒長押し（補充）の誤操作が最有力（持ち運び時に右端へ指がかかる）。対策案をユーザーに提示中（3 秒長押し＋進捗表示 / 取り消し）
- `serlog.py ... reset` は native USB (COM8) では**リセットがかからない**。
  `python -m esptool --chip esp32s3 --port COM8 chip_id` でリセットし、直後に `serlog.py` を開くと起動ログが取れる
- **ユーザーとの運用確認（2026-09-21 夜）**: 朝は必ず 0 杯から始める（夜の残りは配るかアイスコーヒー）→「日付が変わったら LEFT=0」で確定し、設定項目からも外した。HOME の見出しは「今日」「残り」、補充の表示は「補充しました：n 杯」。探偵に Jev は入れない（AI はエスパーと AI DUEL）。
- **フォントの落とし穴**: 文字一覧だけ更新して `.c` を作り直し忘れると、一覧との照合は通るのに画面で □ になる（「犠牲」で発生）。`python tools/collect_ui_chars.py --check` はフォントの実体まで確かめるようにした。Git Bash が動かないときは、`tools/gen_fonts.sh` と同じ `npx lv_font_conv` を PowerShell から実行する
- **Git Bash（msys）がこの PC でときどき起動不能になる**（`add_item ... failed, errno 1`）。push の認証もこれで失敗する。回避: `gh auth token` の値を `GIT_CONFIG_COUNT/KEY_0/VALUE_0`（`http.https://github.com/.extraheader`）の環境変数で渡し、`git -c credential.helper= push`。トークンは表示しないこと
- **人狼に席ごとのキャラクターと世界観の導入を追加**（ユーザー要望・見本を承認）: カップ / パン / スプーン / ポット / クッキー / ほし / はっぱ / ベル / ほん / つき。物語の導入 6 ページ（スキップ可・遊び方の先頭にも）、「あなたは だれ？」の一覧、手渡し・対象選択・投票・結果を名前とアイコンで表示、役職にも絵（秘密として扱う）。追加分の文言は `games/werewolf/data/content.local.ja.json`（設計書由来の JSON は書き換えない）、アイコンは `ct_font_icons_36/88`（Material Icons Round, Apache-2.0）
- **並行作業用の作業コピー `C:\ctw`**（git worktree）: 別のエージェントがビルド中でも独立してビルド・書き込みできる。使う前に `git -C C:/ctw checkout --detach main` で最新に合わせ、`secrets.h` をコピーする
- HOME のメニューボタンを**右斜め下**へ移動（ユーザー要望）。準備中トーストの二重解放（`ui/UiKit.cpp`）は修正済み
- **Wi-Fi の登録漏れを修正**: `NetService.cpp` に `s_wifi_multi.addAP()` が無く、2 つ目の Wi-Fi は実際には一度も試されていなかった。修正して **3 つまで登録可**にした（`secrets.h` の `WIFI_SSID2/3`・`WIFI_PASSWORD2/3`。3 つ目はスマホのテザリング用）。修正後も自宅では `no networks found`（周囲の電波が 1 つも見えない）で、受信感度の問題は別に残っている
- **iPhone のテザリングで接続できた**（2026-09-21。RSSI -47dBm、天気・NTP とも動作。`secrets.h` の `WIFI_SSID3`。つまずいた点: iPhone が自動で付ける名前は `iPhone (n)` のように**半角スペース入り**、パスワードの `0` と `O` の取り違え。失敗理由はログの `[NET] disconnected, reason N`（202=認証失敗、201=見つからない）、近くの未登録の電波名は `W` コマンドで出る）: ESP32 は 2.4GHz 専用なので iPhone の「インターネット共有 → 互換性を優先」をオン、iPhone の名前は半角英数字に。初回は共有の設定画面を開いたまま端末のすぐ横に置く。**つながると T/R や +1 は本物の記録になり、残り 3・0 杯で登録者にメールが飛ぶ**点に注意
- 文書の古い記述: `gas/README.md`（setup() は TOKEN を出さない。現在は `Secret.gs` 方式）、`README.md`（段階 1 の説明・ゲーム未記載）。
  人狼のホスト検証 `tools/run_checks.py` はリポジトリではなく `参考データ/ゲーム4部作/…WEREWOLF_Handoff_v2.0.zip` の中にある

### 実機の現在の状態（2026-09-21 時点）
- 人狼 W2・SD 操作ログ・天気マーク・Wi-Fi 3 件登録までのファームを書き込み済み
- 自宅では iPhone のテザリング（端末のすぐ横に置く）で Wi-Fi 接続できる。自宅の Wi-Fi は -92〜-97dBm で接続不可
- **Wi-Fi 接続中は +1・補充が本物の記録になり、残り 3・0 杯で登録者へメールが飛ぶ**
- スプレッドシート「ログ」には試験のデータが入っている（本番運用前に消すかはユーザーと相談）

## 4. 全体構成とデータの流れ

```
【PC】COFFEE TIME フォルダー（原本）──git push──▶【GitHub】COFFEE-TIME（バックアップ・履歴）
   │ PlatformIO でビルド → USB(COM8) で書き込み        │ clasp push / update-deployment
   ▼                                                   ▼
【ESP32】画面・杯数(NVS)                         【Google Apps Script】（スプレッドシートに紐づけ）
   ├─ Wi-Fi → NTP 時刻 (JST)                         ├─ 「ログ」シートに 1 行追記
   ├─ Wi-Fi → Open-Meteo 天気（千葉市・15 分ごと）     └─ 残りが 3 / 0 に減った瞬間 → 「設定」シートの宛先へ HTML メール
   └─ +1・補充・日付変更 ──HTTPS POST──▶ Web アプリ
```

### ESP32 → GAS の送信内容（JSON）
```json
{"token":"…","device":"coffee-xxxxxx","event":"take|refill|newday","id":"<起動乱数>-<連番>",
 "taken":7,"left":3,"prev":4,"rssi":-80,"ts":1789800000}
```
- `prev` = イベント前の残り杯数。GAS は `event=="take" && prev>left && (left==3 || left==0)` のときだけ通知する
  （0 のまま押され続けても再送しない）。`prev` が無い旧形式は `left+1` とみなす
- `id` で 6 時間の重複排除（CacheService）。ESP32 は失敗時 30 秒ごとに再送（RAM キュー 32 件、再起動で消える）
- GAS 応答は 302 で別ホストに渡される。ESP32 は `Location` を**新しい接続で GET** して `{"ok":true}` を確認する

### 「ログ」シートの列
受信日時 / 端末日時 / 端末ID / イベント / 今日の杯数 / 残り杯数 / Wi-Fi強度(dBm) / イベントID

## 5. 仕様（ユーザーと決めたこと）

### HOME 画面
- 表示: 日付 `9月19日（土）` / 時刻 / 天気 `くもり 22°C` / TODAY / +1 ボタン / LEFT / COFFEE TIME / Wi-Fi アイコン
- 日付は日本語。**「TODAY」「LEFT」は 2026-09-21 にユーザー要望で「今日」「残り」へ変更**（HOME 用フォント `ct_font_22/30` に 今・残 を追加）。COFFEE TIME のロゴは英語のまま。**通知メールの文面に「LEFT を長押し」とあるので、GAS 側も「残り」に直すこと**（`gas/Code.gs`、未実施）
- **天気はマーク＋気温**（2026-09-21 ユーザー要望で文字から変更。`ct_font_weather_44` = Weather Icons の 11 個だけ収録、夜 18〜5 時の晴れは月のマーク。`HomeScreen.cpp` の `kWeatherAsIcon` を false にすると日本語表示に戻る）
- 天気の日本語: 晴れ / 晴れ時々くもり / くもり / 霧 / 霧雨 / 雨 / 雪 / にわか雨 / にわか雪 / 雷雨
- +1: `LV_EVENT_CLICKED` のみで加算（長押し中の連続加算なし）。TODAY+1、LEFT-1（0 未満にしない）
- LEFT を 1.5 秒長押し → 残り **10 杯**（上限 10 はユーザー指定）に補充、トースト表示
- LEFT の色: 3 以下でオレンジ / 0 で赤
- 日付が変わると TODAY=0・LEFT=0（まだ作っていない扱い）。**2026-09-21 確定**: 朝は必ず 0 杯から始める運用（夜の残りは配るかアイスコーヒーにする）
- 杯数は NVS（Preferences, namespace `cup`）に保存。電源を切っても保持
- 背景写真（Codex の画像生成で作成）: 5–11 時 朝 / 11–16 時 昼 / それ以外 夕方。ユーザーは「時間で背景を変える」を気に入っている
- フォント: 時計の数字 Montserrat Light 104px、英数字 Montserrat Medium、日本語 Zen Maru Gothic（使う文字だけ収録）

### 通知メール
- 宛先: スプレッドシート「設定」シートの A 列（現在 2 人。最終的に 4 人の予定）
- 差出人の表示名: `CaféTamu`（`gas/Code.gs` の `SENDER_NAME`。候補に `coffeetime` もあり）。送信元アドレスは実行アカウントのまま
- 残り 3 杯: 件名「☕ コーヒーの残りが3杯になりました」、ブラウン系
- 残り 0 杯: 件名「【至急】コーヒーがなくなりました」、赤基調
- レイアウト: PC 幅 680px の 2 カラム、600px 未満で 1 カラム。HTML 非対応のメールソフト向けにテキスト版も同送
- ユーザーの要望で「スマホ向け → PC 向け」に作り直した経緯あり。PC で見やすいことを優先する

## 6. 開発環境と手順

### この PC の前提（確認済み）
- Windows 11、Python 3.14（pyserial / pillow / esptool / platformio 導入済み）、Node.js、Git、gh、Codex CLI（ChatGPT ログイン済み）
- PlatformIO core は **`C:/pio`**（`platformio.ini` の `core_dir`）。長いパスが無効でユーザーは管理者ではないため
- ボード: `esp32-s3-devkitc-1` + QIO 16MB + OPI PSRAM、USB CDC On Boot 有効（native USB 接続）
- ライブラリ: pioarduino 55.03.311（Arduino-ESP32 3.3.11）/ ESP32_Display_Panel v1.0.4 / ESP32_IO_Expander v1.1.1 / esp-lib-utils v0.2.3 / LVGL 8.4.0 / ArduinoJson 7

### ビルド・書き込み（**必ず PowerShell から**）
```powershell
cd firmware
python -m platformio run -e app -t upload --upload-port COM8      # 本体
python -m platformio run -e hwtest -t upload --upload-port COM8   # LCD・タッチ試験（カラーバー + 座標ログ）
python -m platformio run -e app_uart -t upload --upload-port COM9 # 「USB TO UART」側の端子(CH343)につないだとき
python -m platformio run -e wifitest -t upload --upload-port COM8 # 画面なしで Wi-Fi 受信だけを試す
python -m platformio run -e sdtest -t upload --upload-port COM8   # microSD の読み書き試験（結果はシリアルに [SD] で出る）
```

### 開発用ツール（シリアルポートは同時に 1 プロセスしか開けない）
| コマンド | 内容 |
|---|---|
| `python tools/serlog.py COM8 30` | ログを 30 秒表示（各行に PC 時刻） |
| `python tools/serlog.py COM8 45 send=RTT` | 接続後に開発コマンドを送りつつログ表示 |
| `python tools/snapshot.py COM8 out.png` | **実機画面のスクリーンショット**（エージェントが画面を確認する手段） |
| `python tools/send.py COM8 M` | 1 文字コマンド送信 |
| `python tools/settime.py COM8` | PC の時計を ESP32 と RTC に設定（Wi-Fi 不要） |
| `python tools/img2lvgl.py in.png out.c name --dim 0.75` | 背景画像を LVGL 用 C 配列へ変換 |
| `python tools/collect_ui_chars.py` | ソースの日本語を集めてフォント用の文字一覧を更新（`--check` で不足検出のみ） |
| `bash tools/gen_fonts.sh` | フォント再生成（文字を足したら collect → gen の順で実行） |
| `python tools/gen_game_data.py` | ゲームの文言・ルールを C++ データに変換（入力はリポジトリ内 `games/werewolf/data/`） |
| `powershell -File tools/gen_bg.ps1 <name> "<雰囲気>"` | Codex CLI で背景画像を生成 |

シリアル開発コマンド: `S`=スクショ / `M`・`N`・`E`=背景を朝・昼・夕に固定 / `A`=自動 / `T`=+1 / `R`=補充 /
`W`=Wi-Fi スキャン / `C<UNIX秒>`=時刻設定 / `L`=SD の操作ログの末尾を表示 / `G`=人狼の公開状態 / `5`=今日の状況 / `6`=時間ごと / `7`=履歴 / `8`=日ごと / `9`=設定 / `B`=明るさ / `F`=時刻合わせ / `I`=システム情報 / `4`=探偵を開く / `D`=探偵の状態 / `X`=探偵の進み具合を初期化（試験で初見の記録を消費したら必ず実行）/ `P<x>,<y>`=座標をタップ（`tools/uiwalk.py` が使う） / `1`=メニュー / `2`=ゲーム一覧 / `3`=人数選択 / `0`=HOME へ
**`T`・`R` は本物のイベントとして GAS に送られ、残り 3・0 杯で登録者全員にメールが飛ぶ。**

### GAS（clasp 3.2.0）
- Workspace アカウントは名前付きユーザー **`work`**。既定ユーザーは個人 Gmail なので、**必ず `clasp -u work …`**
- スクリプトはユーザー作成のスプレッドシートに紐づけ済み（ID は `gas/.clasp.json`、Git 管理外）
- 反映手順（URL を変えない）:
  ```bash
  cd gas
  clasp -u work push -f
  clasp -u work list-deployments            # バージョン付き（@数字）のデプロイ ID を使う。@HEAD ではない
  clasp -u work update-deployment <ID> --description "..."
  ```
  `create-deployment` は URL が変わり、ESP32 の `secrets.h` 更新と再書き込みが必要になるので避ける。現在のデプロイは @6
- 見本メール: 正しい TOKEN で `{"event":"preview","left":3}`（または `0`）を POST すると**スクリプト所有者だけ**に届く（ログに残らない）
- メール HTML の確認: `lowStockHtml_` を node で呼んで HTML を書き出し、headless Edge でスクショ（スマホ幅は 375px の iframe に入れて描画）

## 7. 秘密情報の置き場所（すべて Git 管理外）

| ファイル | 中身 |
|---|---|
| `firmware/include/secrets.h` | `WIFI_SSID` / `WIFI_PASSWORD`（ユーザー入力）、`GAS_URL`、`GAS_TOKEN` |
| `gas/Secret.gs` | `TOKEN`（`GAS_TOKEN` と同じ値。clasp push で GAS に送られる） |
| `gas/.clasp.json` | scriptId / parentId |
| `backup/factory_firmware_16MB.bin` | 工場出荷ファームの全体バックアップ（esptool で書き戻せる） |

- `secrets.h` はユーザーのパスワードを表示しないよう、`sed` で該当行だけ置換してきた。ファイル全体を表示・出力しないこと
- コミット前に `grep -rnE "[0-9a-f]{32}|AKfycb|macros/s/" docs README.md gas/Code.gs` で混入がないか確認してきた

## 8. ハマりどころ（解決済み）

1. **Windows のパス長制限** → PlatformIO core を `C:/pio` に。ライブラリの git clone は `-c core.longpaths=true`
2. **Git Bash から PlatformIO** → `MSys/Mingw is not supported` でツールチェーンが空になる。PowerShell で実行
3. **Open-Meteo の JSON 解析失敗 (InvalidInput)** → chunked 転送のため。`http.useHTTP10(true)`
4. **GAS への POST が HTTP 400** → HTTPClient の自動リダイレクトがヘッダーを引き継ぐため。`Location` を新接続で GET
5. **clasp `create-script --type sheets --parentId`** → 親を無視して新規シートを作る。`--parentId` だけで作ること。
   作成時に `appsscript.json` が既定値に上書きされるので戻すこと
6. **curl で GAS を試すときに 411** → `-X POST` と `-L` を併用すると転送先にも POST してしまう。`-d` だけ付けて `-X` は付けない
7. **headless Edge の最小ウィンドウ幅**（約 500px）→ スマホ幅の確認は iframe で行う

## 9. 未解決・要確認事項

ユーザーの回答待ち:
- [x] 人狼の人数の初期値 → 前回の人数を覚える方式で実装（初回 3 人）
- [x] Jev API キー → ユーザー本人が GAS のスクリプト プロパティ `JEV_API_KEY` に設定済み（2026-09-21）
- [ ] 最初の clasp の手違いで Drive にできた**空のスプレッドシート「COFFEE TIME 記録」（ID `1jht7j0…`）**の削除可否
- [x] 日付が変わったときの LEFT → **0 杯で確定**（2026-09-21）
- [ ] 通知先の残り 2 人の登録（ユーザーが「設定」シートに入力）

確認・改善の余地:
- [ ] 人狼 W2 の実装一式（[WEREWOLF_TODO.md](WEREWOLF_TODO.md) に細分化済み。SecretGate の実機検証が最重要）
- [ ] 受入試験 99 項目は全て未実施
- [ ] 【至急】通知の実機確認（LEFT 1 → +1 で 0）。全員にメールが届くのでユーザーと時機を合わせる
- [ ] Wi-Fi の電波が弱い（RSSI 約 -88dBm）。設置場所で確認
- [ ] HTTPS は `setInsecure()`（証明書検証なし）。社内用途として許容中。将来は CA バンドル化
- [ ] 送信キューは RAM のみ。長時間の Wi-Fi 断＋再起動で未送信分が消える（電波が弱いので NVS 化の優先度は上がった）
- [ ] Wi-Fi 受信感度（上記）。ケース見直し or 外付けアンテナ。ユーザー判断で保留中
- [ ] 段階 1 の未実施試験: 50 回連続タップ、電源 OFF→ON（NVS 導入後は「杯数が保持される」が期待値）
- [ ] 背景画像の生成に GPT Image 2.5 が使われたかは、Codex が版数を出さず未確認
- [ ] 「ログ」シートの試験データを本番前に消すか

## 10. 作業の進め方（これまでのやり方）

- 変更 → PowerShell でビルド・書き込み → `snapshot.py` で画面確認 → 必要ならユーザーに目視・タッチを依頼 → コミット・push
- メール・外部送信など全員に影響する操作は、事前にユーザーに伝える（見本は所有者のみに送る）
- 段階が終わったら試験記録を `docs/` に残してタグを打つ（`stage1`〜`stage3` 作成済み）
- 各コミットの末尾に `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>` を付けている
