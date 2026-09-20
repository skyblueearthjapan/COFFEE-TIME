# COFFEE TIME / AI DUEL — 個人履歴つきAIじゃんけん 詳細設計書

**版：1.0.0｜設計確定日：2026-09-20｜対象：Waveshare ESP32-S3-Touch-LCD-2.8C**  
**連携：ESP32 → Google Apps Script → Googleスプレッドシート / TypeSafe Jev**  
**納品物：実装仕様・全選択肢・データ契約・参考コード・検査結果。書込み済みファームウェアではない。**

> コーディングエージェントは、この文書を実装の正本とする。既存のLCD・タッチ初期化、コーヒー＋1処理、Googleスプレッドシート連携を保存してから、独立したゲームモジュールとして追加する。VPSや別のデータベースを必須にしない。
>
> 本書は通常操作だけでなく、個人登録、記録の継続、重複送信、AIの後出し防止、電源断、Google側の制約、退会まで仕様を閉じる。ただし、実機・利用アカウント・Jev APIキーが必要な試験は未実施である。「設計がある」「参考ロジックが通った」「実サービスで動いた」を混同しない。

## 目次

0. 最初に守ること・確定事項
1. ゲームの完成形と全ルール
2. 全データ型・状態・定数
3. 個人登録・本人選択・PIN・ゲスト
4. 個人履歴・統計・癖の定義
5. AIの予測・手の選択・コールドスタート
6. Jevの実API契約
7. 後出し防止とコミット検証
8. Google連携の構成・制約・デプロイ
9. スプレッドシート全定義
10. 通信APIの共通契約と全操作
11. 原子的更新・ロック・冪等性
12. 通信切断・電源断・期限・オフライン
13. 丸型UI・画面遷移・全文言
14. ESP32実装と既存機能の保全
15. 容量・電池・通信頻度
16. セキュリティ・利用同意・削除
17. 運用・保守・負荷・費用
18. 実装フェーズと納品物
19. 受入試験と完成判定
20. 実行済みの事前検査
21. 根拠資料
付録A〜I：設定・操作定義・エラー・シート列・参考コード・検査コード・試験値・結果・取り出し手順

---

## 0. 最初に守ること・確定事項

### 0.1 ユーザー確定事項

| 項目 | 内容 |
|---|---|
| デバイス名 | COFFEE TIME |
| ゲーム名 | AI DUEL。AI ESPERとは別のゲーム |
| ゲーム内容 | グー・チョキ・パーでAIと対戦する |
| 個人履歴 | 個人ごとに保存し、次回以降もその人の過去の手を予測材料として使う |
| 保存先 | Googleスプレッドシート。専用サーバー構築を前提にしない |
| 推論 | TypeSafe JevのAPI。ESP32の中にJevのモデルを保存しない |
| ボード | Waveshare ESP32-S3-Touch-LCD-2.8C、丸型480×480 |
| 入力 | タッチのみ。マイク・音声・カメラを追加しない |
| 電池 | 803450、3.7V、1500mAh、1.25mm 2ピン、保護回路付き。実装時に極性・実寸を照合 |
| ケース | B案の折りたたみスタンド。本体と電池を一緒に収納 |
| 守る既存機能 | コーヒーの＋1カウンター、動作実績のあるLCD/GT911、既存Google連携 |

ボードの16MB Flash、8MB PSRAM、タッチ画面、Wi-Fi等はWaveshare公式資料による。マイク・スピーカーを前提とする新しい部品は本設計にない。[S01]

### 0.2 本書が決める初期仕様

以下は未決定部分に対する本書の設計判断であり、ユーザーの過去の個別承認と区別する。

- 1対戦10ラウンド。あいこも1ラウンド。勝ち1点、負け・あいこ0点。
- 最大50プロフィール・稼働端末1台を初期検証範囲とする。複数端末の衝突にも防御を入れるが、性能保証は拡張試験後。
- 任意参加、ゲーム用ニックネーム、6桁PIN。登録は管理者のGoogle認証済み画面から行う。
- プレイヤーごとの累計集計は退会まで保存。ラウンド明細は標準180日。直近50手と累計の統計を保持する。
- Jevに名前・PIN・社員番号・コーヒー利用状況は送らない。送るのは、その人のじゃんけんの集計と直近の並び。
- Jevの正常な3択予測を使う。API障害時は統計AIへ切り替え、画面と記録の両方で区別する。
- オフラインではゲストのローカル対戦だけ。個人の公式戦績には後から混ぜない。
- 公開個人ランキング、賞金、賭け、人事評価との連携は作らない。

### 0.3 優先順位と外部環境の未確定情報

`ユーザーの最新の確定事項 > 本書の仕様 > 過去の一般提案`。本書の付録JSONはID・列名・数値の正本、本文は動作の正本。不一致なら試験を失敗させて修正する。

過去のAI ESPER仕様は匿名ゲームだったが、今回は**AI DUELだけに明示的な個人登録を追加**する。AI ESPERやコーヒー記録を勝手に個人へひもづけない。個人履歴が増えるのは自社の記録であり、Jevの重みが毎回再学習される仕様ではない。

| 外部情報 | エージェントの確認方法 | 不明な場合 |
|---|---|---|
| 既存ソース・LVGL/Arduino/ESP-IDF版 | 許可されたワークスペース、ビルドログ、設定を読む | PC上のロジック・契約試験のみ進める |
| 現在のSheets接続方式 | ESP32送信コードと既存スクリプトを読む | GASを採用する設計として別モジュールを準備。既存実装だと断定しない |
| Spreadsheet ID・Apps Script ID・/exec URL | 所有者が使用している実設定を確認 | 架空のIDを書いて接続成功扱いにしない |
| Googleの公開設定・利用上限 | 実際のWorkspaceポリシーとデプロイを確認 | 公式個人対戦を無効化。安全な経路の承認待ち |
| Jevキー・モデル利用権 | 管理者がScript Propertiesへ登録 | `stats`モードのみ表示 |
| 空きFlash・日本語フォント | 実ビルド・map・文字描画 | パーティションの初期化や全面更新はしない |

---

## 1. ゲームの完成形と全ルール

### 1.1 一連の流れ

```text
HOME → ゲーム → AI DUEL → 自分を選択 → PIN → 自分の記録
  → 対戦開始 → AIの手を先に確定 → 自分の手をタップ
  → 両者の手と勝敗を公開 → 次の回 → 10回の結果 → ログアウト / もう一度
```

ゲストの場合は個人選択・PINを飛ばす。基本的に1ゲームは1分前後を目標とするが、GAS往復等の実測前に秒数を保証しない。

### 1.2 勝敗表（プレイヤー視点）

| あなた＼相手 | ROCK：グー | SCISSORS：チョキ | PAPER：パー |
|---|---|---|---|
| ROCK：グー | draw | human_win | ai_win |
| SCISSORS：チョキ | ai_win | draw | human_win |
| PAPER：パー | human_win | ai_win | draw |

- 10回終了時、human_win数 > ai_win数なら対戦はプレイヤー勝利。逆ならAI勝利。同数は引き分け。
- 6勝した時点等で打ち切らない。常に10ラウンドを予定し、あいこの再試合はしない。
- 「1ラウンド」と「10ラウンド1対戦」を別に保存・表示する。327ラウンドを327対戦と表示しない。
- 手は**押して離したクリック**で1回だけ確定。長押しリピート・連打の2重送信は認めない。
- 手を決めて送信処理へ進んだ後、別の手への変更・取り消しは不可。
- 最初の指が押したボタンから外へ滑って離れた場合は選択を確定しない。2点タッチは先に確定した1件だけ処理。
- AIの手が確定するまで選択ボタンは無効。今回の手をAIに見せてから予測する処理は禁止。
- 対戦の途中終了は負けにしない。確定済みラウンドは個人履歴へ残し、10回未完了の対戦は勝敗集計に入れない。

### 1.3 モード

| mode / provider | 内容 | 個人記録 |
|---|---|---|
| `mode=jev` | Jevへ3択の次の手予測を依頼 | 保存する |
| `mode=stats` | 後述の統計AIが端末外のGASで予測 | 保存する。Jev戦と区別 |
| オンラインゲスト | 同じゲーム内だけ履歴を使う。Jev/statsは選択できる | 一時ログのみ。個人累計なし |
| オフラインゲスト | ESP32内で同じ統計ロジックを使う | 永続保存・後日の公式戦績への取込みなし |

v1に難易度の手加減・故意の負けは入れない。Jevの返答を使わなかった回は、Jevが予測したことにしない。

### 1.4 途中で別の人に代わる

対戦中のプロフィール変更は禁止。「終了」→ログアウト→別プロフィール。1人に同時に2つのactive matchを作らない。端末もactive matchは最大1つ。別端末で利用中なら再開・終了の案内とし、勝手に奪わない。

---

## 2. 全データ型・状態・定数

### 2.1 全列挙値

| 種別 | 許容値 |
|---|---|
| hand | `ROCK`, `SCISSORS`, `PAPER` の3つだけ。配列順もこの順 |
| player_result / match_result | `human_win`, `ai_win`, `draw`。未確定match_resultはnull |
| match.status | `active`, `completed`, `aborted`, `expired`, `integrity_error` |
| round.status | `preparing`, `committed`, `resolved`, `cancelled`, `expired` |
| player.status | `active`, `disabled`, `delete_pending`, `deleted` |
| session.status | `active`, `revoked`, `expired` |
| provider_requested | `jev`, `stats` |
| provider_used | `jev`, `stats`。まだ予測前はnull |
| close.reason（端末指定可） | `user_exit`, `idle`, `device_restart`, `integrity_error` |
| avatar_id | `cup`, `bean`, `star`, `moon`, `sun`, `leaf`, `cloud`, `gear` |

IDは原則128bit相当のランダムな小文字16進32桁。表示名と別にする。各種ハッシュとnonceは小文字16進64桁。連番のP001を認証の代わりにしない。

### 2.2 数値と時間

すべての保存時刻はUTCのUnix epochミリ秒整数。UIでAsia/Tokyoへ変換する。シートの見た目の日付書式を論理処理に使わない。件数は非負整数、確率は有限の0〜1、空はnull。`NaN`、`Infinity`、文字列の数値、booleanの数値代用を拒否する。

通信上は必須キーを省略しない。null許可の項目だけnullを使う。キー名をローカライズしない。付録Aに全定数を収録する。

---

## 3. 個人登録・本人選択・PIN・ゲスト

### 3.1 登録はPC側で1回だけ

小さい丸型画面に日本語キーボードを作らない。管理者は、ゲーム用スプレッドシートにひもづくGoogle認証済みの管理サイドバーから登録する。

必須入力：ニックネーム1〜8 Unicodeコードポイント、avatar_id、6桁PIN、利用説明を本人へ案内し同意を得たチェック。社員番号・メール・生年月日・性別は登録項目にしない。表示名の重複は禁止（NFKC＋前後空白除去＋ASCII英字casefoldで比較）。制御文字、改行、式を連想させる先頭`= + - @`を拒否する。承認済みフォントにない文字は登録時にエラーにする。

管理関数 `adminProvisionPlayer(input)` は、Googleの編集権限を持つ管理UIからのみ実行する。公開`doPost`に管理者用actionを追加しない。成功時にplayer_id・表示名を返すが、PINはログへ出さない。IDは再利用しない。

### 3.2 PINは共有端末での取り違え防止

- 全員共通PIN・氏名だけのログイン・平文PINの保存は禁止。
- 6桁PINはゲーム用の簡易認証であり、業務アカウントと同じ強度ではない。端末ののぞき見・端末自体の改造への完全な防御は保証しない。
- v1は**秘密pepper付きHMAC-SHA256**と個人saltを使用。低エントロピーPINを高速ハッシュだけで保護しているわけではなく、pepperをシートから分離し、オンライン試行制限を必須とする。これは一般的なパスワード用の遅いKDFの代替として他システムへ横展開しない。
- pepperとソルトが公開された場合に6桁全探索が容易になるため、スクリプト編集権限は管理者だけに限定する。重要情報を扱う用途へ拡張する場合は別の認証方式へ変更する。

保存ダイジェスト：

```text
pin_digest_hex = HMAC_SHA256(
  key = hex_decode(DUEL_PIN_PEPPER_HEX),
  data = UTF8("AI_DUEL_PIN|1|" + player_id + "|" + pin_salt_hex + "|" + pin)
)
```

saltは個人ごとに異なる16byte相当のhex。比較は長さ検証後、全バイトを比較する一定時間型の実装。平文PINは検証終了時に変数から破棄し、例外・HTTPログ・リクエスト台帳に記録しない。pepperのバージョンを保存する。変更時は全員にPIN再設定を求める運用とし、知らないPINを推測して移行しない。[S10][S11]

### 3.3 試行回数とログインセッション

- 同一人5回の連続失敗で15分ロック。端末単位でも15分に10回のPIN試行で15分停止。
- 失敗時もSheetsの原子的バッチで回数を更新。同じrequest_idの再送では失敗回数を増やさない。
- 不存在のIDとPIN不一致は同じ`AUTH_FAILED`。PIN画面にその人の統計を出さない。
- 成功後は20分のplayer_tokenを発行し、device_idとplayer_idに結び付ける。端末ではRAMだけに置く。
- 待機・結果画面で60秒無操作なら表示を消してログアウト。対戦中のタイムアウトは第12章。
- session有効性はサーバー時刻と状態で確認。IDを知っているだけでは`profile.get`できない。
- token自体をSheetsへ平文保存せずSHA-256のみ保存。再送で同じtokenを返すため、tokenは秘密`DUEL_SESSION_KEY_HEX`を用いたHMACから生成する。

```text
player_token = base64url_without_padding(
  HMAC_SHA256(Ksession, UTF8("AI_DUEL_SESSION|1|" + device_id + "|" + auth_id))
)
```

auth_idはランダムに生成してDuelSessionsへ保存。鍵ローテーションでは全sessionを失効。6桁PINを端末のNVSへ保存しない。

### 3.4 画面操作

プロフィール一覧は4件/ページ、ニックネームとアイコンだけ。選択後テンキーでPIN入力。再開可能なactive matchがある場合は「再開 / 終了して戻る」を表示する。アクティブ対戦が別端末ならこの端末へ自動移管しない。

### 3.5 ゲスト

`guest.begin`で期限付きsessionと一時guest player_idを発行する。DuelPlayers・DuelStatsには行を作らない。履歴はDuelMatchesのsession_stats_jsonで当該対戦中だけ参照し、次の10回戦へ持ち越さない。オンラインゲスト明細は24時間経過後の日次保守で削除対象にする。ゲストから個人への過去戦績の移し替えはv1で提供しない。

ゲストはPINがなくtokenをRAMにしか置かないため、**電源断で失ったguest sessionは個人と同じ方法では再開しない**。再起動後は「ゲストの対戦は再開できません。新しく始める」を表示する。新しいguest.beginは同じ認証済みdeviceに残る旧guest対戦を原子的にabortedにし、未提出roundをcancelledにしてから新guest sessionを発行する。すでにresolvedの結果は改変しない。同じrequest_idのguest.begin再送では新しいsessionを増やさない。deviceに登録本人のactive matchが残る場合は、guest.beginでは終了・奪取せずMATCH_ACTIVEとして本人の再認証または期限処理を待つ。


---

## 4. 個人履歴・統計・癖の定義

### 4.1 学習材料に採用する条件

対象は、この個人がオンラインで本人確認後に出し、**サーバーでresolvedになった有効ラウンド**だけ。重複・準備中・未選択・cancelled・期限切れ・オフライン・他人・guestを除外する。途中で対戦をやめても、それより前のresolvedラウンドは残る。

Jevの学習用APIやファインチューニングは呼ばない。ここでの「覚える」は自分のスプレッドシートに蓄積し、その集計を次回の推論入力へ渡すこと。[S03]

### 4.2 統計JSONの全構造

`DuelStats.stats_json`には付録Eの`zero_stats()`と同じ構造を保存する。配列の手の順はR/S/P、結果の順はhuman_win/ai_win/draw。

| 項目 | 型・次元 | 意味 |
|---|---|---|
| stats_version | string | `duel-stats-1` |
| rounds | integer | 有効な累計ラウンド数 |
| hand_counts | int[3] | 手の累計回数 |
| outcome_counts | int[3] | プレイヤー勝・AI勝・あいこの累計 |
| first_hand_counts | int[3] | 各対戦の第1ラウンドに出した手 |
| transition_by_hand | int[3][3] | 前の自分の手→次の自分の手 |
| transition_by_hand_result | int[3][3][3] | 前の自分の手×前の勝敗→次の手 |
| repeat_after_result | int[3][2] | 前の勝敗ごとに、同じ手/変えた手の回数 |
| tail | record[0..50] | 新しい順ではなく古い→新しい順に、直近50件 |
| last_round_id | string/null | 最後に集計へ取り込んだID |
| prediction.jev / prediction.stats | object | それぞれn、hits、brier_sum、nll_sum |
| match_counts | object | completed、human_win、ai_win、draw、abortedの件数 |

tailの各行は`round_id, match_id, round_no, player_hand, ai_hand, player_result`。生の氏名やPINは入れない。

### 4.3 対戦の境界を越えて「次の手」を作らない

**昨日の最後のグー→今日最初のパーを、連続した1組とは数えない。** 遷移を1つ増やすのは、前後のmatch_idが同じでround_noが1だけ増えた場合だけ。違う対戦の第1手はfirst_hand_countsで別に扱う。

「負けた後」の率は、前回がai_winで、同じ対戦内に次の手が存在するケースの数を分母にする。10ラウンド目に負けても次のラウンドがないため、その敗戦だけで分母を増やさない。

### 4.4 新しいresolvedを1件反映する手順

1. 該当roundがまだ未反映であることをround.statusで確認する。
2. 手・勝敗の累計を1増加。
3. 第1回ならfirst_hand_countsを増加。
4. 同じ対戦の直前があれば3種類の遷移を増加。
5. その回のprovider_usedの予測評価を更新。
6. tailの末尾へ追加し、51件目以降は古いものから捨てる。
7. last_round_idとupdated_at_msを更新。
8. 第10回ならmatch_counts.completedと対戦勝敗を1だけ増加。
9. round、match、Stats、active参照、request receiptを**1回のSheets batchUpdate**で保存する。

completedになる前にaborted/expiredで閉じた対戦は、match_counts.abortedを終了遷移時に1回だけ増加する。再送・保守の再処理では増やさない。integrity_errorは調査対象のため勝敗とaborted件数を即時に再解釈せず、管理者の検証後のrepair対象とする。

統計を更新した後にログを別途appendして終わり、という順番は禁止。第11章の一括確定を使う。

### 4.5 表示する癖の種類（v1の全種類）

自由文生成はしない。次のテンプレートだけを使い、裏付けの件数と期間を必ず表示する。

| habit_id | 条件 | 表示例・計算 |
|---|---|---|
| `overall_favorite` | rounds≥20、最多手の比率≥60% | 「これまでグーが多め：30/45回」 |
| `recent_favorite` | 直近が20件そろい、最多手≥60% | 「直近20回はパー：13/20回」 |
| `repeat_after_win` | human_winに続く有効ペア≥20、同じ手≥60% | 「勝った次は同じ手：18/25回」 |
| `change_after_loss` | ai_winに続く有効ペア≥20、変更≥60% | 「負けた次は手を変更：17/24回」 |
| `repeat_after_draw` | drawに続く有効ペア≥20、同じ手≥60% | 「あいこの次も同じ手：15/22回」 |
| `first_hand_favorite` | 有効第1手≥20、最多手≥60% | 「最初はチョキが多め：14/21対戦」 |
| `transition_favorite` | 対象の前の手に続くペア≥20、最多次手≥60% | 「グーの次はパー：19/30回」 |
| `insufficient_data` | 上記なし | 「まだはっきりした偏りは見つかっていません」 |

優先順はrecent_favorite→change_after_loss→repeat_after_win→first_hand_favorite→transition_favorite→repeat_after_draw→overall_favorite。最大3枚、同じ観察の重複は除く。全期間の表示は「登録/リセット以降の集計」と明記する。

これらは記述統計であり、性格・能力・心理状態の診断ではない。「次も73%で変える」と断定せず「過去の対象24回中17回」と言う。60%や20件は本ゲームの表示条件であって、統計的有意性の保証ではない。

---

## 5. AIの予測・手の選択・コールドスタート

### 5.1 統計AIの完全な初期仕様

各手の件数cと総数nから、`p(h)=(c(h)+1)/(n+3)`で0確率を避ける。全履歴0なら一様分布。

利用できる分布を次の重みで混合する。条件を満たさない項目は除外し、**残った重みの和で割り直す**。

| 分布 | 基準重み | 有効条件 |
|---|---:|---|
| 全期間の手 | 0.15 | 常に有効。0件なら一様 |
| 直近20手 | 0.25 | tailに1件以上 |
| 前の手に続く手 | 0.30 | 同じ対戦の前回があり、その条件の履歴が5ペア以上 |
| 前の手＋勝敗に続く手 | 0.30 | 同じ対戦の前回があり、その条件の履歴が8ペア以上 |

第1回でfirst_hand_countsが5対戦以上ある場合は、混合済み分布と第1手分布を50:50で混合する。夜をまたいだことや端末の物理位置を予測材料にしない。

この式は説明可能なバックアップ兼比較対象であり、最良であるという実証済みモデルではない。改善時はstats_versionを上げ、過去の予測を上書きしない。

### 5.2 Jevへ渡す個人状態

- overallの回数、比率、標本数。
- recent20の回数・比率・標本数。
- first_handの回数・標本数。
- 今回が何ラウンド目か。
- 今回の対戦の前の両者の手・前の結果。第1回ではnull。
- 今回条件のtransition_by_handとtransition_by_hand_result。生の全3次元配列を毎回送る必要はない。
- 古い→新しい順の直近12ラウンド。対戦境界は`new_match`フラグで表す。
- 同じ手を何回連続で出したか。ただし現在の対戦内のみ。
- `stats_baseline`の3確率（参考情報として）。

プレイヤーIDすらJevには必須ではないため、v1では送らない。状態に個人名、PIN、社員ID、コーヒーの杯数、現在入力中の手、未公開AI手を混入させない。直近12件の先頭でその対戦の開始点が切れている場合は、`new_match=true`に加えて`history_truncated=true`を付け、表示されている最初の手が実際の第1ラウンドだったと誤解させない。実際のround_noも各履歴要素へ付ける。

### 5.3 個人履歴ゼロ時

登録直後の最初の1回は根拠がないため、Jevを呼ばず統計AIの一様分布で準備し、`failure_code=NO_HISTORY`、`provider_used=stats`。画面は「初回：まだ記録がありません」。2回目からはJevを呼べるが、少量の履歴が高精度を保証するわけではない。

履歴レベルは0/10/50/200ラウンドの区切りによる**記録量表示**に限定。「AI理解度65%」など実測しない精度のようなメーターは表示しない。

### 5.4 勝つ手の選び方を誤らない

Jevは「あなたの次の手」を予測する。プログラムが「AIの手」を選ぶ。

```text
AIの期待得点差（勝ち+1、負け-1、あいこ0）
U(ROCK)     = P(SCISSORS) - P(PAPER)
U(SCISSORS) = P(PAPER)    - P(ROCK)
U(PAPER)    = P(ROCK)     - P(SCISSORS)
```

最大のUを持つ手を選ぶ。同率（差1e-9以下）の候補はサーバー側の秘密乱数で等確率に選び、確定後は再抽選しない。

**最多の予測手に勝つ手を出すだけでは、期待得点差が最大にならない場合がある。** 例えばR=.40、S=.35、P=.25なら、U(R)=.10、U(S)=-.15、U(P)=.05であり、AIの最善はグー。単純に最多のグーに勝つパーではない。本例は参考コードで検査する。

### 5.5 推論の良さを測る指標

- 実際のAI勝率＝ai_win/有効ラウンド数。引き分け込みの分母を画面に明示。
- プレイヤー勝率・あいこ率も同じ分母。
- 予測的中率＝最大予測手と実手が一致した回数/予測が採用された回数。同率の最大手はR/S/P順に選ぶルールで固定。
- 多クラスBrier＝Σ(p(h)−1[実手=h])²。0〜2。小さいほど良い。
- NLL＝−ln(max(p(実手),1e−6))。解析用。確率はあとから差し替えない。
- provider_used別に分ける。Jevの有効予測がない回をJevの精度に入れない。

Jevのconfidenceは候補分布からの指標であり、このゲームの実測正解率・勝率と同一ではない。[S05]

一様独立ランダムの相手から次の手の予測材料は得られない。勝ち・負け・あいこは各1/3が基準。有限の10回戦ごとに必ず同じ比率になるわけではない。Jevが単純統計より強いことは実試験で検証する。

---

## 6. Jevの実API契約

### 6.1 実際の呼出し

TypeSafe公式のHTTP契約は以下。GASではNode.js SDKをそのままimportせず、UrlFetchAppによるHTTPを使う。[S02][S04][S08]

```http
POST https://api.typesafe.ai/v1/systemone
Authorization: Bearer <Script PropertiesのJEV_API_KEY>
Content-Type: application/json
```

```json
{
  "model": "jev-1.13.0",
  "state": {
    "game": "rock_paper_scissors",
    "round_no": 4,
    "history_rounds": 83,
    "overall_counts": {"ROCK": 36, "SCISSORS": 22, "PAPER": 25},
    "recent20_counts": {"ROCK": 10, "SCISSORS": 4, "PAPER": 6},
    "previous_round": {"player_hand": "ROCK", "ai_hand": "PAPER", "player_result": "ai_win"},
    "conditional_next_counts": {"sample_n": 12, "ROCK": 2, "SCISSORS": 3, "PAPER": 7},
    "recent_sequence": [
      {"new_match": true, "player_hand": "SCISSORS", "ai_hand": "PAPER", "player_result": "human_win"},
      {"new_match": false, "player_hand": "ROCK", "ai_hand": "SCISSORS", "player_result": "human_win"},
      {"new_match": false, "player_hand": "ROCK", "ai_hand": "PAPER", "player_result": "ai_win"}
    ],
    "notice": "Counts are observations, not certainties. The current hand is not present."
  },
  "questions": {
    "next_hand": {
      "type": "choice",
      "instructions": "Predict the human player's NEXT hand in rock-paper-scissors from the historical counts and completed rounds only. Account for small sample sizes and possible changes of strategy. Predict the human hand, NOT the AI counter-move. Do not treat a historical hand as the current answer.",
      "criteria": {
        "ROCK": "The human will choose rock (グー).",
        "SCISSORS": "The human will choose scissors (チョキ).",
        "PAPER": "The human will choose paper (パー)."
      }
    }
  }
}
```

上記数値は契約説明用の架空例。全履歴とrecent_sequenceは同じ長さである必要はない。実装は第5章の状態ビルダーで実データから生成し、総数と内訳の一致を検査する。

2026-09-20の公式モデル一覧で`jev-1.13.0`を確認したため初期値とする。利用開始前に実キーで使えるか確認し、別バージョンへ変えるなら設定と評価ログへ残す。`jev-latest`へ無断で追従しない。[S06]

### 6.2 使用する応答

`answers.next_hand.type=choice`、`choice`、`probabilities`、`confidence`、`model`、`usage.input_tokens`、`usage.output_tokens`を解析する。[S02][S04]

検証：手のキー集合が厳密にR/S/Pの3つ、各値が数値・有限・0〜1、合計が1±0.001。丸め誤差だけ再正規化する。choiceは3候補のどれかで、最大確率と1e-6以内で一致すること。confidenceは0〜1の有限数値。必須値欠落・型違い・余分な候補・負値・HTMLを不正応答として扱う。

prediction_jsonは次の形に正規化して保存する。

```text
{probabilities:{ROCK,SCISSORS,PAPER}, predicted_hand, confidence,
 baseline_probabilities:{ROCK,SCISSORS,PAPER}, normalization_applied:boolean}
```

statsモードのconfidenceはnull。予測が強そうに見える数値を捏造しない。original API response全部や入力状態の全文を常用ログへ残さない。

### 6.3 GASの呼出しパラメーター

```javascript
// 接続部の参考例。認証・再送・保存・検証は別途本書どおり実装する。
const response = UrlFetchApp.fetch('https://api.typesafe.ai/v1/systemone', {
  method: 'post',
  contentType: 'application/json',
  headers: { Authorization: 'Bearer ' + apiKey },
  payload: JSON.stringify(requestBody),
  muteHttpExceptions: true,
  followRedirects: false,
  validateHttpsCertificates: true,
  timeoutSeconds: 8
});
```

**現行のGoogle公式UrlFetchApp資料にはtimeoutSecondsが掲載されている。** ただし実アカウントで8秒設定を試験し、全処理が必ず8秒以内になるという意味にしない。GAS起動・Sheets読み書き・通信の前後処理は別。`Utilities.sleep`やPromiseで同期処理を擬似的に中断できることにしない。[S08]

### 6.4 失敗時

| 状況 | 動作 |
|---|---|
| 401/403 | キー・権限異常。Jev呼出しを停止して管理者へ記録。統計AIへ |
| 400/422 | 契約異常。本文を機密除去して検査記録。統計AIへ |
| 429 | 利用制限。Retry-Afterがあれば解釈し、なければ60秒のJevクールダウン。今回の回は統計AIへ |
| 529/5xx | 一時障害。短時間クールダウン後、次のラウンドで再評価。今回の回は統計AIへ |
| 8秒タイムアウト・不正JSON | 統計AIへ |
| GAS処理そのものが中断 | lease経過後に復旧処理。新たなJev呼出しはせず保存済みbaselineで確定 |

1ラウンドにつきアプリケーションからのJev呼出しは最大1回。ボタン再送で何度も課金しない。API自身の内部処理や課金まで「exactly once」と保証はしない。エラーコードの意味はTypeSafe公式資料による。[S04]

---

## 7. 後出し防止とコミット検証

### 7.1 なぜハッシュを渡すか

単に「サーバーへ先に保存した」と言うだけでなく、端末がプレイヤーの選択前に**AIの手のハッシュ**を受け取り、公開後に照合する。画面には「相手の手は確定済み」と表示する。

これは、事前確定後にAIの手が変わっていないことを検査する設計であり、改造された端末、管理者が不正に運用するシステム、時刻まで含めた第三者監査のすべてを保証するものではない。

### 7.2 正規化文字列（完全指定）

```text
AI_DUEL|1|<match_id>|<round_id>|<round_no>|<ai_hand>|<nonce_hex>
```

ASCII、改行なし、空白なし、round_noは先頭ゼロなし10進、手は英大文字。SHA-256を小文字hex64桁にする。JSONの文字列化順に依存させない。

nonce_hexは256bit相当の秘密値。`SHA256(ai_hand)`だけだと3通りを総当たりできるため禁止。GAS側は事前に十分な乱数で作った秘密Kcommitから、generation_id等を入力したHMACでnonceを導出してよい。公開IDだけからnonceを作らない。Math.randomを秘密値生成に使わない。

### 7.3 公開する順番

```text
1. 個人の過去の記録を読む（今回の手はまだ存在しない）
2. Jev / 統計AIで次の手を予測
3. AIの手とnonceを決定
4. AIの手・nonce・予測・commit_hashをSheetsへ一括保存
5. 端末へround_id、commit_hash、期限、providerだけ返す
6. 端末がcommit_hashをNVSへ保存 → 手ボタンを有効化
7. プレイヤーが選ぶ → 手をNVSへ保存 → round.submit
8. 保存済みのAI手で判定し、結果と統計を一括保存
9. AI手・nonce・予測・結果を公開
10. ESP32でもハッシュと勝敗を再計算して一致を確認
```

committed以前のレスポンスにai_hand、nonce、予測確率、推測できるヒントを入れない。管理者向け内部行をそのままJSONで返さず、公開フィールドをホワイトリストで作る。

### 7.4 不一致

ハッシュ不一致は「負け」でも「再抽選」でもない。端末は次の対戦を止め`COMMIT_MISMATCH`を表示し、通信正常化後に`match.close(reason=integrity_error)`を送る。サーバーは該当対戦を調査対象とし、管理者レビューまでゲームを保守停止する。

既に保存済みのラウンドを端末の申告だけで消したり別の勝敗へ変更しない。必要な訂正は明細と検証値を確認して統計を再構築する。新しいAI予測で古い回を埋め直してはいけない。

---

## 8. Google連携の構成・制約・デプロイ

### 8.1 構成

```text
ESP32（LCD・タッチ・一時復旧情報）
   ↓ HTTPS JSON
既存Google連携と共存するApps Script Web App
   ├─ ゲーム用の非公開Googleスプレッドシート
   └─ UrlFetchApp → TypeSafe Jev
```

既存のスプレッドシート連携がGAS以外なら、エージェントが既存方式を確認した上でゲーム用GASを追加する。現在の接続方式を会話だけから断定しない。

ゲーム用Workbookは**同じGoogle運用内の別の非公開ファイル**を初期推奨とする。既存のコーヒー集計Workbookが全社員へ公開されている場合、そこへPINハッシュや個人戦績を追加しない。既存Workbook自体が管理者限定なら、同じファイル内の専用タブでもよい。ゲームのトランザクションに関係する全タブは必ず同じWorkbook内。

### 8.2 Apps ScriptはGoogle側の処理係

ユーザーが管理するVPSは不要だが、処理機能はGASで動く。`doPost(e)`でJSON本文を受け取ってContentServiceで返し、UrlFetchAppで外部APIを呼ぶ。これはGoogle公式の仕組みに基づく。[S07][S08]

**Apps Scriptを普通のExpressサーバーと同一視しない。**

- `e.postData.contents`から読む。イベントに任意のAuthorizationヘッダーがある前提を置かない。
- ESP32→GASのdevice_token / player_tokenはHTTPSのJSON本文へ入れる。URLクエリへ入れない。
- ContentServiceのTextOutputに任意のHTTP statusを設定する一般的なsetStatusCode APIを捏造しない。アプリケーション上のエラーはJSONのok/errorで区別する。
- HTTP 200でもHTMLのログインページやGoogleのエラーなら成功ではない。
- 予約名`c`、`sid`をURLやPOSTパラメーターに使わない。[S07][S09]

### 8.3 ESP32のリダイレクト対応

ContentServiceの返答は`script.googleusercontent.com`上の一時URLへリダイレクトされる。ESP32側で追従が必要。[S09]

1. 設定された`https://script.google.com/macros/s/.../exec`へPOST。
2. 302/303を受けたら、HTTPSかつ許可したGoogleホストであることを検査。
3. 返答取得用URLへ**GET、本文なし、トークン再送なし**でアクセス。
4. 最終JSONを検証。リダイレクトは最大3回。
5. HTTP→HTTP降格、Googleアカウントのログインページ、未知ドメイン、307/308による秘密本文の再送要求はv1で拒否して設定確認。

Arduino HTTPClientのリダイレクトモードに任せてPOSTが別ホストへ再送されないか、採用バージョンで試験する。`HTTPC_FORCE_FOLLOW_REDIRECTS`を無条件で付けて終わりにしない。HTTPS証明書検証を無効化しない。[S18][S19]

### 8.4 配備手順

1. 既存ファーム・既存GAS・既存Sheetsを保存。既存のdoPostの名前を競合させない。
2. ゲーム用Workbookを作成、共有先は管理者のみ。
3. Apps ScriptはV8、時刻はAsia/Tokyo。第9章の初期化関数を管理者が実行。
4. **高度なGoogleサービスのSheets API v4を有効化**する。標準GCPプロジェクトを使う場合は該当APIの有効化も確認。[S13][S14]
5. `DUEL_SPREADSHEET_ID`等をScript Propertiesに設定。秘密をソースへ埋め込まない。
6. 既存doPostはnamespace=`ai_duel`だけ新ルーターへ、その他は元の処理へ渡す。
7. 新しいテスト用デプロイでdevice.hello、認証、統計AI、Sheets一括更新を検証。
8. ESP32がブラウザーのGoogleログインなしに呼べるWeb App設定を確認する。所有者実行・公開呼出しを採用する場合も、アプリ側の端末認証は必須。
9. Workspaceポリシーが匿名Web App呼出しを禁じる場合、承認されていない公開設定へ変えない。既存の認証済み経路の適応を実環境の作業項目として残す。
10. 実際の`/exec` URLで運用。`/dev`を製品へ固定しない。新しい版を配備した後は実URLで疎通を再確認。
11. Jevキーを入れて試験し、最後に個人戦績を有効化。

### 8.5 必要なScript Properties（全種類）

| key | 内容・未設定時 |
|---|---|
| DUEL_SPREADSHEET_ID | ゲームWorkbook。未設定なら全オンラインゲーム停止 |
| JEV_API_KEY | Jevの秘密キー。未設定ならjev選択不可 |
| DUEL_JEV_MODEL | 初期`jev-1.13.0`。利用可能な版を確認して固定 |
| DUEL_PIN_PEPPER_HEX | 32byte以上の秘密。未設定なら個人ログイン停止 |
| DUEL_PIN_KEY_VERSION | `1`から開始 |
| DUEL_SESSION_KEY_HEX | token用32byte以上の秘密 |
| DUEL_COMMIT_KEY_HEX | nonce・同率抽選用32byte以上の秘密 |
| DUEL_RECEIPT_KEY_HEX | PIN等を含むリクエスト指紋用秘密 |
| DUEL_DEVICE_TOKEN_SHA256_<device_id> | 登録端末tokenのSHA-256。端末ごとに別 |
| DUEL_MAINTENANCE | `true`なら新規ゲームを停止。既にcommittedのsubmitは安全な保存経路がある場合だけ継続 |
| DUEL_STORAGE_BARRIER_JSON | 書込み前に置く小さい未確定バッチ識別情報。平常時は未設定。存在時は第11章の照合が終わるまで全ゲーム更新停止 |
| DUEL_JEV_COOLDOWN_UNTIL_MS | APIの再呼出し抑制期限。0が初期 |
| DUEL_DEPLOYMENT_VERSION | 配備したソースのcommit/版 |

鍵はPCの暗号学的乱数から生成して管理者が登録する。4種類の秘密は使い回さない。Script Propertiesは専用Secret Managerと同等の隔離ではなく、スクリプト編集者から秘密を守るものではない。[S10]

---

## 9. スプレッドシート全定義

### 9.1 タブ構成

列の完全な順序は付録D。以下の9パターンを自動初期化する。月次/日次タブ以外の名前は固定。

| タブ | 主キー | 役割 |
|---|---|---|
| DuelMeta | key | schema、版、上限カウンター、保守状態 |
| DuelDevices | device_id | 稼働許可・active match・PIN試行制限 |
| DuelPlayers | player_id | ニックネーム・PIN検証値・同意・active match |
| DuelSessions | auth_id | 期限付き本人/ゲストsession |
| DuelStats | player_id | 個人ごとの累計・遷移・直近履歴 |
| DuelMatches | match_id | 10ラウンド1対戦の状態 |
| DuelRounds_YYYYMM | round_id | 準備済みAI手から確定結果までのラウンド台帳 |
| DuelRequests_YYYYMMDD | request_key | 48時間の冪等処理受付台帳 |
| DuelAdminAudit | event_id | 登録・PIN変更・削除等の管理操作 |

YYYYMM/ YYYYMMDDはサーバー時刻をAsia/Tokyoで区切る。対戦が月をまたいでもmatch_idは同じ。各roundのタブと行はDuelMatchesに記録して照会する。

### 9.2 共通の保存規則

- 1行目は完全一致の英語ヘッダー。途中へ列を勝手に挿入しない。
- ID・列位置は設定とヘッダー検査から決定。シートの見た目の並び替えを論理に使わない。
- 日付はepoch msの数値セル、ID・ハッシュ・JSONは文字列セル、真偽はbooleanセル。
- JSONは`JSON.stringify`相当のminifyで1セルUTF-8 24,000byte以内をアプリ側上限とする。Google側の最大値そのものの宣言ではない。
- `Sheets.Spreadsheets.batchUpdate`の`userEnteredValue.stringValue`等を使い、ユーザー文字列を数式として登録しない。`formulaValue`はゲームの台帳では使わない。
- 保存後の元データ行は人がソート・直接編集・途中挿入・物理削除しない。削除処理は値の消去または状態変更とし、索引を壊さない。
- 新規行番号はDuelMetaの`next_row:<tab_name>`から単調増加で割り当て、次行カウンター更新と行作成を同一batchへ入れる。消した行を再利用しない。getLastRow()+1を唯一の割当規則にしない。容量の拡張も必要なら同じbatchへ含める。
- 集計グラフやレポートは読み取り専用の別タブへ。元データの式や手編集でカウンターを作らない。
- キャッシュは参照を速めるヒントにすぎない。保存成功やロックをCacheServiceだけで保証しない。

### 9.3 各シートの主要フィールドの詳細

**DuelMeta**：`schema_version=1.0.0`、`stats_version`、`policy_version`、`prompt_version`、`installed_at_ms`、`maintenance_job`、`day_budget:<date>`、`prepare_window:<device_id>`、`next_row:<tab_name>`、`last_write_receipt`をkeyとして使う。value_jsonには非秘密の構造値だけ。秘密はここへ置かない。

**DuelDevices**：enabledはboolean。active_match_idはnullまたは32hex。pin_window_start_msから15分を1窓としてpin_window_attemptsを数える。終了したactive参照が残っていれば元matchを確認して修復するが、別matchを作る前に修復も一括保存する。

**DuelPlayers**：statusは第2章。pin_salt_hexは32hex、pin_digest_hexは64hex。consented_at_ms、consent_versionは登録時必須。PIN失敗のカウンターをroundの勝敗とは混ぜない。

**DuelSessions**：player_idは登録本人または一時guest ID。is_guestで区別。token_digest_hexだけを保持。expires_at_msが過ぎたらactiveでも認証不可。logout/PIN再設定/退会でrevoked。

**DuelStats**：history_start_msは登録または直近の履歴リセット時刻。日次処理・ログインで更新しない。stats_jsonは第4章の完全構造。行のstats_versionとJSON内部のstats_versionは一致させる。last_round_idも一致させる。不一致ならMAINTENANCEとして更新を止める。

**DuelMatches**：resolved_countは0〜10、human_wins+ai_wins+drawsと同じ。provider_counts_jsonは`{"jev":n,"stats":n}`。session_stats_jsonはguest時のみzero_stats構造、登録本人ではnull。round_locations_jsonは最大10件の以下の配列。

```text
[{round_id, tab_name, row_index0}]
```

row_index0はSheets APIの0起点で、ヘッダーは0、最初のデータは1。active_round_idは未resolvedの最後の1回のみ。match_resultはcompletedのときだけ勝敗、それ以外null。

DuelMatches.expires_at_msは**created_at_ms + 1,800,000**の固定された絶対期限。idle期限はlast_activity_ms + 300,000で別計算する。last_activity_msは新しい対戦開始・新規予約・commit・有効submitの成功時だけ更新し、再送・poll・match.get・ログインだけでは延長しない。未提出roundの期限はcommit時に`min(committed_at_ms + 300000, match.expires_at_ms)`として固定する。nowが期限と同じなら期限内、超えたら期限切れ。期限を後から延長して同じ手の再抽選を許可しない。

**DuelRounds_YYYYMM**：

- `generation_id`は準備ジョブの世代、lease_until_msはそのジョブの権限期限。
- `prepared_at_ms`は準備予約時刻、committed_at_msはAI手の永続確定時刻、submitted_at_msはプレイヤーの手の受理時刻。
- `provider_call_reserved`はboolean。初回のJev呼出し枠を予約したことを示す。初回workerのみ呼べる。復旧workerは呼ばない。
- `history_count_before`はその回の予測前の個人round数（guestはその対戦の数）。
- `context_hash`は秘密・個人識別子を含まない入力stateのcanonical JSONのSHA-256。
- `stats_fallback_json`は準備開始時の統計AIの3確率を凍結したもの。
- `prediction_json`は第6章の確率等。ai_handとnonce_hexはcommittedで必須。
- player_hand/player_result/submit_request_idはresolvedで必須。
- result_jsonはresolved時の**公開業務レスポンス本体**を保存。再送で同じ結果・その回時点のscoreを返すために使う。認証tokenやdevice_tokenは入れない。
- failure_codeはnullまたは`NO_HISTORY`, `NO_KEY`, `PROVIDER_TIMEOUT`, `PROVIDER_HTTP_<code>`, `PROVIDER_INVALID`, `RECOVERY_AFTER_LEASE`, `RATE_LIMIT_COOLDOWN`。
- tokensとlatencyは実測できた場合だけ非負数。未取得はnull。概算値で埋めない。

**DuelRequests_YYYYMMDD**：request_keyは`device_id + ':' + request_id`。fingerprint_hexは第10章の内容指紋。resource_refは`match/<id>`、`round/<id>`、`auth/<id>`等。response_safe_jsonにはtokenを入れず、authレスポンスのtokenはauth_idと秘密から再構築。準備中の一時BUSYレスポンスを永続的な最終結果として固定しない。

**DuelAdminAudit**：actionは`provision`, `reset_pin`, `disable`, `enable`, `reset_history`, `delete_start`, `delete_complete`, `migrate`, `repair`, `restore`。個人PIN・rawtoken・生の推論入力はdetail_codeへ入れない。

### 9.4 初期化と移行

`installDuelSchema()`は存在チェック→ヘッダー整合性→不足タブの作成だけ行う。既存データを`clear()`して初期化しない。版違いなら止まり、`migrateDuelSchema(from,to)`を別の明示的管理操作にする。テスト用Workbookと本番Workbookを分ける。

台帳は月次で分割し、ゲーム中は対象の1個人・1対戦・最大10roundだけ読む。全履歴の全列を毎ラウンド読み込まない。ID→行の索引がキャッシュミスならID列だけ検索し、当該行を読み直してIDの一致を確認する。

---

## 10. 通信APIの共通契約と全操作

### 10.1 共通リクエスト

全11操作は、GAS `/exec`へのJSON POST。クエリに秘密を付けない。エラーJSON内のcodeは付録Cだけを公開し、参考Python内部のRuleError名を未定義のままHTTPへ流さない。

```json
{
  "namespace": "ai_duel",
  "api_version": 1,
  "action": "round.submit",
  "request_id": "11111111111111111111111111111111",
  "device_id": "22222222222222222222222222222222",
  "device_token": "<登録端末の秘密token>",
  "player_token": "<ログイン後の期限付きtoken>",
  "created_at_ms": 1790000000000,
  "sent_at_ms": 1790000000000,
  "payload": {
    "match_id": "33333333333333333333333333333333",
    "round_id": "44444444444444444444444444444444",
    "round_no": 1,
    "player_hand": "ROCK",
    "commit_hash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
  }
}
```

ID・時刻・ハッシュは説明用で、実在データでも有効な署名値でもない。device_tokenは端末に事前登録した32byte以上の秘密値をbase64url化したもの。GASはそのSHA-256を登録値と照合する。

- `request_id`と`created_at_ms`は業務操作を作ったときに固定する。再送では同じID・同じpayloadを使い、sent_at_msと認証tokenだけ更新できる。
- created_at_msから48時間を超えた更新はREPLAY_TOO_OLD。照会は新しいrequest_idで行う。
- sent_at_msはサーバーと±5分以内。device.helloのみ時刻未同期の0を許可するが、TLS検証は省略しない。
- 本文8KiB上限、payloadに未知のキーを含めない。各操作の必須項目は付録Bのpayloadの全キー。
- deviceのみ認証の操作はplayer_token=null。それ以外は有効なtoken必須。
- 手を送る操作以外のpayloadへplayer_handを入れない。

内容指紋は、`{action,device_id,actor_id,payload}`を再帰的キーソートしたcanonical JSONに対するHMAC-SHA256（Kreceipt）。actor_idはサーバーがtokenから確定した本人/guest、認証前はdevice_id。キーの順序だけ違う再送は同じ指紋。ログインのPINも指紋計算には含めるが、生のpayloadを保存しない。

### 10.2 共通レスポンス

```json
{
  "namespace": "ai_duel",
  "api_version": 1,
  "request_id": "11111111111111111111111111111111",
  "server_time_ms": 1790000000500,
  "ok": true,
  "data": {},
  "error": null
}
```

失敗時はok=false、data=null、errorは`{code,message_ja,retryable,retry_after_ms}`。retry_after_msは0以上整数。アプリエラーでもHTTP層で200になることがある。HTTPコード、最終Content-Type、JSON、namespace、api_version、request_idをすべて検査する。レスポンス16KiB上限。

### 10.3 全11操作

| action | 認証 | payload | 動作 |
|---|---|---|---|
| device.hello | device | {} | 有効端末か確認、サーバー時刻、利用可能mode、配備版を返す |
| players.list | device | page | activeかつ同意済みの表示名・ID・アイコンのみ。4件/頁 |
| player.login | device | player_id,pin | PIN検証、期限付きsession、再開対象IDを返す |
| player.logout | player/guest | {} | session失効。active matchがあればabortedへ。guest tokenでもログアウト可 |
| profile.get | player | {} | tokenに対応する本人だけの集計と最大3枚の癖 |
| guest.begin | device | {} | 一時guest session発行。前guestの履歴を継承しない |
| match.open | player/guest | client_match_id,mode | 新対戦を開始。同じclient_match_idは同じmatchを返す |
| match.get | player/guest | match_id | 所有権確認後、scoreとactive_roundの公開状態を返す |
| round.prepare | player/guest | match_id,round_no | 今回の手を予測してcommittedへ。まだ準備中ならその状態 |
| round.submit | player/guest | match_id,round_id,round_no,player_hand,commit_hash | 先に保存したAI手と判定し、結果と履歴を確定 |
| match.close | player/guest | match_id,reason | 未完了の対戦を閉じ、未提出roundをcancelledにする |

同じmodeの再戦でもclient_match_idは新規生成。match_idは最初のclient_match_idを採用し、所有者衝突はFORBIDDENで拒否。同一プレイヤーのactive matchが他にある場合MATCH_ACTIVE。パラメーターの完全型定義は付録B。

### 10.4 重要レスポンスの全フィールド

**round.prepare committed：**

```json
{
  "round_id": "44444444444444444444444444444444",
  "status": "committed",
  "commit_hash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "commit_version": 1,
  "provider_used": "jev",
  "expires_at_ms": 1790000300000,
  "retry_after_ms": 0
}
```

preparing時はstatus=preparing、commit_hash/provider_used/expires_at_msはnull、retry_after_ms=3000。**gen、AI手、nonce、確率は返さない。** 同じroundが既にresolvedだった場合は、同じ公開フィールドとstatus=resolved、retry_after_ms=0を返す。端末はその応答で手ボタンを有効にせず、match.getのlast_resolved_resultを取得する。

**round.submit resolved：**

```text
{round_id,round_no,player_hand,ai_hand,player_result,nonce,commit_hash,
 prediction:{provider_used,model_returned,probabilities,predicted_hand,confidence},
 score:{human_wins,ai_wins,draws},match_status,match_result,next_round_no}
```

第10回のnext_round_noはnull。nonceは台帳のnonce_hexの値。result_jsonにこのdataを保存し、リトライに同じdataを返す。

**match.get：**

```text
{match_id,status,score:{human_wins,ai_wins,draws},rounds_resolved,next_round_no,
 active_round_public:null|{round_id,round_no,status,commit_hash,commit_version,
 provider_used,expires_at_ms},last_resolved_result:null|<保存したresult_json>,match_result}
```

active_round_publicは第7章の公開範囲だけ。last_resolved_resultは既に受理済みの本人の手がある回だけ。

**profile.get：**

```text
{display_name,stats:{rounds,completed_matches,aborted_matches,
 hand_counts:{ROCK,SCISSORS,PAPER},human_wins,ai_wins,draws,
 prediction_by_provider:{jev:{n,hits},stats:{n,hits}},history_level},
 habit_cards:[{habit_id,text_ja,numerator,denominator,period}],
 history_start_ms,retention_days}
```

全遷移配列・PIN関連・他人のstats・非公開AI手は返さない。

### 10.5 ステータス照会と再送

- round.submitの応答が消失したら**同じ手の同じrequest_id**を再送する。
- submit成功済みなら、別request_idでも同じround＋同じ手には同じresult_json。
- 同じroundに違う手を送ったらHAND_ALREADY_SUBMITTED。2回目で書き換えない。
- 同じrequest_idに違うpayloadはIDEMPOTENCY_CONFLICT。
- round.prepareは自然キー`match_id + round_no`でも冪等。異なるrequest_idで来ても同じroundを再利用。
- Matchを閉じてから遅れて返った古い準備応答は、端末のUI世代番号でもサーバー状態でも拒否する。

player.logoutの同一request再送は例外として、同じdeviceに結び付いた既知の失効済みtokenから元のlogout receiptを照合できれば`logged_out=true`を返す。これはログアウト結果の再確認だけで、失効tokenによるprofile取得・新対戦・手の提出は許可しない。未知tokenや別人/別deviceには使えない。

エラーの全29種類と既定の日本語は付録C。通信失敗時に理由不明のまま勝ち/負けとしない。

---

## 11. 原子的更新・ロック・冪等性

### 11.1 使う仕組み

**GASのScriptLock＋Google Sheetsの単一batchUpdate**を採用する。同一batchUpdate内の要求は一括適用され、検証失敗時は全体が失敗する。ただし、別リクエスト間や他の編集者との完全なトランザクション分離を意味しない。全ゲーム更新・管理操作・保守は同じGASプロジェクトの同じScriptLockを使う。[S12][S15][S16]

GAS Web AppではDocumentLockがnullとなる場合があるため使わない。ScriptLockもtryLock/waitLockで実際に取得する必要がある。[S12]

- `tryLock(2000)`失敗はBUSY。待ち続けない。
- 外部Jev呼出し中はロックを保持しない。
- lock取得→必要行読込→検証→計算→単一batchUpdate→lock解放の順。
- 3つの別々のsetValue/appendRowを「原子的」と呼ばない。
- batchUpdateがタイムアウトした場合、成功か失敗か不明になりうる。再接続後に台帳を照会し、IDと状態で判定する。盲目的にappendし直さない。
- 返答に成功を書くのは一括保存の成功確認後。更新中に失敗したままESP32だけ点数を増やさない。

### 11.2 未確定バッチの防壁

Googleへ書込みを送った後のタイムアウトは、「失敗した」とも「まだ適用されていない」とも断定しない。別workerが古いStatsを読み、その後から旧バッチが到着する事態を避けるため、**全DUEL書込み**に以下を共通適用する。

1. ScriptLock内でDUEL_STORAGE_BARRIER_JSONの有無を確認。有なら原本DuelMeta.last_write_receiptを読み、一致照合以外の書込みをしない。
2. 新しいwrite_idとmutation_hashを生成し、`{write_id,mutation_hash,started_at_ms}`をScript Propertiesの防壁へ先に保存する。保存に失敗・成否不明ならbatchを送らない。防壁にPIN・token・個人履歴を入れない。
3. 実際の変更要求に、DuelMeta.last_write_receiptへ同じwrite_idとmutation_hashを書く要求を追加し、**1つのbatchUpdate**で送る。防壁設定中は、管理者や保守も別batchを送らない。
4. 成功応答を確認してから防壁を消去。バッチ応答が不明、GAS強制終了、防壁消去失敗の場合は、そのまま残す。
5. 復旧workerはロックを取得し、原本のlast_write_receiptが防壁と完全一致すれば、そのバッチの適用済みを認識して防壁を解除し、台帳から業務結果を再構築する。キャッシュで照合しない。
6. 一致しない場合はSTORAGE_UNAVAILABLEとして新しい更新を止める。タイマーで勝手に防壁を消して同じ更新を送り直さない。照会を続けても判定できなければ、管理者が処理ログ・台帳・実行状態を確認する復旧案件とする。

これはSheetsに存在しないcompare-and-swapを想定する代わりの**安全側停止**である。障害によって一時的にゲームを止めることは許容し、成否不明の書込みを重ねて戦績を壊さない。応答不明な初期スキーマ作成も管理保守で確認し、通常ゲーム書込みがまだない状態から復旧する。Script PropertiesはWorkbookと原子的ではないが、「先に防壁→台帳とreceiptは原子的→最後に防壁解除」の順序により、途中停止は再確認側へ倒す。

### 11.3 round.prepareの3段階

**A：予約（ロックあり）**

1. device、session、所有権、active match、next round、日/分の上限を検査。
2. 同じ自然キーのroundがcommitted/resolvedなら既存公開結果を返す。
3. preparingかつlease有効ならpreparingを返す。Jevへ二重発行しない。
4. 新しいround_idとgeneration_idを生成。期限30秒のlease、個人統計から作ったbaseline、context_hash、history_count_beforeを保存。
5. Jevを呼ぶ条件を満たす場合だけprovider_call_reserved=trueで予約。使用回数カウンターも同じバッチで更新。
6. round行、matchのround_locations/active参照、request receiptを一括保存してロック解放。

**B：推論（ロックなし）**

7. 初回予約のworkerだけ、最大1回Jevへ。初回記録なし・stats指定・API停止ならbaseline。
8. 結果を検証し、3確率、選ぶAI手、nonce、commit_hashを用意する。

**C：確定（再ロック）**

9. round.status=preparing、generation_id一致、lease有効、matchがactiveであることを再確認。
10. 不一致なら古いworkerの結果を捨てる。committedを書き換えない。
11. 手・nonce・予測・第9章で定めた期限・status=committed・last_activity_ms・receiptを一括保存。
12. 端末へ公開値だけ返す。

**lease切れの復旧：** 同じround_idを維持し、新しいgeneration_idを発行。Jevは再発行せず、最初に凍結したstats_fallback_jsonで確定する。failure_code=RECOVERY_AFTER_LEASE。後から帰った旧workerの応答は世代不一致で破棄。

### 11.4 round.submitの一括確定

ロック中に以下を行う。

1. 認証とmatch/roundの所有者・device・round_no一致を検査。
2. commit_hashを照合。
3. 既にresolvedなら同じ手か確認して保存済みresult_jsonを返す。期限切れ判定より**resolvedの再送判定を先に**行う。
4. 未提出ならmatch.status=active、round.status=committed、および全期限内を確認する。
5. 保存済みai_handとplayer_handで第1章の勝敗判定。Jevを呼ばない。
6. 更新後のStats、Match、Round、public result_jsonをメモリー上で作る。
7. 第10回はmatch.completedとplayer/deviceのactive解除も同じバッチへ。
8. Roundのresolved化、Stats更新、Match更新、参照解除、receipt保存を**同一Workbookへの1バッチ**で確定。
9. 成功後にresult_jsonを返す。

勝敗と統計の更新を別APIに分ける設計はしない。ESP32が計算した勝敗・累計件数を権威として受け取らない。

### 11.5 読み取りと書き込みの最適化

ID列/小さい索引と対象行をまとめて読む。行ごと・セルごとに多数往復しない。各ラウンドは正常時、主な書込みAPIが予約・commit・submitの3回。複数セル更新も1つのbatchにまとめる。Googleはサービス呼出し削減とバッチ処理を推奨している。[S17]

キャッシュの行参照は原本を再検証する。キャッシュから得た「まだ未提出」という値だけで更新しない。外部の別GASプロジェクトから同じタブに書き込むとScriptLockの範囲外になるため禁止。

---

## 12. 通信切断・電源断・期限・オフライン

### 12.1 ESP32に残す復旧情報

NVS namespace=`ai_duel`に、小さいversion付きレコードを保存する。既存Wi-Fi/コーヒーのnamespaceを消さない。

```text
{format_version,local_generation,player_id,is_guest,match_id,round_id,round_no,
 commit_hash,commit_version,round_expires_at_ms,
 phase,selected_hand,request_id,request_created_at_ms,payload_json,crc32}
```

登録本人の復旧を主対象とする。guestも通信中の再送には使うが、再起動後は第3章どおり新規guestへ移行する。phaseは`none,match_open_pending,awaiting_hand,submit_pending,result_verified,close_pending`。PIN、Jevキー、player_token、全個人統計は保存しない。device_tokenは端末のプロビジョニング領域として別管理。

- committed受信後、選択を有効にする前に復旧情報を保存。
- タップ後、submit前に手とrequest_idを保存。保存失敗ならPERSIST_FAILEDで止める。
- resultのcommit検証成功後、pendingをクリア/遷移。
- NVSの電源断耐性に加え、レコードのversion/CRC検査を行う。保存が必ず成功すると仮定せずエラーを扱う。[S20]

### 12.2 障害の全分岐

| 発生箇所 | 復旧 |
|---|---|
| match.openの返答前 | 同じclient_match_idで再送。新対戦を増やさない |
| AI準備前にWi-Fi断 | 手ボタン無効、再接続/終了。勝敗なし |
| AI準備中にGAS中断 | lease後に同じroundを統計AIで復旧 |
| committed返答消失 | round.prepareまたはmatch.getで同じcommitを取得 |
| committed後・未選択で電源断 | 再起動後PIN再入力、期限内なら同じroundを再開 |
| 手を保存した直後・送信前に電源断 | PIN再入力後、同じ手・同じIDを再送。別の手を選べない |
| submit到達後・返答消失 | 同じ手を再送。サーバーはresult_jsonを返し、二重加算しない |
| result受信後・画面表示前に電源断 | match.getのlast_resolved_resultから復元 |
| PIN/token期限切れ | 登録本人は同じ人で再ログインして状態確認。ログイン期限を延ばすためにPIN保存しない。guestは第3章の新規開始へ |
| 手の期限を超過 | 未提出roundはexpired。matchもexpired。既存resolvedだけ保持 |
| 他人が次にログイン | 前の個人データを画面とRAMから消す。前の未処理submitは本人の再認証まで送らない |
| 原本Sheetsが利用不可 | 公式対戦を止め、勝敗を未確定表示。ローカルで結果を捏造しない |
| commit不一致 | 第7章どおり整合性停止 |

### 12.3 タイムアウトとUI

- 端末の1HTTP要求は20秒上限。初回＋最大2再送、間隔1秒/3秒。いずれも同じ業務操作。
- preparing表示の状態照会は3秒間隔、最大5回。その後は「再接続 / 終了」を表示。無限ポーリングしない。
- roundの選択期限は最大commit後300秒（matchの絶対期限で短縮）。matchは第9章の最終activityから300秒、絶対上限1800秒。pollや再ログインだけで延長しない。
- セッション期限より長い対戦を継続する場合、再PIN入力を求める。対戦の状態は期限内で保持。
- 5分放置を敗戦扱いにしない。期限切れ未提出は学習対象から除外。
- タップ済みでもサーバーが期限内に受け取らなかった場合は新規確定しない。端末の自己申告時刻だけで後から有効化しない。

### 12.4 オフライン

新規個人対戦は開始しない。「通信なしでお試し」はguest_localとして端末内で遊べる。履歴もtokenも持ち越さず、画面は「ローカル対戦・記録なし」。オンライン復帰時にもローカル戦績を個人へ加算しない。オンラインで既に選んだ手の再送と、ローカル新規対戦を同じround_idで混ぜない。

---

## 13. 丸型UI・画面遷移・全文言

### 13.1 座標系

480×480、中心(240,240)、重要要素は半径224以内に置く。四角い画面の四隅は使わない。以下は論理ピクセル上の初期レイアウトであり、実機でベゼル・フォントを確認して調整する。

| 要素 | 位置/サイズ |
|---|---|
| タイトル | x=144,y=42,w=192,h=40 |
| スコア | x=96,y=116,w=288,h=60 |
| ラウンド表示 | 中心(240,190)、幅180 |
| グー | 中心(120,264)、半径54 |
| チョキ | 中心(240,264)、半径54 |
| パー | 中心(360,264)、半径54 |
| 状態ラベル | x=88,y=338,w=304,h=48 |
| 下の戻る/終了 | x=180,y=398,w=120,h=44 |
| 通常の本文 | x=72,y=104,w=336、24px、最大4行。長い説明は分ける |

じゃんけんの3ボタンは円内に収まり、相互に12pxの間隔があることを参考検査で確認する。図形の手アイコン＋「グー/チョキ/パー」を使い、カラー絵文字がフォントにあることを前提にしない。

PINテンキーは3列の中心x=152/240/328、4行の中心y=174/242/310/378、各72×56。1〜9、最下段は「消す/0/決定」。PINは6つの丸で隠し、決定は6桁入力まで無効。テンキー画面の戻るはタイトル側の小さい操作にする。

本文24px、主文32px、点数48pxを初期値とする。個人名は登録時8文字上限。長いエラーは短い本文と詳細コードに分ける。

### 13.2 全画面

| screen_id | 表示 | 有効な操作 | 次の状態 |
|---|---|---|---|
| duel_entry | 「AI DUEL」「あなたの次の一手を読めるか」 | 個人/ゲスト/通信なし/戻る | profiles / mode / offline |
| profiles | 4人の名前・アイコン | 選択/前頁/次頁/戻る | pin |
| pin | 表示名・6桁入力 | 数字/消す/決定/戻る | profile / error |
| profile | 本人の累計・履歴量レベル | Jev対戦/統計AI/記録を見る/終了 | match_intro / habits |
| match_intro | 「10回勝負。相手は先に手を決めます」 | 開始/戻る | preparing |
| preparing | 「相手が準備中です」「Jev/統計AI」 | 終了/コーヒー | choose / recovery |
| choose | 何回目、点数、3つの手、「相手の手は確定済み」 | 1つの手/終了/コーヒー | submitting |
| submitting | 「選んだ手：グー」「記録を確認中」 | 手の変更不可、コーヒーは独立 | round_result / recovery |
| round_result | 両者の手・勝敗・予測・現在点数 | 次へ/終了/予測詳細 | preparing / summary |
| summary | 10回の勝敗数、対戦の勝者、provider内訳 | もう一度/癖を見る/終了 | match_intro / habits / HOME |
| habits | 本人の最大3枚の統計説明 | 前/次/戻る | profile / summary |
| resume | 「前の対戦があります」 | 再開/終了して戻る | match.get後の状態 |
| recovery | 「結果を確認しています」 | 再接続/終了 | 状態照会後に復帰 |
| offline | 「ローカル対戦・記録なし」 | 開始/戻る | ローカルの同じ10回戦 |
| exit_confirm | 「途中終了しますか？確定済みの記録は残ります」 | 終了/続ける | match.close / 元画面 |
| error | 短文とerror.code | 再試行可なら再接続/戻る | 適切な状態 |

mode選択はprofileまたはguest開始直後に行う。Jev未設定なら灰色で「設定待ち」と表示する。Jevの利用権がないのに接続中の演出を見せない。

### 13.3 固定文言

- プレイヤー勝ち：「あなたの勝ち！」
- AI勝ち（Jev）：「Jevの勝ち！」
- AI勝ち（stats）：「統計AIの勝ち！」
- あいこ：「あいこ」
- 10回戦同点：「今回は引き分け」
- フォールバック：「この回は統計AIで対戦しました」
- 的中説明：「相手の予測：グー {percent}%」※結果公開後だけ。
- 少量履歴：「まだ記録が少ないため、予測は不確かです」
- guest：「今回だけの記録です。次回には引き継ぎません」
- オフライン：「通信なしの練習です。個人記録には保存しません」
- 保存失敗：「記録できないため、手を送信していません」
- 確認中：「結果を確認しています。もう一度手を選ばないでください」
- 完了：「記録しました」※サーバー確定＋端末検証後だけ。

本文・エラー・操作名からフォント抽出対象を作る。ニックネームを追加した際はその文字も含める。必要な字体データは開発環境でライセンスを確認して導入する。本納品にフォントファイルは含めない。

### 13.4 コーヒーカウンターを妨げない

アプリ共通のコーヒー＋1へ戻る/記録する導線を常に確保する。既存の＋1イベントを利用し、ゲーム用の新しいカウント関数を別に作らない。

共通のコーヒー操作を開くと手ボタンの操作を一時停止し、コーヒーのタッチがじゃんけんへ伝播しないようイベントを消費する。＋1は既存方式で記録し、1.2秒程度の確認表示後にゲームへ戻れる。roundのサーバー期限は延長しない。通信待ちでもUIは応答し、コーヒー操作は既存の保存/再送方針を維持する。

コーヒー記録にログイン中のplayer_idを追加しない。Jevへコーヒーの履歴を送らない。

---

## 14. ESP32実装と既存機能の保全

### 14.1 既存環境を優先

Arduino / ESP-IDF / PlatformIOを名前だけで判断しない。既存のビルド成功環境を記録する。LCD初期化、GT911、I/Oエキスパンダー、バックライト、RGBタイミング、PSRAMの設定は最初の実装で変更しない。

2.8CはST7701系RGB表示とGT911タッチを使う構成で、LCDのCSやタッチのRESETにI/Oエキスパンダーが関わる。通常のESP32 GPIO番号へ置き換えない。以前の段階1引継ぎの基板設定も参照するが、その資料がなくても既存の動作済みコードを基準にできる。

動作する土台がない場合のみ、Espressifの2.8C専用ボード設定と同じリリースのサンプルを独立フォルダーに取得する。`BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_2_8_C`を使い、2.8B等を選ばない。LVGL 8プロジェクトへ9用APIを混ぜない。[S25]

### 14.2 推奨モジュール分割

```text
firmware/
  duel/duel_controller.*        状態機械・世代番号・UIイベント
  duel/duel_ui.*                画面生成と更新
  duel/duel_protocol.*          JSON検証、APIデータ変換
  duel/duel_transport.*         HTTPS、GAS redirect、再送
  duel/duel_recovery.*          NVSの復旧情報
  duel/duel_rules.*             勝敗、commit検証、ローカル統計
  duel/duel_i18n.*              日本語文字列
  duel/duel_models.*            enum/構造体
  [既存LCD/LVGL/coffee/networkモジュールは保存]
apps_script/
  Router.gs / Auth.gs / Players.gs / Matches.gs / Rounds.gs
  Stats.gs / JevAdapter.gs / SheetsRepository.gs / Protocol.gs
  Admin.gs / Maintenance.gs / Config.gs / Tests.gs
contracts/
  duel_config.json / api_actions.json / sheet_headers.json / errors.json
verification/
  duel_reference.py / verify_duel.py / test_vectors.json
```

上のファイル名は提案。既存プロジェクトの配置規約へ合わせてよいが、責務を混ぜない。

### 14.3 タスクとイベント

LVGLのクリック処理内でHTTPを実行しない。クリック→内部キュー→ネットワークworker→結果キュー→LVGL側で更新。LVGLのmutex/既存portを守る。ネットワークthreadからlv_label等を直接触らない。[S21][S22]

処理中に前画面が破棄されても、古いworkerから破棄済みポインターを触らない。`ui_generation`と`request_id`を結果に付け、現在の状態と一致したものだけ反映する。タイマー・イベント登録・画面オブジェクトの解放は対にする。

手ボタンは`LV_EVENT_CLICKED`で1回処理する。`LV_EVENT_PRESSING`や長押し繰返しではカウントしない。移動・領域外リリースはキャンセルフラグで明示管理する。[S21]

### 14.4 秘密と乱数

オンラインIDはESP32のハードウェア乱数を適切な条件で使って作る。ESP32-S3の真の乱数の成立条件はWi-Fi等のエントロピー供給状態に関係するため、公式条件と採用coreを確認する。[S23]

GASのAI同率抽選は秘密Kcommitと`AI_DUEL_TIE|1|round_id|generation_id|counter`のHMACから32bit値を取り、候補数kに対し`floor(2^32/k)*k`以上の値を棄却してmod kを取る。これで不要なmodulo biasを避ける。nonce用HMACとは文字列のドメインを分ける。

GASが確定した手をESP32側で再計算して置き換えない。ESP32側で行うのは公開後のハッシュと勝敗の照合だけ。

---

## 15. 容量・電池・通信頻度

### 15.1 ゲームの追加予算（設計上の上限）

| 資源 | 初期予算・確認 |
|---|---|
| HTTP request | 最大8KiB |
| HTTP response | 最大16KiB |
| Jevへ送るstate | 最大16KiB。超えたら直近列を短縮し、集計は維持 |
| pending NVS | 原則1件、合計4KiB以内を目標 |
| 1人のStats JSON | 24KiB以内 |
| ESP32常駐ゲームデータ | 全社員分を常駐させず、現在の本人表示情報と対戦だけ |
| 手アイコン | 小さい図形・単色画像を優先 |
| 日本語フォント | 必要文字＋承認したニックネームをsubset化。実際の容量を測定 |

480×480のRGB565フルフレームは480×480×2=460,800byte。これと日本語フォントはじゃんけんの文字列より大きい。既存のframebufferを重複確保せず、8MB PSRAMや16MB Flashの全量を新ゲームで自由に使えると考えない。[S01]

### 15.2 電池

1500mAhの動作時間は本設計書で保証しない。バックライト・Wi-Fi・API往復・実装されたスリープ条件で変わる。USB給電のmAをそのまま3.7V電池のmAhへ割り算しない。電池側電流を測るか、電力と変換効率を含めて評価する。

対戦中は、GT911の復帰条件が未検証のdeep sleepへ入れない。初版はバックライト制御だけ。無操作で低輝度、操作時復帰。最初の起床タッチを即「グー」にしない。HOMEの既存省電力方針と整合させる。

### 15.3 通信頻度

1ラウンドは通常prepare＋submit、1対戦は約20件のゲームHTTP要求＋ログイン・照会等。常時pollや画面更新ごとのHTTPは禁止。新しいprepareは端末あたり1分12回まで、開始間隔は最低5秒を初期値とする。

ゲーム準備中でもCOFFEE TIMEの時計描画やタッチ入力を止めない。Wi-Fiの再接続は1つの共通管理に集約し、複数モジュールが同時にbegin/disconnectしない。

---

## 16. セキュリティ・利用同意・削除

### 16.1 初回に示す利用説明の文面

> このゲームは任意参加です。ニックネームとじゃんけんの手・結果をGoogleスプレッドシートに保存し、次回の対戦に使います。予測のため、名前やPINを含めない対戦履歴・統計をTypeSafeのJevへ送信します。人事評価や勤怠管理には利用しません。個人記録を残さずに遊べるゲストも選べます。記録のリセット・削除は管理者に依頼できます。各ラウンドの明細は標準180日、集計は削除依頼まで保存します。

これは製品内の説明案であり、法令適合性を認定する文書ではない。社内のデータ取扱い規程と外部API利用の承認を運用開始前に確認する。API先の保持・利用条件が承認できない場合はstatsモードのみ。

### 16.2 不要な収集をしない

氏名・社員番号・顔・声・位置・コーヒー習慣を予測へ入れない。他人への公開ランキングはv1対象外。プロフィール一覧は同意したゲーム用ニックネームだけ。本人の統計画面はログアウトで消し、画面キャッシュを次の利用者へ再利用しない。

### 16.3 管理機能（公開APIとは別）

| 関数 | 処理 |
|---|---|
| adminProvisionPlayer | 同意確認、名前/アイコン/PIN検証、Playersと空Statsを原子的作成 |
| adminResetPin | 本人確認後、新saltとdigest、全session失効。active matchは終了 |
| adminSetPlayerEnabled | active/disabled変更。停止時はsession失効と未提出round取消 |
| adminResetHistory | active matchがない状態で確認後、対象明細・match・Statsの削除ジョブ。再開時は0から |
| adminDeletePlayer | delete_pending→ログイン禁止→全関連データ消去→deleted。ID再利用なし |
| adminRepairStats | 保守中だけバックアップ/明細から再構築。Jevへ再推論しない |
| adminRotateDeviceToken | 旧端末token無効化、実機へ新tokenを再設定 |
| adminRunMaintenance | 期限処理・保持期限・バックアップ・ヘッダー整合性検査 |

PINを忘れた人へ古いPINを表示する機能はない。再設定だけ。

### 16.4 削除処理の範囲

DuelPlayersの表示名・PIN・同意情報、Stats、Matches、全対象月のRounds、Sessions、Requestsの個人参照、保存した個人バックアップを対象とする。件数が多い場合はdelete_pendingとDuelMetaの進捗ジョブを残し、小さいバッチで再開可能にする。

現在の表から消すだけで、Googleの版履歴や提供者側の保持データまで即時完全消去されたと表示しない。バックアップは通常30日以内に期限削除。削除要求済みIDの最小限のtombstoneを保持し、古いバックアップから復元してもそのIDを再生成しない。削除開始/完了の管理監査にはPINや手の全履歴を残さない。

### 16.5 攻撃・誤用対策

端末token失効、ログイン失敗制限、手の改変拒否、request bodyサイズ上限、actionのホワイトリスト、URL固定、JSON数値検証、適切なHTTPS検証を必須にする。未認証requestはJevや全履歴読込へ進めない。

ユーザー入力をJevのinstructionsへ連結しない。表示名をHTMLへ挿入する管理UIはtextContent等で扱う。HTTPのログからtokenをマスクする。シートの編集権限だけでなくApps Scriptの編集権限も制限する。

GASの公開エンドポイントに対する大量アクセスは、アプリ内認証だけでGoogle実行枠を完全に守れるものではない。一般公開の大規模サービスではなく、登録済み社内端末向けとして運用する。

---

## 17. 運用・保守・負荷・費用

### 17.1 保存上限

登録本人のラウンド明細と終了済みDuelMatchesは標準180日、guestの明細/対戦は24時間、request receiptは48時間、有効期限切れsessionは24時間を保持期間として日次保守で期限対象を削除する。日次保守の実行間隔分は削除が遅れる場合があるため「24時間以内に完全削除」とは案内しない。累計Statsは保持し、明細削除だけで累計を減らさない。削除済みの古いmatch_id/round_idに新しいupdateを送っても再作成せずNOT_FOUND。新規match.openについても旧client_match_idの意図しない再利用を禁止する。

台帳の各行を消去しても割当済み行数は減らない。月次/日次タブ全体が期限済みで参照中の対戦もないことを検証した場合だけ、管理保守でタブ全体を削除してセル数を解放する。そのtabの古い参照は必ずNOT_FOUNDになることを検査する。固定タブの圧縮はゲーム停止中の明示的移行として行い、索引を一緒に再構築する。

毎日の保守では、個人Statsとactive参照の非公開スナップショットを先に取得し、成功確認後に期限済み明細を削除する。バックアップは管理者だけがアクセスできる保存先とし、30日ローテーション。コードに秘密を含むWorkbook全体の無制限複製を作らない。

180日を過ぎて生の手順を削除した部分は、保存済みの累計やスナップショットから継続できるが、後日新しい特徴量で全履歴を再解析できない。保管要件を変える場合は**削除前**に変更する。完全な再集計が必要な統計仕様変更は、残っている明細とバックアップの範囲を表示して移行する。

### 17.2 Google側の上限

調査時の公式表では、Apps Scriptは1実行6分、URL Fetchは一般アカウント20,000回/日、Workspace100,000回/日。Sheets APIは読み/書き各60回/分/ユーザー/プロジェクト、各300回/分/プロジェクトの枠がある。**日次API回数が少なくても毎分のSheets書込みで詰まる場合がある。** これらは変更されうるため実アカウントと導入時点で再確認。[S16][S24]

本設計の通常3書込み/ラウンドでは、12ラウンド/分なら36回/分の主要書込み。ログイン・保守・他アプリにも枠を使うので60回まで使い切らない。読み込み回数も別に測定し、429時はバックオフ。勝手に複数アカウントへ分散して制限回避する設計にはしない。

負荷見積もり例（計算上の仮定）：20人×1日2対戦×10回=400ラウンド/日。Jevは最大400回、主な書込みは約1,200回/日。ただし同時刻の集中が別問題。実測に基づきprepare頻度を調整する。

### 17.3 v1の運用ガード

- 1端末1日最大1,000ラウンド。
- 1個人1日20対戦まで。abortedも開始回数の上限には数える。
- Jev呼出し予約は1日1,500回のソフト上限。実際の請求上限ではない。
- Workbookの割当セル数5,000,000を警告、8,000,000で保守停止。空行・空列も含めて計測する。
- 上限に達したら新しい個人対戦を止め、既にcommittedの結果保存を優先する。新規開始制限のためにsubmitを拒否しない。
- 料金は実際のTypeSafe契約とusageを集計する。金額を端末へ固定せず、`Σinput_tokens / 1,000,000 × 契約単価`で管理する。

### 17.4 実測する時間

GAS起動を含むprepare全体、Jev単体、Sheet読込、一括保存、submit全体を分けて記録。目標は正常時prepare p95≤5秒、submit p95≤3秒。ただしこれは**受入目標であり保証値ではない**。超えたら最適化し、届かない場合は統計AIを既定にする等の判断を実測とともに報告する。Jevの推論速度だけで端末の操作感を説明しない。

### 17.5 保守中

日次保守は新規matchを止めて行い、committed/submit_pendingの対戦を強制的に消さない。保守関数もScriptLockを取得。期限処理でaborted/expiredを二重集計しない。運用用のステータス表示はaggregateのみで個人の手を公開しない。

---

## 18. 実装フェーズと納品物

### Phase 0 — 保全と現状確認

既存ファーム・GAS・Sheets設定を保存し、LCD/タッチ/＋1/Google連携を再現。採用ライブラリ・ビルド設定・配備URLを秘密抜きで記録。接続情報がない場合は以下のPCロジックまで進め、架空の実機成功を書かない。

### Phase 1 — ゲームの純粋ロジック

3手、9勝敗、10回戦、確率検証、期待得点差、統計更新、コミット文字列を実装。付録の試験値と同じ結果をC++/JavaScriptで得る。ボタンや通信より先にロジックを確認。

### Phase 2 — 端末のローカルguest UI

16画面の必要部分、長押し・連打対策、終了・コーヒーへの導線。既存カウンターが壊れないことを確認。ローカル結果を公式戦績へ入れない。

### Phase 3 — GAS＋Sheets、まず統計AI

スキーマ、個人登録、PIN、session、active制約、prepare/submit、原子的バッチ、冪等再送。実際のSheetsで障害を挟む試験を行う。Jevなしでも「翌日の同じ人の記録を読める」ことをここで完成させる。

### Phase 4 — Jev接続

Script Propertiesの実キーで1回の手動接続試験→レスポンスfixture保存（秘密除去）→adapter→失敗分岐→統計AIとの区分。名前・PIN・今回の手がAPI stateに含まれないことを自動検査。

### Phase 5 — 電源断・待機・全体統合

NVS復旧、PIN再入力、途中終了、GAS302/303、タイムアウト、期限、コーヒー操作の競合、バッテリー時の連続利用を試験。USB接続成功だけで終わらない。

### Phase 6 — 小規模運用と評価

少人数で使い、統計AI・Jevの予測精度、APIエラー、体感待ち時間、個人取り違えを確認。記録数が増えるだけで精度が上がると断定しない。条件を満たしたものを正式稼働とする。

### エージェントの納品物

- 変更した全ソースとGit差分、動いた版数のlock/設定。
- 再ビルド・書込み・GASデプロイの手順。秘密を含めない。
- Sheets初期化・管理・保守関数と管理者用操作説明。
- C++/GASの単体試験、接続試験、実機試験のログ。
- 実際の状態遷移と復旧時の画面写真/スクリーンショット。
- provider別の評価、未達項目、既知の制約。
- API接続をモックした部分と実APIを呼んだ部分の一覧。
- 更新前へ戻す手順。既存コーヒーカウンターへの影響確認。

---

## 19. 受入試験と完成判定

各項目に`PASS / FAIL / NOT_RUN`、試験環境、版数、日時、証拠ファイルを記録する。NOT_RUNをPASSにしない。

### 19.1 ロジックと個人履歴

| ID | 試験 | 合格条件 |
|---|---|---|
| L01 | 9通りの勝敗 | 第1章と一致 |
| L02 | 同点・あいこ混在10回戦 | 10回で終了し、点数と対戦勝敗が一致 |
| L03 | 確率の全辺界・同率 | 有限値のみ、期待得点差の最大手を選ぶ |
| L04 | R=.40,S=.35,P=.25 | AIはROCKを選ぶ |
| L05 | 0件の個人 | 一様予測。未測定の癖を出さない |
| L06 | 10回後再ログイン | 10ラウンドの履歴を継続、0へ戻らない |
| L07 | 別の人の開始 | 前の人の履歴が混ざらない |
| L08 | 日/対戦境界 | 遷移を対戦間でつながない。第1手は別集計 |
| L09 | 50件tail上限 | 累計は保持、tailだけ古い順に削除 |
| L10 | 母数20未満の癖 | 条件つきの強い癖表示をしない |
| L11 | provider混在 | Jevと統計AIのn/hits/勝敗表示を区別 |
| L12 | 途中終了 | 既存resolvedを保持、対戦の勝敗は付けない |

### 19.2 公平性・認証

| ID | 試験 | 合格条件 |
|---|---|---|
| F01 | prepare時のstate検査 | 現在の手、PIN、名前、社員番号を含まない |
| F02 | submit前の全レスポンス検査 | ai_hand/nonce/確率が漏れない |
| F03 | commit試験値 | C++/GAS/Pythonで同じ64hex |
| F04 | AI手/nonce/round番号改変 | commit照合失敗 |
| F05 | 同じroundへ別の手 | 拒否され、原結果不変 |
| F06 | 他人のmatch_id/profile要求 | FORBIDDEN。本人の統計のみ |
| F07 | PIN5連続失敗・端末上限 | 規定時間ロック。再送による二重失敗加算なし |
| F08 | 期限切れ/失効token | 拒否。再PIN後は本人の状態を再取得 |
| F09 | ログ・シート・NVS検査 | 平文PIN/Jevキー/player_tokenの意図しない保存なし |
| F10 | 退会・リセット | 対象のみ処理し、復元で再出現しない |

### 19.3 GAS・Sheets・ネットワーク

| ID | 試験 | 合格条件 |
|---|---|---|
| N01 | /execの302/303 | GETでJSON取得、秘密POSTを他ホストへ転送しない |
| N02 | 200のHTMLログイン画面 | JSON成功にしない |
| N03 | 同じsubmitを32回並行送信 | resolvedと統計は1回だけ |
| N04 | 同じID・違う本文 | IDEMPOTENCY_CONFLICT |
| N05 | prepareを複数同時送信 | 同じround、Jevアプリ呼出し最大1回 |
| N06 | lease期限後の古いworker応答 | 新しい状態を上書きしない |
| N07 | batchに不正な更新を混ぜる | そのバッチ全体が未適用、部分戦績なし |
| N08 | batch成功直後に応答を落とす/遅延させる | 防壁とlast_write_receiptを照合。未確定の間は更新停止、確定後の再送は原結果、行/点数増殖なし |
| N09 | Sheets429/一時エラー | バックオフし、新規ラウンドを連続増殖させない |
| N10 | Jev401/422/429/529/5xx/timeout | 規定の統計AI切替と管理記録 |
| N11 | Jev不正確率/キー欠落/負値/NaN | 不正応答を採用しない |
| N12 | 想定400ラウンド/日相当の負荷 | 実測レイテンシ、毎分API使用量、原本整合性を報告 |
| N13 | 月末/日末/時差 | roundとreceiptが正しいタブに保存され復旧可能 |
| N14 | 別デプロイ/管理者編集の並行書込み | 設計禁止事項を検知/運用で排除 |

### 19.4 ESP32実機

| ID | 試験 | 合格条件 |
|---|---|---|
| H01 | 3ボタンの中央/縁/外側 | 正しい手だけ選ばれる |
| H02 | 長押し・連打・2点タッチ | 1roundにつき1選択 |
| H03 | 日本語・名前・PIN画面 | 円外欠け、豆腐文字、極小文字なし |
| H04 | prepare中・submit中の画面操作 | UIが固まらず、誤選択なし |
| H05 | 第12章の各電源断位置 | 同じ手/commitで復旧、記録の二重加算なし |
| H06 | NVS書込失敗注入 | 手を送信せずPERSIST_FAILED |
| H07 | 電池駆動・充電接続中 | 起動/表示/通信が安定。持続時間は実測値を記載 |
| H08 | 無操作からの復帰 | 起床タッチがじゃんけんの手にならない |
| H09 | 連続50対戦 | 画面オブジェクト・heap・socketの継続的リークなし |
| H10 | ゲーム中のコーヒー＋1 | 既存仕様で1回だけ記録、個人IDをひもづけない |
| H11 | オフライン→オンライン | localguestの戦績を混ぜない |
| H12 | 既存AI ESPER/HOMEへ往復 | 初期化の二重実行や状態混入なし |

### 19.5 完成の段階を分ける

- **仕様・参考ロジックの検査済み**：付録のPython検査が通る段階。本納品はここまで。
- **統計AI版の実機完成**：Jevに依存しない個人履歴、Google保存、公平性、復旧、UI試験に合格。
- **Jev版の実機完成**：実API契約・障害分岐・区分集計・実測操作性まで合格。

本書を読んだだけで「ESP32実装完了」と報告しない。ボードへ書き込み、実際のタッチ、再起動後の個人履歴、Googleの台帳、Jevの実レスポンスが揃って初めて該当段階を完了とする。

---

## 20. 実行済みの事前検査

この文書に添付するPython標準ライブラリのみの参考コードについて、次を実行した。

- 25テスト、失敗0、エラー0。
- 手の全9組の勝敗表。
- 0.01刻みの確率格子5,151点で、期待得点差の最大手と正規化を確認。
- 同一submitを32回、スレッド並行でテストし、メモリー上の台帳/統計が1回だけ更新されることを確認。
- 1,000ラウンドの履歴更新で、累計1,000、tail50、100対戦の第1手、対戦内遷移900を確認。
- ハッシュ改変、salt/pepper、期限、世代競合、プロフィール分離、10回戦終了、3ボタンの円内配置を確認。

**これはGASやGoogle Sheetsの同時更新を実測した結果ではない。** テストのRLockとメモリー更新は本番のScriptLock/batchUpdateの代わりのモデルである。実機のビルド、GASデプロイ、実Sheet、Jev API、実電池は本環境では未試験。結果JSONを付録Hにそのまま収録する。

参考コードはルール・統計・コミット・状態機械の核の検査用であり、認証API全体、削除ジョブ、HTTP、全UIを実装した完成アプリではない。エージェントは本文の未実装責務を省略してはいけない。

---

## 21. 根拠資料

外部仕様の確認日：2026-09-20。公式資料を優先した。数値の閾値・モード・保存日数・勝敗方針は本書の設計判断。APIや利用制限は変更されるため実装開始時に確認する。Webの`stable`やGitHubの`master`をそのまま本番の版固定と考えない。

[S01] Waveshare公式・対象ボードの概要と搭載資源  
https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8C

[S02] TypeSafe・Choice（選択肢と確率）  
https://docs.typesafe.ai/primitives/choice

[S03] TypeSafe・State（推論へ渡す状態）  
https://docs.typesafe.ai/concepts/state

[S04] TypeSafe・HTTP API（実エンドポイント、認証、型、エラー）  
https://docs.typesafe.ai/api

[S05] TypeSafe・Confidence  
https://docs.typesafe.ai/confidence

[S06] TypeSafe・Models（調査時のモデルID・条件）  
https://docs.typesafe.ai/models

[S07] Google・Apps Script Web Apps  
https://developers.google.com/apps-script/guides/web

[S08] Google・UrlFetchApp（timeoutSecondsを含む現行パラメーター）  
https://developers.google.com/apps-script/reference/url-fetch/url-fetch-app

[S09] Google・Content Service（返答のリダイレクト）  
https://developers.google.com/apps-script/guides/content

[S10] Google・Properties Service  
https://developers.google.com/apps-script/guides/properties

[S11] Google・Utilities（HMAC/SHA等）  
https://developers.google.com/apps-script/reference/utilities/utilities

[S12] Google・LockService  
https://developers.google.com/apps-script/reference/lock/lock-service

[S13] Google・Advanced Sheets Service  
https://developers.google.com/apps-script/advanced/sheets

[S14] Google・高度なサービスの有効化  
https://developers.google.com/apps-script/guides/services/advanced

[S15] Google・Sheets batchUpdate  
https://developers.google.com/workspace/sheets/api/reference/rest/v4/spreadsheets/batchUpdate

[S16] Google・Sheets APIの利用上限と原子性  
https://developers.google.com/workspace/sheets/api/limits

[S17] Google・Apps Script Best Practices（バッチ読書き）  
https://developers.google.com/apps-script/guides/support/best-practices

[S18] Espressif・ESP HTTP Client  
https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/protocols/esp_http_client.html

[S19] Espressif・Arduino HTTPClientソース（採用版で挙動確認）  
https://raw.githubusercontent.com/espressif/arduino-esp32/master/libraries/HTTPClient/src/HTTPClient.cpp

[S20] Espressif・NVS  
https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/storage/nvs_flash.html

[S21] LVGL 8・Events  
https://docs.lvgl.io/8.3/overview/event.html

[S22] LVGL 8・OS/threads  
https://docs.lvgl.io/8.3/porting/os.html

[S23] Espressif・乱数生成の条件  
https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/random.html

[S24] Google・Apps Scriptの利用上限  
https://developers.google.com/apps-script/guides/services/quotas

[S25] Espressif・Waveshare対応ボード定義と環境資料  
https://github.com/esp-arduino-libs/ESP32_Display_Panel/blob/master/docs/board/board_waveshare.md

参考にした既存引継ぎ：`COFFEE_TIME_Stage1_Agent_Brief.md`、`COFFEE_TIME_AI_ESPER_Design_v1.0.md`。これらは今回のゲームAPI契約の代わりではなく、既存初期化・確定ハード・既存機能の保全確認のために参照した。

---

## 付録A：設定の正本

### File: duel_config.json

```json
{
  "schema_version": "1.0.0",
  "game_id": "ai_duel",
  "api_namespace": "ai_duel",
  "api_version": 1,
  "timezone": "Asia/Tokyo",
  "hands": [
    "ROCK",
    "SCISSORS",
    "PAPER"
  ],
  "beats": {
    "ROCK": "SCISSORS",
    "SCISSORS": "PAPER",
    "PAPER": "ROCK"
  },
  "match": {
    "rounds": 10,
    "points_win": 1,
    "points_draw": 0,
    "points_loss": 0,
    "max_active_per_player": 1,
    "max_active_per_device": 1,
    "idle_expiry_seconds": 300,
    "absolute_expiry_seconds": 1800,
    "inter_round_min_seconds": 5
  },
  "identity": {
    "pin_digits": 6,
    "pin_attempts": 5,
    "pin_lock_seconds": 900,
    "session_seconds": 1200,
    "device_idle_logout_seconds": 60,
    "max_players": 50,
    "page_size": 4,
    "nickname_max_codepoints": 8,
    "consent_version": "duel-consent-1"
  },
  "network": {
    "client_timeout_ms": 20000,
    "client_attempts": 3,
    "retry_ms": [
      1000,
      3000
    ],
    "prepare_poll_ms": 3000,
    "prepare_poll_limit": 5,
    "provider_timeout_seconds": 8,
    "provider_calls_per_round": 1,
    "prepare_lease_seconds": 30,
    "lock_wait_ms": 2000,
    "max_request_bytes": 8192,
    "max_response_bytes": 16384,
    "max_redirects": 3,
    "request_receipt_seconds": 172800
  },
  "prediction": {
    "source_modes": [
      "jev",
      "stats"
    ],
    "smoothing_alpha": 1,
    "recent_window": 20,
    "sequence_window": 12,
    "tail_capacity": 50,
    "transition_min_n": 5,
    "result_transition_min_n": 8,
    "first_hand_min_n": 5,
    "component_weights": {
      "overall": 0.15,
      "recent20": 0.25,
      "previous_hand": 0.3,
      "previous_hand_result": 0.3
    },
    "first_hand_weight": 0.5,
    "probability_sum_tolerance": 0.001,
    "tie_epsilon": 1e-09,
    "profile_levels": [
      {
        "min_rounds": 0,
        "name": "はじめまして"
      },
      {
        "min_rounds": 10,
        "name": "記録が増えています"
      },
      {
        "min_rounds": 50,
        "name": "履歴を活用中"
      },
      {
        "min_rounds": 200,
        "name": "長期履歴あり"
      }
    ],
    "provider_endpoint": "https://api.typesafe.ai/v1/systemone",
    "model_initial": "jev-1.13.0",
    "prompt_version": "duel-next-hand-1",
    "policy_version": "max_expected_margin-1",
    "stats_version": "duel-stats-1"
  },
  "habits": {
    "min_n": 20,
    "max_cards": 3,
    "min_share": 0.6,
    "recent_min_n": 20
  },
  "retention": {
    "round_days": 180,
    "requests_hours": 48,
    "session_cleanup_hours": 24,
    "profile_until_delete": true,
    "backup_days": 30,
    "soft_cell_limit": 5000000,
    "hard_cell_limit": 8000000,
    "guest_hours": 24,
    "closed_match_days": 180
  },
  "operations": {
    "max_registered_matches_per_player_day": 20,
    "max_rounds_device_day": 1000,
    "max_new_prepares_per_device_minute": 12,
    "max_attempted_pin_per_device_15min": 10,
    "api_fetch_soft_cap_day": 1500
  },
  "display": {
    "width": 480,
    "height": 480,
    "safe_center": [
      240,
      240
    ],
    "safe_radius": 224,
    "text_px_body": 24,
    "text_px_main": 32,
    "text_px_score": 48,
    "hand_button_radius": 54,
    "hand_button_centers": [
      [
        120,
        264
      ],
      [
        240,
        264
      ],
      [
        360,
        264
      ]
    ],
    "game_sleep": "backlight_only",
    "game_auto_rotation": false
  }
}
```

---

## 付録B：全11操作の入力契約

### File: api_actions.json

```json
{
  "device.hello": {
    "auth": "device",
    "payload": {},
    "returns": "schema_version, server_time_ms, available_modes, device_enabled, deployment_version",
    "effect": "read"
  },
  "players.list": {
    "auth": "device",
    "payload": {
      "page": {
        "type": "integer",
        "minimum": 0
      }
    },
    "returns": "players[{player_id,display_name,avatar_id}],page,has_more",
    "effect": "read"
  },
  "player.login": {
    "auth": "device",
    "payload": {
      "player_id": {
        "type": "string"
      },
      "pin": {
        "type": "string",
        "pattern": "^[0-9]{6}$"
      }
    },
    "returns": "player_token,auth_id,expires_at_ms,player{player_id,display_name,avatar_id},active_match_id",
    "effect": "auth"
  },
  "player.logout": {
    "auth": "player_or_guest",
    "payload": {},
    "returns": "logged_out",
    "effect": "write"
  },
  "profile.get": {
    "auth": "player",
    "payload": {},
    "returns": "display_name,stats,habit_cards,history_start_ms,retention_days",
    "effect": "read"
  },
  "guest.begin": {
    "auth": "device",
    "payload": {},
    "returns": "player_token,auth_id,expires_at_ms,guest=true",
    "effect": "auth"
  },
  "match.open": {
    "auth": "player_or_guest",
    "payload": {
      "client_match_id": {
        "type": "string",
        "pattern": "^[0-9a-f]{32}$"
      },
      "mode": {
        "type": "string",
        "enum": [
          "jev",
          "stats"
        ]
      }
    },
    "returns": "match_id,round_total,next_round_no,status,score,mode",
    "effect": "write"
  },
  "match.get": {
    "auth": "player_or_guest",
    "payload": {
      "match_id": {
        "type": "string",
        "pattern": "^[0-9a-f]{32}$"
      }
    },
    "returns": "match_id,status,score,rounds_resolved,next_round_no,active_round_public,last_resolved_result,match_result",
    "effect": "read"
  },
  "round.prepare": {
    "auth": "player_or_guest",
    "payload": {
      "match_id": {
        "type": "string",
        "pattern": "^[0-9a-f]{32}$"
      },
      "round_no": {
        "type": "integer",
        "minimum": 1,
        "maximum": 10
      }
    },
    "returns": "round_id,status,commit_hash,commit_version,provider_used,expires_at_ms,retry_after_ms",
    "effect": "prepare"
  },
  "round.submit": {
    "auth": "player_or_guest",
    "payload": {
      "match_id": {
        "type": "string",
        "pattern": "^[0-9a-f]{32}$"
      },
      "round_id": {
        "type": "string",
        "pattern": "^[0-9a-f]{32}$"
      },
      "round_no": {
        "type": "integer",
        "minimum": 1,
        "maximum": 10
      },
      "player_hand": {
        "type": "string",
        "enum": [
          "ROCK",
          "SCISSORS",
          "PAPER"
        ]
      },
      "commit_hash": {
        "type": "string",
        "pattern": "^[0-9a-f]{64}$"
      }
    },
    "returns": "round_id,round_no,player_hand,ai_hand,player_result,nonce,commit_hash,prediction,score,match_status,match_result,next_round_no",
    "effect": "resolve"
  },
  "match.close": {
    "auth": "player_or_guest",
    "payload": {
      "match_id": {
        "type": "string",
        "pattern": "^[0-9a-f]{32}$"
      },
      "reason": {
        "type": "string",
        "enum": [
          "user_exit",
          "idle",
          "device_restart",
          "integrity_error"
        ]
      }
    },
    "returns": "match_id,status,rounds_resolved,score",
    "effect": "write"
  }
}
```

---

## 付録C：全29エラーの既定文言と再試行区分

各値は`[message_ja, retryable]`。APIでは第10章のerrorオブジェクトへ変換し、retry_after_msも設定する。

### File: errors.json

```json
{
  "BAD_REQUEST": [
    "入力形式を確認してください",
    false
  ],
  "SCHEMA_MISMATCH": [
    "更新が必要です",
    false
  ],
  "DEVICE_UNAUTHORIZED": [
    "端末の登録を確認してください",
    false
  ],
  "DEVICE_DISABLED": [
    "この端末は停止中です",
    false
  ],
  "AUTH_FAILED": [
    "番号またはPINを確認してください",
    false
  ],
  "PIN_LOCKED": [
    "しばらくしてから再入力してください",
    true
  ],
  "AUTH_EXPIRED": [
    "もう一度ログインしてください",
    false
  ],
  "PLAYER_DISABLED": [
    "このプロフィールは利用できません",
    false
  ],
  "CONSENT_REQUIRED": [
    "管理者に利用設定を確認してください",
    false
  ],
  "FORBIDDEN": [
    "この記録にはアクセスできません",
    false
  ],
  "IDEMPOTENCY_CONFLICT": [
    "送信内容の不一致を検出しました",
    false
  ],
  "MATCH_ACTIVE": [
    "前の対戦を再開または終了してください",
    false
  ],
  "MATCH_CLOSED": [
    "この対戦は終了しています",
    false
  ],
  "ROUND_ORDER": [
    "対戦状態を確認します",
    false
  ],
  "ROUND_NOT_READY": [
    "相手の準備中です",
    true
  ],
  "ROUND_EXPIRED": [
    "時間が経ったため対戦を終了しました",
    false
  ],
  "HAND_ALREADY_SUBMITTED": [
    "この回の手は変更できません",
    false
  ],
  "COMMIT_MISMATCH": [
    "整合性エラーのため停止しました",
    false
  ],
  "BUSY": [
    "ただいま通信中です",
    true
  ],
  "RATE_LIMITED": [
    "少し間を空けてください",
    true
  ],
  "DAILY_LIMIT": [
    "本日の上限に達しました",
    false
  ],
  "PROVIDER_UNAVAILABLE": [
    "Jevが利用できません。統計AIへ切り替えます",
    true
  ],
  "STORAGE_UNAVAILABLE": [
    "保存状態を確認しています",
    true
  ],
  "REPLAY_TOO_OLD": [
    "古い送信です。現在の状態を確認します",
    false
  ],
  "NOT_FOUND": [
    "対戦記録が見つかりません",
    false
  ],
  "MAINTENANCE": [
    "記録の保守中です",
    true
  ],
  "RESPONSE_INVALID": [
    "通信内容を確認できませんでした",
    true
  ],
  "PERSIST_FAILED": [
    "端末に記録できないため手を送信できません",
    false
  ],
  "INTERNAL": [
    "処理できませんでした。状態を確認します",
    true
  ]
}
```

---

## 付録D：全シートの列順

### File: sheet_headers.json

```json
{
  "DuelMeta": [
    "key",
    "value_json",
    "schema_version",
    "updated_at_ms"
  ],
  "DuelDevices": [
    "device_id",
    "enabled",
    "active_match_id",
    "pin_window_start_ms",
    "pin_window_attempts",
    "locked_until_ms",
    "updated_at_ms"
  ],
  "DuelPlayers": [
    "player_id",
    "display_name",
    "avatar_id",
    "status",
    "pin_salt_hex",
    "pin_digest_hex",
    "pin_key_version",
    "pin_failures",
    "pin_locked_until_ms",
    "consent_version",
    "consented_at_ms",
    "active_match_id",
    "created_at_ms",
    "updated_at_ms"
  ],
  "DuelSessions": [
    "auth_id",
    "token_digest_hex",
    "player_id",
    "device_id",
    "is_guest",
    "status",
    "created_at_ms",
    "expires_at_ms",
    "login_request_id"
  ],
  "DuelStats": [
    "player_id",
    "history_start_ms",
    "stats_version",
    "stats_json",
    "last_round_id",
    "updated_at_ms"
  ],
  "DuelMatches": [
    "match_id",
    "player_id",
    "device_id",
    "is_guest",
    "requested_mode",
    "status",
    "created_at_ms",
    "expires_at_ms",
    "last_activity_ms",
    "resolved_count",
    "human_wins",
    "ai_wins",
    "draws",
    "active_round_id",
    "round_locations_json",
    "session_stats_json",
    "close_reason",
    "provider_counts_json",
    "match_result"
  ],
  "DuelRounds_YYYYMM": [
    "round_id",
    "match_id",
    "round_no",
    "player_id",
    "device_id",
    "is_guest",
    "status",
    "generation_id",
    "lease_until_ms",
    "prepared_at_ms",
    "expires_at_ms",
    "provider_requested",
    "provider_call_reserved",
    "provider_used",
    "model_requested",
    "model_returned",
    "prompt_version",
    "policy_version",
    "history_count_before",
    "context_hash",
    "stats_fallback_json",
    "prediction_json",
    "ai_hand",
    "nonce_hex",
    "commit_hash",
    "committed_at_ms",
    "player_hand",
    "player_result",
    "submitted_at_ms",
    "submit_request_id",
    "provider_latency_ms",
    "input_tokens",
    "output_tokens",
    "failure_code",
    "result_json"
  ],
  "DuelRequests_YYYYMMDD": [
    "request_key",
    "action",
    "fingerprint_hex",
    "resource_ref",
    "result_code",
    "response_safe_json",
    "created_at_ms",
    "expires_at_ms"
  ],
  "DuelAdminAudit": [
    "event_id",
    "event_at_ms",
    "action",
    "target_player_id",
    "result",
    "detail_code"
  ]
}
```

---

## 付録E：実行済み参考ロジック

このPythonはロジックの照合用で、GAS/ESP32へそのままコピーするファームではない。内部TestEngineの時刻は試験用の秒単位、本番API・Sheetsの時刻は本文どおりミリ秒。公開HTTPレスポンスも本文のホワイトリストに従い、TestEngine内部オブジェクトを送信しない。

### File: duel_reference.py

```python
"""AI DUEL v1: executable reference logic, NOT ESP32 firmware or a GAS backend.
Python 3.10+, standard library only. The in-memory lock is a test double, not
proof of real Sheets concurrency. Production must use the specified GAS lock
and atomic Sheets batches. Randomness here is supplied by secrets.
"""
from __future__ import annotations
import copy
import hashlib
import hmac
import math
import secrets
import threading
from dataclasses import dataclass
from typing import Any

HANDS = ('ROCK', 'SCISSORS', 'PAPER')
BEATS = {'ROCK': 'SCISSORS', 'SCISSORS': 'PAPER', 'PAPER': 'ROCK'}
RESULTS = ('human_win', 'ai_win', 'draw')

class RuleError(ValueError):
    pass

def result(human: str, ai: str) -> str:
    if human not in HANDS or ai not in HANDS:
        raise RuleError('BAD_REQUEST')
    return 'draw' if human == ai else ('human_win' if BEATS[human] == ai else 'ai_win')

def normalize_probabilities(raw: dict[str, float]) -> dict[str, float]:
    if not isinstance(raw, dict) or set(raw) != set(HANDS):
        raise RuleError('INVALID_PROBABILITIES')
    if any(isinstance(x, bool) or not isinstance(x, (int, float))
           or not math.isfinite(x) or x < 0 or x > 1 for x in raw.values()):
        raise RuleError('INVALID_PROBABILITIES')
    s = sum(raw.values())
    if s <= 0 or abs(s - 1.0) > .001 + 1e-12:
        raise RuleError('INVALID_PROBABILITIES')
    return {h: raw[h] / s for h in HANDS}

def expected_margins(p: dict[str, float]) -> dict[str, float]:
    p = normalize_probabilities(p)
    return {a: sum((1 if BEATS[a] == h else -1 if BEATS[h] == a else 0) * p[h]
                   for h in HANDS) for a in HANDS}

def optimal_actions(p: dict[str, float]) -> list[str]:
    u = expected_margins(p)
    best = max(u.values())
    return [h for h in HANDS if abs(u[h] - best) <= 1e-9]

def choose_ai(p: dict[str, float]) -> str:
    return secrets.choice(optimal_actions(p))

def commitment(match_id: str, round_id: str, round_no: int, ai_hand: str, nonce: str) -> str:
    if ai_hand not in HANDS or not 1 <= round_no <= 10:
        raise RuleError('BAD_REQUEST')
    if any(len(x) != 32 or any(c not in '0123456789abcdef' for c in x) for x in (match_id, round_id)):
        raise RuleError('BAD_REQUEST')
    if len(nonce) != 64 or any(c not in '0123456789abcdef' for c in nonce):
        raise RuleError('BAD_REQUEST')
    text = f'AI_DUEL|1|{match_id}|{round_id}|{round_no}|{ai_hand}|{nonce}'
    return hashlib.sha256(text.encode('ascii')).hexdigest()

def pin_digest(pepper: bytes, player_id: str, salt_hex: str, pin: str) -> str:
    if len(pepper) < 32 or len(pin) != 6 or not pin.isascii() or not pin.isdigit():
        raise RuleError('BAD_PIN')
    msg = f'AI_DUEL_PIN|1|{player_id}|{salt_hex}|{pin}'.encode('utf-8')
    return hmac.new(pepper, msg, hashlib.sha256).hexdigest()

def zero_stats() -> dict[str, Any]:
    return {
        'stats_version':'duel-stats-1', 'rounds':0, 'hand_counts':[0,0,0],
        'outcome_counts':[0,0,0], 'first_hand_counts':[0,0,0],
        'transition_by_hand':[[0]*3 for _ in range(3)],
        'transition_by_hand_result':[[[0]*3 for _ in range(3)] for _ in range(3)],
        'repeat_after_result':[[0,0] for _ in range(3)],
        'tail':[], 'last_round_id':None,
        'prediction':{provider:{'n':0,'hits':0,'brier_sum':0.0,'nll_sum':0.0}
                      for provider in ('jev','stats')},
        'match_counts':{'completed':0,'human_win':0,'ai_win':0,'draw':0,'aborted':0},
    }

def append_resolved(stats: dict[str, Any], r: dict[str, Any]) -> dict[str, Any]:
    """Caller must enforce round idempotency BEFORE invoking this function."""
    s = copy.deepcopy(stats)
    if r['round_id'] == s['last_round_id']:
        raise RuleError('DUPLICATE_STATS_UPDATE')
    hi, ri = HANDS.index(r['player_hand']), RESULTS.index(r['player_result'])
    if result(r['player_hand'], r['ai_hand']) != r['player_result']:
        raise RuleError('RESULT_MISMATCH')
    previous = s['tail'][-1] if s['tail'] else None
    s['rounds'] += 1
    s['hand_counts'][hi] += 1
    s['outcome_counts'][ri] += 1
    if r['round_no'] == 1:
        s['first_hand_counts'][hi] += 1
    if previous and previous['match_id'] == r['match_id'] and previous['round_no'] + 1 == r['round_no']:
        pi = HANDS.index(previous['player_hand'])
        pri = RESULTS.index(previous['player_result'])
        s['transition_by_hand'][pi][hi] += 1
        s['transition_by_hand_result'][pi][pri][hi] += 1
        s['repeat_after_result'][pri][0 if hi == pi else 1] += 1
    provider = r['provider_used']
    p = normalize_probabilities(r['probabilities'])
    m = s['prediction'][provider]
    m['n'] += 1
    predicted = next(h for h in HANDS if p[h] == max(p.values()))
    m['hits'] += int(predicted == r['player_hand'])
    m['brier_sum'] += sum((p[h] - int(h == r['player_hand']))**2 for h in HANDS)
    m['nll_sum'] += -math.log(max(p[r['player_hand']], 1e-6))
    s['tail'].append({k:r[k] for k in ('round_id','match_id','round_no','player_hand','ai_hand','player_result')})
    s['tail'] = s['tail'][-50:]
    s['last_round_id'] = r['round_id']
    return s

def smooth(counts: list[int]) -> dict[str, float]:
    return {h:(counts[i]+1)/(sum(counts)+3) for i,h in enumerate(HANDS)}

def stats_prediction(s: dict[str, Any], match_id: str, round_no: int) -> dict[str, float]:
    components:list[tuple[float,dict[str,float]]] = [(0.15, smooth(s['hand_counts']))]
    recent = s['tail'][-20:]
    if recent:
        counts = [sum(r['player_hand'] == h for r in recent) for h in HANDS]
        components.append((0.25,smooth(counts)))
    previous = s['tail'][-1] if s['tail'] else None
    if round_no > 1 and previous and previous['match_id'] == match_id and previous['round_no'] == round_no-1:
        hi, ri = HANDS.index(previous['player_hand']), RESULTS.index(previous['player_result'])
        hcounts = s['transition_by_hand'][hi]
        rcounts = s['transition_by_hand_result'][hi][ri]
        if sum(hcounts) >= 5:
            components.append((0.30,smooth(hcounts)))
        if sum(rcounts) >= 8:
            components.append((0.30,smooth(rcounts)))
    ws = sum(w for w,_ in components)
    q = {h:sum(w*p[h] for w,p in components)/ws for h in HANDS}
    if round_no == 1 and sum(s['first_hand_counts']) >= 5:
        f = smooth(s['first_hand_counts'])
        q = {h:0.5*q[h]+0.5*f[h] for h in HANDS}
    return normalize_probabilities(q)

@dataclass
class TestEngine:
    """Reference state machine with an in-memory atomic critical section."""
    def __init__(self):
        self.lock=threading.RLock()
        self.players:dict[str,dict[str,Any]]={}
        self.matches:dict[str,dict[str,Any]]={}
        self.rounds:dict[str,dict[str,Any]]={}

    def open(self, player: str, match_id: str) -> dict[str,Any]:
        with self.lock:
            if match_id in self.matches:
                if self.matches[match_id]['player_id'] != player:
                    raise RuleError('FORBIDDEN')
                return copy.deepcopy(self.matches[match_id])
            if any(m['player_id']==player and m['status']=='active' for m in self.matches.values()):
                raise RuleError('MATCH_ACTIVE')
            self.players.setdefault(player,zero_stats())
            self.matches[match_id]={'player_id':player,'status':'active','resolved':0,'round_ids':[],
                                    'score':{'human_win':0,'ai_win':0,'draw':0}}
            return copy.deepcopy(self.matches[match_id])

    def reserve(self, match_id: str, round_no: int, now: int) -> dict[str,Any]:
        with self.lock:
            m=self.matches[match_id]
            # Natural-key replay also works after completion.
            if round_no <= len(m['round_ids']):
                r=self.rounds[m['round_ids'][round_no-1]]
                if r['status'] != 'preparing' or now < r['lease_until']:
                    return copy.deepcopy(r)
                r['generation_id']=secrets.token_hex(16)
                r['lease_until']=now+30
                return copy.deepcopy(r)
            if m['status']!='active':
                raise RuleError('MATCH_CLOSED')
            if round_no != m['resolved']+1 or round_no != len(m['round_ids'])+1:
                raise RuleError('ROUND_ORDER')
            rid=secrets.token_hex(16)
            r={'round_id':rid,'match_id':match_id,'round_no':round_no,'status':'preparing',
               'generation_id':secrets.token_hex(16),'lease_until':now+30,'player_id':m['player_id'],
               'history_count_before':self.players[m['player_id']]['rounds']}
            self.rounds[rid]=r
            m['round_ids'].append(rid)
            return copy.deepcopy(r)

    def commit(self, rid: str, gen: str, p: dict[str,float], now: int, provider: str='stats') -> dict[str,Any]:
        p=normalize_probabilities(p)
        with self.lock:
            r=self.rounds[rid]
            if r['status']!='preparing' or gen!=r['generation_id']:
                raise RuleError('STALE_GENERATION')
            if self.matches[r['match_id']]['status']!='active':
                raise RuleError('MATCH_CLOSED')
            if now >= r['lease_until']:
                raise RuleError('LEASE_EXPIRED')
            r.update(status='committed',ai_hand=choose_ai(p),probabilities=p,provider_used=provider,
                     nonce=secrets.token_hex(32),expires=now+300)
            r['commit_hash']=commitment(r['match_id'],rid,r['round_no'],r['ai_hand'],r['nonce'])
            return self.public(r)

    @staticmethod
    def public(r: dict[str,Any]) -> dict[str,Any]:
        allowed=('round_id','match_id','round_no','status','commit_hash','provider_used','expires')
        return {k:r[k] for k in allowed if k in r}

    def submit(self, rid: str, hand: str, commit_hash: str, now: int) -> dict[str,Any]:
        with self.lock:
            r=self.rounds[rid]
            if hand not in HANDS:
                raise RuleError('BAD_REQUEST')
            if not hmac.compare_digest(r.get('commit_hash',''),commit_hash):
                raise RuleError('COMMIT_MISMATCH')
            if r['status']=='resolved':
                if r['player_hand'] != hand:
                    raise RuleError('HAND_ALREADY_SUBMITTED')
                return copy.deepcopy(r)
            if r['status']!='committed':
                raise RuleError('ROUND_NOT_READY')
            if now > r['expires']:
                raise RuleError('ROUND_EXPIRED')
            r2=copy.deepcopy(r)
            r2.update(status='resolved',player_hand=hand,player_result=result(hand,r['ai_hand']))
            m2=copy.deepcopy(self.matches[r['match_id']])
            s2=append_resolved(self.players[r['player_id']],r2)
            m2['resolved']+=1
            m2['score'][r2['player_result']]+=1
            if m2['resolved']==10:
                m2['status']='completed'
                winner=('human_win' if m2['score']['human_win']>m2['score']['ai_win'] else
                        'ai_win' if m2['score']['human_win']<m2['score']['ai_win'] else 'draw')
                s2['match_counts']['completed']+=1
                s2['match_counts'][winner]+=1
            # Three assignments under one lock model one Sheets atomic batch.
            self.rounds[rid]=r2
            self.matches[r['match_id']]=m2
            self.players[r['player_id']]=s2
            return copy.deepcopy(r2)
```

---

## 付録F：参考ロジックの検査プログラム

### File: verify_duel.py

```python
"""Run: python verify_duel.py. No API credentials and no network access needed."""
from __future__ import annotations
import concurrent.futures
import copy
import json
import math
import pathlib
import unittest
from duel_reference import *

ROOT=pathlib.Path(__file__).resolve().parent
COUNTS={'outcome_pairs':0,'probability_grid_points':0,'duplicate_submit_calls':0,'long_history_rounds':0}

def rec(i, match='a'*32, number=None, hand='ROCK', ai='SCISSORS'):
    return {'round_id':f'{i:032x}','match_id':match,'round_no':number or (i-1)%10+1,
            'player_hand':hand,'ai_hand':ai,'player_result':result(hand,ai),
            'provider_used':'stats','probabilities':dict(zip(HANDS,[1/3]*3))}

def committed():
    e=TestEngine(); mid='a'*32; e.open('p1',mid)
    r=e.reserve(mid,1,0)
    pub=e.commit(r['round_id'],r['generation_id'],dict(zip(HANDS,[.4,.35,.25])),1)
    return e,r['round_id'],pub

class DuelTests(unittest.TestCase):
    def test_all_outcomes(self):
        expected=[['draw','human_win','ai_win'],['ai_win','draw','human_win'],['human_win','ai_win','draw']]
        for i,h in enumerate(HANDS):
            for j,a in enumerate(HANDS):
                self.assertEqual(result(h,a),expected[i][j]); COUNTS['outcome_pairs']+=1
    def test_invalid_hand(self):
        with self.assertRaises(RuleError): result('rock','ROCK')
    def test_probability_grid(self):
        for a in range(101):
            for b in range(101-a):
                p=dict(zip(HANDS,[a/100,b/100,(100-a-b)/100]))
                u=expected_margins(p)
                self.assertAlmostEqual(sum(u.values()),0)
                self.assertGreaterEqual(max(u.values()),-1e-12)
                for h in optimal_actions(p): self.assertAlmostEqual(u[h],max(u.values()))
                COUNTS['probability_grid_points']+=1
    def test_greedy_counterexample(self):
        self.assertEqual(optimal_actions(dict(zip(HANDS,[.4,.35,.25]))),['ROCK'])
    def test_uniform_tie(self):
        self.assertEqual(optimal_actions(dict(zip(HANDS,[1/3]*3))),list(HANDS))
    def test_invalid_probabilities(self):
        bad=[{},dict(zip(HANDS,[.1,.1,.1])),dict(zip(HANDS,[1,-.1,.1])),
             dict(zip(HANDS,[True,0,0])),dict(zip(HANDS,[math.nan,.5,.5])),
             dict(zip(HANDS,[math.inf,0,0])),{'ROCK':1,'SCISSORS':0,'PAPER':0,'OTHER':0}]
        for p in bad:
            with self.assertRaises(RuleError):normalize_probabilities(p)
    def test_rounding_tolerance(self):
        p=normalize_probabilities(dict(zip(HANDS,[.3333,.3333,.3333])))
        self.assertAlmostEqual(sum(p.values()),1)
    def test_hash_tampering(self):
        args=['a'*32,'b'*32,1,'ROCK','c'*64]; original=commitment(*args)
        for i,x in enumerate(['d'*32,'e'*32,2,'PAPER','f'*64]):
            changed=args.copy(); changed[i]=x; self.assertNotEqual(original,commitment(*changed))
    def test_pin_domain_salt(self):
        k=b'x'*32
        self.assertNotEqual(pin_digest(k,'p1','a'*32,'123456'),pin_digest(k,'p2','a'*32,'123456'))
        self.assertNotEqual(pin_digest(k,'p1','a'*32,'123456'),pin_digest(k,'p1','b'*32,'123456'))
        self.assertNotEqual(pin_digest(k,'p1','a'*32,'123456'),pin_digest(b'y'*32,'p1','a'*32,'123456'))
    def test_no_cross_match_transition(self):
        s=append_resolved(zero_stats(),rec(1))
        s=append_resolved(s,rec(2,match='b'*32,number=1,hand='PAPER'))
        self.assertEqual(sum(map(sum,s['transition_by_hand'])),0)
        self.assertEqual(sum(s['first_hand_counts']),2)
    def test_within_match_transition(self):
        s=append_resolved(zero_stats(),rec(1));s=append_resolved(s,rec(2,hand='PAPER'))
        self.assertEqual(s['transition_by_hand'][0][2],1)
        self.assertEqual(s['repeat_after_result'][0],[0,1])
    def test_stats_cold_start(self):
        p=stats_prediction(zero_stats(),'a'*32,1)
        for v in p.values():self.assertAlmostEqual(v,1/3)
    def test_tail_bounded_long_history(self):
        s=zero_stats()
        for i in range(1,1001):
            s=append_resolved(s,rec(i,match=f'{(i-1)//10:032x}',hand=HANDS[i%3]))
        COUNTS['long_history_rounds']=1000
        self.assertEqual(s['rounds'],1000);self.assertEqual(len(s['tail']),50)
        self.assertEqual(sum(s['hand_counts']),1000)
        self.assertEqual(sum(map(sum,s['transition_by_hand'])),900)
        self.assertEqual(sum(s['first_hand_counts']),100)
        self.assertAlmostEqual(sum(stats_prediction(s,'f'*32,1).values()),1)
    def test_duplicate_stats_guard(self):
        r=rec(1); s=append_resolved(zero_stats(),r)
        with self.assertRaises(RuleError):append_resolved(s,r)
    def test_no_hidden_fields_before_submit(self):
        _,_,p=committed()
        for k in ['ai_hand','nonce','probabilities','prediction','generation_id']:
            self.assertNotIn(k,p)
    def test_concurrent_duplicates(self):
        e,rid,p=committed()
        def send(_):return e.submit(rid,'ROCK',p['commit_hash'],2)
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            rows=list(pool.map(send,range(32)))
        COUNTS['duplicate_submit_calls']=len(rows)
        self.assertEqual(e.players['p1']['rounds'],1)
        self.assertEqual(e.matches['a'*32]['resolved'],1)
        self.assertTrue(all(r==rows[0] for r in rows))
    def test_changed_hand_rejected(self):
        e,rid,p=committed();e.submit(rid,'ROCK',p['commit_hash'],2)
        with self.assertRaisesRegex(RuleError,'HAND_ALREADY_SUBMITTED'):
            e.submit(rid,'PAPER',p['commit_hash'],3)
    def test_submit_before_commit_rejected(self):
        e=TestEngine();e.open('p1','a'*32);r=e.reserve('a'*32,1,0)
        with self.assertRaises(RuleError):e.submit(r['round_id'],'ROCK','0'*64,1)
    def test_stale_generation(self):
        e=TestEngine();e.open('p1','a'*32);a=e.reserve('a'*32,1,0);b=e.reserve('a'*32,1,31)
        with self.assertRaisesRegex(RuleError,'STALE_GENERATION'):
            e.commit(a['round_id'],a['generation_id'],dict(zip(HANDS,[1/3]*3)),32)
        e.commit(b['round_id'],b['generation_id'],dict(zip(HANDS,[1/3]*3)),32)
    def test_expired_submit(self):
        e,rid,p=committed()
        with self.assertRaisesRegex(RuleError,'ROUND_EXPIRED'):e.submit(rid,'ROCK',p['commit_hash'],302)
    def test_commit_mismatch(self):
        e,rid,p=committed()
        with self.assertRaisesRegex(RuleError,'COMMIT_MISMATCH'):e.submit(rid,'ROCK','0'*64,2)
    def test_ten_rounds_completion_and_retention(self):
        e=TestEngine();mid='a'*32;e.open('p1',mid)
        for n in range(1,11):
            r=e.reserve(mid,n,n*5)
            pub=e.commit(r['round_id'],r['generation_id'],dict(zip(HANDS,[1/3]*3)),n*5+1)
            e.submit(r['round_id'],HANDS[n%3],pub['commit_hash'],n*5+2)
        m=e.matches[mid];s=e.players['p1']
        self.assertEqual(m['status'],'completed');self.assertEqual(sum(m['score'].values()),10)
        self.assertEqual(s['match_counts']['completed'],1)
        e.open('p1','b'*32)
        self.assertEqual(e.players['p1']['rounds'],10)
        e.open('p2','c'*32)
        self.assertEqual(e.players['p2']['rounds'],0)
    def test_active_player_collision(self):
        e=TestEngine();e.open('p1','a'*32)
        with self.assertRaisesRegex(RuleError,'MATCH_ACTIVE'):e.open('p1','b'*32)
    def test_contract_schema(self):
        c=json.loads((ROOT/'duel_config.json').read_text());a=json.loads((ROOT/'api_actions.json').read_text())
        sh=json.loads((ROOT/'sheet_headers.json').read_text())
        self.assertEqual(c['hands'],list(HANDS));self.assertEqual(c['beats'],BEATS)
        self.assertEqual(len(a),11);self.assertEqual(len(sh),9)
        self.assertEqual(c['match']['rounds'],10)
        self.assertEqual(set(a['round.submit']['payload']['player_hand']['enum']),set(HANDS))
        for cols in sh.values():self.assertEqual(len(cols),len(set(cols)))
    def test_hand_geometry(self):
        c=json.loads((ROOT/'duel_config.json').read_text())['display'];r=c['hand_button_radius']
        centers=c['hand_button_centers']
        for x,y in centers:self.assertLessEqual(math.hypot(x-240,y-240)+r,c['safe_radius'])
        for a,b in zip(centers,centers[1:]):self.assertGreaterEqual(math.dist(a,b),2*r+8)

if __name__=='__main__':
    suite=unittest.defaultTestLoader.loadTestsFromTestCase(DuelTests)
    runner=unittest.TextTestRunner(verbosity=2)
    out=runner.run(suite)
    vectors={'commit':{'match_id':'a'*32,'round_id':'b'*32,'round_no':1,'ai_hand':'ROCK','nonce':'c'*64,
                        'expected_sha256':commitment('a'*32,'b'*32,1,'ROCK','c'*64)},
             'pin':{'pepper_hex':(b'x'*32).hex(),'player_id':'p1','salt_hex':'a'*32,'pin':'123456',
                    'expected_hmac_sha256':pin_digest(b'x'*32,'p1','a'*32,'123456')},
             'greedy_counterexample':{'probabilities':dict(zip(HANDS,[.4,.35,.25])),
                                      'expected_margins':expected_margins(dict(zip(HANDS,[.4,.35,.25]))),'best':'ROCK'}}
    report={'scope':'reference_python_only','tests_run':out.testsRun,'failures':len(out.failures),
            'errors':len(out.errors),'passed':out.wasSuccessful(),'checks':COUNTS,
            'not_tested':['ESP32 compile/flash/touch','Apps Script deployment','real Google Sheets atomicity','real Jev API','real power and latency']}
    (ROOT/'test_vectors.json').write_text(json.dumps(vectors,ensure_ascii=False,indent=2)+'\n')
    (ROOT/'validation_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    raise SystemExit(0 if out.wasSuccessful() else 1)
```

---

## 付録G：他言語と照合する試験値

以下のPIN・pepper・IDは**既知の試験専用値**。実利用者や本番の秘密ではない。実際の設定へ流用してはいけない。

### File: test_vectors.json

```json
{
  "commit": {
    "match_id": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "round_id": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
    "round_no": 1,
    "ai_hand": "ROCK",
    "nonce": "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
    "expected_sha256": "d081020337d65f8bb576c0bac2d653294f5c9f63f33c23689174c0031b1e1464"
  },
  "pin": {
    "pepper_hex": "7878787878787878787878787878787878787878787878787878787878787878",
    "player_id": "p1",
    "salt_hex": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "pin": "123456",
    "expected_hmac_sha256": "4a178b939f7dbfb573aa582ac2abbe40884cf3b36454684f4b3cc20a1d3777ad"
  },
  "greedy_counterexample": {
    "probabilities": {
      "ROCK": 0.4,
      "SCISSORS": 0.35,
      "PAPER": 0.25
    },
    "expected_margins": {
      "ROCK": 0.09999999999999998,
      "SCISSORS": -0.15000000000000002,
      "PAPER": 0.050000000000000044
    },
    "best": "ROCK"
  }
}
```

---

## 付録H：実際の検査結果

### File: validation_report.json

```json
{
  "scope": "reference_python_only",
  "tests_run": 25,
  "failures": 0,
  "errors": 0,
  "passed": true,
  "checks": {
    "outcome_pairs": 9,
    "probability_grid_points": 5151,
    "duplicate_submit_calls": 32,
    "long_history_rounds": 1000
  },
  "not_tested": [
    "ESP32 compile/flash/touch",
    "Apps Script deployment",
    "real Google Sheets atomicity",
    "real Jev API",
    "real power and latency"
  ]
}
```

---

## 付録I：この1ファイルから取り出す手順

Markdown本文だけで設計を読める。検査を実行する場合は付録A〜Hを各ファイル名で保存するか、以下のスクリプトを`extract_bundle.py`という名前で保存する。元の8ファイルを抽出するだけで、抽出したコードを自動実行しない。Python 3.10以上、外部パッケージ不要。

```sh
python extract_bundle.py COFFEE_TIME_AI_DUEL_Design_v1.0.md duel_work
cd duel_work
python verify_duel.py
```

`duel_work`は新しい空フォルダーを使う。既存ファイルは上書きしない。ZIP一式を渡す場合、8ファイルとextract_bundle.pyは取り出し済みなので、展開先で`python verify_duel.py`を実行できる。

```python
"""Extract the eight embedded implementation-design files, without executing them.
Usage: python extract_bundle.py DESIGN.md DESTINATION
Existing files are not overwritten. Python 3.10+, standard library only.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import re

ALLOWED = (
    'duel_config.json', 'api_actions.json', 'errors.json', 'sheet_headers.json',
    'duel_reference.py', 'verify_duel.py', 'test_vectors.json', 'validation_report.json',
)

def extract(markdown: Path, destination: Path) -> list[Path]:
    text = markdown.read_text(encoding='utf-8')
    pattern = r'^### File: ([A-Za-z0-9_.]+)\n\n```[A-Za-z0-9_]*\n(.*?)^```[ \t]*$'
    blocks = re.findall(pattern, text, flags=re.MULTILINE | re.DOTALL)
    names = [name for name, _ in blocks]
    if len(names) != len(set(names)) or set(names) != set(ALLOWED):
        raise ValueError('Embedded file set is missing, duplicated, or unexpected.')
    destination.mkdir(parents=True, exist_ok=True)
    for name in ALLOWED:
        if (destination / name).exists():
            raise FileExistsError(f'Will not overwrite: {destination / name}')
    written: list[Path] = []
    for name, contents in blocks:
        target = destination / name
        with target.open('x', encoding='utf-8', newline='\n') as handle:
            handle.write(contents)
        written.append(target)
    return written

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('markdown', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    for target in extract(args.markdown, args.destination):
        print(target.name)
```

### 実装エージェントへの開始指示

> この文書をAI DUEL v1.0の正本として実装してください。既存のLCD・タッチ初期化、＋1カウンター、Google連携を保存してください。まず付録の検査を再実行し、次に統計AI・個人履歴・GAS/Sheetsの保存と復旧を完成させ、その後にJev実APIを接続してください。認証情報や実機環境の未提供は勝手に補わず、モック・コンパイル・実機試験を区別して報告してください。第19章の全48項目を証拠付きで記録し、NOT_RUNをPASSとしないでください。

### 本書の終了

この文書に示した仕様を実装担当の自由解釈で削らず、変更が必要な場合は理由・変更箇所・影響する試験IDを記録して改版する。実機へ書き込むのは、このMarkdownではなく、エージェントが既存プロジェクトへ組み込んでビルド・検証したファームウェアである。
