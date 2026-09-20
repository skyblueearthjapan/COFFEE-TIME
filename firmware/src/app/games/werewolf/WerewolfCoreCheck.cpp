// 人狼ロジックコア（core/ 配下、無改変移植）と生成済み文言データが
// ESP32-S3 / Arduino-ESP32 (gnu++17) 向けにコンパイルできることを確認するための
// 翻訳単位。main.cpp からは呼び出さない。UI・ゲーム進行の実装はここでは行わない。
//
// build_src_filter = +<app/> (platformio.ini) により、既存 env `app` の
// ビルドへ自動的に含まれる。

#include "core/werewolf_core.hpp"
#include "core/public_meta.hpp"
#include "core/paging.hpp"
#include "core/privacy_fence.hpp"
#include "core/port_contract.hpp"
#include "WerewolfContent.h"

namespace coffee { namespace wolf { namespace check {

static_assert(MIN_PLAYERS == 3, "werewolf_core: MIN_PLAYERS mismatch");
static_assert(MAX_PLAYERS == 10, "werewolf_core: MAX_PLAYERS mismatch");
static_assert(ROLE_SLOTS == 12, "werewolf_core: ROLE_SLOTS mismatch");
static_assert(TARGET_SLOTS == 13, "werewolf_core: TARGET_SLOTS mismatch");
static_assert(rules::kPageSize == PAGE_SIZE, "rules.json page_size vs paging.hpp PAGE_SIZE mismatch");
static_assert(rules::kDefaultPlayers == MIN_PLAYERS, "rules.json default_players vs MIN_PLAYERS mismatch");

// 実際には使わないダミー実装。リンクされないよう main.cpp からは参照しない。
class NullPort : public Port {
public:
    uint64_t monotonicNowMs() override { return 0; }
    bool randomWord(uint32_t &out) override { out = 0; return true; }
    void cutBacklight() override {}
    void requestNeutralScanout(uint64_t) override {}
    void restoreBacklight(uint64_t) override {}
    TouchSample latestFreshDriverSample() override { return TouchSample{}; }
    bool readPublicMeta(std::array<uint8_t, 20> &, bool &exists) override { exists = false; return true; }
    bool writePublicMetaAndReadBack(const std::array<uint8_t, 20> &) override { return true; }
    void inhibitDeepSleep(bool) override {}
    void openExistingCafePanel() override {}
    bool releasePriorSinglePlayerSession() override { return true; }
};

// Engine / SecretGate / PrivacyFence / PublicMeta encode-decode / seatPage / 生成データ参照を
// 一通り呼び出し、リンクまで通ることを確認するためだけのダミー関数。呼び出し元は無い。
bool unusedCompileCheck() {
    Engine engine;
    SecretGate gate;
    PrivacyFence fence;
    NullPort port;
    (void)port;

    const Stamp s = engine.stamp();
    if (engine.start(s, 5, 0, 1) != Err::Ok) return false;
    (void)engine.publicView();
    gate.enter(1);
    (void)fence.beginHide();

    PublicMeta meta;
    meta.players = 5;
    std::array<uint8_t, 20> encoded{};
    if (!encodeMeta(meta, encoded)) return false;
    PublicMeta decoded;
    if (decodeMeta(encoded.data(), encoded.size(), decoded) == MetaFormat::Invalid) return false;

    (void)seatPage(5, 0);

    const char *subtitle = content::findString("game.subtitle");
    const rules::TimingByPlayers *timing = rules::findTiming(5);
    return subtitle != nullptr && timing != nullptr && content::kStringCount > 0;
}

}}} // namespace coffee::wolf::check
