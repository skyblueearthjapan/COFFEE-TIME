#pragma once

#include <esp_display_panel.hpp>

// Arduino.h の bit(b) マクロが coffee::wolf::bit() と衝突するため、コアを読む前に無効化する
#undef bit
#include "core/port_contract.hpp"
#include "core/privacy_fence.hpp"

/**
 * 人狼ロジックコア（core/port_contract.hpp の Port）を、この基板の実機能へつなぐ層。
 *
 * コアは LCD・Arduino・ファイルシステム・通信を一切知らない。
 * 時刻・乱数・バックライト・タッチの生データ・NVS への保存だけをここで引き受ける。
 *
 * 覗き見防止の要は requestNeutralScanout()。RGB パネルはフレームバッファの内容を
 * そのまま走査し続けるので、「LVGL 上で秘密のラベルを消した」だけでは画面から消えない。
 * 実際に新しいフレームが走査され終わるまで待ってから PrivacyFence に完了を伝える。
 */
namespace werewolf {

class EspPort : public coffee::wolf::Port {
public:
    // 画面・バックライトを触るため Board が要る。main.cpp が board->begin() の後に渡す
    void setBoard(esp_panel::board::Board *board) { board_ = board; }
    // 中立化の完了を書き込む先。ゲームの制御役（WerewolfGame）が自分の PrivacyFence を渡す
    void setFence(coffee::wolf::PrivacyFence *fence) { fence_ = fence; }

    uint64_t monotonicNowMs() override;
    bool randomWord(uint32_t &out) override;
    void cutBacklight() override;
    void requestNeutralScanout(uint64_t privacy_epoch) override;
    void restoreBacklight(uint64_t verified_privacy_epoch) override;
    coffee::wolf::TouchSample latestFreshDriverSample() override;
    bool readPublicMeta(std::array<uint8_t, 20> &bytes, bool &exists) override;
    bool writePublicMetaAndReadBack(const std::array<uint8_t, 20> &bytes) override;
    void inhibitDeepSleep(bool inhibit) override;
    void openExistingCafePanel() override;
    bool releasePriorSinglePlayerSession() override;

private:
    esp_panel::board::Board *board_ = nullptr;
    coffee::wolf::PrivacyFence *fence_ = nullptr;
};

// 端末に 1 つだけある Port。ゲームの制御役と main.cpp が共有する
EspPort &port();

// main.cpp から board->begin() の後に 1 回だけ呼ぶ（Board が無いとバックライトを操作できない）
void setBoard(esp_panel::board::Board *board);

// 前回選んだ遊び方（0 = ワンナイト / 1 = 通常ルール）。
// PublicMeta の 20 バイトは予備バイトが 0 でないと無効になる形式なので流用せず、
// 同じ名前空間（ct_wolf）の別のキー "mode" に 1 バイトで持つ。
// これは公開情報（どちらで遊ぶか）で、秘密は一切含まない
uint8_t readLastMode();
void writeLastMode(uint8_t mode);

}  // namespace werewolf
