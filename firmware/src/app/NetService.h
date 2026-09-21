#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * Wi-Fi 接続・時刻同期 (NTP)・天気取得 (Open-Meteo)・記録サーバー (GAS) への送信。
 * loop() から net::poll() を呼び続ける。LVGL は触らない。
 */
namespace net {

struct Weather {
    bool valid = false;
    float temperature = 0;  // 現在気温 (°C)
    int code = -1;          // WMO 天気コード
};

void begin();
// 新しい天気を取得したら true を返し、out に格納する
bool poll(Weather &out);
bool wifiConnected();
// ゲームの画面が開いている間は true にする（display::setGameActive が呼ぶ）。Wi-Fi の省電力を切って、
// GAS とのやり取りの取りこぼしを減らす。どのタスクから呼んでもよい（実際の切り替えは poll() が行う）
void setLowLatency(bool on);
void debugScan();          // 開発用：周囲の Wi-Fi をスキャンしてログに出す
bool timeSynced();

// 「設定 → Wi-Fi / システム情報」の表示用（Wi-Fi の名前とパスワードは出さない）
int rssi();                // 接続中の電波の強さ (dBm)。未接続なら 0
int registeredCount();     // secrets.h に登録された Wi-Fi の数
bool ntpSynced();          // 起動後に一度でも NTP で時刻が合ったか

// 杯数イベントを送信キューに積む（どのタスクからでも呼べる）。
// event: "take" / "refill" / "newday"。prev はイベント前の残り杯数（通知の重複防止に使う）
void reportEvent(const char *event, uint32_t taken, uint32_t left, uint32_t prev);

// --- GAS への「1 件だけ」の往復（ゲームに依存しない依頼箱）-------------------
//
// AI DUEL の予測依頼のように「頼んで、あとで返事を受け取る」用。エスパー第 2 段階
// でも同じ口を使える。**通信そのものは loop() 側の poll() が行う**ので、
// LVGL のコールバックから呼んでも画面は止まらない（この 3 つは即座に戻る）。
//
// 一度に 1 件しか預かれない。コーヒーの記録（reportEvent）とは別の口で、
// 送る順番はコーヒーが先。コーヒーの再送の邪魔もしない。
// 1 往復にかかる時間の上限は つなぐまで 3 秒 ＋ POST 6.5 秒 ＋ 転送先の GET 5 秒
// （GAS は Jev の呼び出しとシート書き込みを終えてから 302 を返すため）。
//
// **預け方が 2 つある。ゲームごとに使い分けること**（エスパー第 2 段階も同じ口を使う）:
//
//   ふつう（detached = false）
//     返事を gasTakeResult() で受け取る。受け取るまで箱はふさがったまま。
//     AI DUEL の予測のように「返事を使って先へ進む」依頼はこちら。
//     受け取る側（画面）が消えるときは必ず gasCancel() すること。
//
//   投げっぱなし（detached = true）
//     返事は net::poll が 1 行ログに出して捨て、箱はすぐ空く。
//     AI DUEL の対戦記録のように「届けば十分・返事は要らない」依頼はこちら。
//     画面を閉じた直後に預けても、箱が Done のまま誰にも引き取られずに残らない。
//
// 依頼の本文（JSON）の上限。AI DUEL の state は直近 12 件の履歴を含めて実測 2.5KB ほど
constexpr size_t kGasRequestMax = 3584;
// 応答の本文の上限（超えたら切る）。AI DUEL の返事は 200 バイト前後だが、
// JEV REVERSI は合法手すべての確率が入るので、8×8 の広い局面で 500 バイトを超える
constexpr size_t kGasResultMax = 768;

struct GasResult {
    uint32_t req = 0;           // gasRequest に渡した依頼番号
    bool ok = false;            // HTTP 200 で本文を受け取れたか
    int status = 0;             // HTTPClient の戻り値（負の値は接続失敗。原因の記録用）
    uint32_t elapsed_ms = 0;    // 依頼を預けてから返事が揃うまで
    char body[kGasResultMax] = {};
};

// 依頼できる状態か（GAS_URL があり Wi-Fi につながっている）。
// false のときはゲーム側が最初から統計 AI で進めること
bool gasReady();

// 依頼を預ける。body_json は `{` `}` を含む JSON の本文で、`token` と `device` は
// ここが足す。前の依頼がまだ片付いていない・長すぎる・通信できないときは false。
// detached = true なら返事を取りに来なくてよい（上の「投げっぱなし」）
bool gasRequest(uint32_t req, const char *body_json, bool detached = false);

// 返事を受け取る（1 回だけ取り出せる）。届いていれば true。
// detached で預けた依頼の返事はここへは来ない
bool gasTakeResult(GasResult &out);

// 待っている依頼を捨てる（画面を閉じるとき・遅れた返事を無視したいとき）。
// 錠が取れず何もできなかったときだけ false（呼び出し側は次の機会に出し直してよい）。
// LVGL のコールバックから呼べるよう、錠を待つのは最大 2ms
bool gasCancel();

}  // namespace net
