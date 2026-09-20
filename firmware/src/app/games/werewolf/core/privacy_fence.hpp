#pragma once
#include <cstdint>
namespace coffee { namespace wolf {
// Token discipline only. The port MUST prove all actual scanout buffers are neutral.
class PrivacyFence {
public:
    uint64_t beginHide() { ++epoch_; ready_=false; return epoch_; }
    bool completeNeutral(uint64_t epoch) { if(epoch!=epoch_)return false;ready_=true;return true; }
    bool canRestore(uint64_t epoch) const { return ready_&&epoch==epoch_; }
    uint64_t epoch() const {return epoch_;}
private:
    uint64_t epoch_=1;bool ready_=false;
};
}} // namespace
