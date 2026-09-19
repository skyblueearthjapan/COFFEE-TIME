# COFFEE TIME

> 開発を引き継ぐ場合は、まず [docs/HANDOFF.md](docs/HANDOFF.md) を読むこと。

社内カフェコーナー用のタッチ式コーヒーカウンター端末。

- ハードウェア: Waveshare ESP32-S3-Touch-LCD-2.8C（480×480 丸型 LCD / GT911 タッチ）
- ファームウェア: PlatformIO + Arduino-ESP32 3.3.11 + ESP32_Display_Panel 1.0.4 + LVGL 8.4.0

## フォルダー構成

| パス | 内容 |
|---|---|
| `firmware/` | ESP32 のプログラム（PlatformIO プロジェクト） |
| `firmware/src/app/` | COFFEE TIME アプリ本体 |
| `firmware/src/hwtest/` | LCD・タッチのハードウェア試験 |
| `firmware/include/` | ボード選択（2.8C）・LVGL などの設定ファイル |
| `gas/` | 記録・通知サーバー（Google Apps Script、clasp で管理） |
| `tools/` | 開発補助スクリプト |
| `docs/` | 引き継ぎ書・試験記録 |
| `参考データ/` | 企画資料・UI イメージ |

## ビルドと書き込み（PowerShell から実行）

> Git Bash からは ESP32 ツールの導入に失敗するため、PowerShell を使うこと。

```powershell
cd firmware
python -m platformio run -e app -t upload --upload-port COM8   # アプリを書き込む
python -m platformio run -e hwtest -t upload --upload-port COM8 # ハード試験を書き込む
python ..\tools\serlog.py COM8 30                               # ログを 30 秒表示
```

- PlatformIO のツール類は Windows のパス長制限を避けるため `C:\pio` に置いている（`platformio.ini` の `core_dir`）。
- COM ポート番号は接続する PC・USB 端子で変わる。

## 開発段階

1. ✅ 段階0: 実機の動作確認
2. ✅ 段階1: +1 カウンター（再起動で 0 に戻る） — [試験記録](docs/TEST_RECORD_stage1.md)
3. ✅ 段階2: 時刻・天気・残り杯数・時間帯別の背景・日本語表示
4. ✅ 段階3: Google Apps Script へ記録・残り 3 杯でメール通知（`gas/`）
5. 段階4: 画面追加（今日の状況・メニュー・設定など）
6. 段階5: 省電力・筐体
7. 段階6: 遊び要素
