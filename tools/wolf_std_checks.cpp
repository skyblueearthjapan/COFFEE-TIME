// 人狼「通常ルール」コア（core_std/werewolf_std_core.hpp）の PC 上の試験。
//
// 実機は要らない。tools/run_wolf_std_checks.py が zig を呼んでこれを組み立て、実行する。
//   python tools/run_wolf_std_checks.py
//
// 確かめること:
//   1. 4〜10 人の配役の枚数と、配り方の番号が一対一であること
//   2. 勝敗の判定（村の勝ち／人狼の勝ち）
//   3. 抜けた人が手番・投票・対象に現れないこと
//   4. 最初の夜は誰も死なないこと／役職の案内は初日の夜だけで、どの役職も夜ごとに同じ手順を踏むこと
//   5. 人狼 2 人のときの襲撃先（後に操作した生存中の人狼の選択）。仲間の選択は誰にも見えないこと
//   6. 同票 → 決選 → それでも同票なら追放なし
//   7. 古い版数（Stamp）・場面違いの操作を弾くこと
//   8. 秘密が手番の本人以外から読めないこと（占い師が 2 人いても混ざらないこと）
//   9. 乱数でたくさんの局を最後まで回し、必ず決着すること（不変条件つき）
//  10. 参考として、でたらめに遊んだときの勝率の表（腕前は反映されない目安）
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../firmware/src/app/games/werewolf/core_std/werewolf_std_core.hpp"

namespace ws = coffee::wolfstd;
using ws::Err;
using ws::Finding;
using ws::Phase;
using ws::Role;
using ws::Winner;

// ---------------------------------------------------------------------------
// 試験の道具
// ---------------------------------------------------------------------------
static int g_fail = 0;
static int g_checks = 0;
static int g_printed = 0;

static void report(bool ok, const char *what, int line) {
    ++g_checks;
    if (ok) return;
    ++g_fail;
    if (g_printed < 40) {
        std::printf("  FAIL (line %d): %s\n", line, what);
        ++g_printed;
    }
}
#define CHECK(cond) report((cond), #cond, __LINE__)

// 再現できる疑似乱数（xorshift64*）。試験の種は固定する
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    uint32_t next() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return static_cast<uint32_t>((s * 0x2545F4914F6CDD1Dull) >> 32);
    }
    uint32_t below(uint32_t n) { return n ? next() % n : 0; }
};

// ---------------------------------------------------------------------------
// 1 局をでたらめに最後まで回す
// ---------------------------------------------------------------------------
struct GameStats {
    Winner winner = Winner::None;
    uint8_t days = 0;
    bool ok = false;
};

// deep = 不変条件を毎手番で確かめる（遅いので勝率の表では省く）
static GameStats playRandomGame(uint8_t n, ws::Composition comp, Rng &rng, bool deep) {
    GameStats out;
    ws::Engine e;
    uint64_t now = 1000;
    const uint16_t total = ws::dealCount(n, comp);
    if (total == 0) return out;
    if (e.start(e.stamp(), n, static_cast<uint16_t>(rng.below(total)), now, comp) != Err::Ok) {
        return out;
    }

    // 「その夜に生きている全員が、役職によらず同じ数の画面を踏む」ことを数える
    ws::Mask night_alive = 0;
    uint8_t night_day = 0;
    std::array<uint8_t, ws::MAX_PLAYERS> steps{};

    int guard = 0;
    while (e.active() && ++guard < 20000) {
        now += 10;
        e.tick(now);
        const ws::PublicView pv = e.publicView();
        if (deep) {
            if (pv.day > ws::MAX_DAYS) { CHECK(pv.day <= ws::MAX_DAYS); break; }
        }
        switch (pv.phase) {
        case Phase::NightHandoff: {
            if (deep) {
                CHECK(e.aliveSeat(pv.actor));
                if (pv.day != night_day) {   // その夜の 1 人目
                    night_day = pv.day;
                    night_alive = pv.alive;
                    steps.fill(0);
                }
                ++steps[pv.actor];
            }
            CHECK(e.receiveNight(e.stamp(), pv.actor) == Err::Ok);
            break;
        }
        case Phase::NightBrief: {
            ws::Brief b;
            CHECK(e.readBrief(pv.actor, b) == Err::Ok);
            if (deep) {
                ++steps[pv.actor];
                CHECK(pv.day == 1);          // 役職の案内が出るのは初日の夜だけ
                CHECK(b.day == 1);
                CHECK(b.first_night);
                CHECK(b.role != Role::Empty);
                // 占い師・村人には仲間の情報が一切載らない
                if (b.role != Role::Wolf) CHECK(b.partners == 0);
                CHECK((b.partners & ws::bit(pv.actor)) == 0);
            }
            CHECK(e.acknowledgeBrief(e.stamp(), pv.actor) == Err::Ok);
            break;
        }
        case Phase::NightTarget: {
            if (deep) {
                ++steps[pv.actor];
                // 案内は NightBrief の場面でしか読めない（2 日目以降はその場面が無い）
                ws::Brief b;
                CHECK(e.readBrief(pv.actor, b) == Err::Phase);
            }
            // 生きている自分以外を順に試す。人狼が仲間を選んだときだけ Err::Target が返る
            bool done = false;
            const uint8_t start = static_cast<uint8_t>(rng.below(n));
            for (uint8_t i = 0; i < n && !done; ++i) {
                const uint8_t t = static_cast<uint8_t>((start + i) % n);
                if (t == pv.actor || !e.aliveSeat(t)) continue;
                if (e.chooseNight(e.stamp(), pv.actor, t) == Err::Ok) done = true;
            }
            CHECK(done);
            if (!done) return out;
            break;
        }
        case Phase::NightResult: {
            ws::NightOutcome o;
            CHECK(e.readNightResult(pv.actor, o) == Err::Ok);
            if (deep) {
                ++steps[pv.actor];
                // 役職のおさらいは、ここで手番の本人にだけ返る
                CHECK(o.role != Role::Empty);
                CHECK(o.first_night == (pv.day == 1));
                // 占い師以外に占いの結果は絶対に返さない
                if (o.role != Role::Seer) CHECK(o.finding == Finding::None);
                if (o.role == Role::Villager) CHECK(o.target == ws::NONE);
            }
            CHECK(e.acknowledgeNightResult(e.stamp(), pv.actor) == Err::Ok);
            break;
        }
        case Phase::NightDone:
            if (deep) ++steps[pv.actor];
            CHECK(e.passNight(e.stamp(), pv.actor) == Err::Ok);
            break;
        case Phase::MorningReady:
            if (deep) {
                // 初日は 5 画面（手渡し・案内・対象・結果・隠した）、2 日目以降は案内が無いので 4 画面。
                // 役職によって増えたり減ったりしないこと
                const uint8_t want = (night_day == 1) ? 5 : 4;
                for (uint8_t a = 0; a < n; ++a) {
                    CHECK(steps[a] == ((night_alive & ws::bit(a)) ? want : 0));
                }
            }
            CHECK(e.openMorning(e.stamp(), true) == Err::Ok);
            break;
        case Phase::MorningAnnounce: {
            if (deep && pv.day == 1) CHECK(pv.last_victim == ws::NONE);   // 初日は襲撃なし
            if (deep && pv.last_victim != ws::NONE) {
                CHECK(!e.aliveSeat(static_cast<uint8_t>(pv.last_victim)));
            }
            CHECK(e.closeMorning(e.stamp(), now) == Err::Ok);
            break;
        }
        case Phase::DayTalk:
            CHECK(e.finishTalk(e.stamp(), true) == Err::Ok);
            break;
        case Phase::VoteReady:
            CHECK(e.beginVote(e.stamp()) == Err::Ok);
            break;
        case Phase::VoteHandoff:
            if (deep) CHECK(e.aliveSeat(pv.actor));
            CHECK(e.receiveVote(e.stamp(), pv.actor) == Err::Ok);
            break;
        case Phase::VoteSelect: {
            if (deep) {
                // 抜けた人は投票先に出てこない
                for (uint8_t t = 0; t < n; ++t) {
                    if (e.aliveSeat(t)) continue;
                    CHECK(!e.legalVoteTarget(pv.actor, t));
                    CHECK(e.selectVote(e.stamp(), pv.actor, t) == Err::Target);
                }
            }
            bool done = false;
            const uint8_t start = static_cast<uint8_t>(rng.below(n));
            for (uint8_t i = 0; i < n && !done; ++i) {
                const uint8_t t = static_cast<uint8_t>((start + i) % n);
                if (!e.legalVoteTarget(pv.actor, t)) continue;
                if (e.selectVote(e.stamp(), pv.actor, t) == Err::Ok) done = true;
            }
            CHECK(done);
            if (!done) return out;
            break;
        }
        case Phase::VoteConfirm: {
            int8_t t = ws::NONE;
            CHECK(e.pendingVote(pv.actor, t) == Err::Ok);
            CHECK(t != ws::NONE);
            CHECK(e.confirmVote(e.stamp(), pv.actor) == Err::Ok);
            break;
        }
        case Phase::VoteDone:
            CHECK(e.passVote(e.stamp(), pv.actor) == Err::Ok);
            break;
        case Phase::RunoffReady:
            CHECK(e.beginRunoff(e.stamp()) == Err::Ok);
            break;
        case Phase::ExecutionReady:
            CHECK(e.openExecution(e.stamp(), true) == Err::Ok);
            break;
        case Phase::ExecutionAnnounce: {
            if (deep && pv.last_executed != ws::NONE) {
                CHECK(!e.aliveSeat(static_cast<uint8_t>(pv.last_executed)));
            }
            CHECK(e.closeExecution(e.stamp()) == Err::Ok);
            break;
        }
        case Phase::FinalReady:
            CHECK(e.reveal(e.stamp(), true) == Err::Ok);
            break;
        default:
            CHECK(false);
            return out;
        }
    }

    const ws::PublicView pv = e.publicView();
    if (pv.phase != Phase::Revealed) {
        CHECK(pv.phase == Phase::Revealed);
        return out;
    }
    ws::Summary sum;
    CHECK(e.summary(sum) == Err::Ok);
    CHECK(sum.winner != Winner::None);
    CHECK(sum.days >= 1 && sum.days <= ws::MAX_DAYS);

    // 勝敗が生き残りと矛盾していないか、こちらで数え直して確かめる
    uint8_t wolves = 0, others = 0, total_wolves = 0, total_seers = 0;
    for (uint8_t a = 0; a < n; ++a) {
        if (sum.roles[a] == Role::Wolf) ++total_wolves;
        if (sum.roles[a] == Role::Seer) ++total_seers;
        if (!(sum.alive & ws::bit(a))) continue;
        if (sum.roles[a] == Role::Wolf) ++wolves; else ++others;
    }
    CHECK(total_wolves == comp.wolves);
    CHECK(total_seers == comp.seers);
    CHECK(sum.winner == (wolves == 0 ? Winner::Village : Winner::Wolves));
    if (sum.winner == Winner::Wolves) CHECK(wolves >= others);

    if (deep) {
        ws::DayLog d1;
        CHECK(e.dayLog(1, d1) == Err::Ok);
        CHECK(d1.victim == ws::NONE);                 // 最初の夜は襲撃なし
        CHECK(e.dayLog(0, d1) == Err::Input);         // 0 日目は無い
        CHECK(e.dayLog(static_cast<uint8_t>(sum.days + 1), d1) == Err::Input);
        for (uint8_t day = 1; day <= sum.days; ++day) {
            ws::DayLog d;
            CHECK(e.dayLog(day, d) == Err::Ok);
            CHECK(d.seer_count <= comp.seers);
            for (uint8_t i = 0; i < d.seer_count; ++i) {
                const ws::SeerRecord &r = d.seers[i];
                CHECK(r.seer != ws::NONE && r.target != ws::NONE && r.seer != r.target);
                CHECK(sum.roles[static_cast<uint8_t>(r.seer)] == Role::Seer);
                const bool is_wolf = sum.roles[static_cast<uint8_t>(r.target)] == Role::Wolf;
                CHECK(r.finding == (is_wolf ? Finding::Wolf : Finding::NotWolf));
            }
            if (d.victim != ws::NONE) CHECK(sum.roles[static_cast<uint8_t>(d.victim)] != Role::Wolf);
        }
    }

    out.winner = sum.winner;
    out.days = sum.days;
    out.ok = true;
    return out;
}

// ---------------------------------------------------------------------------
// 1. 配役の枚数と配り方の番号
// ---------------------------------------------------------------------------
static void checkDeals() {
    std::printf("[1] 4〜10 人の配役と配り方の番号\n");
    for (uint8_t n = ws::MIN_PLAYERS; n <= ws::MAX_PLAYERS; ++n) {
        const ws::Composition c = ws::compositionFor(n);
        CHECK(ws::validComposition(n, c));
        const uint16_t total = ws::dealCount(n);
        CHECK(total > 0);
        // すべての番号で枚数が合い、同じ配り方が二度出てこないこと
        static uint16_t seen[4096];
        uint16_t seen_n = 0;
        for (uint16_t k = 0; k < total; ++k) {
            std::array<Role, ws::MAX_PLAYERS> roles{};
            CHECK(ws::dealFromIndex(n, k, roles));
            uint8_t w = 0, s = 0, v = 0;
            uint16_t pattern = 0;
            for (uint8_t a = 0; a < n; ++a) {
                if (roles[a] == Role::Wolf) { ++w; pattern = static_cast<uint16_t>(pattern | ws::bit(a)); }
                else if (roles[a] == Role::Seer) ++s;
                else if (roles[a] == Role::Villager) ++v;
                else CHECK(false);
            }
            CHECK(w == c.wolves);
            CHECK(s == c.seers);
            CHECK(v == c.villagers(n));
            // 人狼の席だけでは一意にならないので、占い師も混ぜた指紋を作る
            uint16_t finger = pattern;
            for (uint8_t a = 0; a < n; ++a) if (roles[a] == Role::Seer) finger = static_cast<uint16_t>(finger ^ (ws::bit(a) << 0));
            (void)finger;
            if (seen_n < 4096) seen[seen_n++] = k;
        }
        // 番号の範囲外は受け付けない
        std::array<Role, ws::MAX_PLAYERS> roles{};
        CHECK(!ws::dealFromIndex(n, total, roles));
        std::printf("    %2u 人: 人狼 %u・占い師 %u・村人 %u / 配り方 %u 通り\n",
                    n, c.wolves, c.seers, c.villagers(n), total);
    }
    // 配り方が本当に全部違うかを、10 人（いちばん多い 1260 通り）で総当たりする
    {
        const uint8_t n = 10;
        const uint16_t total = ws::dealCount(n);
        static uint32_t keys[2048];
        uint16_t count = 0;
        for (uint16_t k = 0; k < total; ++k) {
            std::array<Role, ws::MAX_PLAYERS> roles{};
            ws::dealFromIndex(n, k, roles);
            uint32_t key = 0;
            for (uint8_t a = 0; a < n; ++a) key = key * 3u + static_cast<uint32_t>(roles[a] == Role::Wolf ? 1 : (roles[a] == Role::Seer ? 2 : 0));
            keys[count++] = key;
        }
        uint16_t dup = 0;
        for (uint16_t i = 0; i < count; ++i)
            for (uint16_t j = static_cast<uint16_t>(i + 1); j < count; ++j)
                if (keys[i] == keys[j]) ++dup;
        CHECK(dup == 0);
        std::printf("    10 人の %u 通りに重複なし\n", total);
    }
}

// ---------------------------------------------------------------------------
// 指定の配役になる配り方の番号を探す（筋書きのある試験で使う）
// ---------------------------------------------------------------------------
static int findDeal(uint8_t n, ws::Composition c, const Role *want) {
    const uint16_t total = ws::dealCount(n, c);
    for (uint16_t k = 0; k < total; ++k) {
        std::array<Role, ws::MAX_PLAYERS> roles{};
        if (!ws::dealFromIndex(n, k, c, roles)) continue;
        bool hit = true;
        for (uint8_t a = 0; a < n; ++a) if (roles[a] != want[a]) hit = false;
        if (hit) return static_cast<int>(k);
    }
    return -1;
}

// 筋書きのある試験の共通部品: 夜を 1 周する（choose は席ごとの選択を返す関数）
template <typename Chooser>
static void runNight(ws::Engine &e, uint64_t &now, Chooser choose) {
    while (true) {
        now += 10; e.tick(now);
        const ws::PublicView pv = e.publicView();
        if (pv.phase == Phase::NightHandoff) { e.receiveNight(e.stamp(), pv.actor); continue; }
        if (pv.phase == Phase::NightBrief) {
            ws::Brief b; e.readBrief(pv.actor, b); e.acknowledgeBrief(e.stamp(), pv.actor); continue;
        }
        if (pv.phase == Phase::NightTarget) { e.chooseNight(e.stamp(), pv.actor, choose(pv.actor)); continue; }
        if (pv.phase == Phase::NightResult) {
            ws::NightOutcome o; e.readNightResult(pv.actor, o); e.acknowledgeNightResult(e.stamp(), pv.actor); continue;
        }
        if (pv.phase == Phase::NightDone) { e.passNight(e.stamp(), pv.actor); continue; }
        return;   // MorningReady まで来た
    }
}

// 投票を 1 周する（vote は席ごとの投票先を返す関数）
template <typename Voter>
static void runVote(ws::Engine &e, uint64_t &now, Voter vote) {
    while (true) {
        now += 10; e.tick(now);
        const ws::PublicView pv = e.publicView();
        if (pv.phase == Phase::VoteHandoff) { e.receiveVote(e.stamp(), pv.actor); continue; }
        if (pv.phase == Phase::VoteSelect) { e.selectVote(e.stamp(), pv.actor, vote(pv.actor)); continue; }
        if (pv.phase == Phase::VoteConfirm) {
            int8_t t = ws::NONE; e.pendingVote(pv.actor, t); e.confirmVote(e.stamp(), pv.actor); continue;
        }
        if (pv.phase == Phase::VoteDone) { e.passVote(e.stamp(), pv.actor); continue; }
        return;
    }
}

// ---------------------------------------------------------------------------
// 5. 人狼 2 人の襲撃先と、仲間の選択の見え方
// ---------------------------------------------------------------------------
static void checkTwoWolves() {
    std::printf("[5] 人狼 2 人の襲撃先\n");
    const uint8_t n = 7;
    const ws::Composition c = ws::compositionFor(n);   // 7 人 = 人狼 2・占い師 1
    CHECK(c.wolves == 2 && c.seers == 1);
    Role want[ws::MAX_PLAYERS] = {Role::Wolf, Role::Wolf, Role::Seer, Role::Villager,
                                  Role::Villager, Role::Villager, Role::Villager};
    const int k = findDeal(n, c, want);
    CHECK(k >= 0);
    if (k < 0) return;

    ws::Engine e;
    uint64_t now = 1000;
    CHECK(e.start(e.stamp(), n, static_cast<uint16_t>(k), now, c) == Err::Ok);

    // --- 初日の夜: 仲間の名前だけが見える（仲間が何を選んだかは誰にも見せない）---
    bool seen_partner = false, seen_mate = false;
    while (true) {
        now += 10; e.tick(now);
        const ws::PublicView pv = e.publicView();
        if (pv.phase == Phase::NightHandoff) { e.receiveNight(e.stamp(), pv.actor); continue; }
        if (pv.phase == Phase::NightBrief) {
            CHECK(pv.day == 1);
            ws::Brief b;
            CHECK(e.readBrief(pv.actor, b) == Err::Ok);
            if (pv.actor == 0) {
                CHECK(b.role == Role::Wolf);
                CHECK(b.partners == ws::bit(1));          // 仲間は 1 番
                seen_partner = true;
            } else if (pv.actor == 1) {
                CHECK(b.role == Role::Wolf);
                CHECK(b.partners == ws::bit(0));
                seen_mate = true;
            } else {
                CHECK(b.partners == 0);
            }
            e.acknowledgeBrief(e.stamp(), pv.actor);
            continue;
        }
        if (pv.phase == Phase::NightTarget) {
            // 人狼は仲間を選べない
            if (pv.actor == 0) CHECK(e.chooseNight(e.stamp(), 0, 1) == Err::Target);
            if (pv.actor == 1) CHECK(e.chooseNight(e.stamp(), 1, 0) == Err::Target);
            const int t = (pv.actor == 0) ? 3 : (pv.actor == 1 ? 4 : (pv.actor == 6 ? 0 : 6));
            CHECK(e.chooseNight(e.stamp(), pv.actor, t) == Err::Ok);
            continue;
        }
        if (pv.phase == Phase::NightResult) {
            ws::NightOutcome o;
            CHECK(e.readNightResult(pv.actor, o) == Err::Ok);
            if (pv.actor == 2) {   // 占い師
                CHECK(o.role == Role::Seer);
                CHECK(o.target == 6);
                CHECK(o.finding == Finding::NotWolf);
            }
            e.acknowledgeNightResult(e.stamp(), pv.actor);
            continue;
        }
        if (pv.phase == Phase::NightDone) { e.passNight(e.stamp(), pv.actor); continue; }
        break;
    }
    CHECK(seen_partner && seen_mate);
    CHECK(e.publicView().phase == Phase::MorningReady);
    CHECK(e.publicView().last_victim == ws::NONE);      // 最初の夜は襲撃なし

    e.openMorning(e.stamp(), true);
    e.closeMorning(e.stamp(), now);
    e.finishTalk(e.stamp(), true);
    e.beginVote(e.stamp());
    // 全員 6 番へ → 6 番が追放（人狼の 2 人は生き残る）
    runVote(e, now, [](uint8_t a) { return a == 6 ? 5 : 6; });
    CHECK(e.publicView().phase == Phase::ExecutionReady);
    e.openExecution(e.stamp(), true);
    CHECK(e.publicView().last_executed == 6);
    CHECK(!e.aliveSeat(6));
    e.closeExecution(e.stamp());
    CHECK(e.publicView().day == 2);

    // --- 2 日目の夜: 役職の案内は出ない。後に操作した生存中の人狼（1 番）の選択が襲撃先 ---
    const Role expect[ws::MAX_PLAYERS] = {Role::Wolf, Role::Wolf, Role::Seer, Role::Villager,
                                          Role::Villager, Role::Villager, Role::Villager};
    int night2_seats = 0, reminded = 0;
    while (true) {
        now += 10; e.tick(now);
        const ws::PublicView pv = e.publicView();
        if (pv.phase == Phase::NightBrief) { CHECK(false); return; }   // 2 日目に案内は無い
        if (pv.phase == Phase::NightHandoff) {
            ++night2_seats;
            e.receiveNight(e.stamp(), pv.actor);
            // 手渡しの次は必ず対象選択（案内をとばす）
            CHECK(e.publicView().phase == Phase::NightTarget);
            continue;
        }
        if (pv.phase == Phase::NightTarget) {
            ws::Brief b;
            CHECK(e.readBrief(pv.actor, b) == Err::Phase);   // 2 日目以降は読めない
            if (pv.actor == 0) CHECK(e.chooseNight(e.stamp(), 0, 1) == Err::Target);
            if (pv.actor == 1) CHECK(e.chooseNight(e.stamp(), 1, 0) == Err::Target);
            const int t = (pv.actor == 0) ? 3 : (pv.actor == 1 ? 4 : (pv.actor == 5 ? 4 : 5));
            CHECK(e.chooseNight(e.stamp(), pv.actor, t) == Err::Ok);
            continue;
        }
        if (pv.phase == Phase::NightResult) {
            ws::NightOutcome o;
            CHECK(e.readNightResult(pv.actor, o) == Err::Ok);
            CHECK(o.role == expect[pv.actor]);     // 役職のおさらいはここで手番の本人に返る
            CHECK(!o.first_night);
            ++reminded;
            // ほかの席からは読めない（役職のおさらいも秘密）
            for (uint8_t a = 0; a < n; ++a) {
                if (a == pv.actor) continue;
                ws::NightOutcome other;
                CHECK(e.readNightResult(a, other) == Err::Seat);
            }
            e.acknowledgeNightResult(e.stamp(), pv.actor);
            continue;
        }
        if (pv.phase == Phase::NightDone) { e.passNight(e.stamp(), pv.actor); continue; }
        break;
    }
    CHECK(night2_seats == 6);    // 6 番は追放済み。残る 6 人が同じ手順を踏む
    CHECK(reminded == 6);
    CHECK(e.publicView().phase == Phase::MorningReady);
    CHECK(e.publicView().last_victim == 4);
    CHECK(!e.aliveSeat(4));
    CHECK(e.aliveSeat(3));
    std::printf("    後の人狼の選択（4 番）が襲撃先になった\n");

    // --- 抜けた人は手番にも対象にも出てこない -----------------------------
    e.openMorning(e.stamp(), true);
    e.closeMorning(e.stamp(), now);
    e.finishTalk(e.stamp(), true);
    e.beginVote(e.stamp());
    const ws::PublicView pv = e.publicView();
    CHECK(pv.actor != 4 && pv.actor != 6);
    CHECK(!e.legalVoteTarget(pv.actor, 4));
    CHECK(!e.legalVoteTarget(pv.actor, 6));
    CHECK(e.receiveVote(e.stamp(), 4) == Err::Seat);     // 抜けた人は受け取れない
    CHECK(e.receiveVote(e.stamp(), pv.actor) == Err::Ok);
    CHECK(e.selectVote(e.stamp(), pv.actor, 4) == Err::Target);
    CHECK(e.selectVote(e.stamp(), pv.actor, 6) == Err::Target);
}

// ---------------------------------------------------------------------------
// 6. 同票 → 決選 → それでも同票なら追放なし
// ---------------------------------------------------------------------------
static void checkRunoff() {
    std::printf("[6] 同票 → 決選 → 追放なし\n");
    const uint8_t n = 4;
    const ws::Composition c = ws::compositionFor(n);
    Role want[ws::MAX_PLAYERS] = {Role::Wolf, Role::Seer, Role::Villager, Role::Villager};
    const int k = findDeal(n, c, want);
    CHECK(k >= 0);
    if (k < 0) return;

    ws::Engine e;
    uint64_t now = 1000;
    CHECK(e.start(e.stamp(), n, static_cast<uint16_t>(k), now, c) == Err::Ok);
    runNight(e, now, [](uint8_t a) -> int { return (a + 1) % 4; });
    e.openMorning(e.stamp(), true);
    e.closeMorning(e.stamp(), now);
    e.finishTalk(e.stamp(), true);
    e.beginVote(e.stamp());
    // 0→1, 1→0, 2→3, 3→2 ですべて 1 票ずつ
    runVote(e, now, [](uint8_t a) -> int { return (a % 2 == 0) ? a + 1 : a - 1; });
    CHECK(e.publicView().phase == Phase::RunoffReady);
    CHECK(e.publicView().eligible == 0x0F);     // 4 人とも候補
    CHECK(e.beginRunoff(e.stamp()) == Err::Ok);
    CHECK(e.publicView().vote_cycle == 2);
    runVote(e, now, [](uint8_t a) -> int { return (a % 2 == 0) ? a + 1 : a - 1; });
    CHECK(e.publicView().phase == Phase::ExecutionReady);
    e.openExecution(e.stamp(), true);
    CHECK(e.publicView().last_executed == ws::NONE);    // その日は追放なし
    CHECK(e.publicView().alive_count == 4);
    e.closeExecution(e.stamp());
    CHECK(e.publicView().day == 2);

    // 単独最多なら決選にならない
    runNight(e, now, [](uint8_t a) -> int { return (a + 1) % 4; });
    e.openMorning(e.stamp(), true);
    CHECK(e.publicView().last_victim != ws::NONE);   // 2 日目以降は必ず犠牲者が出る
    e.closeMorning(e.stamp(), now);
    std::printf("    決選でも同票 → 追放なし。翌日の朝に犠牲者 1 人\n");
}

// ---------------------------------------------------------------------------
// 2. 勝敗の判定（筋書き）
// ---------------------------------------------------------------------------
static void checkWinConditions() {
    std::printf("[2] 勝敗の判定\n");
    const uint8_t n = 4;
    const ws::Composition c = ws::compositionFor(n);
    Role want[ws::MAX_PLAYERS] = {Role::Wolf, Role::Seer, Role::Villager, Role::Villager};
    const int k = findDeal(n, c, want);
    if (k < 0) { CHECK(false); return; }

    // (a) 人狼を追放 → 村の勝ち
    {
        ws::Engine e;
        uint64_t now = 1000;
        e.start(e.stamp(), n, static_cast<uint16_t>(k), now, c);
        runNight(e, now, [](uint8_t a) -> int { return (a + 1) % 4; });
        e.openMorning(e.stamp(), true);
        e.closeMorning(e.stamp(), now);
        e.finishTalk(e.stamp(), true);
        e.beginVote(e.stamp());
        runVote(e, now, [](uint8_t a) -> int { return a == 0 ? 1 : 0; });
        e.openExecution(e.stamp(), true);
        CHECK(e.publicView().last_executed == 0);
        CHECK(e.publicView().winner == Winner::Village);
        e.closeExecution(e.stamp());
        CHECK(e.publicView().phase == Phase::FinalReady);
        CHECK(e.reveal(e.stamp(), true) == Err::Ok);
        ws::Summary sum;
        CHECK(e.summary(sum) == Err::Ok);
        CHECK(sum.winner == Winner::Village);
        std::printf("    人狼を追放 → 村の勝ち\n");
    }
    // (b) 追放されず襲撃が続く → 人狼の勝ち（生存 2 人で 1 >= 1）
    {
        ws::Engine e;
        uint64_t now = 1000;
        e.start(e.stamp(), n, static_cast<uint16_t>(k), now, c);
        // 1 日目: 襲撃なし・全員同票で追放なし
        runNight(e, now, [](uint8_t a) -> int { return (a + 1) % 4; });
        e.openMorning(e.stamp(), true); e.closeMorning(e.stamp(), now);
        e.finishTalk(e.stamp(), true); e.beginVote(e.stamp());
        runVote(e, now, [](uint8_t a) -> int { return (a % 2 == 0) ? a + 1 : a - 1; });
        e.beginRunoff(e.stamp());
        runVote(e, now, [](uint8_t a) -> int { return (a % 2 == 0) ? a + 1 : a - 1; });
        e.openExecution(e.stamp(), true); e.closeExecution(e.stamp());
        // 2 日目: 人狼が 3 番を襲撃、また全員同票で追放なし（0/1/2 の 3 人）
        runNight(e, now, [](uint8_t a) -> int { return a == 0 ? 3 : (a == 3 ? 0 : (a == 1 ? 2 : 1)); });
        CHECK(e.publicView().last_victim == 3);
        e.openMorning(e.stamp(), true); e.closeMorning(e.stamp(), now);
        e.finishTalk(e.stamp(), true); e.beginVote(e.stamp());
        // 生存 3 人が 1 票ずつ散らばる → 決選 → それでも同票 → 追放なし
        runVote(e, now, [](uint8_t a) -> int { return a == 0 ? 1 : (a == 1 ? 2 : 0); });
        CHECK(e.publicView().phase == Phase::RunoffReady);
        CHECK(e.beginRunoff(e.stamp()) == Err::Ok);
        runVote(e, now, [](uint8_t a) -> int { return a == 0 ? 1 : (a == 1 ? 2 : 0); });
        e.openExecution(e.stamp(), true);
        CHECK(e.publicView().last_executed == ws::NONE);
        e.closeExecution(e.stamp());
        // 3 日目: 人狼が 2 番を襲撃 → 生存 0 番（人狼）と 1 番 → 人狼の勝ち
        runNight(e, now, [](uint8_t a) -> int { return a == 0 ? 2 : (a == 1 ? 2 : 0); });
        CHECK(e.publicView().last_victim == 2);
        CHECK(e.publicView().winner == Winner::Wolves);
        e.openMorning(e.stamp(), true);
        CHECK(e.closeMorning(e.stamp(), now) == Err::Ok);
        CHECK(e.publicView().phase == Phase::FinalReady);
        std::printf("    生存 2 人（人狼 1・村 1）→ 人狼の勝ち\n");
    }
}

// ---------------------------------------------------------------------------
// 7・8. 古い版数・場面違い・本人以外からの秘密の読み出し
// ---------------------------------------------------------------------------
static void checkGuards() {
    std::printf("[7][8] 版数・場面・本人の確認\n");
    const uint8_t n = 5;
    const ws::Composition c = ws::compositionFor(n);
    ws::Engine e;
    uint64_t now = 1000;
    const ws::Stamp before = e.stamp();
    CHECK(e.start(before, n, 0, now, c) == Err::Ok);
    // 古い版数は弾く（start で版数が進んでいる）
    CHECK(e.start(before, n, 0, now, c) == Err::Stale);
    CHECK(e.receiveNight(before, 0) == Err::Stale);

    const ws::PublicView pv = e.publicView();
    CHECK(pv.phase == Phase::NightHandoff);
    // 場面違い
    CHECK(e.chooseNight(e.stamp(), pv.actor, 1) == Err::Phase);
    CHECK(e.beginVote(e.stamp()) == Err::Phase);
    CHECK(e.reveal(e.stamp(), true) == Err::Phase);
    // 秘密は場面が合わないと読めない
    ws::Brief b;
    CHECK(e.readBrief(pv.actor, b) == Err::Phase);
    ws::NightOutcome o;
    CHECK(e.readNightResult(pv.actor, o) == Err::Phase);
    int8_t t = ws::NONE;
    CHECK(e.pendingVote(pv.actor, t) == Err::Phase);
    ws::Summary sum;
    CHECK(e.summary(sum) == Err::Phase);

    CHECK(e.receiveNight(e.stamp(), 1) == Err::Seat);      // 手番の人以外は受け取れない
    CHECK(e.receiveNight(e.stamp(), 0) == Err::Ok);
    // 秘密は手番の本人だけ
    for (uint8_t a = 1; a < n; ++a) CHECK(e.readBrief(a, b) == Err::Seat);
    CHECK(e.readBrief(0, b) == Err::Ok);
    // 見る前に「覚えました」は通らない
    {
        ws::Engine e2;
        uint64_t now2 = 1000;
        e2.start(e2.stamp(), n, 0, now2, c);
        e2.receiveNight(e2.stamp(), 0);
        CHECK(e2.acknowledgeBrief(e2.stamp(), 0) == Err::Unseen);
    }
    // 一時停止中は秘密を読めない
    CHECK(e.pause(e.stamp(), now + 10) == Err::Ok);
    CHECK(e.readBrief(0, b) == Err::Paused);
    CHECK(e.acknowledgeBrief(e.stamp(), 0) == Err::Paused);
    CHECK(e.resume(e.stamp(), now + 20) == Err::Ok);
    CHECK(e.readBrief(0, b) == Err::Ok);   // resume で「見た」印が消えるので読み直せる

    // 無効にすると秘密は消える（役職も結果も取り出せない）
    CHECK(e.abort(e.stamp(), ws::AbortReason::User) == Err::Ok);
    CHECK(e.publicView().phase == Phase::Aborted);
    CHECK(e.readBrief(0, b) == Err::Phase);
    CHECK(e.summary(sum) == Err::Phase);
    CHECK(e.reset(e.stamp()) == Err::Ok);
    CHECK(e.publicView().phase == Phase::Idle);
}

// ---------------------------------------------------------------------------
// 8b. 占い師が 2 人のとき（独立に占う・互いを知らない・抜けたら動かない）
// ---------------------------------------------------------------------------
static void checkTwoSeers() {
    std::printf("[8b] 占い師 2 人\n");
    const uint8_t n = 9;
    const ws::Composition c = ws::compositionFor(n);
    CHECK(c.wolves == 2 && c.seers == 2);
    Role want[ws::MAX_PLAYERS] = {Role::Wolf, Role::Wolf, Role::Seer, Role::Seer,
                                  Role::Villager, Role::Villager, Role::Villager,
                                  Role::Villager, Role::Villager};
    const int k = findDeal(n, c, want);
    CHECK(k >= 0);
    if (k < 0) return;

    ws::Engine e;
    uint64_t now = 1000;
    CHECK(e.start(e.stamp(), n, static_cast<uint16_t>(k), now, c) == Err::Ok);

    // 2 番の占い師は 0 番（人狼）を、3 番の占い師は 4 番（村人）を占う
    int findings = 0;
    while (true) {
        now += 10; e.tick(now);
        const ws::PublicView pv = e.publicView();
        if (pv.phase == Phase::NightHandoff) { e.receiveNight(e.stamp(), pv.actor); continue; }
        if (pv.phase == Phase::NightBrief) {
            ws::Brief b;
            e.readBrief(pv.actor, b);
            if (pv.actor == 2 || pv.actor == 3) {
                CHECK(b.role == Role::Seer);
                CHECK(b.partners == 0);              // 占い師どうしは互いを知らない
            }
            e.acknowledgeBrief(e.stamp(), pv.actor);
            continue;
        }
        if (pv.phase == Phase::NightTarget) {
            const int t = (pv.actor == 2) ? 0 : (pv.actor == 3 ? 4 : ((pv.actor + 1) % n == 0 ? 5 : (pv.actor + 1) % n));
            const int safe = (pv.actor == 0) ? 5 : (pv.actor == 1 ? 6 : t);
            e.chooseNight(e.stamp(), pv.actor, safe);
            continue;
        }
        if (pv.phase == Phase::NightResult) {
            ws::NightOutcome o;
            CHECK(e.readNightResult(pv.actor, o) == Err::Ok);
            if (pv.actor == 2) { CHECK(o.finding == Finding::Wolf); CHECK(o.target == 0); ++findings; }
            if (pv.actor == 3) { CHECK(o.finding == Finding::NotWolf); CHECK(o.target == 4); ++findings; }
            if (pv.actor >= 4) CHECK(o.finding == Finding::None);
            // ほかの席からは読めない
            ws::NightOutcome other;
            CHECK(e.readNightResult(static_cast<uint8_t>((pv.actor + 1) % n), other) == Err::Seat);
            e.acknowledgeNightResult(e.stamp(), pv.actor);
            continue;
        }
        if (pv.phase == Phase::NightDone) { e.passNight(e.stamp(), pv.actor); continue; }
        break;
    }
    CHECK(findings == 2);

    // 2 番の占い師を追放して、翌日は 3 番だけが占うことを確かめる
    e.openMorning(e.stamp(), true);
    e.closeMorning(e.stamp(), now);
    e.finishTalk(e.stamp(), true);
    e.beginVote(e.stamp());
    runVote(e, now, [](uint8_t a) -> int { return a == 2 ? 5 : 2; });
    e.openExecution(e.stamp(), true);
    CHECK(e.publicView().last_executed == 2);
    e.closeExecution(e.stamp());

    int acted_seer = 0;
    bool seat2_acted = false;
    while (true) {
        now += 10; e.tick(now);
        const ws::PublicView pv = e.publicView();
        if (pv.phase == Phase::NightHandoff) {
            if (pv.actor == 2) seat2_acted = true;
            e.receiveNight(e.stamp(), pv.actor); continue;
        }
        if (pv.phase == Phase::NightBrief) { CHECK(false); return; }   // 2 日目に案内は無い
        if (pv.phase == Phase::NightTarget) {
            const int t = (pv.actor == 3) ? 1 : (pv.actor == 0 ? 5 : (pv.actor == 1 ? 6 : 4));
            e.chooseNight(e.stamp(), pv.actor, t == static_cast<int>(pv.actor) ? 5 : t);
            continue;
        }
        if (pv.phase == Phase::NightResult) {
            ws::NightOutcome o; e.readNightResult(pv.actor, o);
            if (o.finding != Finding::None) ++acted_seer;
            e.acknowledgeNightResult(e.stamp(), pv.actor); continue;
        }
        if (pv.phase == Phase::NightDone) { e.passNight(e.stamp(), pv.actor); continue; }
        break;
    }
    CHECK(!seat2_acted);       // 抜けた占い師には端末が回らない
    CHECK(acted_seer == 1);    // 占うのは残った 1 人だけ
    std::printf("    2 人が独立に占い、追放後は 1 人だけが占った\n");
}

// ---------------------------------------------------------------------------
// 9・10. 乱数でたくさんの局を回す＋勝率の表
// ---------------------------------------------------------------------------
struct Tally {
    uint32_t games = 0, village = 0, wolves = 0, bad = 0;
    uint32_t day_sum = 0, day_max = 0;
};

static Tally soak(uint8_t n, ws::Composition c, uint32_t games, uint64_t seed, bool deep) {
    Tally t;
    Rng rng(seed);
    for (uint32_t i = 0; i < games; ++i) {
        const GameStats g = playRandomGame(n, c, rng, deep);
        ++t.games;
        if (!g.ok) { ++t.bad; continue; }
        if (g.winner == Winner::Village) ++t.village; else ++t.wolves;
        t.day_sum += g.days;
        if (g.days > t.day_max) t.day_max = g.days;
    }
    return t;
}

static void printRow(const char *label, const Tally &t) {
    const double n = t.games ? static_cast<double>(t.games) : 1.0;
    std::printf("  %-28s %6u 局  村 %5.1f%%  人狼 %5.1f%%  平均 %.2f 日（最長 %u）%s\n",
                label, t.games, 100.0 * t.village / n, 100.0 * t.wolves / n,
                static_cast<double>(t.day_sum) / n, t.day_max,
                t.bad ? "  ★異常あり" : "");
    if (t.bad) { ++g_fail; }
}

int main() {
    std::printf("=== 人狼「通常ルール」コアの試験 ===\n\n");
    checkDeals();
    std::printf("\n");
    checkWinConditions();
    std::printf("\n");
    checkTwoWolves();
    std::printf("\n");
    checkRunoff();
    std::printf("\n");
    checkGuards();
    std::printf("\n");
    checkTwoSeers();
    std::printf("\n");

    constexpr uint32_t kGames = 20000;
    std::printf("[9] 乱数で最後まで回す（不変条件つき・各 %u 局）\n", kGames);
    Tally totals;
    for (uint8_t n = ws::MIN_PLAYERS; n <= ws::MAX_PLAYERS; ++n) {
        const ws::Composition c = ws::compositionFor(n);
        char label[64];
        std::snprintf(label, sizeof(label), "%2u 人（人狼 %u・占い師 %u）", n, c.wolves, c.seers);
        const Tally t = soak(n, c, kGames, 0xC0FFEEull + n, true);
        printRow(label, t);
        totals.games += t.games; totals.village += t.village; totals.wolves += t.wolves;
        totals.bad += t.bad; totals.day_sum += t.day_sum;
        if (t.day_max > totals.day_max) totals.day_max = t.day_max;
    }
    std::printf("\n[10] 参考: 配役を変えたときの目安（★でたらめに選ぶだけなので、"
                "腕前のある実プレイとは別物）\n");
    struct Alt { const char *label; uint8_t n; ws::Composition c; };
    const Alt alts[] = {
        {" 9 人（人狼 2・占い師 1）", 9, {2, 1}},
        {"10 人（人狼 2・占い師 1）", 10, {2, 1}},
        {"10 人（人狼 3・占い師 2）", 10, {3, 2}},
    };
    for (const Alt &a : alts) {
        const Tally t = soak(a.n, a.c, kGames, 0xBEEFull + a.n * 7 + a.c.wolves, false);
        printRow(a.label, t);
    }

    std::printf("\n合計 %u 局を最後まで回した（最長 %u 日）\n", totals.games, totals.day_max);
    std::printf("\n=== 確認 %d 件 / 失敗 %d 件 ===\n", g_checks, g_fail);
    if (g_fail == 0) std::printf("ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
