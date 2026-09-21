#pragma once

#include <lvgl.h>

/**
 * エスパー対決（AI ESPER）— 段階 1「通信なしで最後まで遊べる版」。
 *
 * むずかしさ → 候補一覧（頭の中で 1 つ決める）→ 質問に はい／いいえ
 * → 最終予想 →「合っていましたか？」→ 結果（外れたら本当の答えを教えてもらう）
 * までを、LVGL の画面 1 枚の中身を作り替えて進める（人狼・探偵と同じ作り）。
 *
 * この段階では通信も AI（Jev）も使わない。候補の絞り込みと質問の選び方は
 * 端末の中の推論コア（core/esper_core.hpp）が設計書 3.2〜3.6 のとおりに行う。
 * 段階 2 で Jev をつなぐときは `esper::Advisor` の実装を差し替えるだけでよく、
 * 画面とルールはそのまま使える（継ぎ目の説明は esper_core.hpp の Advisor を参照）。
 *
 * **プレイヤーが頭の中で決めた答えは、端末のどこにも記録しない。**
 * 残すのは NVS `ct_esp/stat` の勝敗の回数（EsperStats.h）と、
 * cup::stats::gamePlayed による「遊んだ回数」だけ。SD にも通信にも答えは出ない。
 */
namespace esper {

// ゲーム画面を作る（ui::push に渡す）。むずかしさ選びから結果までこの 1 枚で進む
lv_obj_t *createGameScreen();

// 開発用：今の場面をシリアルへ 1 行出す（画面・モード・問数・残り候補数）。
// **頭の中の答えは端末が持っていないので、そもそも出しようがない**
void debugPrintPublicState();

// 開発用：NVS の勝敗記録を全部消す（試験のたびに記録が増えてしまうのを防ぐ）
void debugResetStats();

}  // namespace esper
