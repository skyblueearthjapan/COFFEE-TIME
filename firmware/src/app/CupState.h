#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * コーヒーの杯数状態。
 * 変更は LVGL のロックを取った状態（LVGL タスク内、または lvgl_port_lock 中）でのみ行う。
 *
 * 保存先（NVS）は docs/MENU_DESIGN.md §3 のとおり 3 つ:
 *   cup      / ymd taken left   … 従来どおりの杯数（この 3 つが本体。ほかが壊れても守る）
 *   cup      / today  (blob)    … 今日の時間帯別の杯数など。杯数と同じ時機にまとめて書く
 *   cup_hist / days   (blob)    … 直近 35 日の輪。**日付が変わったときだけ** 1 回書く
 */
namespace cup {

// ゲームの種類。**並びは NVS に保存されるので絶対に変えない**
enum class GameId : uint8_t {
    Werewolf = 0,   // 閉店後の人狼会
    Detective = 1,  // 喫茶「余白」の事件簿
    Esper = 2,      // エスパー対決
    Duel = 3,       // AI DUEL
    Reversi = 4,    // リバーシ（JEV REVERSI）
    Cards = 5,      // POKER TABLE（トランプ 4 種）
};
constexpr size_t kGameCount = 6;

// **0〜3 番だけが `cup/today` と `cup_hist/days` の塊に入る。**
// 杯数の記録（いちばん大事なデータ）と同じ塊なので、形を 1 バイトも変えない。
// 4 番から先は別の名前空間 `cup_ext` に置き、下の関数からは同じように読める
// （docs/REVERSI_PLAN.md §2 のディレクター判断）
constexpr size_t kLegacyGameCount = 4;

// 「今日の状況」の中身。時刻が分からない間に押された分は unknown に入れ、
// 時間帯のグラフ (hour) には入れない
struct Today {
    uint32_t ymd;               // この記録の日付 (YYYYMMDD)。時刻未取得なら 0
    uint8_t hour[24];           // 時間帯ごとの杯数（1 時間あたり 255 杯で頭打ち）
    uint16_t unknown;           // 時刻が分からないまま数えた杯数
    uint16_t refills;           // 今日の補充回数
    int16_t last_take_min;      // 最後の 1 杯の時刻（0 時からの分）。無ければ -1
    int16_t last_refill_min;    // 最後の補充の時刻。無ければ -1
    uint8_t games[kGameCount];  // 今日ゲームを遊んだ回数（255 で頭打ち）
};

// 履歴 1 日分（確定値）
struct DayRecord {
    uint32_t ymd;
    uint16_t cups;
    uint8_t refills;
    uint8_t games[kGameCount];
};

constexpr size_t kHistoryDays = 35;     // 輪の大きさ（1 か月＋αを残す）

void load();                        // NVS から読み込む（起動時に 1 回。settings::load() の後）
void saveIfDirty();                 // 変更があれば NVS に保存（loop から呼ぶ）

void takeOne();                     // +1：飲んだ杯数を増やし、残りを減らす
void refill();                      // 作った：残りを上限に戻す
bool checkNewDay(uint32_t ymd);     // 日付が変わっていれば本日分をリセット。切り替えたら true

uint32_t taken();                   // 本日飲まれた杯数
uint32_t remaining();               // 残り杯数
uint32_t maxCups();                 // 1 回に作る杯数（設定値）

const Today &today();

size_t historyCount();              // 記録のある日数（0〜kHistoryDays）
DayRecord historyAt(size_t index);  // 0 がいちばん古い。範囲外は空の記録
bool historyFor(uint32_t ymd, DayRecord &out);   // その日の記録があれば true

// --- ゲームを遊んだ回数 -----------------------------------------------------
// **数えるのは回数だけ**。役職・投票・勝敗・答えは絶対に記録しない
// （人狼の秘密は RAM のみ、探偵の結果は探偵自身の記録に入っている）。
namespace stats {

// 1 回遊び終えたときに呼ぶ（LVGL タスクから呼んでよい）。
// 今日の分と累計を増やし、履歴 blob を 1 回だけ書き直し、SD の操作ログに "game" を残す。
// note は SD ログ用の短い補足（例 "wolf players=4" / "detective CD001"）
void gamePlayed(GameId id, const char *note = "");

uint8_t todayGames(GameId id);      // 今日遊んだ回数
uint32_t totalGames(GameId id);     // 累計（35 日の輪から外れた分も残る）

}  // namespace stats

}  // namespace cup
