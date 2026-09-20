#pragma once
// COFFEE TIME / CAFE WEREWOLF 2.0.0 — one application, 3 through 10 humans.
// Portable reference core. No LCD, Arduino, filesystem, AI or network dependencies.
#include <array>
#include <cstdint>
#include <cstddef>
namespace coffee { namespace wolf {
constexpr uint8_t MIN_PLAYERS = 3;
constexpr uint8_t MAX_PLAYERS = 10;
constexpr uint8_t RESERVE_A = 10, RESERVE_B = 11, PEACE = 12;
constexpr std::size_t ROLE_SLOTS = 12, TARGET_SLOTS = 13;
constexpr int8_t NONE = -1;
using Mask = uint16_t;
using Votes = std::array<int8_t, MAX_PLAYERS>;
constexpr uint64_t HARD_LIMIT_MS = 2700000; // 45 minutes, including pauses
inline bool validPlayers(uint8_t n) { return n >= MIN_PLAYERS && n <= MAX_PLAYERS; }
inline Mask bit(uint8_t target) { return target < 16 ? static_cast<Mask>(uint16_t(1) << target) : 0; }
inline Mask seatsMask(uint8_t n) { return validPlayers(n) ? static_cast<Mask>((1u << n)-1u) : 0; }
inline Mask voteMask(uint8_t n) { return validPlayers(n) ? static_cast<Mask>(seatsMask(n)|bit(PEACE)) : 0; }
inline bool isSeat(uint8_t n, int target) { return validPlayers(n) && target>=0 && target<n; }
inline bool roleSlot(uint8_t n, int target) {
    return isSeat(n,target) || (validPlayers(n) && (target==RESERVE_A || target==RESERVE_B));
}
inline bool legalNightTarget(uint8_t n, uint8_t actor, int target) {
    return actor<n && roleSlot(n,target) && target!=actor;
}
inline bool legalVoteTarget(uint8_t n, uint8_t actor, int target, Mask eligible) {
    return validPlayers(n) && actor<n && target>=0 && target<static_cast<int>(TARGET_SLOTS) &&
        target!=actor && (isSeat(n,target)||target==PEACE) &&
        eligible!=0 && (eligible & static_cast<Mask>(~voteMask(n)))==0 &&
        (eligible & bit(static_cast<uint8_t>(target)))!=0;
}
inline uint16_t defaultDiscussion(uint8_t n) { return validPlayers(n) ? uint16_t(120+30*(n-3)) : 0; }
inline uint16_t runoffDiscussion(uint8_t n) { return n>=7 ? 90 : 60; }
inline bool validDiscussion(uint16_t seconds) { return seconds==0 || (seconds>=90 && seconds<=600 && seconds%30==0); }
inline uint16_t dealCount(uint8_t n) { return validPlayers(n) ? uint16_t((n+2)*(n+1)) : 0; }
inline uint8_t ordinalToSlot(uint8_t n, uint8_t ordinal) {
    return ordinal<n ? ordinal : static_cast<uint8_t>(RESERVE_A+ordinal-n);
}
enum class Role : uint8_t { Empty, Villager, Wolf, Seer };
enum class Finding : uint8_t { None, NotWolf, Wolf };
enum class Phase : uint8_t {
    Idle, NightHandoff, RoleCheck, NightTarget, NightResult, NightDone,
    DayReady, DayTalk, VoteReady, VoteHandoff, VoteSelect, VoteConfirm,
    VoteDone, RunoffReady, RunoffTalk, FinalReady, Revealed, Aborted
};
enum class Outcome : uint8_t { None, CaughtWolf, WolfEscaped, PeaceCorrect, FalseAccusation, Draw };
enum class Err : uint8_t { Ok, Stale, Phase, Seat, Target, Unseen, Paused, Input };
enum class AbortReason : uint8_t { None, User, Privacy, HardLimit, ClockFault };
struct Stamp { uint64_t generation; uint64_t revision; };
inline bool same(Stamp a, Stamp b) { return a.generation==b.generation && a.revision==b.revision; }
struct NightInfo { Finding finding=Finding::None; int8_t target=NONE; };
struct PublicView {
    Phase phase; uint8_t player_count; uint8_t actor; uint8_t vote_cycle; Mask eligible;
    bool paused; bool extension_used; uint64_t remaining_ms; Stamp stamp;
};
struct Tally {
    bool valid=false;
    std::array<uint8_t,TARGET_SLOTS> counts{};
    Mask leaders=0;
    int8_t selected=NONE;
};
inline Tally tally(uint8_t n, const Votes& votes, Mask eligible) {
    Tally t;
    if(!validPlayers(n)||!eligible||(eligible & static_cast<Mask>(~voteMask(n))))return t;
    for(uint8_t a=0;a<n;++a) {
        if(!legalVoteTarget(n,a,votes[a],eligible))return t;
        ++t.counts[static_cast<uint8_t>(votes[a])];
    }
    for(uint8_t a=n;a<MAX_PLAYERS;++a)if(votes[a]!=NONE)return t;
    uint8_t best=0; unsigned count=0;
    for(uint8_t x=0;x<TARGET_SLOTS;++x)if((eligible&bit(x)) && t.counts[x]>best)best=t.counts[x];
    for(uint8_t x=0;x<TARGET_SLOTS;++x)if((eligible&bit(x)) && t.counts[x]==best) {
        t.leaders|=bit(x);t.selected=static_cast<int8_t>(x);++count;
    }
    if(count!=1)t.selected=NONE;
    t.valid=true;return t;
}
// N actual seats plus TWO reserve cards. Exactly one Wolf and one Seer in the pool.
inline bool dealFromIndex(uint8_t n, uint16_t k, std::array<Role,ROLE_SLOTS>& out) {
    if(!validPlayers(n)||k>=dealCount(n))return false;
    out.fill(Role::Empty);
    for(uint8_t a=0;a<n;++a)out[a]=Role::Villager;
    out[RESERVE_A]=out[RESERVE_B]=Role::Villager;
    const uint8_t w=static_cast<uint8_t>(k/(n+1));
    uint8_t s=static_cast<uint8_t>(k%(n+1));if(s>=w)++s;
    out[ordinalToSlot(n,w)]=Role::Wolf;out[ordinalToSlot(n,s)]=Role::Seer;
    return true;
}
inline Outcome outcomeFor(uint8_t n, const std::array<Role,ROLE_SLOTS>& roles, int8_t selected) {
    if(!validPlayers(n))return Outcome::None;
    if(selected==NONE)return Outcome::Draw;
    if(!isSeat(n,selected)&&selected!=PEACE)return Outcome::None;
    bool has_wolf=false;for(uint8_t a=0;a<n;++a)if(roles[a]==Role::Wolf)has_wolf=true;
    if(selected==PEACE)return has_wolf ? Outcome::WolfEscaped : Outcome::PeaceCorrect;
    if(!has_wolf)return Outcome::FalseAccusation;
    return roles[static_cast<uint8_t>(selected)]==Role::Wolf ? Outcome::CaughtWolf : Outcome::WolfEscaped;
}
using WordSource = bool (*)(void*, uint32_t&);
inline bool uniformBelow(uint32_t n, WordSource source, void* context, uint32_t& out) {
    if(!n||!source)return false;
    const uint32_t threshold=static_cast<uint32_t>(0u-n)%n;
    for(unsigned i=0;i<128;++i) {
        uint32_t w=0;if(!source(context,w))return false;
        if(w>=threshold){out=w%n;return true;}
    }
    return false;
}
struct Result {
    uint8_t player_count=0;
    Outcome outcome=Outcome::None;
    int8_t selected=NONE;
    std::array<Role,ROLE_SLOTS> roles{};
    Votes first{},runoff{};
    bool had_runoff=false;
    int8_t seer=NONE,inspected=NONE;
    Finding finding=Finding::None;
};
class Engine {
public:
    Engine() { clearPrivate(); }
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    ~Engine() { clearPrivate(); }
    Stamp stamp() const { return {generation_, revision_}; }
    PublicView publicView() const {
        Mask published = voteMask(n_);
        if (cycle_ == 2 || phase_ == Phase::RunoffReady) published = eligible_;
        return {phase_, n_, actor_, cycle_, published, paused_, extension_, remaining_, stamp()};
    }
    bool active() const {
        return phase_ != Phase::Idle && phase_ != Phase::Revealed && phase_ != Phase::Aborted;
    }
    AbortReason abortReason() const { return abort_reason_; }
    // Only trusted application code calls this after entropy and boot-marker preparation.
    Err start(Stamp s, uint8_t players, uint16_t deal_index, uint64_t now_ms, uint16_t discussion_s = 0) {
        if (!same(s, stamp())) return Err::Stale;
        if (phase_ != Phase::Idle) return Err::Phase;
        if (!validPlayers(players) || !validDiscussion(discussion_s)) return Err::Input;
        std::array<Role, ROLE_SLOTS> generated{};
        if (!dealFromIndex(players, deal_index, generated)) return Err::Input;
        clearPrivate(); n_ = players; roles_ = generated;
        // Clear local copy as well; not a guarantee against an instrumented firmware.
        volatile Role* p = generated.data(); for (std::size_t i=0;i<ROLE_SLOTS;++i) p[i]=Role::Empty;
        ++generation_; ++revision_;
        actor_ = 0; cycle_ = 1; eligible_ = voteMask(n_); phase_ = Phase::NightHandoff;
        start_ms_ = now_ms; clock_ms_ = now_ms; discussion_s_ = discussion_s ? discussion_s : defaultDiscussion(n_);
        remaining_ = 0; paused_ = false; extension_ = false; abort_reason_ = AbortReason::None;
        return Err::Ok;
    }
    Err receiveNight(Stamp s, uint8_t actor) {
        Err e = check(s, Phase::NightHandoff, actor); if (e != Err::Ok) return e;
        viewed_role_ = false; viewed_result_ = false; setPhase(Phase::RoleCheck); return Err::Ok;
    }
    // Renderer calls only after SecretGate grants a fresh press. No generic role-array getter.
    Err readRole(uint8_t actor, Role& out) {
        if (paused_) return Err::Paused;
        if (phase_ != Phase::RoleCheck) return Err::Phase;
        if (actor != actor_) return Err::Seat;
        out = roles_[actor_]; viewed_role_ = true; return Err::Ok;
    }
    Err acknowledgeRole(Stamp s, uint8_t actor) {
        Err e = check(s, Phase::RoleCheck, actor); if (e != Err::Ok) return e;
        if (!viewed_role_) return Err::Unseen;
        setPhase(Phase::NightTarget); return Err::Ok;
    }
    Err chooseNight(Stamp s, uint8_t actor, int target) {
        Err e = check(s, Phase::NightTarget, actor); if (e != Err::Ok) return e;
        if (!legalNightTarget(n_, actor, target)) return Err::Target;
        night_targets_[actor] = static_cast<int8_t>(target); viewed_result_ = false;
        setPhase(Phase::NightResult); return Err::Ok;
    }
    Err readNight(uint8_t actor, NightInfo& out) {
        if (paused_) return Err::Paused;
        if (phase_ != Phase::NightResult) return Err::Phase;
        if (actor != actor_) return Err::Seat;
        const uint8_t t = static_cast<uint8_t>(night_targets_[actor]);
        out.target = t;
        out.finding = roles_[actor] != Role::Seer ? Finding::None :
            (roles_[t] == Role::Wolf ? Finding::Wolf : Finding::NotWolf);
        viewed_result_ = true; return Err::Ok;
    }
    Err acknowledgeNight(Stamp s, uint8_t actor) {
        Err e = check(s, Phase::NightResult, actor); if (e != Err::Ok) return e;
        if (!viewed_result_) return Err::Unseen;
        setPhase(Phase::NightDone); return Err::Ok;
    }
    Err passNight(Stamp s, uint8_t actor) {
        Err e = check(s, Phase::NightDone, actor); if (e != Err::Ok) return e;
        viewed_role_ = viewed_result_ = false;
        if (actor_ + 1 < n_) { ++actor_; setPhase(Phase::NightHandoff); }
        else { actor_ = 0; setPhase(Phase::DayReady); }
        return Err::Ok;
    }
    Err startTalk(Stamp s, uint64_t now_ms) {
        Err e = checkStamp(s); if (e != Err::Ok) return e;
        if (phase_ != Phase::DayReady && phase_ != Phase::RunoffReady) return Err::Phase;
        if (now_ms < clock_ms_) return Err::Input;
        const bool runoff = phase_ == Phase::RunoffReady;
        if (runoff) cycle_ = 2;
        remaining_ = runoff ? uint64_t(runoffDiscussion(n_))*1000 : uint64_t(discussion_s_) * 1000;
        clock_ms_ = now_ms; extension_ = false;
        setPhase(runoff ? Phase::RunoffTalk : Phase::DayTalk); return Err::Ok;
    }
    // Call in dispatcher before every command, even while pause/Home is visible.
    void tick(uint64_t now_ms) {
        if (!active()) return;
        if (now_ms < clock_ms_ || now_ms < start_ms_) { forceAbort(AbortReason::ClockFault); return; }
        if (now_ms - start_ms_ >= HARD_LIMIT_MS) { forceAbort(AbortReason::HardLimit); return; }
        if (!paused_ && talking()) {
            const uint64_t dt = now_ms - clock_ms_;
            if (dt >= remaining_) { remaining_ = 0; setPhase(Phase::VoteReady); }
            else remaining_ -= dt;
        }
        clock_ms_ = now_ms;
    }
    Err extendTalk(Stamp s) {
        Err e = checkStamp(s); if (e != Err::Ok) return e;
        if (phase_ != Phase::DayTalk || extension_) return Err::Phase;
        remaining_ += 60000; extension_ = true; bump(); return Err::Ok;
    }
    Err finishTalk(Stamp s, bool all_agree) {
        Err e = checkStamp(s); if (e != Err::Ok) return e;
        if (!talking()) return Err::Phase;
        if (!all_agree) return Err::Input;
        remaining_ = 0; setPhase(Phase::VoteReady); return Err::Ok;
    }
    Err beginVote(Stamp s) {
        Err e = checkStamp(s); if (e != Err::Ok) return e;
        if (phase_ != Phase::VoteReady) return Err::Phase;
        actor_ = 0; pending_vote_ = NONE; setPhase(Phase::VoteHandoff); return Err::Ok;
    }
    Err receiveVote(Stamp s, uint8_t actor) {
        Err e = check(s, Phase::VoteHandoff, actor); if (e != Err::Ok) return e;
        pending_vote_ = NONE; setPhase(Phase::VoteSelect); return Err::Ok;
    }
    Err selectVote(Stamp s, uint8_t actor, int target) {
        Err e = check(s, Phase::VoteSelect, actor); if (e != Err::Ok) return e;
        if (!legalVoteTarget(n_, actor, target, eligible_)) return Err::Target;
        pending_vote_ = static_cast<int8_t>(target); viewed_vote_ = false; setPhase(Phase::VoteConfirm); return Err::Ok;
    }
    Err pendingVote(uint8_t actor, int8_t& target) {
        if (paused_) return Err::Paused;
        if (phase_ != Phase::VoteConfirm) return Err::Phase;
        if (actor != actor_) return Err::Seat;
        target = pending_vote_; viewed_vote_ = true; return Err::Ok;
    }
    Err changeVote(Stamp s, uint8_t actor) {
        Err e = check(s, Phase::VoteConfirm, actor); if (e != Err::Ok) return e;
        pending_vote_ = NONE; setPhase(Phase::VoteSelect); return Err::Ok;
    }
    Err confirmVote(Stamp s, uint8_t actor) {
        Err e = check(s, Phase::VoteConfirm, actor); if (e != Err::Ok) return e;
        if (!viewed_vote_) return Err::Unseen;
        if (!legalVoteTarget(n_, actor, pending_vote_, eligible_)) return Err::Target;
        votes_[cycle_ - 1][actor] = pending_vote_; pending_vote_ = NONE;
        setPhase(Phase::VoteDone); return Err::Ok;
    }
    Err passVote(Stamp s, uint8_t actor) {
        Err e = check(s, Phase::VoteDone, actor); if (e != Err::Ok) return e;
        if (actor_ + 1 < n_) { ++actor_; setPhase(Phase::VoteHandoff); return Err::Ok; }
        actor_ = 0;
        const Tally t = tally(n_, votes_[cycle_-1], eligible_);
        if (!t.valid) { forceAbort(AbortReason::Privacy); return Err::Input; }
        selected_ = t.selected;
        if (selected_ == NONE && cycle_ == 1) {
            eligible_ = t.leaders; setPhase(Phase::RunoffReady);
        } else {
            outcome_ = outcomeFor(n_, roles_, selected_);
            setPhase(Phase::FinalReady);
        }
        return Err::Ok;
    }
    Err reveal(Stamp s, bool device_on_table) {
        Err e = checkStamp(s); if (e != Err::Ok) return e;
        if (phase_ != Phase::FinalReady) return Err::Phase;
        if (!device_on_table) return Err::Input;
        setPhase(Phase::Revealed); return Err::Ok;
    }
    Err result(Result& out) const {
        if (phase_ != Phase::Revealed || paused_) return Err::Phase;
        out = Result{}; out.player_count = n_;
        out.outcome = outcome_; out.selected = selected_; out.roles = roles_;
        out.first = votes_[0]; out.runoff = votes_[1]; out.had_runoff = cycle_ == 2;
        for (uint8_t i = 0; i < n_; ++i) if (roles_[i] == Role::Seer) out.seer = static_cast<int8_t>(i);
        if (out.seer != NONE) {
            out.inspected = night_targets_[static_cast<uint8_t>(out.seer)];
            out.finding = roles_[static_cast<uint8_t>(out.inspected)] == Role::Wolf ? Finding::Wolf : Finding::NotWolf;
        }
        return Err::Ok;
    }
    Err pause(Stamp s, uint64_t now_ms) {
        if (!same(s, stamp())) return Err::Stale;
        if (!active() || paused_) return Err::Phase;
        tick(now_ms); if (!active()) return Err::Phase;
        paused_ = true; bump(); return Err::Ok;
    }
    Err resume(Stamp s, uint64_t now_ms) {
        if (!same(s, stamp())) return Err::Stale;
        if (!active() || !paused_) return Err::Phase;
        tick(now_ms); if (!active()) return Err::Phase;
        paused_ = false; viewed_role_ = viewed_result_ = viewed_vote_ = false; bump(); return Err::Ok;
    }
    Err abort(Stamp s, AbortReason reason) {
        if (!same(s, stamp())) return Err::Stale;
        if (!active()) return Err::Phase;
        if (reason != AbortReason::User && reason != AbortReason::Privacy) return Err::Input;
        forceAbort(reason); return Err::Ok;
    }
    Err reset(Stamp s) {
        if (!same(s, stamp())) return Err::Stale;
        if (active()) return Err::Phase;
        clearPrivate(); n_ = 0; phase_ = Phase::Idle; paused_ = false; remaining_ = 0;
        actor_ = 0; cycle_ = 1; eligible_ = voteMask(n_); abort_reason_ = AbortReason::None;
        ++generation_; bump(); return Err::Ok;
    }
private:
    std::array<Role, ROLE_SLOTS> roles_{};
    std::array<int8_t, MAX_PLAYERS> night_targets_{};
    std::array<Votes, 2> votes_{};
    Phase phase_ = Phase::Idle;
    Outcome outcome_ = Outcome::None;
    AbortReason abort_reason_ = AbortReason::None;
    uint64_t generation_ = 1, revision_ = 0, start_ms_ = 0, clock_ms_ = 0, remaining_ = 0;
    uint16_t discussion_s_ = 180;
    uint8_t n_ = 0, actor_ = 0, cycle_ = 1; Mask eligible_ = 0;
    int8_t pending_vote_ = NONE, selected_ = NONE;
    bool paused_ = false, viewed_role_ = false, viewed_result_ = false, viewed_vote_ = false, extension_ = false;
    bool talking() const { return phase_ == Phase::DayTalk || phase_ == Phase::RunoffTalk; }
    void bump() { ++revision_; }
    void setPhase(Phase p) { phase_ = p; bump(); }
    Err checkStamp(Stamp s) const {
        if (!same(s, stamp())) return Err::Stale;
        return paused_ ? Err::Paused : Err::Ok;
    }
    Err check(Stamp s, Phase p, uint8_t a) const {
        Err e = checkStamp(s); if (e != Err::Ok) return e;
        if (phase_ != p) return Err::Phase;
        return a == actor_ && a < n_ ? Err::Ok : Err::Seat;
    }
    void clearPrivate() {
        volatile Role* r = roles_.data();
        volatile int8_t* n = night_targets_.data();
        for (std::size_t i = 0; i < ROLE_SLOTS; ++i) r[i] = Role::Empty;
        for (std::size_t i = 0; i < MAX_PLAYERS; ++i) n[i] = NONE;
        for (auto& a : votes_) { volatile int8_t* v = a.data(); for (std::size_t i = 0; i < MAX_PLAYERS; ++i) v[i] = NONE; }
        pending_vote_ = selected_ = NONE; outcome_ = Outcome::None;
        viewed_role_ = viewed_result_ = viewed_vote_ = false;
    }
    void forceAbort(AbortReason reason) {
        clearPrivate(); paused_ = false; remaining_ = 0; abort_reason_ = reason;
        setPhase(Phase::Aborted);
    }
};

// Independent press-to-show gate. A stale touch report never keeps a secret visible.
class SecretGate {
public:
    void enter(uint64_t epoch) {
        epoch_ = epoch; released_ = false; pressing_ = false; blocked_ = false; visible_ = false;
        down_at_ = 0;
    }
    void close() { visible_ = false; released_ = false; pressing_ = false; blocked_ = true; }
    bool update(uint64_t epoch, uint64_t now_ms, uint64_t last_good_sample_ms,
                bool down, bool inside) {
        if (epoch != epoch_ || now_ms < last_good_sample_ms || now_ms-last_good_sample_ms > 200) {
            close(); return false;
        }
        if (!down) {
            released_ = true; pressing_ = false; blocked_ = false; visible_ = false; return false;
        }
        if (!released_ || !inside || blocked_) { visible_ = false; if (!inside) blocked_ = true; return false; }
        if (!pressing_) { pressing_ = true; down_at_ = now_ms; }
        if (now_ms < down_at_) { close(); return false; }
        const uint64_t held = now_ms - down_at_;
        if (held >= 8500) { blocked_ = true; visible_ = false; return false; }
        visible_ = held >= 500; return visible_;
    }
    bool visible() const { return visible_; }
private:
    uint64_t epoch_ = 0, down_at_ = 0;
    bool released_ = false, pressing_ = false, blocked_ = false, visible_ = false;
};
}} // namespace coffee::wolf
