#pragma once

#include <lvgl.h>

#include <cstddef>

#include "UiKit.h"

/**
 * 通信待ちの「考え中」表示（AI DUEL・JEV REVERSI・エスパー対決の共通部品）。
 *
 * 小さな回る弧と、1.5 秒ごとに入れ替わる一言を枠の中に出す。Jev の返事は 4〜7 秒
 * かかることがあり、止まった字だけだと「固まった」ように見えるため、
 * **いま何をしているか**を言葉で出して待ちを短く感じさせる。
 * 進み具合（％）や AI の理解度のような**作り物の数値は出さない**。音も鳴らさない。
 */
namespace ui {

struct Thinking;      // 中身は Thinking.cpp（呼ぶ側は作るだけでよい）

// 枠の中に「考え中」の表示を作る。
//
//   phrases … 順に出す一言。**ずっと残る文字列**の配列を渡すこと
//             （文字列リテラルか、生成済みの文言表から取った const char*）。
//   1 行に入る長さは (area.w - 28) / 20 文字まで（ct_font_jp_20。はみ出す分は切る）
//
// 5 秒を過ぎたら「通信に少し時間がかかっています」も出す。枠の高さが 48px 以上
// あれば一言の下にそのまま出し、1 行しか置けない枠では一言の順番に混ぜる。
//
// タイマーと動きは、この部品の根っこが消えるときに自分で片付く
// （ゲーム画面の lv_obj_clean での作り直しでも漏れない）ので、返り値は使わなくてよい。
Thinking *thinkingCreate(lv_obj_t *parent, const Rect &area, const char *const *phrases,
                         size_t count);

}  // namespace ui
