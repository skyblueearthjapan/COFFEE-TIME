#pragma once
#include "werewolf_core.hpp"
#include "public_meta.hpp"
namespace coffee { namespace wolf {
// Application-defined contracts. These names are NOT claimed to be BSP/LVGL APIs.
struct TouchSample {
    uint64_t sampled_ms=0;
    bool valid=false,down=false,inside_hold=false,multiple_points=false;
};
struct Port {
    virtual ~Port()=default;
    virtual uint64_t monotonicNowMs()=0;
    virtual bool randomWord(uint32_t& out)=0;
    virtual void cutBacklight()=0;
    virtual void requestNeutralScanout(uint64_t privacy_epoch)=0;
    virtual void restoreBacklight(uint64_t verified_privacy_epoch)=0;
    virtual TouchSample latestFreshDriverSample()=0;
    virtual bool readPublicMeta(std::array<uint8_t,20>& bytes,bool& exists)=0;
    virtual bool writePublicMetaAndReadBack(const std::array<uint8_t,20>& bytes)=0;
    virtual void inhibitDeepSleep(bool inhibit)=0;
    virtual void openExistingCafePanel()=0;
    virtual bool releasePriorSinglePlayerSession()=0;
};
// Start ordering, to be used by the single UI controller:
// 1) validate lobby count/time and record current screen epoch; prevent repeated Start
// 2) securely obtain unbiased k<dealCount(players), without logging k
// 3) encode/write/read-back armed=true, plus public player/time/flavor settings
// 4) only on success: Engine::start(stamp,players,k,now,override_seconds)
// 5) erase local k; neutral scanout completion; then publish NightHandoff
// Failures may leave an armed marker, but MUST NOT expose partial roles or silently redeal.
}} // namespace
