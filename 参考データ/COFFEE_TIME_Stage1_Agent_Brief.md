# COFFEE TIME — 段階1「＋1カウンター」開発Agent向け引継ぎ

作成日：2026-09-19  
対象：**Waveshare ESP32-S3-Touch-LCD-2.8C（丸型・480×480）**  
対象外：末尾なし2.8、2.8B、1.28、1.85Cなど、別型式の設定の流用

## 0. 最初に読むこと

**既に動いている基板初期化を残し、アプリケーションの画面だけを交換する。** これが第一方針です。

段階0で実際に何を使って書き込んだか、ソースの保存場所、インストール済みの版数は、この資料では未確認です。「Arduinoだった」「このフォルダーにある」と推測して報告しないでください。PCにアクセスできる開発Agentは、最初に既存プロジェクト・ビルドログ・IDE設定を確認してください。VS Codeはエディター名なので、それだけではArduino/ESP-IDF/PlatformIOの区別はできません。

Waveshareの説明書・回路図と、Espressifの公開ボード定義・LVGL移植サンプルのソースを照合しています。ただし、**Waveshare配布ZIPの中身をこの作業環境に取得して検証することはできていません**。ZIP内の実装詳細は、実際に取得した開発Agentが確認してください。添付UIはオリジナルのLVGL 8用コードです。**実ライブラリでのコンパイルと実機動作は未検証**です。

この資料で区別するもの：

- **確認済み事実**：メーカー資料・Espressif公開コードに記載された仕様。
- **今回の設計提案**：画面レイアウト、段階分け、試験項目。
- **未確認事項**：ユーザーのPC環境、基板リビジョンとの最終適合、実際のビルド・書込み・タッチ動作。

## 1. 段階1の完成条件

起動すると「COFFEE TIME」「0」「CUPS TAKEN」「＋1」のボタンを表示します。ボタンを1回押して離すと1増え、素早く2回押して離すと2増えます。押し続けている間に連続加算してはいけません。

この段階では**起動後の消費杯数**だけを数えます。再起動で0に戻る仕様です。日付境界処理がないため「TODAY」という表記は使いません。「＋1」はコーヒーを作った量ではなく、**取った・飲んだ杯数を記録する操作**です。

Wi-Fi、時計、天気、残量、通知、NVS保存、microSD、ブザー、バッテリー残量、深いスリープは追加しません。最終製品では1500mAhのLiPoを使う予定ですが、段階1の動作切り分けはUSB給電で行います。

## 2. 段階0の環境を壊さず確認する

### 2.1 調べる対象

ユーザーがアクセスを許可した作業フォルダーを優先し、必要に応じてArduino IDEの「最近使ったスケッチ」と「スケッチブックの保存場所」、VS Codeの開いているワークスペース、既存のビルドログを確認します。PC全体を無差別に走査しないでください。

| 見つかったもの | 判定・確認事項 |
|---|---|
| `.ino`、Arduinoのビルドログ | Arduinoベースの可能性。実際の使用coreとライブラリのパス・版数も確認。 |
| `platformio.ini` | `framework = arduino` / `espidf` を確認。PlatformIOがあるだけでESP-IDFと決めつけない。 |
| `CMakeLists.txt`、`sdkconfig`、`idf_component.yml`など | CMake内のESP-IDF project includeなどでESP-IDFプロジェクトか確認。 |
| `*.bin`のみ | 書込み済みバイナリーであって、編集できるソースとは限らない。 |
| 起動するとデモが動くだけ | 工場出荷時または完成済みファームを書いただけの可能性もあり、開発環境は特定できない。 |

Waveshareは完成済みテストファームの書込み手順も公開しています。サンプルが動くことと、再ビルドできるソースが手元にあることは別です。[S4]

### 2.2 保全

元プロジェクトを別フォルダーにコピーするか、Gitで変更前をコミットします。既存コード・設定・版数が不明なまま、ライブラリの全更新、Arduino15の置換、Flashの全消去はしません。

次を記録します。

```text
基板型式／基板印刷のリビジョン：
既存プロジェクトの絶対パス：
元デモの入手先・ZIP名またはGit commit：
Arduino coreまたはESP-IDFの版数：
ESP32_Display_Panelの版数（使用する場合）：
LVGLの版数：
実際に読み込まれたlv_conf.hのパス：
ボード設定、Flash、PSRAM、USB CDC設定：
接続したUSB端子：
実際のCOMポート：
段階0の再ビルド結果：
段階0の表示確認：
段階0のタッチ確認：
```

画面が表示できただけでは、タッチが使えていると判断しません。段階0を再ビルドして書き込み、タッチ座標またはクリック反応まで確認できれば、その環境を継続します。

## 3. 採用する土台を決める — 2系統を混ぜない

### A. 既存のWaveshareサンプルが再現できる場合：そちらを優先

公式Arduino説明ページは、`01_LVGL_Arduino`、LVGL **8.3.10**、同梱`Arduino/libraries`、オフライン導入の **esp32-XIP-3.0.2** を指定しています。この指定は「Waveshareの当該デモを再現するための組合せ」であって、2.8Cを使うすべての開発方法の唯一の条件ではありません。[S2]

注意：2026-09-19に確認したページでは、本文はXIP 3.0.2ですが、インストールリンクのリンク先名は`Esp32-XIP-3.1.1_Development_Board_Installation`でした。**版数の不一致があります。勝手に同一視しないでください。** 実際のZIP、既存core、変更履歴を確認し、再現できた組合せを固定してください。[S2]

WaveshareのESP-IDF側には`ESP32-S3-Touch-LCD-2.8C-Test`というテストプロジェクトが案内されています。既にそれが動くなら、Arduinoへ移行せずその`app_main`側で画面を置き換えます。公式手順に登場するESP-IDF 5.5.2は説明例で、すべての配布サンプルの必須バージョンと断定しないでください。[S3]

#### Aで変更する範囲

1. 元デモのLCD・タッチ・外部I/O・バックライト・PSRAM設定をそのまま残す。
2. 実際のソースから、LVGLの画面生成箇所を探す。`.ino`の`setup()`またはIDFの`app_main()`内など。
3. デモ画面を生成する呼出しだけを、添付の`coffee_counter_create()`へ置き換える。
4. 既存のLVGLタスク／タイマー呼出し／mutexの扱いを維持する。
5. ドライバーの定期処理が旧デモのウィジェットを直接更新していないか確認する。参照があるなら、その**旧UI更新だけ**を切り離し、必要なハードウェア初期化を削除しない。
6. 公式説明にはBOOTボタンでのデモ操作も記載されている。画面成功・BOOT操作成功だけで、GT911タッチの実装・初期化成功と判断しない。[S2]

既存サンプルの具体的な関数名・ファイル構成は、取得した実物に合わせて変更してください。この資料の別系統の`lvgl_port_lock()`などを、存在しないプロジェクトにそのまま足してはいけません。

### B. 元ソースが不明／再現できない場合：Espressifの対応済みボード設定を使う

新規の独立プロジェクトで、Espressifの **ESP32_Display_Panel** を使います。同ライブラリには2.8C専用のボード定義が存在します。これはWaveshare配布ZIPとは**別系統の公式サポート経路**です。[S6][S7][S8]

確認した組合せの指定：

| 部品 | 公開資料にある条件 |
|---|---|
| Arduino IDE | 2.x系を使う案内 |
| arduino-esp32 core | **3.1.0以上** |
| ESP32_Display_Panel | 選択したリリースまたはcommitと同じサンプル・設定を使用 |
| ESP32_IO_Expander | **1.0.0以上、2.0.0未満** |
| esp-lib-utils | **0.2.0以上、0.3.0未満** |
| LVGL | `lvgl_v8/simple_port`のREADME指定は **8.4.0** |

これらは公開資料上の要件であり、この引継ぎで実機試験した組合せではありません。coreの最小条件を満たすだけで、将来の任意の最新版まで保証されるわけではありません。実際に選んだ版数・commitを記録し、再現試験を行います。[S6][S10][S14]

調査時のリポジトリ`master`の`library.properties`は1.0.5表記でした。ただし、同名Gitタグの存在は確認できていません。「タグv1.0.5が必ずある」として取得処理を書かないでください。[S14]

## 4. なぜ「配線・初期化が特殊」なのか

基板上のLCDとタッチは既に接続されています。段階1で別途ジャンパー線を引くという意味ではありません。**ソフトウェアが基板内の制御経路を正しく扱う必要がある**という意味です。

Espressifの2.8C専用ボード定義では、次の構造です。[S8]

| 機能 | 基板設定の要点 |
|---|---|
| 画面 | ST7701系、480×480、16bit RGBで画像を更新 |
| 画面の初期設定 | 3線式SPIのコマンドで設定後、RGBバスで表示 |
| 画面のCS | **I/O拡張チップのP2**。ESP32のGPIO2ではない |
| SPIのクロック | ESP32の**GPIO2**。上のP2と別物 |
| SPIのデータ | GPIO1 |
| タッチ | GT911、I²C。SCL=GPIO7、SDA=GPIO15、INT=GPIO16 |
| タッチのリセット | **I/O拡張チップのP1**を使う処理 |
| I/O拡張 | TCA9554系、ボード定義のドライバー名はTCA95XX_8BIT、アドレス0x20 |
| バックライト | GPIO6、アクティブHigh |

特に避けること：

- `CS=2`だけ見て`digitalWrite(2, ...)`する。
- タッチの`RST_IO=-1`だけを見て「リセットは不要」と判断する。
- GT911用I²Cを、同じバスの別設定で勝手に初期化し直す。
- ST7701のベンダー初期化コマンドを、別LCDの短いサンプルに置き換える。
- RGBタイミングやPSRAM設定を、別のサイズ・型番からコピーする。

ボード定義はGT911のINTと拡張P1を操作する起動シーケンスまで保持しています。`Board::init()`でオブジェクトを用意し、`Board::begin()`で外部I/O、LCD、タッチ、バックライトを開始する流れごと使います。[S8][S15]

## 5. Bの具体的な準備手順

### 5.1 PC接続

書込みとログの条件を揃えるため、最初は基板の **USB TO UART側Type-C** とデータ通信対応USBケーブルを使います。基板には別のUSB Type-Cもあるため、外観で曖昧に決めず公式写真・基板印刷で確認します。USB TO UART側にはCH343Pが接続されています。[S1]

Windowsの場合はデバイスマネージャーで実際のCOMポートを確認します。`COM3`などを決め打ちしません。ポートが出ない場合はケーブル、接続口、認識状態を先に調べ、必要なドライバーはメーカーの公式案内から導入します。[S5]

### 5.2 ライブラリ

Espressifの環境構築資料[S6]に従い、上記Bのライブラリ群を導入します。同名LVGLが複数存在する場合は、コンパイル詳細ログから**本当に読み込まれたパス**を確認します。Waveshare用LVGL 8.3.10が既にある環境を、無断で8.4.0に上書きしないでください。別のスケッチブック等で環境を分離するか、元に戻せる方法で管理します。

最初から最新版LVGL 9を入れないでください。ここで選んでいるのはLVGL 8のサンプルです。[S6][S10]

### 5.3 Arduinoのボード設定

Espressifが2.8C用に公開している設定を使います。[S7]

| 設定 | 値 |
|---|---|
| Board | `ESP32S3 Dev Module` |
| PSRAM | `OPI PSRAM`／`OPI` |
| Flash Size | `16MB` |
| Flash Mode | `QIO 80MHz` |
| Partition Scheme | `16M Flash (3MB)`相当の16MB向け設定 |
| USB CDC On Boot | **USB TO UART側：Disabled**。native USB側を使う場合は**Enabled**。 |
| Port | 接続した実機のCOMポート |
| Serial Monitor | `115200` |

USB CDCは一律Enabledではありません。Espressifの表にはEnabledが載っていますが、同資料の注記は「UARTではDisabled、USBではEnabled」と区別しています。既存の動作環境では、その接続方法に合った設定を保持してください。[S7]

### 5.4 まずハードウェアだけを試験

同じ版の`ESP32_Display_Panel`から、次の**フォルダー全体**をコピーします。[S9]

```text
examples/arduino/board/board_static_config/
```

`.ino`だけを抜き出すのではなく、付随する設定ヘッダーも保持します。プロジェクト内の`esp_panel_board_supported_conf.h`の既存項目を変更します。[S11]

```cpp
#define ESP_PANEL_BOARD_DEFAULT_USE_SUPPORTED (1)
#define BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_2_8_C
```

型番のマクロは**`_2_8_C`**です。他のボードの選択行は有効にしません。custom boardの選択フラグも同時に有効にしません。設定ファイル全体をこの2行だけに置き換えないでください。末尾の設定バージョン項目なども必要です。[S6][S11]

期待する結果は、LCDのカラーバー表示と、タッチに応じて変化する座標のログです。中央・上下左右を触って確認してください。ここが成功しなければカウンター実装へ進まず、先にハード設定を解決します。[S9]

### 5.5 LVGLの基本画面を試験

同じ版の次の**フォルダー全体**を別プロジェクトにコピーします。[S10]

```text
examples/arduino/gui/lvgl_v8/simple_port/
```

最低限、次のような一式を保持します。

```text
coffee_time_stage1/
  coffee_time_stage1.ino      ← simple_port.inoをプロジェクト名に合わせて変更
  esp_panel_board_supported_conf.h
  esp_panel_board_custom_conf.h
  esp_panel_drivers_conf.h
  esp_utils_conf.h
  lv_conf.h
  lvgl_v8_port.cpp
  lvgl_v8_port.h
```

こちらのプロジェクトにも、5.4と同じ2.8Cのボード選択を行います。`board_static_config`側を直しただけでは、別コピーの`simple_port`側が自動で変更されるとは限りません。

まず無改造の`Hello World`表示を確認します。RGB用の色設定、フレームバッファ、反転、回転、アンチティアリング等は、最初は同梱設定を維持します。公開READMEにはRGBで`LV_COLOR_16_SWAP=0`、ESP32-S3ではOPI PSRAMを使う注意があります。[S10]

## 6. カウンターUIへの交換

### 6.1 添付ファイルの役割

```text
CoffeeCounterUI.h
CoffeeCounterUI.cpp
```

これは**完全なファームウェアではありません**。LCD・タッチが動作するLVGL 8プロジェクトに追加する、画面部分だけのコードです。ボードドライバー、LVGL本体、フォントファイルは同梱していません。

表示は英語ASCIIだけを使用し、日本語・絵文字用フォントの準備を段階1から除外しています。`lv_font_montserrat_24`と`48`が有効なら使い、無効なら既存のデフォルトフォントにフォールバックします。小さく見える場合は、実際に使われている`lv_conf.h`の該当フォント設定を確認してください。公式`simple_port`の調査時設定では両サイズが有効でした。[S16]

### 6.2 Bのsimple_portでの変更位置

`coffee_time_stage1.ino`に追加します。

```cpp
#include "CoffeeCounterUI.h"
```

元の`setup()`にあるボード初期化・フレームバッファ設定・`lvgl_port_init()`は残します。元のロックとアンロックの**間にあるHello Worldなどのラベル生成部分だけ**を、次に置き換えます。[S12]

```cpp
// 既存の「Creating UI」部分を置換する例。
// BoardとLVGLは、ここより前で初期化済みであること。
if (!lvgl_port_lock(-1)) {
    Serial.println("ERROR: LVGL lock failed");
    return;
}
const bool ui_ok = coffee_counter_create();
lvgl_port_unlock();
if (!ui_ok) {
    Serial.println("ERROR: COFFEE TIME UI creation failed");
}
```

既存の`lvgl_port_lock()`を残したまま、このブロックを内側に追加しないでください。**ロックの組は1組**になるように置き換えます。`coffee_counter_create()`は1回だけ呼びます。

初期化が失敗した場合は画面生成に進まないよう、採用版の`Board::init()`、`Board::begin()`、`lvgl_port_init()`の戻り値を確認してください。既存のRGB設定処理を削除しないことを優先します。

同じプロジェクトに2つの`setup()`や`loop()`を作らないでください。`lv_init()`を重複させず、`loop()`へ新しい`lv_timer_handler()`を追加しません。このportにはLVGL処理タスクがあるので、二重実行は避けます。[S12][S13]

### 6.3 Aの既存Waveshareプロジェクトの場合

同じUIファイルはLVGL 8用ですが、起動処理・mutex APIはプロジェクト依存です。Bの`lvgl_v8_port.*`をそのまま混ぜないでください。既存のLVGL実行方式で安全に呼べる位置から`coffee_counter_create()`を呼びます。

ESP-IDFで`.cpp`を追加する場合は、既存コンポーネントのソース登録にも追加します。ヘッダーは`extern "C"`対応のため、C側から呼ぶことも想定しています。使用LVGLが8以外なら、先に既存環境を確認し、無断でLVGL全体を差し替えないでください。

### 6.4 タッチの仕様

カウントに使うのは**`LV_EVENT_CLICKED`だけ**です。LVGL 8では、スクロールされなかった押下のリリース時に発生し、長押し後のリリースも含みます。一方`LV_EVENT_PRESSING`は押下中に繰り返されるため、カウンターには不適切です。[S17]

```text
1回押す→離す             +1
2回押す→離すを繰り返す   +2
3秒押し続ける            押下中は加算なし
3秒押した後に離す        +1だけ
```

「連打防止」で0.5秒の`delay()`を入れると、2杯を素早く入力する操作を妨げます。イベント内で待機しないでください。ネットワーク送信・Flash書込みも段階1のイベント内には追加しません。

表示上の「＋1」は消費記録です。「コーヒーを追加しました」など、作り足した量と紛らわしい通知文は使いません。

## 7. 動作確認項目

実施結果は、添付の`TEST_RECORD.md`へ事実だけを記録してください。

| 試験 | 期待結果 |
|---|---|
| 起動 | COFFEE TIME、0、CUPS TAKEN、＋1が表示される |
| 1回タップ | 0→1 |
| すばやく独立した2回タップ | 1→3。押す／離すを2回行う |
| 長押し3秒 | 押下中は増えず、離したとき1だけ増える |
| ボタンの外をタッチ | 増えない |
| 独立したタップを50回 | 開始数＋50と一致する |
| 画面各部・丸いボタン縁 | ボタン以外で誤加算せず、表示位置と反応範囲が一致する |
| 10分程度待機後に操作 | フリーズ・意図しない再起動・描画崩れがなく、その後も＋1できる |
| RESETによる再起動 | 0へ戻る。段階1では正常な仕様 |
| 完全な電源OFF→ON | 正しく初期化され、0で起動する |

LiPoがつながっている場合、USBを抜いただけでは電源OFFにならないことがあります。電源スイッチ等で実際に電源が落ちたか確認し、通電したまま配線を抜き差ししないでください。

## 8. よくある不具合の切り分け

| 症状 | 最初に確認すること |
|---|---|
| `lv_btn_create`等が見つからない | LVGL 9を拾っていないか、ライブラリの実パスと版数を確認 |
| 設定未選択・別画面の初期化ログ | supported flagと`BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_2_8_C`、別設定の同時選択 |
| 黒画面・白画面 | 元ハード試験に戻し、電源、OPI PSRAM、ボード型式、バックライト、ST7701初期化の順に確認 |
| 表示は出るが押せない | `getTouch()`、GT911ログ、拡張P1のreset、INT16、I²C初期化、LVGL input登録を確認 |
| 画面とタッチ座標がずれる | XY交換・mirror・rotationの設定がLCDとtouchで整合しているか確認 |
| 押しっぱなしで数が増える | `PRESSING`やlong-press-repeatへ登録していないか確認 |
| フリーズする | LVGL mutex、二重timer handler、旧デモが消したUIを更新していないか確認 |
| 色がおかしい | 同梱RGB設定・RGB565・`LV_COLOR_16_SWAP`から不用意に変えていないか確認 |
| 書込みできない | シリアルモニターを閉じる、実COM、データケーブル、USB端子を確認 |
| ログだけ出ない | USB TO UART/native USBとUSB CDC設定の組合せを確認。画面だけで通信不良と断定しない |

Waveshare FAQでは、シリアルモニターがポートを占有すると書込みに失敗すること、BOOTを押した状態で電源投入してダウンロードモードへ入れる対処等が説明されています。通常書込みで問題があるときに限り、この公式手順を使います。[S5]

完成済みファームの書込みページにある0x00などのアドレスは、その配布ファームの説明です。自分でビルドした任意の`app.bin`を同じアドレスに書く指示へ一般化しないでください。Arduino/IDFのプロジェクト本来のupload/flash手順を優先します。[S4]

## 9. Agentが完了時に報告すること

「完成しました」だけではなく、変更したファイル、元プロジェクト、採用版数、ビルドログ、書込み結果、各受入試験の結果を分けて報告します。

- ビルド成功と実機動作成功を区別する。
- USB/実機にアクセスできないなら「ビルドまで」「実機未確認」と明記する。
- 色・文字表示だけを確認して「タッチ確認済み」と書かない。
- ユーザーのPC保存先は実際に確認した絶対パスだけを書く。
- 一度再現できた段階1は、次段階の前に別コミット／別フォルダーとして保存する。

## 10. 参考：ボード定義のRGBピン（再配線の指示ではない）

次は調査時のEspressif専用定義[S8]の値です。**実装ではヘッダーを再利用し、この表から新しいドライバーを手書きしない**ことを推奨します。

```text
PCLK = 41 / HSYNC = 38 / VSYNC = 39 / DE = 40
RGB D0..D15 = 5,45,48,47,21,14,13,12,11,10,9,46,3,8,18,17
Pixel clock = 18 MHz
H pulse/back/front = 8 / 10 / 50
V pulse/back/front = 2 / 18 / 8
```

Waveshare ZIP内の実装がすべて同じ値・関数名であることまで確認したものではありません。調査後に公式リポジトリが更新される可能性もあるため、採用commitを記録してください。

## 11. 一次資料

以下は2026-09-19に参照した公開情報です。`master`は可変です。開発Agentは使用時に採用する版を固定してください。

[S1] Waveshare：本体仕様・搭載部品・端子
`https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8C`

[S2] Waveshare：Arduino手順、LVGL 8.3.10、XIP指定、01_LVGL_Arduino
`https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8C/Arduino`

[S3] Waveshare：ESP-IDF手順
`https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8C/ESP-IDF`

[S4] Waveshare：完成済みファームの書込み
`https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8C/Firmware-Flashing`

[S5] Waveshare：FAQ
`https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8C/FAQ`

[S6] Espressif：ESP32_Display_PanelのArduino利用手順
`https://github.com/esp-arduino-libs/ESP32_Display_Panel/blob/master/docs/envs/use_with_arduino.md`

[S7] Espressif：Waveshare各ボード対応表・Arduino設定
`https://github.com/esp-arduino-libs/ESP32_Display_Panel/blob/master/docs/board/board_waveshare.md`

[S8] Espressif：2.8C専用ボード定義・ST7701初期化・GT911 reset
`https://github.com/esp-arduino-libs/ESP32_Display_Panel/blob/master/src/board/supported/waveshare/BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_2_8_C.h`

[S9] Espressif：board_static_config（LCDとタッチの試験）
`https://github.com/esp-arduino-libs/ESP32_Display_Panel/tree/master/examples/arduino/board/board_static_config`

[S10] Espressif：LVGL 8 simple_port一式とREADME
`https://github.com/esp-arduino-libs/ESP32_Display_Panel/tree/master/examples/arduino/gui/lvgl_v8/simple_port`

[S11] Espressif：supported board選択ファイル
`https://github.com/esp-arduino-libs/ESP32_Display_Panel/blob/master/esp_panel_board_supported_conf.h`

[S12] Espressif：simple_port.ino、初期化とUI生成の位置
`https://github.com/esp-arduino-libs/ESP32_Display_Panel/blob/master/examples/arduino/gui/lvgl_v8/simple_port/simple_port.ino`

[S13] Espressif：lvgl_v8_port.h、mutexとLVGLタスク設定
`https://github.com/esp-arduino-libs/ESP32_Display_Panel/blob/master/examples/arduino/gui/lvgl_v8/simple_port/lvgl_v8_port.h`

[S14] Espressif：library.properties、依存関係と版数表記
`https://github.com/esp-arduino-libs/ESP32_Display_Panel/blob/master/library.properties`

[S15] Espressif：Board API
`https://github.com/esp-arduino-libs/ESP32_Display_Panel/blob/master/src/board/esp_panel_board.hpp`

[S16] Espressif：同梱lv_conf.h
`https://github.com/esp-arduino-libs/ESP32_Display_Panel/blob/master/examples/arduino/gui/lvgl_v8/simple_port/lv_conf.h`

[S17] LVGL 8.3：イベント定義
`https://docs.lvgl.io/8.3/overview/event.html`

[S18] Waveshare：サンプル・回路図の配布元
`https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8C/Resources-And-Documents`

公式Demo ZIP（本引継ぎ環境では取得・内容検証できなかったもの）：
`https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.8C/ESP32-S3-Touch-LCD-2.8C-Demo.zip`

公式回路図（ブラウザーで図面を確認）：
`https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.8C/ESP32-S3-Touch-LCD-2.8C_schematic_diagram.pdf`
