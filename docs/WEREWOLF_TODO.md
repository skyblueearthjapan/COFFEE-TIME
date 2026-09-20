# 人狼（CAFE WEREWOLF v2.0）実装 やることリスト

最終更新: 2026-09-21 / 最新コミット `fe1f785` / 段階 **W1 完了・W2 未着手**

正本の設計書: `参考データ/ゲーム4部作/COFFEE_TIME_CAFE_WEREWOLF_Design_v2.0.md`（214KB）
付属データ（リポジトリ内に取り込み済み）: `firmware/src/app/games/werewolf/data/`
受入試験 99 項目: 同フォルダーの `acceptance_tests.json`（**すべて未実施**）

---

## 0. 次の担当者がまず行うこと

1. ESP32 を USB でつなぎ、COM ポートを確認（native USB = COM8 / UART 側 = COM9）
2. `python tools/snapshot.py COM8 now.png` で現在の画面を確認
3. ビルドは **PowerShell から** `cd firmware; python -m platformio run -e app -t upload --upload-port COM8`
4. 開発用シリアルコマンドで画面を直接開ける: `1`=メニュー / `2`=ゲーム一覧 / `3`=人数選択 / `0`=HOME へ戻る
   （`python tools/send.py COM8 2` または `python tools/serlog.py COM8 20 send=2`）
5. ユーザーへの未回答の質問（§5）を確認する

---

## 1. W1 で完成しているもの（実機確認済み）

- [x] 人狼ロジックコアの取り込み（`games/werewolf/core/*.hpp`、**無改変**）
- [x] ホスト検証の再実行（この PC で全 PASS。`python -m pip install --user ziglang` で入れた zig を
      コンパイラーに指定して `tools/run_checks.py --compiler <ラッパー>` を実行）
- [x] 文言・ルールデータの C++ 化（`tools/gen_game_data.py` → `WerewolfContent.h/.cpp`、再生成可能）
- [x] 画面スタック（`ui/ScreenManager.*`）、共通 UI 部品（`ui/UiKit.*`）、メニュー（`ui/MainMenu.*`）
- [x] ゲーム一覧・人数選択（`games/werewolf/WerewolfUI.*`）。人数 3〜10、役職枚数と議論時間を表示
- [x] 日本語フォント 431 字（`ct_font_jp_20/22/40`）と数字 64px（`ct_font_time_64`）
- [x] 使用文字の自動収集（`tools/collect_ui_chars.py`。`--check` で不足検出）
- [x] コードレビュー指摘 9 件（高 2・中 2・低 5）の修正

**まだコアはゲーム進行に使われていない。** `WerewolfCoreCheck.cpp` はコンパイル確認用のダミー。

---

## 2. W2（本体の実装）— ここから着手

### 2-1. Port（ハード接続層）の実装 ★最初にやる
`core/port_contract.hpp` の `struct Port` を実装したクラスを `WerewolfPort.cpp` として作る。

- [ ] `monotonicNowMs()` … `millis()`（64bit へ拡張。49 日で桁溢れしないよう積算する）
- [ ] `randomWord()` … `esp_random()`。**Wi-Fi/BT が無効だと質が落ちる点**を設計書が警告しているので、
      配役時は Wi-Fi 有効を確認するか、RTC・ADC ノイズを混ぜる
- [ ] `cutBacklight()` / `restoreBacklight()` … `board->getBacklight()`（ESP32_Display_Panel）
- [ ] `requestNeutralScanout()` … 中立画面を表示し、**RGB パネルに実際に描画が届くまで**待つ
      （`privacy_fence.hpp` の epoch と突き合わせる。物理消去の完了検知は設計書も「未実装」と明記）
- [ ] `latestFreshDriverSample()` … GT911 の生データ。**LVGL の indev 経由ではなく**、
      `board->getTouch()` から直接読む（`touch_stale_ms=200` の鮮度判定が必要なため）
- [ ] `readPublicMeta()` / `writePublicMetaAndReadBack()` … NVS `ct_wolf` / キー `meta` / 20 バイト
      （`public_meta.hpp` の CTW2。書き戻し確認まで行う）
- [ ] `inhibitDeepSleep()` … 省電力導入前は空実装でよい
- [ ] `openExistingCafePanel()` … `ui::goHome()`
- [ ] `releasePriorSinglePlayerSession()` … 現状は常に true

### 2-2. 画面（約 30 種類。`data/layout.json` に配置仕様あり）
- [ ] ロビー（人数確定・注意書き 4 ページの必読、`mandatory_brief`）
- [ ] 夜：手渡し案内 → 役職確認（SecretGate）→ 占い対象の選択 → 中立画面 → 次の人へ
- [ ] 昼：議論タイマー（`ct_font_time_64`、延長 1 回まで・`discussion_extension_s=60`）
- [ ] 投票：自分以外＋「人狼はいない」、4 席ずつのページ送り（`paging.hpp`）
- [ ] 決選投票（最大 1 回）
- [ ] 結果：役職・伏せ札 2 枚・全員の票を開示
- [ ] 中断（`abort_reasons` の 7 種）と、再起動時の「無効な局」表示

### 2-3. SecretGate（覗き見防止）★最重要
- [ ] 押し続けている間だけ秘密を表示（`hold_before_reveal_ms=500`、`max_visible_ms=8000`）
- [ ] 指を離したら即座に消す（`privacy_hide_normal_max_ms=120` / 遅くとも 250ms）
- [ ] 画面に残像が残らないこと（RGB パネルのため、実際に書き換わるまで待つ必要あり）
- [ ] 複数点タッチ・古いタッチ情報（200ms 超）を無効として扱う
- [ ] **実機で必ず動画撮影して確認する**（設計書 §privacy の受入試験）

### 2-4. 音（ブザー）
- [ ] `rules.json` の `sounds`（1600Hz/30ms、1200Hz/70ms）を GPIO のブザーで鳴らす
- [ ] 既定はオフ。**秘密画面では絶対に鳴らさない**（設計書の要件）
- [ ] ディレクター判断として「秘密の表示開始・終了だけ音を出す案」をユーザーに相談する

### 2-5. 仕上げ
- [ ] `acceptance_tests.json` の 99 項目を実施し、結果を `docs/TEST_RECORD_werewolf.md` に記録
- [ ] 3 人・4 人での実プレイ（設計書は 3〜4 人を優先と明記）
- [ ] 電池での連続稼働時間の実測
- [ ] タグ `werewolf-v2` を打つ

---

## 3. 実装中に気をつけること（今回ハマった点）

1. **LVGL のボタンは既定の内側余白が大きい**。`lv_obj_align` が効かないように見えたら
   `lv_obj_set_style_pad_all(btn, 0, 0)` を疑う（実測: 高さ 62px のボタンで文字領域は 18px しかなかった）
2. **日本語は自動折り返しされない**。長い文字列は `lv_label_set_long_mode(LV_LABEL_LONG_CLIP)` ＋ `lv_obj_set_width`
3. **フォントを 1 つに統合しない**。共有フォントに 431 字を足したらリンクエラー
   （`dangerous relocation: call8: call target out of range`）。HOME 用とゲーム用は分ける
4. **画面遷移中に `lv_obj_del` しない**。`lv_scr_load_anim(..., auto_del=true)` を使う
5. **Arduino の `bit()` マクロ**がコアの `coffee::wolf::bit()` と衝突する → `#undef bit` 後にコアを include
6. **Python のヒアドキュメントで `\n` を書くと実際の改行になり C++ の文字列が壊れる**。
   Edit ツールを使うか `chr(10)` を使う（今回 5 回踏んだ）
7. **秘密は RAM のみ**。NVS に配役を書かない（設計書の `secret_persistence: ram_only`）
8. 文字を追加したら `python tools/collect_ui_chars.py` → `bash tools/gen_fonts.sh` の順で更新

---

## 4. 残りの 3 作（着手順は 人狼 → 探偵 → エスパー → AI DUEL）

- **喫茶「余白」の事件簿（探偵）**: 1 人用・全 3 話。AI なしでも成立する。必要文字 1293 字（フォント追加が必要）。
  GAS に 6 タブ追加。ロジックは Node テスト 71/71 PASS 済み
- **エスパー対決**: AI 必須（通信断時はローカル簡易版）。**設計書が新規 Python サーバーを前提**にしており、
  既存 GAS に寄せるか要判断
- **AI DUEL**: 最大規模（XL）。PIN 認証・シート 9 タブ・コミット検証。Wi-Fi が弱い現状では通信 20 往復が不安

---

## 5. ユーザーへの未回答の質問（次の担当者が最初に確認する）

- [ ] **人数の初期値**: 3 人のままか、4〜5 人にするか、前回の人数を覚えるか（W2 の最初に反映したい）
- [ ] 日付が変わったときの LEFT を 0 にするか 10 にするか（カフェ機能・現在は 0）
- [ ] 通知先の残り 2 人の登録（スプレッドシート「設定」シート）
- [ ] 手違いで Drive にできた空のスプレッドシート（ID `1jht7j0…`）を削除してよいか
- [ ] Jev API キーの受け渡し方法（ユーザーは入手済み。**端末には置かずサーバー側に置く**方針。
      チャットに書かず Git 管理外のファイルへ貼ってもらう）
- [ ] Wi-Fi の受信感度対策（ケースの金属リングが原因と推定。現在は保留）
