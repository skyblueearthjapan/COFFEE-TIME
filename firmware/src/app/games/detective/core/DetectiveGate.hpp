#ifndef CAFE_DETECTIVE_GATE_HPP
#define CAFE_DETECTIVE_GATE_HPP
#include <cstdint>
namespace cd {
// UI-thread-only state. Send copies of epoch/request/choice to a network queue.
class Gate {
public:
  enum class Stage { Reading, Choosing, Confirming, Pending, Result };
  void newScreen() noexcept { ++epoch_; stage_=Stage::Reading; choice_=-1; request_=0; }
  void enableChoices() noexcept { stage_=Stage::Choosing; choice_=-1; }
  bool choose(int id) noexcept { if(stage_!=Stage::Choosing||id<0||id>2)return false;choice_=id;stage_=Stage::Confirming;return true; }
  bool change() noexcept { if(stage_!=Stage::Confirming)return false;stage_=Stage::Choosing;choice_=-1;return true; }
  bool submit(std::uint32_t request) noexcept { if(stage_!=Stage::Confirming||!request)return false;request_=request;stage_=Stage::Pending;return true; }
  bool receive(std::uint32_t epoch,std::uint32_t request,bool resolved) noexcept {
    if(epoch!=epoch_||request!=request_||stage_!=Stage::Pending)return false;
    if(resolved)stage_=Stage::Result;
    // Failure/unknown is still Pending: transport retries the SAME request, not a new answer.
    return true;
  }
  void detach() noexcept {newScreen();} // invalidates late network callbacks
  std::uint32_t epoch() const noexcept {return epoch_;}
  Stage stage() const noexcept {return stage_;}
  int choice() const noexcept{return choice_;}
private:
  std::uint32_t epoch_=1,request_=0;
  Stage stage_=Stage::Reading;
  int choice_=-1;
};
}
#endif
