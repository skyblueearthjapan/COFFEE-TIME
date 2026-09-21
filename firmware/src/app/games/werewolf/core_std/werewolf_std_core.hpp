#pragma once
// COFFEE TIME / CAFE WEREWOLF —「通常ルール」（決着がつくまで続ける多日制）の携帯可能なコア。
//
// LCD・Arduino・ファイルシステム・通信・乱数源を一切知らない。C++17 だけで動く。
// 仕様は docs/WEREWOLF_STANDARD_RULES.md。作り方は検証済みの
// core/werewolf_core.hpp（ワンナイト・無改変）に合わせてある:
//   - 進行を変える操作はすべて版数（Stamp）つき。古い操作は Err::Stale で弾く
//   - 役職の配列を丸ごと返す口は作らない。秘密は「その場面の手番の本人」にだけ返す
//   - 終了（無効・やり直し）のときに秘密を消す
//
// ワンナイト版（coffee::wolf）とは別の名前空間にしてある。両方を同じ翻訳単位から
// 読んでも名前がぶつからない（Role / Phase / Err などは意味が違うので共有しない）。
#include <array>
#include <cstddef>
#include <cstdint>

namespace coffee {
namespace wolfstd {

// ---------------------------------------------------------------------------
// 大きさの上限（配列の確保に使う）
// ---------------------------------------------------------------------------
constexpr uint8_t MIN_PLAYERS = 4;    // 3 人では通常ルールが成立しない（ワンナイトのみ）
constexpr uint8_t MAX_PLAYERS = 10;
constexpr uint8_t MAX_WOLVES = 4;     // 配役表を変えたときの余裕（試験では 3 人狼も回す）
constexpr uint8_t MAX_SEERS = 4;
// 1 日に最低 1 人は減る（2 日目以降は必ず襲撃がある）ので、10 人でも 9 日あれば必ず決着する。
// 余裕をみて 12 日。これを超えたら不具合なので中断する
constexpr uint8_t MAX_DAYS = 12;
constexpr int8_t NONE = -1;

using Mask = uint16_t;
using Votes = std::array<int8_t, MAX_PLAYERS>;

// 1 局の上限は 120 分（一時停止の時間も含む）。ワンナイトの 45 分とは別の値
constexpr uint64_t HARD_LIMIT_MS = 7200000ULL;

inline bool validPlayers(uint8_t n) { return n >= MIN_PLAYERS && n <= MAX_PLAYERS; }
inline Mask bit(uint8_t seat) { return seat < 16 ? static_cast<Mask>(uint16_t(1) << seat) : 0; }
inline Mask seatsMask(uint8_t n) { return validPlayers(n) ? static_cast<Mask>((1u << n) - 1u) : 0; }
inline uint8_t popcount(Mask m) {
    uint8_t c = 0;
    while (m) { m = static_cast<Mask>(m & (m - 1)); ++c; }
    return c;
}

// ---------------------------------------------------------------------------
// 配役表 ★ここだけを直せば人数ごとの配役が変わる
//
// docs/WEREWOLF_STANDARD_RULES.md §1 の表と対にしてある。数字を変えるときは
// 両方を直すこと（ほかの場所に人数分岐は書かない）。村人 = 人数 − 人狼 − 占い師。
// ---------------------------------------------------------------------------
struct Composition {
    uint8_t wolves = 0;
    uint8_t seers = 0;
    uint8_t villagers(uint8_t n) const {
        return (n >= wolves + seers) ? static_cast<uint8_t>(n - wolves - seers) : 0;
    }
};

inline Composition compositionFor(uint8_t n) {
    if (!validPlayers(n)) return Composition{};
    if (n <= 6) return Composition{1, 1};   // 4〜6 人
    if (n <= 8) return Composition{2, 1};   // 7〜8 人
    return Composition{2, 2};               // 9〜10 人（2026-09-21 ユーザー確認済み）
}

// 配役として成り立つか。人狼だけで村が既に負けている配分は受け付けない
inline bool validComposition(uint8_t n, Composition c) {
    if (!validPlayers(n)) return false;
    if (c.wolves == 0 || c.wolves > MAX_WOLVES || c.seers > MAX_SEERS) return false;
    if (static_cast<unsigned>(c.wolves) + c.seers > n) return false;
    return c.wolves < static_cast<uint8_t>(n - c.wolves);
}

// ---------------------------------------------------------------------------
// 話し合いの時間: 60 秒 + 30 秒 × 生存人数（最短 120 秒・最長 360 秒）
// ---------------------------------------------------------------------------
constexpr uint16_t DISCUSSION_MIN_S = 120;
constexpr uint16_t DISCUSSION_MAX_S = 360;
constexpr uint32_t EXTENSION_MS = 60000;   // +1 分。その日に 1 回だけ

inline uint16_t discussionSeconds(uint8_t alive) {
    const uint32_t raw = 60u + 30u * alive;
    if (raw < DISCUSSION_MIN_S) return DISCUSSION_MIN_S;
    if (raw > DISCUSSION_MAX_S) return DISCUSSION_MAX_S;
    return static_cast<uint16_t>(raw);
}

// ---------------------------------------------------------------------------
// 配り方の番号（deal index）
//
// ワンナイト版と同じく「偏りのない整数 k を外から渡す」方式にする。
// k は 0 以上 dealCount(n) 未満で、人狼の席の組み合わせ × 占い師の席の組み合わせを
// 一意に指す。いちばん多い 10 人（人狼 2・占い師 2）でも 45 × 28 = 1260 通り。
// ---------------------------------------------------------------------------
inline uint16_t binom(uint8_t n, uint8_t k) {
    if (k > n) return 0;
    if (k > static_cast<uint8_t>(n - k)) k = static_cast<uint8_t>(n - k);
    uint32_t r = 1;
    for (uint8_t i = 0; i < k; ++i) {
        r = r * static_cast<uint32_t>(n - i) / (i + 1u);
    }
    return static_cast<uint16_t>(r);
}

// index 番目（辞書順）の「n 個から k 個を選ぶ組み合わせ」を out[0..k) に昇順で入れる
inline bool combination(uint8_t n, uint8_t k, uint16_t index, uint8_t *out) {
    if (k > n || out == nullptr) return false;
    if (index >= binom(n, k)) return false;
    uint8_t pos = 0;
    for (uint8_t v = 0; v < n && pos < k; ++v) {
        const uint16_t skip = binom(static_cast<uint8_t>(n - v - 1), static_cast<uint8_t>(k - pos - 1));
        if (index < skip) {
            out[pos++] = v;
        } else {
            index = static_cast<uint16_t>(index - skip);
        }
    }
    return pos == k;
}

inline uint16_t dealCount(uint8_t n, Composition c) {
    if (!validComposition(n, c)) return 0;
    return static_cast<uint16_t>(binom(n, c.wolves) *
                                 binom(static_cast<uint8_t>(n - c.wolves), c.seers));
}
inline uint16_t dealCount(uint8_t n) { return dealCount(n, compositionFor(n)); }

enum class Role : uint8_t { Empty, Villager, Wolf, Seer };

inline bool dealFromIndex(uint8_t n, uint16_t k, Composition c,
                          std::array<Role, MAX_PLAYERS> &out) {
    const uint16_t total = dealCount(n, c);
    if (total == 0 || k >= total) return false;
    out.fill(Role::Empty);
    for (uint8_t a = 0; a < n; ++a) out[a] = Role::Villager;

    const uint8_t rest_n = static_cast<uint8_t>(n - c.wolves);
    const uint16_t seer_ways = binom(rest_n, c.seers);
    if (seer_ways == 0) return false;
    uint8_t wolves[MAX_WOLVES]{};
    uint8_t seers[MAX_SEERS]{};
    if (!combination(n, c.wolves, static_cast<uint16_t>(k / seer_ways), wolves)) return false;
    if (!combination(rest_n, c.seers, static_cast<uint16_t>(k % seer_ways), seers)) return false;

    // 人狼に選ばれなかった席を昇順に並べ、その中から占い師を選ぶ
    uint8_t rest[MAX_PLAYERS]{};
    uint8_t rn = 0;
    for (uint8_t a = 0; a < n; ++a) {
        bool is_wolf = false;
        for (uint8_t i = 0; i < c.wolves; ++i) if (wolves[i] == a) is_wolf = true;
        if (!is_wolf) rest[rn++] = a;
    }
    if (rn != rest_n) return false;
    for (uint8_t i = 0; i < c.wolves; ++i) out[wolves[i]] = Role::Wolf;
    for (uint8_t i = 0; i < c.seers; ++i) out[rest[seers[i]]] = Role::Seer;
    return true;
}
inline bool dealFromIndex(uint8_t n, uint16_t k, std::array<Role, MAX_PLAYERS> &out) {
    return dealFromIndex(n, k, compositionFor(n), out);
}

// 外から渡す乱数源。ワンナイト版と同じ作り（剰余の偏りを捨てる）
using WordSource = bool (*)(void *, uint32_t &);
inline bool uniformBelow(uint32_t n, WordSource source, void *context, uint32_t &out) {
    if (!n || !source) return false;
    const uint32_t threshold = static_cast<uint32_t>(0u - n) % n;
    for (unsigned i = 0; i < 128; ++i) {
        uint32_t w = 0;
        if (!source(context, w)) return false;
        if (w >= threshold) { out = w % n; return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// 場面・結果・誤り
// ---------------------------------------------------------------------------
enum class Finding : uint8_t { None, NotWolf, Wolf };
enum class Winner : uint8_t { None, Village, Wolves };

enum class Phase : uint8_t {
    Idle,
    NightHandoff,       // 手渡し（生きている人だけを席順に回す）
    NightBrief,         // 秘密: 今夜のあなた（役職・今夜やること・仲間の名前）★初日の夜だけ
    NightTarget,        // 夜の行動: 1 人選ぼう（全員同じ画面）
    NightResult,        // 秘密: 夜の結果（役職のおさらいもここに出す）
    NightDone,          // 隠しました → 次の人へ
    MorningReady,       // 公開の前の「端末をテーブルに置こう」
    MorningAnnounce,    // 朝の発表（昨夜の犠牲者）
    DayTalk,            // n 日目の話し合い
    VoteReady,          // 投票の案内
    VoteHandoff,        // 投票の手渡し
    VoteSelect,         // 人狼だと思う人を選ぼう
    VoteConfirm,        // 秘密: 自分の投票先の確認
    VoteDone,           // 投票を隠しました
    RunoffReady,        // 同票 → 決選投票の案内
    ExecutionReady,     // 公開の前の「端末をテーブルに置こう」
    ExecutionAnnounce,  // 追放の発表
    FinalReady,         // 決着。みんなで答え合わせ
    Revealed,           // 全公開
    Aborted,            // 無効
};

enum class Err : uint8_t { Ok, Stale, Phase, Seat, Target, Unseen, Paused, Input };
enum class AbortReason : uint8_t { None, User, Privacy, HardLimit, ClockFault, Internal };

struct Stamp { uint64_t generation; uint64_t revision; };
inline bool same(Stamp a, Stamp b) { return a.generation == b.generation && a.revision == b.revision; }

// 公開情報だけ。役職・占い結果・襲撃先・各自の投票はここに入れない
struct PublicView {
    Phase phase = Phase::Idle;
    uint8_t player_count = 0;
    uint8_t actor = 0;          // 今この端末を持っている人（手渡し中の場面でのみ意味を持つ）
    uint8_t day = 0;            // 1 日目から数える
    uint8_t alive_count = 0;
    Mask alive = 0;
    Mask eligible = 0;          // 投票で選べる相手（決選では候補だけ）
    uint8_t vote_cycle = 1;     // 1 = 本投票 / 2 = 決選
    uint8_t voted_count = 0;    // 投票を終えた人数（誰に入れたかは公開しない）
    uint8_t wolves = 0;         // 配役の枚数（公開情報。ロビーでも出す）
    uint8_t seers = 0;
    int8_t last_victim = NONE;    // 直近の朝に発表した犠牲者
    int8_t last_executed = NONE;  // 直近に追放された人
    Winner winner = Winner::None;
    bool paused = false;
    bool extension_used = false;
    uint64_t remaining_ms = 0;
    Stamp stamp{1, 0};
};

// 秘密。手番の本人だけが、その場面でだけ読める。
// 2026-09-21（ユーザーの声「役職は変わらないのに毎晩出るのは無駄」）以降、
// この案内は**初日の夜だけ**。2 日目以降の夜は手渡しのあとすぐ対象選択に進み、
// 役職のおさらいは夜の結果（NightOutcome）の中で見せる
struct Brief {
    Role role = Role::Empty;
    uint8_t day = 0;               // 常に 1（初日の夜にしか読めない）
    bool first_night = false;      // 初日は襲撃なし。常に true
    Mask partners = 0;             // 人狼のときだけ: 自分以外の人狼の席
};

// 夜の結果。2 日目以降は「今夜のあなた」をはさまないので、役職のおさらい（role）は
// ここに載せて、手番の本人にだけ見せる
struct NightOutcome {
    Role role = Role::Empty;
    bool first_night = false;
    int8_t target = NONE;          // 自分が選んだ相手（占い師・人狼のときだけ意味がある）
    Finding finding = Finding::None;   // 占い師だけ
};

// 1 日の記録（最後の全公開でだけ使う）
struct SeerRecord {
    int8_t seer = NONE;
    int8_t target = NONE;
    Finding finding = Finding::None;
};
struct DayLog {
    int8_t victim = NONE;      // 朝に発表した犠牲者（初日は必ず NONE）
    int8_t executed = NONE;    // 追放された人（同票続きなら NONE）
    bool had_runoff = false;
    Votes first{};
    Votes runoff{};
    std::array<SeerRecord, MAX_SEERS> seers{};
    uint8_t seer_count = 0;
};
struct Summary {
    uint8_t player_count = 0;
    uint8_t days = 0;              // 記録のある日数
    Composition composition{};
    Winner winner = Winner::None;
    std::array<Role, MAX_PLAYERS> roles{};
    Mask alive = 0;
};

// ---------------------------------------------------------------------------
// 進行の本体
// ---------------------------------------------------------------------------
class Engine {
public:
    Engine() { clearPrivate(); }
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;
    ~Engine() { clearPrivate(); }

    Stamp stamp() const { return {generation_, revision_}; }
    bool active() const {
        return phase_ != Phase::Idle && phase_ != Phase::Revealed && phase_ != Phase::Aborted;
    }
    AbortReason abortReason() const { return abort_reason_; }

    PublicView publicView() const {
        PublicView v;
        v.phase = phase_;
        v.player_count = n_;
        v.actor = actor_;
        v.day = day_;
        v.alive = alive_;
        v.alive_count = popcount(alive_);
        v.eligible = eligible_;
        v.vote_cycle = cycle_;
        v.voted_count = voted_count_;
        v.wolves = comp_.wolves;
        v.seers = comp_.seers;
        v.last_victim = last_victim_;
        v.last_executed = last_executed_;
        v.winner = winner_;
        v.paused = paused_;
        v.extension_used = extension_;
        v.remaining_ms = remaining_;
        v.stamp = stamp();
        return v;
    }

    // 生きているか（公開情報）
    bool aliveSeat(uint8_t seat) const { return seat < n_ && (alive_ & bit(seat)) != 0; }

    // 夜・投票で選べる相手か（公開情報。役職では変わらない）
    bool legalNightTarget(uint8_t actor, int target) const {
        return actor < n_ && target >= 0 && target < static_cast<int>(n_) && target != actor &&
               aliveSeat(static_cast<uint8_t>(target)) && aliveSeat(actor);
    }
    bool legalVoteTarget(uint8_t actor, int target) const {
        return legalNightTarget(actor, target) && (eligible_ & bit(static_cast<uint8_t>(target))) != 0;
    }

    // -----------------------------------------------------------------------
    // 開始。信頼できる呼び出し側が、乱数と「配った」印の準備を終えてから呼ぶ
    // -----------------------------------------------------------------------
    Err start(Stamp s, uint8_t players, uint16_t deal_index, uint64_t now_ms) {
        return start(s, players, deal_index, now_ms, compositionFor(players));
    }
    // 配役を明示する版（PC 上の試験でだけ使う。ファームは既定の配役表を使う）
    Err start(Stamp s, uint8_t players, uint16_t deal_index, uint64_t now_ms, Composition c) {
        if (!same(s, stamp())) return Err::Stale;
        if (phase_ != Phase::Idle) return Err::Phase;
        if (!validComposition(players, c)) return Err::Input;
        std::array<Role, MAX_PLAYERS> generated{};
        if (!dealFromIndex(players, deal_index, c, generated)) return Err::Input;

        clearPrivate();
        n_ = players;
        comp_ = c;
        roles_ = generated;
        // 手元の写しも消す（計装されたファームまでは防げないが、置きっぱなしにはしない）
        {
            volatile Role *p = generated.data();
            for (std::size_t i = 0; i < MAX_PLAYERS; ++i) p[i] = Role::Empty;
        }
        ++generation_;
        ++revision_;
        alive_ = seatsMask(n_);
        day_ = 1;
        cycle_ = 1;
        eligible_ = alive_;
        winner_ = Winner::None;
        last_victim_ = last_executed_ = NONE;
        actor_ = firstLiving();
        phase_ = Phase::NightHandoff;
        start_ms_ = clock_ms_ = now_ms;
        remaining_ = 0;
        paused_ = false;
        extension_ = false;
        abort_reason_ = AbortReason::None;
        return Err::Ok;
    }

    // -----------------------------------------------------------------------
    // 夜
    // -----------------------------------------------------------------------
    // 初日の夜だけ「今夜のあなた」をはさむ。2 日目以降は手渡しのあとすぐ対象選択へ。
    // どの役職も同じ手順を踏むことは変わらない（画面の数は夜ごとに全員そろっている）
    Err receiveNight(Stamp s, uint8_t actor) {
        Err e = check(s, Phase::NightHandoff, actor); if (e != Err::Ok) return e;
        viewed_brief_ = viewed_result_ = false;
        setPhase(day_ == 1 ? Phase::NightBrief : Phase::NightTarget);
        return Err::Ok;
    }

    // SecretGate が新しい押下を認めたときだけ描画側が呼ぶ。役職の一覧を返す口は作らない。
    // 読めるのは初日の夜だけ（2 日目以降は NightBrief にならないので Err::Phase）
    Err readBrief(uint8_t actor, Brief &out) {
        if (paused_) return Err::Paused;
        if (phase_ != Phase::NightBrief || day_ != 1) return Err::Phase;
        if (actor != actor_ || !aliveSeat(actor)) return Err::Seat;
        out = Brief{};
        out.role = roles_[actor];
        out.day = day_;
        out.first_night = true;
        if (out.role == Role::Wolf) {
            // 仲間の名前を知るのは初日の夜だけ（以後は覚えておいてもらう）
            for (uint8_t a = 0; a < n_; ++a) {
                if (a != actor && roles_[a] == Role::Wolf) out.partners |= bit(a);
            }
        }
        viewed_brief_ = true;
        return Err::Ok;
    }

    Err acknowledgeBrief(Stamp s, uint8_t actor) {
        Err e = check(s, Phase::NightBrief, actor); if (e != Err::Ok) return e;
        if (!viewed_brief_) return Err::Unseen;
        setPhase(Phase::NightTarget);
        return Err::Ok;
    }

    Err chooseNight(Stamp s, uint8_t actor, int target) {
        Err e = check(s, Phase::NightTarget, actor); if (e != Err::Ok) return e;
        if (!legalNightTarget(actor, target)) return Err::Target;
        const uint8_t t = static_cast<uint8_t>(target);
        // 人狼は仲間を襲えない。仲間の名前は直前の秘密画面に出ているので迷わない
        if (roles_[actor] == Role::Wolf && roles_[t] == Role::Wolf) return Err::Target;
        night_target_[actor] = static_cast<int8_t>(t);
        if (roles_[actor] == Role::Wolf) {
            // 後に操作した生存中の人狼の選択で上書きする（= 最終的な襲撃先）。
            // 誰が選んだかは誰にも見せないので持たない
            wolf_pick_ = static_cast<int8_t>(t);
        }
        viewed_result_ = false;
        setPhase(Phase::NightResult);
        return Err::Ok;
    }

    Err readNightResult(uint8_t actor, NightOutcome &out) {
        if (paused_) return Err::Paused;
        if (phase_ != Phase::NightResult) return Err::Phase;
        if (actor != actor_ || !aliveSeat(actor)) return Err::Seat;
        out = NightOutcome{};
        out.role = roles_[actor];
        out.first_night = (day_ == 1);
        const int8_t t = night_target_[actor];
        if (out.role == Role::Seer && t != NONE) {
            out.target = t;
            out.finding = roles_[static_cast<uint8_t>(t)] == Role::Wolf ? Finding::Wolf : Finding::NotWolf;
        } else if (out.role == Role::Wolf) {
            out.target = t;   // 村人には何も返さない（target は NONE のまま）
        }
        viewed_result_ = true;
        return Err::Ok;
    }

    Err acknowledgeNightResult(Stamp s, uint8_t actor) {
        Err e = check(s, Phase::NightResult, actor); if (e != Err::Ok) return e;
        if (!viewed_result_) return Err::Unseen;
        setPhase(Phase::NightDone);
        return Err::Ok;
    }

    // 次の生きている人へ。いなければ夜を締めて朝の発表の準備に入る
    Err passNight(Stamp s, uint8_t actor) {
        Err e = check(s, Phase::NightDone, actor); if (e != Err::Ok) return e;
        viewed_brief_ = viewed_result_ = false;
        const int8_t next = nextLiving(actor);
        if (next != NONE) {
            actor_ = static_cast<uint8_t>(next);
            setPhase(Phase::NightHandoff);
            return Err::Ok;
        }
        resolveNight();
        return Err::Ok;
    }

    // -----------------------------------------------------------------------
    // 朝
    // -----------------------------------------------------------------------
    Err openMorning(Stamp s, bool device_on_table) {
        Err e = checkStamp(s); if (e != Err::Ok) return e;
        if (phase_ != Phase::MorningReady) return Err::Phase;
        if (!device_on_table) return Err::Input;
        setPhase(Phase::MorningAnnounce);
        return Err::Ok;
    }

    // 朝の発表のあと。決着していれば答え合わせへ、続くなら話し合いへ
    Err closeMorning(Stamp s, uint64_t now_ms) {
        Err e = checkStamp(s); if (e != Err::Ok) return e;
        if (phase_ != Phase::MorningAnnounce) return Err::Phase;
        if (winner_ != Winner::None) { setPhase(Phase::FinalReady); return Err::Ok; }
        if (now_ms < clock_ms_) return Err::Input;
        remaining_ = static_cast<uint64_t>(discussionSeconds(popcount(alive_))) * 1000ull;
        clock_ms_ = now_ms;
        extension_ = false;
        setPhase(Phase::DayTalk);
        return Err::Ok;
    }

    // -----------------------------------------------------------------------
    // 話し合い
    // -----------------------------------------------------------------------
    Err extendTalk(Stamp s) {
        Err e = checkStamp(s); if (e != Err::Ok) return e;
        if (phase_ != Phase::DayTalk || extension_) return Err::Phase;
        remaining_ += EXTENSION_MS;
        extension_ = true;
        bump();
        return Err::Ok;
    }

    Err finishTalk(Stamp s, bool all_agree) {
        Err e = checkStamp(s); if (e != Err::Ok) return e;
        if (phase_ != Phase::DayTalk) return Err::Phase;
        if (!all_agree) return Err::Input;
        remaining_ = 0;
        setPhase(Phase::VoteReady);
        return Err::Ok;
    }

    // -----------------------------------------------------------------------
    // 投票
    // -----------------------------------------------------------------------
    Err beginVote(Stamp s) {
        Err e = checkStamp(s); if (e != Err::Ok) return e;
        if (phase_ != Phase::VoteReady) return Err::Phase;
        cycle_ = 1;
        eligible_ = alive_;
        voted_count_ = 0;
        pending_vote_ = NONE;
        for (auto &v : votes_[0]) v = NONE;
        for (auto &v : votes_[1]) v = NONE;
        actor_ = firstLiving();
        setPhase(Phase::VoteHandoff);
        return Err::Ok;
    }

    Err beginRunoff(Stamp s) {
        Err e = checkStamp(s); if (e != Err::Ok) return e;
        if (phase_ != Phase::RunoffReady) return Err::Phase;
        cycle_ = 2;
        voted_count_ = 0;
        pending_vote_ = NONE;
        actor_ = firstLiving();
        setPhase(Phase::VoteHandoff);
        return Err::Ok;
    }

    Err receiveVote(Stamp s, uint8_t actor) {
        Err e = check(s, Phase::VoteHandoff, actor); if (e != Err::Ok) return e;
        pending_vote_ = NONE;
        viewed_vote_ = false;
        setPhase(Phase::VoteSelect);
        return Err::Ok;
    }

    Err selectVote(Stamp s, uint8_t actor, int target) {
        Err e = check(s, Phase::VoteSelect, actor); if (e != Err::Ok) return e;
        if (!legalVoteTarget(actor, target)) return Err::Target;
        pending_vote_ = static_cast<int8_t>(target);
        viewed_vote_ = false;
        setPhase(Phase::VoteConfirm);
        return Err::Ok;
    }

    Err pendingVote(uint8_t actor, int8_t &target) {
        if (paused_) return Err::Paused;
        if (phase_ != Phase::VoteConfirm) return Err::Phase;
        if (actor != actor_ || !aliveSeat(actor)) return Err::Seat;
        target = pending_vote_;
        viewed_vote_ = true;
        return Err::Ok;
    }

    Err changeVote(Stamp s, uint8_t actor) {
        Err e = check(s, Phase::VoteConfirm, actor); if (e != Err::Ok) return e;
        pending_vote_ = NONE;
        setPhase(Phase::VoteSelect);
        return Err::Ok;
    }

    Err confirmVote(Stamp s, uint8_t actor) {
        Err e = check(s, Phase::VoteConfirm, actor); if (e != Err::Ok) return e;
        if (!viewed_vote_) return Err::Unseen;
        if (!legalVoteTarget(actor, pending_vote_)) return Err::Target;
        votes_[cycle_ - 1][actor] = pending_vote_;
        pending_vote_ = NONE;
        ++voted_count_;
        setPhase(Phase::VoteDone);
        return Err::Ok;
    }

    Err passVote(Stamp s, uint8_t actor) {
        Err e = check(s, Phase::VoteDone, actor); if (e != Err::Ok) return e;
        const int8_t next = nextLiving(actor);
        if (next != NONE) {
            actor_ = static_cast<uint8_t>(next);
            setPhase(Phase::VoteHandoff);
            return Err::Ok;
        }
        tallyVotes();
        return Err::Ok;
    }

    // -----------------------------------------------------------------------
    // 追放の発表
    // -----------------------------------------------------------------------
    Err openExecution(Stamp s, bool device_on_table) {
        Err e = checkStamp(s); if (e != Err::Ok) return e;
        if (phase_ != Phase::ExecutionReady) return Err::Phase;
        if (!device_on_table) return Err::Input;
        applyExecution();
        setPhase(Phase::ExecutionAnnounce);
        return Err::Ok;
    }

    // 追放の発表のあと。決着していれば答え合わせへ、続くなら次の夜へ
    Err closeExecution(Stamp s) {
        Err e = checkStamp(s); if (e != Err::Ok) return e;
        if (phase_ != Phase::ExecutionAnnounce) return Err::Phase;
        if (winner_ != Winner::None) { setPhase(Phase::FinalReady); return Err::Ok; }
        if (day_ >= MAX_DAYS) { forceAbort(AbortReason::Internal); return Err::Input; }
        ++day_;
        cycle_ = 1;
        voted_count_ = 0;
        eligible_ = alive_;
        last_victim_ = NONE;
        wolf_pick_ = NONE;
        for (auto &t : night_target_) t = NONE;
        actor_ = firstLiving();
        setPhase(Phase::NightHandoff);
        return Err::Ok;
    }

    // -----------------------------------------------------------------------
    // 全公開
    // -----------------------------------------------------------------------
    Err reveal(Stamp s, bool device_on_table) {
        Err e = checkStamp(s); if (e != Err::Ok) return e;
        if (phase_ != Phase::FinalReady) return Err::Phase;
        if (!device_on_table) return Err::Input;
        setPhase(Phase::Revealed);
        return Err::Ok;
    }

    Err summary(Summary &out) const {
        if (phase_ != Phase::Revealed || paused_) return Err::Phase;
        out = Summary{};
        out.player_count = n_;
        out.days = day_;
        out.composition = comp_;
        out.winner = winner_;
        out.roles = roles_;
        out.alive = alive_;
        return Err::Ok;
    }
    // 1 日分ずつ返す（丸ごと返すと 700 バイト近くをスタックに積むことになるため）
    Err dayLog(uint8_t day, DayLog &out) const {
        if (phase_ != Phase::Revealed || paused_) return Err::Phase;
        if (day < 1 || day > day_ || day > MAX_DAYS) return Err::Input;
        out = log_[day - 1];
        return Err::Ok;
    }

    // -----------------------------------------------------------------------
    // 時計・一時停止・中断
    // -----------------------------------------------------------------------
    // 操作の前に必ず呼ぶ（一時停止中や HOME 表示中も含めて）
    void tick(uint64_t now_ms) {
        if (!active()) return;
        if (now_ms < clock_ms_ || now_ms < start_ms_) { forceAbort(AbortReason::ClockFault); return; }
        if (now_ms - start_ms_ >= HARD_LIMIT_MS) { forceAbort(AbortReason::HardLimit); return; }
        if (!paused_ && phase_ == Phase::DayTalk) {
            const uint64_t dt = now_ms - clock_ms_;
            if (dt >= remaining_) { remaining_ = 0; setPhase(Phase::VoteReady); }
            else remaining_ -= dt;
        }
        clock_ms_ = now_ms;
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
        paused_ = false;
        viewed_brief_ = viewed_result_ = viewed_vote_ = false;
        bump(); return Err::Ok;
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
        clearPrivate();
        n_ = 0; comp_ = Composition{}; phase_ = Phase::Idle; paused_ = false; remaining_ = 0;
        actor_ = 0; day_ = 0; cycle_ = 1; voted_count_ = 0; alive_ = 0; eligible_ = 0;
        winner_ = Winner::None; abort_reason_ = AbortReason::None;
        ++generation_; bump();
        return Err::Ok;
    }

private:
    // --- 秘密（RAM のみ。終了・無効・やり直しで消す）---
    std::array<Role, MAX_PLAYERS> roles_{};
    std::array<int8_t, MAX_PLAYERS> night_target_{};
    std::array<Votes, 2> votes_{};
    std::array<DayLog, MAX_DAYS> log_{};
    int8_t pending_vote_ = NONE;
    int8_t wolf_pick_ = NONE;   // 今夜の襲撃先。誰の選択かは残さない

    // --- 公開・進行 ---
    Phase phase_ = Phase::Idle;
    Composition comp_{};
    Winner winner_ = Winner::None;
    AbortReason abort_reason_ = AbortReason::None;
    uint64_t generation_ = 1, revision_ = 0, start_ms_ = 0, clock_ms_ = 0, remaining_ = 0;
    Mask alive_ = 0, eligible_ = 0;
    uint8_t n_ = 0, actor_ = 0, day_ = 0, cycle_ = 1, voted_count_ = 0;
    int8_t last_victim_ = NONE, last_executed_ = NONE;
    int8_t pending_execution_ = NONE;
    bool paused_ = false, extension_ = false;
    bool viewed_brief_ = false, viewed_result_ = false, viewed_vote_ = false;

    void bump() { ++revision_; }
    void setPhase(Phase p) { phase_ = p; bump(); }

    Err checkStamp(Stamp s) const {
        if (!same(s, stamp())) return Err::Stale;
        return paused_ ? Err::Paused : Err::Ok;
    }
    Err check(Stamp s, Phase p, uint8_t a) const {
        Err e = checkStamp(s); if (e != Err::Ok) return e;
        if (phase_ != p) return Err::Phase;
        if (a != actor_ || a >= n_) return Err::Seat;
        return aliveSeat(a) ? Err::Ok : Err::Seat;   // 抜けた人は手番に現れない
    }

    int8_t firstLiving() const {
        for (uint8_t a = 0; a < n_; ++a) if (alive_ & bit(a)) return static_cast<int8_t>(a);
        return NONE;
    }
    int8_t nextLiving(uint8_t from) const {
        for (uint8_t a = static_cast<uint8_t>(from + 1); a < n_; ++a) {
            if (alive_ & bit(a)) return static_cast<int8_t>(a);
        }
        return NONE;
    }

    DayLog &today() { return log_[(day_ >= 1 && day_ <= MAX_DAYS) ? day_ - 1 : 0]; }

    void checkWinner() {
        uint8_t wolves = 0, others = 0;
        for (uint8_t a = 0; a < n_; ++a) {
            if (!(alive_ & bit(a))) continue;
            if (roles_[a] == Role::Wolf) ++wolves; else ++others;
        }
        if (wolves == 0) winner_ = Winner::Village;
        else if (wolves >= others) winner_ = Winner::Wolves;
    }

    // 夜の締め。占いの結果を記録し、襲撃を確定する（初日は襲撃なし）
    void resolveNight() {
        DayLog &d = today();
        d.seer_count = 0;
        for (uint8_t a = 0; a < n_; ++a) {
            if (!(alive_ & bit(a)) || roles_[a] != Role::Seer) continue;
            const int8_t t = night_target_[a];
            if (t == NONE || d.seer_count >= MAX_SEERS) continue;
            SeerRecord &r = d.seers[d.seer_count++];
            r.seer = static_cast<int8_t>(a);
            r.target = t;
            r.finding = roles_[static_cast<uint8_t>(t)] == Role::Wolf ? Finding::Wolf : Finding::NotWolf;
        }
        int8_t victim = NONE;
        if (day_ > 1 && wolf_pick_ != NONE) {
            const uint8_t t = static_cast<uint8_t>(wolf_pick_);
            if ((alive_ & bit(t)) && roles_[t] != Role::Wolf) victim = static_cast<int8_t>(t);
        }
        d.victim = victim;
        last_victim_ = victim;
        if (victim != NONE) alive_ = static_cast<Mask>(alive_ & ~bit(static_cast<uint8_t>(victim)));
        checkWinner();
        actor_ = 0;
        setPhase(Phase::MorningReady);
    }

    // 投票の集計。単独最多なら追放、同票なら 1 回だけ決選、決選も同票なら追放なし
    void tallyVotes() {
        std::array<uint8_t, MAX_PLAYERS> counts{};
        const Votes &v = votes_[cycle_ - 1];
        for (uint8_t a = 0; a < n_; ++a) {
            if (!(alive_ & bit(a))) continue;
            if (!legalVoteTarget(a, v[a])) { forceAbort(AbortReason::Privacy); return; }
            ++counts[static_cast<uint8_t>(v[a])];
        }
        uint8_t best = 0;
        for (uint8_t t = 0; t < n_; ++t) if ((eligible_ & bit(t)) && counts[t] > best) best = counts[t];
        Mask leaders = 0;
        uint8_t leader_count = 0;
        for (uint8_t t = 0; t < n_; ++t) {
            if ((eligible_ & bit(t)) && counts[t] == best) { leaders |= bit(t); ++leader_count; }
        }
        DayLog &d = today();
        if (cycle_ == 1) d.first = v; else { d.runoff = v; d.had_runoff = true; }

        if (leader_count == 1 && best > 0) {
            for (uint8_t t = 0; t < n_; ++t) if (leaders & bit(t)) pending_execution_ = static_cast<int8_t>(t);
            actor_ = 0;
            setPhase(Phase::ExecutionReady);
            return;
        }
        if (cycle_ == 1) {
            eligible_ = leaders;
            actor_ = 0;
            setPhase(Phase::RunoffReady);
            return;
        }
        pending_execution_ = NONE;   // 決選でも同票 → その日は追放なし
        actor_ = 0;
        setPhase(Phase::ExecutionReady);
    }

    void applyExecution() {
        last_executed_ = pending_execution_;
        today().executed = pending_execution_;
        if (pending_execution_ != NONE) {
            alive_ = static_cast<Mask>(alive_ & ~bit(static_cast<uint8_t>(pending_execution_)));
        }
        pending_execution_ = NONE;
        checkWinner();
    }

    void clearPrivate() {
        volatile Role *r = roles_.data();
        for (std::size_t i = 0; i < MAX_PLAYERS; ++i) r[i] = Role::Empty;
        volatile int8_t *t = night_target_.data();
        for (std::size_t i = 0; i < MAX_PLAYERS; ++i) t[i] = NONE;
        for (auto &a : votes_) {
            volatile int8_t *v = a.data();
            for (std::size_t i = 0; i < MAX_PLAYERS; ++i) v[i] = NONE;
        }
        for (auto &d : log_) {
            volatile int8_t *f = d.first.data();
            volatile int8_t *ru = d.runoff.data();
            for (std::size_t i = 0; i < MAX_PLAYERS; ++i) { f[i] = NONE; ru[i] = NONE; }
            for (auto &sr : d.seers) { sr.seer = NONE; sr.target = NONE; sr.finding = Finding::None; }
            d.seer_count = 0; d.victim = NONE; d.executed = NONE; d.had_runoff = false;
        }
        pending_vote_ = pending_execution_ = NONE;
        wolf_pick_ = NONE;
        last_victim_ = last_executed_ = NONE;
        viewed_brief_ = viewed_result_ = viewed_vote_ = false;
    }

    void forceAbort(AbortReason reason) {
        clearPrivate();
        paused_ = false;
        remaining_ = 0;
        winner_ = Winner::None;
        abort_reason_ = reason;
        setPhase(Phase::Aborted);
    }
};

}  // namespace wolfstd
}  // namespace coffee
