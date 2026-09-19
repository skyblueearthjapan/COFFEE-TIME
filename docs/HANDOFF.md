# COFFEE TIME 引き継ぎ書

- 最終更新: 2026-09-20（第 3 版）
- 最新コミット: `e004abc`（電池表示・RTC・時刻設定コマンドまで反映）
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
| 4 | 画面追加（今日の状況グラフ・メニュー・設定・スクリーンセーバー） | 未着手 |
| — | LINE WORKS 通知 | 未着手（GAS に送信先を足す想定） |
| 5 | 省電力・LiPo 1500mAh・3D プリント筐体 | 未着手 |
| 6 | 遊び要素（達成アニメ・季節テーマ等） | 未着手 |

次に何をやるかはユーザー未決定（4 / LINE WORKS / 5 を提示済み）。
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

### 実機の現在の状態（2026-09-19 夕方時点）
- 最新ファームを書き込み済み。Wi-Fi 接続・時刻・天気取得は正常
- 試験の影響で TODAY=48、LEFT=1。**この状態で +1 を 1 回押すと、登録者全員に【至急】メールが届く**
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
- **日付と天気は日本語、TODAY・LEFT・COFFEE TIME は英語のまま**（どちらもユーザー指示）
- 天気の日本語: 晴れ / 晴れ時々くもり / くもり / 霧 / 霧雨 / 雨 / 雪 / にわか雨 / にわか雪 / 雷雨
- +1: `LV_EVENT_CLICKED` のみで加算（長押し中の連続加算なし）。TODAY+1、LEFT-1（0 未満にしない）
- LEFT を 1.5 秒長押し → 残り **10 杯**（上限 10 はユーザー指定）に補充、トースト表示
- LEFT の色: 3 以下でオレンジ / 0 で赤
- 日付が変わると TODAY=0・LEFT=0（まだ作っていない扱い）。**ユーザー未確定**
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
| `bash tools/gen_fonts.sh` | フォント再生成（日本語を足したら先に `fonts/ct_font_jp_symbols.txt` へ文字を追加） |
| `powershell -File tools/gen_bg.ps1 <name> "<雰囲気>"` | Codex CLI で背景画像を生成 |

シリアル開発コマンド: `S`=スクショ / `M`・`N`・`E`=背景を朝・昼・夕に固定 / `A`=自動 / `T`=+1 / `R`=補充 / `W`=Wi-Fi スキャン / `C<UNIX秒>`=時刻設定
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
  `create-deployment` は URL が変わり、ESP32 の `secrets.h` 更新と再書き込みが必要になるので避ける。現在のデプロイは @4
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
- [ ] 次の作業: 段階 4（画面追加）/ LINE WORKS 通知 / 段階 5（省電力・筐体）のどれか
- [ ] 最初の clasp の手違いで Drive にできた**空のスプレッドシート「COFFEE TIME 記録」（ID `1jht7j0…`）**の削除可否
- [ ] 日付が変わったときの LEFT を 0 にするか 10 にするか（現在 0）
- [ ] 通知先の残り 2 人の登録（ユーザーが「設定」シートに入力）

確認・改善の余地:
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
