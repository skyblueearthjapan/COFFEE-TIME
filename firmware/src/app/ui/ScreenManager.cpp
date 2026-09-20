#include "ScreenManager.h"

#include <Arduino.h>

namespace ui {

// HOME を含めて最大 6 階層（メニュー → ゲーム → 各局面）
static constexpr int kMaxDepth = 6;

static lv_obj_t *s_stack[kMaxDepth] = {};
static int s_depth = 0;                 // s_stack[0] が HOME
static constexpr uint32_t kAnimMs = 200;

void begin(lv_obj_t *home_screen)
{
    s_stack[0] = home_screen;
    s_depth = 1;
}

void push(ScreenFactory factory)
{
    if (factory == nullptr || s_depth >= kMaxDepth) {
        Serial.println("[UI] push rejected");
        return;
    }
    lv_obj_t *scr = factory();
    if (scr == nullptr) {
        Serial.println("[UI] screen creation failed");
        return;
    }
    s_stack[s_depth++] = scr;
    // 呼び出し元の画面は戻り先として残すので auto_del は false
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, kAnimMs, 0, false);
}

void pop()
{
    if (s_depth <= 1) {
        return;
    }
    s_stack[--s_depth] = nullptr;
    // 表示中の画面は LVGL に任せて削除する（auto_del = true）。
    // 自前で lv_obj_del_async すると、200ms の遷移アニメーション中に解放され、
    // アニメーション完了時に解放済みメモリへイベントが送られてしまう。
    lv_scr_load_anim(s_stack[s_depth - 1], LV_SCR_LOAD_ANIM_MOVE_RIGHT, kAnimMs, 0, true);
}

void goHome()
{
    if (s_depth <= 1) {
        return;
    }
    // 表示されていない中間の画面はその場で破棄してよい
    while (s_depth > 2) {
        lv_obj_t *hidden = s_stack[--s_depth];
        s_stack[s_depth] = nullptr;
        lv_obj_del(hidden);
    }
    s_stack[--s_depth] = nullptr;
    lv_scr_load_anim(s_stack[0], LV_SCR_LOAD_ANIM_MOVE_RIGHT, kAnimMs, 0, true);
}

bool isHome()
{
    return s_depth <= 1;
}

}  // namespace ui
