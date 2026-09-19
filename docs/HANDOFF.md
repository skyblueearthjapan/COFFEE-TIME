# COFFEE TIME 引き継ぎ書

最終更新: 2026-09-19

> ⚠️ **このリポジトリは PUBLIC。** パスワード・合言葉 (TOKEN)・GAS の URL をこのファイルやコミットに書かないこと。
> 秘密情報は Git 管理外のファイル（下記「秘密情報の置き場所」）にだけ存在する。

---

## 1. プロジェクト概要

社内カフェに置くタッチ式コーヒーカウンター端末。コーヒーを 1 杯取ったら画面の「+1」を押し、
本日の杯数・残り杯数を表示し、残り 3 杯と 0 杯（至急）でメール通知する。企画書は `参考データ/`。

- ハード: **Waveshare ESP32-S3-Touch-LCD-2.8C**（480×480 丸型 / ST7701 RGB / GT911 タッチ / Flash 16MB / PSRAM 8MB）
- ユーザー: 組み込み開発は初心者。**理解を深めながら、作業はできる限りエージェント側で進めてほしい**という方針。
  説明は日本語で、専門用語は噛み砕く。ユーザーに頼むのは「画面の目視・タッチ操作・秘密情報の入力・Google の承認操作」だけにする。

## 2. 進捗

| 段階 | 内容 | 状態 |
|---|---|---|
| 0 | 実機動作確認 | ✅ |
| 1 | +1 カウンター | ✅ タグ `stage1`、`docs/TEST_RECORD_stage1.md` |
| 2 | Wi-Fi・時計(NTP)・天気・残り杯数・背景写真・日本語表示 | ✅ 実機確認済み（タグ `stage2`） |
| 3 | GAS へ記録・残り 3 杯 / 0 杯（至急）でメール通知 | ✅ 実機で送信〜メール受信まで確認済み（タグ `stage3`） |
| 4 | 画面追加（今日の状況グラフ・メニュー・設定・スクリーンセーバー） | 未着手 |
| — | LINE WORKS 通知 | 未着手（GAS に送信先を足す想定） |
| 5 | 省電力・バッテリー・3D プリント筐体 | 未着手 |
| 6 | 遊び要素（達成アニメ等） | 未着手 |

次に何をやるかはユーザーが未決定（上の 4 / LINE WORKS / 5 を提示済み）。

## 3. 全体構成

```
ESP32 (firmware/)
  ├─ 画面: LVGL 8.4 / HOME 画面のみ
  ├─ Wi-Fi → NTP (JST) / Open-Meteo 天気（千葉市, 15 分ごと）
  └─ +1・補充・日付変更イベント → HTTPS POST → Google Apps Script (gas/)
                                                 ├─ スプレッドシート「ログ」に追記
                                                 └─ event=take で残りが 3 または 0 に「減った瞬間」(prev>left) → 「設定」シートの宛先へ HTML メール
                                                    （0 杯は件名【至急】・赤基調。0 のまま押され続けても再送しない）
```

### HOME 画面の仕様
- 表示: 日付 `9月19日（土）` / 時刻 / 天気 `くもり 22°C` / TODAY（本日杯数）/ +1 ボタン / LEFT（残り）/ COFFEE TIME ロゴ / Wi-Fi アイコン
- **TODAY・LEFT・COFFEE TIME は英語のまま**にする（ユーザー指示）。日付と天気は日本語（ユーザー指示）。
- +1: `LV_EVENT_CLICKED` のみで加算（長押し中の連続加算なし）。TODAY+1、LEFT-1（0 未満にしない）
- LEFT を 1.5 秒長押し → 残り 10 杯に補充（上限 10 はユーザー指定）、トースト表示
- LEFT の色: 3 以下オレンジ / 0 で赤
- 日付が変わると TODAY=0, LEFT=0（「まだ作っていない」扱い）。**この仕様はユーザー未確定**。運用で要相談
- 杯数は NVS（Preferences, namespace `cup`）に保存。電源 OFF でも保持
- 背景写真は時間帯で切替: 5–11 時 朝 / 11–16 時 昼 / それ以外 夕方（Codex の画像生成で作成）

## 4. リポジトリ構成

| パス | 内容 |
|---|---|
| `firmware/platformio.ini` | env `app`（本体）/ `hwtest`（LCD・タッチ試験） |
| `firmware/src/app/main.cpp` | 初期化、loop（ネット処理・NVS 保存・開発用シリアルコマンド） |
| `firmware/src/app/HomeScreen.*` | HOME 画面 |
| `firmware/src/app/CupState.*` | 杯数状態と NVS |
| `firmware/src/app/NetService.*` | Wi-Fi / NTP / 天気 / GAS 送信キュー |
| `firmware/src/app/lvgl_v8_port.*` | ESP32_Display_Panel 公式 simple_port のまま（触らない） |
| `firmware/src/app/fonts/` | 生成フォント。日本語は `ct_font_jp_symbols.txt` の文字だけ収録 |
| `firmware/src/app/images/` | 背景画像（480×480 RGB565 の C 配列） |
| `firmware/include/` | ボード選択 (2.8C)・LVGL 設定・`secrets.h`（Git 管理外） |
| `gas/Code.gs` | GAS 本体（記録・通知・HTML メール） |
| `gas/appsscript.json` | マニフェスト（Web アプリ: 自分として実行 / 全員アクセス） |
| `assets/backgrounds/` | 背景画像の元 PNG |
| `tools/` | 開発補助（下記） |

## 5. 開発環境と手順

### 前提（この PC で確認済み）
- Python 3.14（pyserial, pillow, esptool, platformio 導入済み）、Node.js、Git、gh（`skyblueearthjapan` でログイン済み）
- PlatformIO core は **`C:/pio`**（`platformio.ini` の `core_dir`）。Windows の長いパスが無効・ユーザーは管理者でないため
- ESP32 は native USB で **COM8**（変わる可能性あり）

### ビルド・書き込み（**必ず PowerShell から**）
```powershell
cd firmware
python -m platformio run -e app -t upload --upload-port COM8
```
> ⚠️ Git Bash から PlatformIO を初回実行すると `MSys/Mingw is not supported` でツールチェーンが空のままになる。PowerShell を使う。

### 開発用ツール（シリアルポートは同時に 1 プロセスしか開けない）
| コマンド | 内容 |
|---|---|
| `python tools/serlog.py COM8 30` | ログを 30 秒表示（各行に PC 時刻付き） |
| `python tools/serlog.py COM8 45 send=RTT` | 接続後に開発コマンドを送りつつログ表示 |
| `python tools/snapshot.py COM8 out.png` | **実機画面をスクリーンショット**（エージェントが画面を確認する手段） |
| `python tools/send.py COM8 M` | 1 文字コマンド送信 |
| `python tools/img2lvgl.py in.png out.c name --dim 0.75` | 背景画像を LVGL 用に変換 |
| `bash tools/gen_fonts.sh` | フォント再生成（日本語を足したら `ct_font_jp_symbols.txt` に文字を追加してから） |
| `powershell -File tools/gen_bg.ps1 <name> "<雰囲気>"` | Codex CLI で背景画像生成 |

シリアル開発コマンド（`main.cpp`）: `S`=スクショ / `M`・`N`・`E`=背景を朝・昼・夕に固定 / `A`=自動に戻す /
`T`=+1 / `R`=補充。**`T` と `R` は実際に GAS へ送信され、残り 3 杯・0 杯になると本当に宛先全員へメールが飛ぶ**ので注意。

### GAS（clasp）
- clasp 3.2.0。**Workspace アカウントは名前付きユーザー `work`**（`imaizumi@lineworks-local.info`）。
  既定ユーザーは個人 Gmail なので、**GAS 操作は必ず `clasp -u work ...`**
- スクリプトはユーザーが作成したスプレッドシートに紐づけ済み（`gas/.clasp.json`、Git 管理外）
- 変更の反映:
  ```bash
  cd gas
  clasp -u work push -f
  clasp -u work list-deployments          # デプロイ ID を確認
  clasp -u work update-deployment <デプロイID> --description "..."   # URL を変えずに更新
  ```
  `create-deployment` すると URL が変わり ESP32 の書き換えが必要になるので、通常は `update-deployment`。
- 通知先: スプレッドシート「設定」シート A 列（現在 2 人登録。ユーザーは最終的に 4 人の予定）
- 差出人表示名: `SENDER_NAME = 'CaféTamu'`（ユーザーは `coffeetime` も候補に挙げていた）。アドレス自体は実行アカウントのまま
- 見本メール: `event: "preview"`（任意で `left`）を正しい TOKEN で POST すると**所有者だけ**に送られる（ログに残らない）
- ESP32 は各イベントに `prev`（イベント前の残り杯数）を付けて送る。GAS の通知判定に使用
- メール HTML は PC 2 カラム / 600px 未満で 1 カラム。確認は headless Edge + 375px iframe で描画して行った

## 6. 秘密情報の置き場所（すべて Git 管理外・中身をコミットしないこと）

| ファイル | 中身 |
|---|---|
| `firmware/include/secrets.h` | `WIFI_SSID` / `WIFI_PASSWORD`（ユーザー入力）、`GAS_URL`、`GAS_TOKEN` |
| `gas/Secret.gs` | `TOKEN`（`GAS_TOKEN` と同じ値。clasp push で GAS に送られる） |
| `gas/.clasp.json` | scriptId / parentId |
| `backup/factory_firmware_16MB.bin` | 工場出荷ファームのフルバックアップ（書き戻し可能） |

`secrets.h` を編集するときは、ユーザーの Wi-Fi パスワードを表示しないよう、`sed` で該当行だけ置換する運用にしてきた。

## 7. ハマりどころ（解決済み・再発防止）

1. **Windows のパス長制限** → PlatformIO core を `C:/pio` に。ライブラリの git clone は `core.longpaths=true`
2. **Git Bash で PlatformIO** → ツールチェーン導入失敗。PowerShell で実行
3. **Open-Meteo の JSON 解析失敗** → chunked 転送のため。`http.useHTTP10(true)`
4. **GAS への POST が HTTP 400** → GAS は 302 で別ホストへ応答を渡す。HTTPClient の自動追従はヘッダーを引き継いで 400 になる。
   `Location` を取り出し**新しい接続で GET** するよう修正済み（`NetService.cpp` の `sendReport`）
5. **clasp `create-script --type sheets --parentId`** は親を無視して新規シートを作る → `--parentId` のみで作成すること。
   また create 時に `appsscript.json` が既定値で上書きされるので作成後に戻す
6. 送信は失敗時 30 秒ごとに再送。キューは RAM（32 件）なので再起動で未送信分は消える。GAS 側はイベント ID で 6 時間重複排除

## 8. 未解決・要確認事項

- [ ] 最初の clasp の手違いで Drive に**空のスプレッドシート「COFFEE TIME 記録」(ID `1jht7j0…`) が残っている**。ユーザーに削除可否を確認中（ゴミ箱に移すか、エージェントが削除するか）
- [ ] 日付変更時に LEFT を 0 にするか 10 にするか（現在 0）
- [ ] Wi-Fi 電波が弱い（RSSI 約 -88dBm）。設置場所で要確認
- [ ] HTTPS は `setInsecure()`（証明書検証なし）。社内用途として許容しているが、将来は CA バンドル化を検討
- [ ] 段階 1 の未実施試験: 50 回連続タップ、電源 OFF→ON（NVS 導入後は「杯数が保持される」が期待値に変わった）
- [ ] 背景画像生成で GPT Image 2.5 が使われたかは Codex から版数が出ず未確認
- [ ] 0 杯（至急）通知は見本メールのみ確認済み。実機で 1→0 にしての送信確認は未実施
- [ ] 通知先はあと 2 人追加予定（ユーザーが「設定」シートに入力）

## 9. 次の担当者への作業の進め方

- 変更 → PowerShell でビルド・書き込み → `snapshot.py` で画面確認 → 必要ならユーザーに目視・タッチ確認 → コミット・push、の順
- ユーザーは結果だけでなく「どこに保存され、どこにデプロイされたか」を知りたがる。都度、簡潔に説明する
- 大きな段階が終わったら、試験記録を `docs/` に残し、タグを打つ（`stage1`〜`stage3` 作成済み）
