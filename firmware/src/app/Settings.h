#pragma once

#include <stdint.h>

/**
 * 端末の設定（NVS の名前空間 `cfg` / キー `v1`）。
 *
 * 版と CRC を付けた 1 個の blob にまとめてある。壊れている・版が違うときは
 * 既定値で始め、杯数（`cup`）には一切影響させない（docs/MENU_DESIGN.md §3）。
 * 書き込みは設定を変えて「決定」したときだけ。画面が一瞬止まるのでタイマーからは書かない。
 */
namespace settings {

// 「画面を暗くする」までの時間の選択肢（添字で保存する）。
// 画面に出す日本語は ui/SettingsScreen.cpp 側が持つ（フォント収録の都合。SysInfo.h の注記を参照）
constexpr uint8_t kDimChoiceCount = 5;
extern const uint32_t kDimSeconds[kDimChoiceCount];      // 60 / 300 / 600 / 1800 / 0(しない)

constexpr uint8_t kMinCups = 5;
constexpr uint8_t kMaxCupsLimit = 15;

void load();            // 起動時に 1 回。cup::load() より先に呼ぶこと（杯数の丸めに使う）
void save();            // 変更を NVS に書き、読み返して照合する

uint8_t brightness();   // 画面の明るさ 10〜100 (%)
uint8_t dimChoice();    // 暗くするまでの時間の添字 (0〜4)
uint32_t dimSeconds();  // 同上を秒に直したもの。0 なら暗くしない
bool sound();           // 操作音
uint8_t maxCups();      // 1 回に作る杯数 5〜15
bool morningFull();     // 日付が変わったときの残りを満杯にするか（false なら 0 杯）

// いずれも値を丸めて保持するだけ。NVS への書き込みは save() で行う
void setBrightness(uint8_t percent);
void setDimChoice(uint8_t index);
void setSound(bool on);
void setMaxCups(uint8_t cups);
void setMorningFull(bool full);

}  // namespace settings
