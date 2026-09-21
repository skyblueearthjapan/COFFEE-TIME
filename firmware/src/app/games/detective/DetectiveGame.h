#pragma once

#include <lvgl.h>

/**
 * 喫茶「余白」の事件簿（CAFE DETECTIVE）— 段階 B「実機のひとり推理」。
 *
 * 話を選ぶ → 表紙 → 導入 → 今回の約束 → 手掛かり集め（証拠 3 件・手帳・店の中・ヒント）
 * → 三択 → 確認 → 答え合わせ → 解説 → 結末 → 記念の品 → 章のおわり
 * までを、LVGL の画面 1 枚の中身を作り替えて進める（人狼と同じ作り）。
 *
 * この段階では通信・AI・SD を一切使わない。正解の判定も物語も端末の中で完結する
 * （設計書 6.2 の solo パック）。オンライン専用の画面（CD_PREPARE / CD_SUBMIT /
 * CD_PAUSED / モード選択）は作らず、状態表示は「ひとり推理」で固定する。
 *
 * 記録は NVS の `ct_det` に置く 20 バイトだけ（DetectiveProgress.h）。
 * 個人プロフィール・通信・SD への書き込みはしない。
 */
namespace detective {

// ゲーム画面を作る（ui::push に渡す）。話の一覧から章のおわりまでこの 1 枚で進む
lv_obj_t *createGameScreen();

// 開発用：今の場面をシリアルへ 1 行出す（画面・話・ページ・既読・ヒント段階）。
// 答えそのものは出さない
void debugPrintPublicState();

// 開発用：NVS の進み具合を全部消して「初見・未読」に戻す。
// 試験のたびに利用者の「初回の結果」が埋まってしまうのを防ぐためのもの
void debugResetProgress();

}  // namespace detective
