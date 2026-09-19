#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * COFFEE TIME 段階1のカウンター画面を生成する。
 * LVGL 初期化後、LVGL のロックを取得した状態で 1 回だけ呼ぶこと。
 */
bool coffee_counter_create(void);

/** 起動後に記録した杯数（段階1では再起動で 0 に戻る） */
uint32_t coffee_counter_get(void);

#ifdef __cplusplus
}
#endif
