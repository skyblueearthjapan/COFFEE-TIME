// AI DUEL の純粋ロジック（games/duel/core/duel_core.hpp）の PC 上の試験。
//
// 実機は要らない。tools/run_duel_checks.py が zig を呼んでこれを組み立て、実行する:
//   python tools/run_duel_checks.py
//
// 確かめること:
//   1. 勝敗表が設計書 1.2 のとおり（9 通り全部）と、確率の検証・正規化（設計書 6.2）
//   2. 設計書 5.4 の「最多手に勝つだけでは最善にならない例」（test_vectors.json の
//      greedy_counterexample）を、期待得点差の 1 ビットまで再現すること
//   3. **参考実装 duel_reference.py が作った正解データと 1e-9 以内で一致**
//      （各ラウンド前の統計 AI の確率・最善手の集合・1 局ぶんの統計まるごと）
//   4. NVS へ入れる塊の書き出し／読み戻しが完全に往復すること（壊すと弾くこと）と、
//      版 1（488B・相手ごとの勝敗が無い）の記録が消えずに版 2（512B）へ引き継がれること
//   5. 癖のカード（設計書 4.5）の境目 19/20 件・59%/60%、優先順・最大 3 枚・重複除去
//   6. Jev へ渡す状態の組み立て（直近 12 件・対戦の境目・連続回数・条件つき集計）
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../firmware/src/app/games/duel/core/duel_core.hpp"
#include "duel_golden.inc"

namespace d = coffee::duel;

using d::Hand;
using d::HabitCard;
using d::HabitId;
using d::Probs;
using d::Provider;
using d::Result;
using d::Stats;

// ---------------------------------------------------------------------------
// 試験の道具
// ---------------------------------------------------------------------------
static int g_fail = 0;
static int g_checks = 0;
static int g_printed = 0;

static void report(bool ok, const char *what, int line)
{
    ++g_checks;
    if (ok) return;
    ++g_fail;
    if (g_printed < 40) {
        std::printf("  FAIL (line %d): %s\n", line, what);
        ++g_printed;
    }
}
#define CHECK(cond) report((cond), #cond, __LINE__)

static void note(const char *fmt, ...)
{
    if (g_printed >= 40) return;
    va_list ap;
    va_start(ap, fmt);
    std::printf("  ");
    std::vprintf(fmt, ap);
    va_end(ap);
    ++g_printed;
}

static constexpr double kTol = 1e-9;

static bool near(double a, double b)
{
    return std::fabs(a - b) <= kTol;
}

static const char *handName(uint8_t h)
{
    static const char *kNames[] = {"ROCK", "SCISSORS", "PAPER"};
    return h < 3 ? kNames[h] : "?";
}

// ---------------------------------------------------------------------------
// 1. 勝敗表と確率の検証
// ---------------------------------------------------------------------------
static void checkRules()
{
    std::printf("[1] 勝敗表（設計書 1.2）と確率の検証（設計書 6.2）\n");

    // 表のとおり（行 = あなた、列 = 相手）
    static const uint8_t kTable[3][3] = {
        // 相手 R          S                 P
        {(uint8_t)Result::Draw,     (uint8_t)Result::HumanWin, (uint8_t)Result::AiWin},   // あなた R
        {(uint8_t)Result::AiWin,    (uint8_t)Result::Draw,     (uint8_t)Result::HumanWin},// あなた S
        {(uint8_t)Result::HumanWin, (uint8_t)Result::AiWin,    (uint8_t)Result::Draw},    // あなた P
    };
    for (uint8_t human = 0; human < 3; ++human) {
        for (uint8_t ai = 0; ai < 3; ++ai) {
            CHECK((uint8_t)d::resultOf((Hand)human, (Hand)ai) == kTable[human][ai]);
        }
    }
    CHECK(d::beats(Hand::Rock) == Hand::Scissors);
    CHECK(d::beats(Hand::Scissors) == Hand::Paper);
    CHECK(d::beats(Hand::Paper) == Hand::Rock);

    Probs out;
    // 合計が 1 から 0.001 を超えて離れていたら拒む
    CHECK(!d::normalizeProbabilities(Probs{{0.5, 0.5, 0.5}}, out));
    CHECK(!d::normalizeProbabilities(Probs{{0.0, 0.0, 0.0}}, out));
    CHECK(!d::normalizeProbabilities(Probs{{-0.1, 0.6, 0.5}}, out));
    CHECK(!d::normalizeProbabilities(Probs{{1.5, -0.5, 0.0}}, out));
    Probs nan_case{{0.5, 0.5, 0.0}};
    nan_case.p[2] = std::nan("");
    CHECK(!d::normalizeProbabilities(nan_case, out));
    Probs inf_case{{0.5, 0.5, 0.0}};
    inf_case.p[2] = HUGE_VAL;
    CHECK(!d::normalizeProbabilities(inf_case, out));

    // 0.001 以内のずれは受け入れて割り直す
    CHECK(d::normalizeProbabilities(Probs{{0.3336, 0.3332, 0.3337}}, out));
    CHECK(near(out.p[0] + out.p[1] + out.p[2], 1.0));
    CHECK(d::normalizeProbabilities(Probs::uniform(), out));
    CHECK(near(out.p[0], 1.0 / 3.0));

    std::printf("  9 通りの勝敗と、確率の 3 キー・有限・0〜1・合計 1±0.001 を確かめた\n");
}

// ---------------------------------------------------------------------------
// 2. 設計書 5.4 の反例（test_vectors.json の greedy_counterexample）
// ---------------------------------------------------------------------------
static void checkGreedyCounterexample()
{
    std::printf("[2] 「最多手に勝つだけでは最善にならない」例（test_vectors.json）\n");

    Probs p;
    for (size_t i = 0; i < 3; ++i) {
        p.p[i] = kVectorProbabilities[i];
    }
    Probs u;
    CHECK(d::expectedMargins(p, u));
    for (size_t i = 0; i < 3; ++i) {
        // 手本の値は Python の double をそのまま 16 進浮動小数で埋め込んである。
        // 足す順序まで同じなので、ここは 1e-9 ではなく**完全一致**を求める
        CHECK(u.p[i] == kVectorExpectedMargins[i]);
    }
    const uint8_t mask = d::optimalActions(p);
    CHECK(mask == (uint8_t)(1u << kVectorBest));
    // 最多の予測手（ROCK .40）に勝つ手は PAPER だが、最善は ROCK
    CHECK(kVectorBest == (uint8_t)Hand::Rock);
    CHECK((mask & (1u << (uint8_t)Hand::Paper)) == 0);

    // 同率のときは候補が複数になり、選び方は等確率
    const uint8_t all = d::optimalActions(Probs::uniform());
    CHECK(all == 0x07);
    uint32_t seen[3] = {0, 0, 0};
    for (uint32_t r = 0; r < 300; ++r) {
        ++seen[(size_t)d::pickFromActions(all, r)];
    }
    CHECK(seen[0] == 100 && seen[1] == 100 && seen[2] == 100);

    note("U(ROCK)=%.17g U(SCISSORS)=%.17g U(PAPER)=%.17g -> %s\n",
         u.p[0], u.p[1], u.p[2], handName(kVectorBest));
}

// ---------------------------------------------------------------------------
// 3. 参考実装との突き合わせ
// ---------------------------------------------------------------------------
static bool sameStats(const Stats &s, const GoldenStats &g)
{
    bool ok = true;
    auto eq = [&](bool c) { if (!c) ok = false; };
    eq(s.rounds == g.rounds);
    for (size_t i = 0; i < 3; ++i) {
        eq(s.hand_counts[i] == g.hand_counts[i]);
        eq(s.outcome_counts[i] == g.outcome_counts[i]);
        eq(s.first_hand_counts[i] == g.first_hand_counts[i]);
    }
    for (size_t a = 0; a < 3; ++a) {
        for (size_t b = 0; b < 3; ++b) {
            eq(s.transition_by_hand[a][b] == g.transition_by_hand[a * 3 + b]);
        }
    }
    for (size_t a = 0; a < 3; ++a) {
        for (size_t b = 0; b < 3; ++b) {
            for (size_t c = 0; c < 3; ++c) {
                eq(s.transition_by_hand_result[a][b][c] ==
                   g.transition_by_hand_result[(a * 3 + b) * 3 + c]);
            }
        }
    }
    for (size_t a = 0; a < 3; ++a) {
        for (size_t b = 0; b < 2; ++b) {
            eq(s.repeat_after_result[a][b] == g.repeat_after_result[a * 2 + b]);
        }
    }
    eq(s.tail_count == g.tail_count);
    for (uint8_t i = 0; i < g.tail_count && i < s.tail_count; ++i) {
        eq(s.tail[i].match_id == g.tail[i].match_id);
        eq(s.tail[i].round_no == g.tail[i].round_no);
        eq((uint8_t)s.tail[i].player_hand == g.tail[i].player_hand);
        eq((uint8_t)s.tail[i].ai_hand == g.tail[i].ai_hand);
        eq((uint8_t)s.tail[i].player_result == g.tail[i].player_result);
    }
    for (size_t i = 0; i < 2; ++i) {
        eq(s.prediction[i].n == g.pred_n[i]);
        eq(s.prediction[i].hits == g.pred_hits[i]);
        eq(near(s.prediction[i].brier_sum, g.pred_brier[i]));
        eq(near(s.prediction[i].nll_sum, g.pred_nll[i]));
    }
    eq(s.match_counts.completed == g.match_counts[0]);
    eq(s.match_counts.human_win == g.match_counts[1]);
    eq(s.match_counts.ai_win == g.match_counts[2]);
    eq(s.match_counts.draw == g.match_counts[3]);
    eq(s.match_counts.aborted == g.match_counts[4]);
    return ok;
}

static Stats g_final[kGoldenCaseCount];

static void checkGolden()
{
    std::printf("[3] 参考実装の正解データ %d 局 / %d ラウンドとの突き合わせ\n",
                kGoldenCaseCount, kGoldenRoundCount);

    uint32_t worst_line = 0;
    double worst = 0.0;

    for (int ci = 0; ci < kGoldenCaseCount; ++ci) {
        const GoldenCase &c = kGoldenCases[ci];
        Stats s;
        int step = 0;

        for (int mi = 0; mi < c.match_count; ++mi) {
            const GoldenMatch &m = c.matches[mi];
            uint8_t human_wins = 0, ai_wins = 0;

            for (uint8_t round_no = 1; round_no <= m.rounds; ++round_no) {
                const GoldenStep &g = c.steps[step++];
                CHECK(g.match_id == m.match_id && g.round_no == round_no);

                // (a) そのラウンドを出す前の統計 AI の確率
                Probs baseline;
                CHECK(d::statsPrediction(s, m.match_id, round_no, baseline));
                for (size_t i = 0; i < 3; ++i) {
                    const double diff = std::fabs(baseline.p[i] - g.baseline[i]);
                    if (diff > worst) {
                        worst = diff;
                        worst_line = (uint32_t)(ci * 1000 + step);
                    }
                    CHECK(diff <= kTol);
                }
                CHECK(d::optimalActions(baseline) == g.baseline_optimal);

                // (b) 実際に採用した確率からの最善手の集合と、選ばれた AI の手
                Probs used;
                for (size_t i = 0; i < 3; ++i) {
                    used.p[i] = g.probabilities[i];
                }
                const uint8_t mask = d::optimalActions(used);
                CHECK(mask == g.optimal);
                CHECK((mask & (1u << g.ai_hand)) != 0);
                CHECK((uint8_t)d::resultOf((Hand)g.player_hand, (Hand)g.ai_hand) ==
                      g.player_result);

                // (c) 1 ラウンド反映
                d::ResolvedRound r;
                r.match_id = m.match_id;
                r.round_no = round_no;
                r.player_hand = (Hand)g.player_hand;
                r.ai_hand = (Hand)g.ai_hand;
                r.player_result = (Result)g.player_result;
                r.provider_used = (Provider)g.provider;
                r.probabilities = used;
                CHECK(d::appendResolved(s, r));
                // 同じラウンドを二度渡しても増えない（設計書 4.4 の 1）
                CHECK(!d::appendResolved(s, r));

                if (g.player_result == (uint8_t)Result::HumanWin) ++human_wins;
                if (g.player_result == (uint8_t)Result::AiWin) ++ai_wins;
            }

            if (m.rounds == d::kRoundsPerMatch) {
                d::noteMatchCompleted(s, d::matchWinner(human_wins, ai_wins));
            } else {
                d::noteMatchAborted(s);
            }
        }
        CHECK(step == c.step_count);
        if (!sameStats(s, c.final_stats)) {
            note("局 %d の統計が手本と違う（rounds=%u 手本=%u）\n", ci, s.rounds,
                 c.final_stats.rounds);
            ++g_fail;
        }
        g_final[ci] = s;
    }
    std::printf("  確率の最大のずれ %.3g（許容 %.0e・場所 %u）\n", worst, kTol, worst_line);
    std::printf("  統計・遷移・予測評価・対戦の数え方まで手本と一致\n");
}

// 勝敗の食い違い・範囲外は受け付けない
static void checkAppendGuards()
{
    std::printf("[3b] 受け付けない入力\n");
    Stats s;
    d::ResolvedRound r;
    r.match_id = 1;
    r.round_no = 1;
    r.player_hand = Hand::Rock;
    r.ai_hand = Hand::Paper;
    r.player_result = Result::HumanWin;    // 本当は ai_win
    r.probabilities = Probs::uniform();
    CHECK(!d::appendResolved(s, r));
    r.player_result = Result::AiWin;
    CHECK(d::appendResolved(s, r));
    r.round_no = 0;
    CHECK(!d::appendResolved(s, r));
    r.round_no = 11;
    CHECK(!d::appendResolved(s, r));
    r.round_no = 2;
    r.probabilities = Probs{{0.9, 0.9, 0.9}};
    CHECK(!d::appendResolved(s, r));
    CHECK(s.rounds == 1);

    // tail は 50 件で頭打ちになり、古いものから捨てられる
    Stats t;
    for (uint16_t match = 1; match <= 12; ++match) {
        for (uint8_t round_no = 1; round_no <= 10; ++round_no) {
            d::ResolvedRound x;
            x.match_id = match;
            x.round_no = round_no;
            x.player_hand = (Hand)(round_no % 3);
            x.ai_hand = Hand::Rock;
            x.player_result = d::resultOf(x.player_hand, x.ai_hand);
            x.probabilities = Probs::uniform();
            CHECK(d::appendResolved(t, x));
        }
    }
    CHECK(t.rounds == 120);
    CHECK(t.tail_count == d::kTailCapacity);
    CHECK(t.tail[d::kTailCapacity - 1].match_id == 12);
    CHECK(t.tail[d::kTailCapacity - 1].round_no == 10);
    CHECK(t.tail[0].match_id == 8);       // 120 - 50 = 70 件目の次 = 8 局目の 1 回目
    CHECK(t.tail[0].round_no == 1);
    std::printf("  食い違い・範囲外・重複を弾き、tail は 50 件で回る\n");
}

// ---------------------------------------------------------------------------
// 4. NVS の塊の往復
// ---------------------------------------------------------------------------
static bool sameStatsExact(const Stats &a, const Stats &b)
{
    if (std::memcmp(&a.rounds, &b.rounds, sizeof(a.rounds)) != 0) return false;
    if (std::memcmp(a.hand_counts, b.hand_counts, sizeof(a.hand_counts)) != 0) return false;
    if (std::memcmp(a.outcome_counts, b.outcome_counts, sizeof(a.outcome_counts)) != 0) return false;
    if (std::memcmp(a.outcome_by_provider, b.outcome_by_provider,
                    sizeof(a.outcome_by_provider)) != 0) return false;
    if (std::memcmp(a.first_hand_counts, b.first_hand_counts, sizeof(a.first_hand_counts)) != 0) return false;
    if (std::memcmp(a.transition_by_hand, b.transition_by_hand, sizeof(a.transition_by_hand)) != 0) return false;
    if (std::memcmp(a.transition_by_hand_result, b.transition_by_hand_result,
                    sizeof(a.transition_by_hand_result)) != 0) return false;
    if (std::memcmp(a.repeat_after_result, b.repeat_after_result, sizeof(a.repeat_after_result)) != 0) return false;
    if (a.tail_count != b.tail_count) return false;
    for (uint8_t i = 0; i < a.tail_count; ++i) {
        if (a.tail[i].match_id != b.tail[i].match_id) return false;
        if (a.tail[i].round_no != b.tail[i].round_no) return false;
        if (a.tail[i].player_hand != b.tail[i].player_hand) return false;
        if (a.tail[i].ai_hand != b.tail[i].ai_hand) return false;
        if (a.tail[i].player_result != b.tail[i].player_result) return false;
    }
    if (a.has_last != b.has_last || a.last_match_id != b.last_match_id ||
        a.last_round_no != b.last_round_no) return false;
    for (size_t i = 0; i < 2; ++i) {
        if (a.prediction[i].n != b.prediction[i].n) return false;
        if (a.prediction[i].hits != b.prediction[i].hits) return false;
        // double は 1 ビットまで同じであること（保存で丸めない）
        if (a.prediction[i].brier_sum != b.prediction[i].brier_sum) return false;
        if (a.prediction[i].nll_sum != b.prediction[i].nll_sum) return false;
    }
    if (std::memcmp(&a.match_counts, &b.match_counts, sizeof(a.match_counts)) != 0) return false;
    return true;
}

// 版 2 の塊から、同じ中身の版 1 の塊（末尾の相手別勝敗が無いもの）を作る。
// 先頭 484 バイトの並びが同じなので、版番号と CRC を入れ直すだけでよい
static void makeV1Blob(const Stats &s, uint8_t *out)
{
    uint8_t v2[d::kBlobBytes];
    d::encodeStats(s, v2);
    std::memcpy(out, v2, d::kBlobCommonBytes);
    out[0] = 1;
    d::detail::put32(out + d::kBlobCommonBytes,
                     d::detail::crc32(out, d::kBlobCommonBytes));
}

static void checkSerialization()
{
    std::printf("[4] NVS へ入れる塊（版 2 = %u バイト / 版 1 = %u バイト）の往復と引き継ぎ\n",
                (unsigned)d::kBlobBytes, (unsigned)d::kBlobBytesV1);

    uint8_t blob[d::kBlobBytes];
    // 空の統計
    Stats empty;
    d::encodeStats(empty, blob);
    Stats back;
    CHECK(blob[0] == 2);                // 書き出しは必ず版 2
    CHECK(d::decodeStats(blob, d::kBlobBytes, back));
    CHECK(sameStatsExact(empty, back));

    for (int ci = 0; ci < kGoldenCaseCount; ++ci) {
        d::encodeStats(g_final[ci], blob);
        Stats out;
        CHECK(d::decodeStats(blob, d::kBlobBytes, out));
        if (!sameStatsExact(g_final[ci], out)) {
            note("局 %d の塊が往復しない\n", ci);
            ++g_fail;
        }
        // 読み戻したものから作った確率も同じであること
        Probs a, b;
        CHECK(d::statsPrediction(g_final[ci], 9999, 1, a));
        CHECK(d::statsPrediction(out, 9999, 1, b));
        for (size_t i = 0; i < 3; ++i) {
            CHECK(a.p[i] == b.p[i]);
        }
    }

    // --- 版 1 → 版 2 の引き継ぎ（**記録を消さない**ことがいちばん大事） ---------
    int migrated = 0;
    for (int ci = 0; ci < kGoldenCaseCount; ++ci) {
        uint8_t v1[d::kBlobBytesV1];
        makeV1Blob(g_final[ci], v1);
        CHECK(v1[0] == 1);

        Stats old;
        CHECK(d::decodeStats(v1, d::kBlobBytesV1, old));
        // 相手ごとの勝敗だけが 0 で、ほかは 1 ビットまで同じであること
        Stats want = g_final[ci];
        std::memset(want.outcome_by_provider, 0, sizeof(want.outcome_by_provider));
        if (!sameStatsExact(want, old)) {
            note("局 %d の版 1 の引き継ぎで古い項目が変わってしまう\n", ci);
            ++g_fail;
        }
        uint32_t sum = 0;
        for (size_t p = 0; p < 2; ++p) {
            for (size_t i = 0; i < 3; ++i) {
                sum += old.outcome_by_provider[p][i];
            }
        }
        CHECK(sum == 0);
        CHECK(old.rounds == g_final[ci].rounds);    // ラウンド数は消えない

        // 次の保存で版 2 になり、そのあとは完全に往復する
        uint8_t v2[d::kBlobBytes];
        d::encodeStats(old, v2);
        CHECK(v2[0] == 2);
        Stats again;
        CHECK(d::decodeStats(v2, d::kBlobBytes, again));
        CHECK(sameStatsExact(old, again));
        ++migrated;
    }

    // 壊したら弾く
    d::encodeStats(g_final[0], blob);
    blob[20] = (uint8_t)(blob[20] ^ 0xFFu);
    Stats broken;
    CHECK(!d::decodeStats(blob, d::kBlobBytes, broken));

    d::encodeStats(g_final[0], blob);
    blob[486] = (uint8_t)(blob[486] ^ 0xFFu);    // 版 2 で足した場所
    CHECK(!d::decodeStats(blob, d::kBlobBytes, broken));

    d::encodeStats(g_final[0], blob);
    blob[0] = 99;                       // 知らない版
    CHECK(!d::decodeStats(blob, d::kBlobBytes, broken));

    d::encodeStats(g_final[0], blob);
    CHECK(!d::decodeStats(blob, d::kBlobBytesV1, broken));   // 版 2 を版 1 の長さで読まない

    uint8_t v1[d::kBlobBytesV1];
    makeV1Blob(g_final[0], v1);
    CHECK(!d::decodeStats(v1, d::kBlobBytes, broken));        // その逆も
    v1[30] = (uint8_t)(v1[30] ^ 0xFFu);
    CHECK(!d::decodeStats(v1, d::kBlobBytesV1, broken));      // 版 1 の CRC も見ている

    d::encodeStats(g_final[0], blob);
    blob[1] = 200;                      // ありえない tail の数
    CHECK(!d::decodeStats(blob, d::kBlobBytes, broken));

    std::printf("  空・%d 局ぶんが 1 ビットまで往復し、版 1 の %d 局ぶんも中身を保ったまま"
                "版 2 になる\n", kGoldenCaseCount, migrated);
    std::printf("  版違い・長さ違い・CRC 違い（版 1 / 版 2 とも）を弾く\n");
}

// 相手ごとの勝敗は、provider 別の予測回数と必ずつじつまが合う
static void checkProviderOutcomes()
{
    std::printf("[4b] 相手（Jev / 統計AI）ごとの勝敗の数え方\n");

    for (int ci = 0; ci < kGoldenCaseCount; ++ci) {
        const Stats &s = g_final[ci];
        uint32_t total = 0;
        for (size_t p = 0; p < 2; ++p) {
            uint32_t sum = 0;
            for (size_t i = 0; i < 3; ++i) {
                sum += s.outcome_by_provider[p][i];
            }
            // 1 ラウンド確定するたびに provider 別の n と同時に 1 だけ増えるので必ず一致する
            CHECK(sum == s.prediction[p].n);
            total += sum;
        }
        CHECK(total == s.rounds);
        // 勝ち / 負け / あいこの内訳も、相手をまたいで足せば全体と一致する
        for (size_t i = 0; i < 3; ++i) {
            CHECK(s.outcome_by_provider[0][i] + s.outcome_by_provider[1][i] ==
                  s.outcome_counts[i]);
        }
    }

    // 受け付けられなかったラウンドでは増えないこと
    Stats s;
    d::ResolvedRound r;
    r.match_id = 1;
    r.round_no = 1;
    r.player_hand = Hand::Rock;
    r.ai_hand = Hand::Scissors;
    r.player_result = Result::HumanWin;
    r.provider_used = Provider::Jev;
    r.probabilities = Probs::uniform();
    CHECK(d::appendResolved(s, r));
    CHECK(s.outcome_by_provider[(size_t)Provider::Jev][(size_t)Result::HumanWin] == 1);
    CHECK(!d::appendResolved(s, r));    // 同じ回は二度数えない
    CHECK(s.outcome_by_provider[(size_t)Provider::Jev][(size_t)Result::HumanWin] == 1);

    r.round_no = 2;
    r.player_hand = Hand::Paper;
    r.ai_hand = Hand::Scissors;
    r.player_result = Result::AiWin;
    r.provider_used = Provider::Stats;
    CHECK(d::appendResolved(s, r));
    CHECK(s.outcome_by_provider[(size_t)Provider::Stats][(size_t)Result::AiWin] == 1);
    CHECK(s.outcome_by_provider[(size_t)Provider::Jev][(size_t)Result::AiWin] == 0);

    std::printf("  相手別の勝敗の合計が predictor 別の回数・全体の内訳と一致する\n");
}

// ---------------------------------------------------------------------------
// 5. 癖のカード（設計書 4.5）
// ---------------------------------------------------------------------------
// tail に「その手を n 回」だけ積む（癖の条件を作るための道具）
static void pushHands(Stats &s, uint16_t match_id, const Hand *hands, uint8_t n)
{
    for (uint8_t i = 0; i < n; ++i) {
        d::ResolvedRound r;
        r.match_id = match_id;
        r.round_no = (uint8_t)((i % d::kRoundsPerMatch) + 1);
        r.player_hand = hands[i];
        r.ai_hand = Hand::Rock;
        r.player_result = d::resultOf(r.player_hand, r.ai_hand);
        r.probabilities = Probs::uniform();
        d::appendResolved(s, r);
    }
}

static void checkHabits()
{
    std::printf("[5] 癖のカード（設計書 4.5）\n");

    HabitCard cards[d::kMaxHabitCards];

    // (a) 何も無ければ「まだはっきりした偏りは見つかっていません」1 枚だけ
    {
        Stats s;
        CHECK(d::habitCards(s, cards, d::kMaxHabitCards) == 1);
        CHECK(cards[0].id == HabitId::InsufficientData);
    }

    // (b) 件数の境目：19 件では出ない・20 件で出る
    {
        Stats s19, s20;
        s19.rounds = 19;
        s19.hand_counts[0] = 19;
        CHECK(d::habitCards(s19, cards, d::kMaxHabitCards) == 1);
        CHECK(cards[0].id == HabitId::InsufficientData);

        s20.rounds = 20;
        s20.hand_counts[0] = 20;
        CHECK(d::habitCards(s20, cards, d::kMaxHabitCards) == 1);
        CHECK(cards[0].id == HabitId::OverallFavorite);
        CHECK(cards[0].subject == Hand::Rock);
        CHECK(cards[0].hits == 20 && cards[0].total == 20);
    }

    // (c) 割合の境目：59% では出ない・ちょうど 60% で出る
    {
        Stats a, b;
        a.rounds = 100;
        a.hand_counts[0] = 59;
        a.hand_counts[1] = 41;
        CHECK(d::habitCards(a, cards, d::kMaxHabitCards) == 1);
        CHECK(cards[0].id == HabitId::InsufficientData);

        b.rounds = 100;
        b.hand_counts[0] = 60;
        b.hand_counts[1] = 40;
        CHECK(d::habitCards(b, cards, d::kMaxHabitCards) == 1);
        CHECK(cards[0].id == HabitId::OverallFavorite);
        CHECK(cards[0].hits == 60 && cards[0].total == 100);

        // 12/20 = ちょうど 60%（浮動小数で落ちないこと）／11/20 = 55% は出ない
        Stats c12, c11;
        c12.rounds = 20;
        c12.hand_counts[2] = 12;
        c12.hand_counts[0] = 8;
        CHECK(d::habitCards(c12, cards, d::kMaxHabitCards) == 1);
        CHECK(cards[0].id == HabitId::OverallFavorite && cards[0].subject == Hand::Paper);
        c11.rounds = 20;
        c11.hand_counts[2] = 11;
        c11.hand_counts[0] = 9;
        CHECK(d::habitCards(c11, cards, d::kMaxHabitCards) == 1);
        CHECK(cards[0].id == HabitId::InsufficientData);
    }

    // (d) 勝敗ごとの癖。ペアの件数の境目も見る
    {
        Stats s;
        s.repeat_after_result[(size_t)Result::AiWin][0] = 7;    // 同じ手
        s.repeat_after_result[(size_t)Result::AiWin][1] = 12;   // 変えた（19 ペア = 足りない）
        CHECK(d::habitCards(s, cards, d::kMaxHabitCards) == 1);
        CHECK(cards[0].id == HabitId::InsufficientData);

        s.repeat_after_result[(size_t)Result::AiWin][1] = 13;   // 20 ペア・13/20 = 65%
        CHECK(d::habitCards(s, cards, d::kMaxHabitCards) == 1);
        CHECK(cards[0].id == HabitId::ChangeAfterLoss);
        CHECK(cards[0].hits == 13 && cards[0].total == 20);

        s.repeat_after_result[(size_t)Result::HumanWin][0] = 18;
        s.repeat_after_result[(size_t)Result::HumanWin][1] = 7;   // 18/25 = 72%
        s.repeat_after_result[(size_t)Result::Draw][0] = 15;
        s.repeat_after_result[(size_t)Result::Draw][1] = 7;       // 15/22 = 68%
        const uint8_t n = d::habitCards(s, cards, d::kMaxHabitCards);
        CHECK(n == 3);
        // 優先順は change_after_loss → repeat_after_win → …（repeat_after_draw は 6 番目）
        CHECK(cards[0].id == HabitId::ChangeAfterLoss);
        CHECK(cards[1].id == HabitId::RepeatAfterWin);
        CHECK(cards[2].id == HabitId::RepeatAfterDraw);
    }

    // (e) 直近 20 回が最優先。全期間が同じ手を指していたら重複として落とす
    {
        Stats s;
        Hand hands[50];
        for (uint8_t i = 0; i < 50; ++i) {
            hands[i] = (i % 5 == 0) ? Hand::Scissors : Hand::Rock;   // グー 40 / チョキ 10
        }
        pushHands(s, 1, hands, 50);
        CHECK(s.tail_count == d::kTailCapacity);
        const uint8_t n = d::habitCards(s, cards, d::kMaxHabitCards);
        CHECK(n >= 1);
        CHECK(cards[0].id == HabitId::RecentFavorite);
        CHECK(cards[0].subject == Hand::Rock);
        CHECK(cards[0].total == d::kRecentWindow);
        for (uint8_t i = 1; i < n; ++i) {
            // 全期間もグーなので、同じ観察は出さない
            CHECK(!(cards[i].id == HabitId::OverallFavorite && cards[i].subject == Hand::Rock));
        }
    }

    // (f) 前の手ごとの遷移。並びは R → S → P、最大 3 枚で打ち切る
    {
        Stats s;
        for (size_t prev = 0; prev < 3; ++prev) {
            s.transition_by_hand[prev][(prev + 2) % 3] = 19;
            s.transition_by_hand[prev][prev] = 11;                 // 19/30 = 63%
        }
        const uint8_t n = d::habitCards(s, cards, d::kMaxHabitCards);
        CHECK(n == 3);
        for (uint8_t i = 0; i < 3; ++i) {
            CHECK(cards[i].id == HabitId::TransitionFavorite);
            CHECK(cards[i].subject == (Hand)i);
            CHECK(cards[i].object == (Hand)((i + 2) % 3));
            CHECK(cards[i].hits == 19 && cards[i].total == 30);
        }
    }

    // (g) 第 1 手の癖（分母は「対戦」）
    {
        Stats s;
        s.first_hand_counts[1] = 14;
        s.first_hand_counts[0] = 7;      // 14/21 = 66%
        CHECK(d::habitCards(s, cards, d::kMaxHabitCards) == 1);
        CHECK(cards[0].id == HabitId::FirstHandFavorite);
        CHECK(cards[0].subject == Hand::Scissors);
        CHECK(cards[0].hits == 14 && cards[0].total == 21);
    }

    // (h) 記録量レベルの区切り 0/10/50/200
    CHECK(d::recordLevel(0) == 0);
    CHECK(d::recordLevel(9) == 0);
    CHECK(d::recordLevel(10) == 1);
    CHECK(d::recordLevel(49) == 1);
    CHECK(d::recordLevel(50) == 2);
    CHECK(d::recordLevel(199) == 2);
    CHECK(d::recordLevel(200) == 3);

    std::printf("  19/20 件・59%%/60%% の境目、優先順、最大 3 枚、重複除去を確かめた\n");
}

// ---------------------------------------------------------------------------
// 6. Jev へ渡す状態（設計書 5.2）
// ---------------------------------------------------------------------------
static void checkJevState()
{
    std::printf("[6] Jev へ渡す状態の組み立て（設計書 5.2）\n");

    d::JevState st;
    Probs baseline = Probs::uniform();

    // (a) まっさら＝第 1 回。前の回も条件つき集計も無い
    {
        Stats s;
        d::buildJevState(s, 1, 1, baseline, st);
        CHECK(st.round_no == 1);
        CHECK(st.history_rounds == 0);
        CHECK(st.sequence_count == 0);
        CHECK(!st.has_previous);
        CHECK(st.same_hand_streak == 0);
        CHECK(st.by_hand_sample_n == 0 && st.by_hand_result_sample_n == 0);
    }

    // (b) 1 局 10 回 ＋ 次の局の 3 回。直近 12 件に対戦の境目が入る
    {
        Stats s;
        Hand a[10];
        for (uint8_t i = 0; i < 10; ++i) {
            a[i] = Hand::Rock;
        }
        pushHands(s, 7, a, 10);
        Hand b[3] = {Hand::Paper, Hand::Paper, Hand::Paper};
        pushHands(s, 8, b, 3);

        d::buildJevState(s, 8, 4, baseline, st);
        CHECK(st.history_rounds == 13);
        CHECK(st.sequence_count == d::kSequenceWindow);       // 13 件あるので 12 件に切る
        // 先頭は 7 局目の 2 回目（切れている）
        CHECK(st.sequence[0].new_match);
        CHECK(st.sequence[0].history_truncated);
        CHECK(st.sequence[0].round_no == 2);
        // 8 局目の 1 回目に本物の境目がある
        uint8_t boundaries = 0;
        for (uint8_t i = 1; i < st.sequence_count; ++i) {
            if (st.sequence[i].new_match) {
                ++boundaries;
                CHECK(st.sequence[i].round_no == 1);
                CHECK(!st.sequence[i].history_truncated);
            }
        }
        CHECK(boundaries == 1);
        // いま 8 局目・4 回目。前の回は 8 局目の 3 回目（パー）
        CHECK(st.has_previous);
        CHECK(st.previous_player_hand == Hand::Paper);
        CHECK(st.same_hand_streak == 3);                      // 同じ対戦の中だけ
        CHECK(st.by_hand_sample_n == st.by_hand[0] + st.by_hand[1] + st.by_hand[2]);
        CHECK(st.recent20_counts[0] + st.recent20_counts[1] + st.recent20_counts[2] == 13);
        CHECK(st.overall_counts[0] == 10 && st.overall_counts[2] == 3);
        CHECK(st.first_hand_counts[0] == 1 && st.first_hand_counts[2] == 1);

        // 対戦が違えば前の回として扱わない（設計書 4.3）
        d::buildJevState(s, 9, 1, baseline, st);
        CHECK(!st.has_previous);
        CHECK(st.same_hand_streak == 0);
        CHECK(st.sequence[0].new_match && st.sequence[0].history_truncated);
    }

    // (c) 先頭がちょうど第 1 ラウンドなら history_truncated は立てない
    {
        Stats s;
        Hand a[4] = {Hand::Rock, Hand::Scissors, Hand::Rock, Hand::Paper};
        pushHands(s, 3, a, 4);
        d::buildJevState(s, 3, 5, baseline, st);
        CHECK(st.sequence_count == 4);
        CHECK(st.sequence[0].new_match);
        CHECK(!st.sequence[0].history_truncated);
        CHECK(st.sequence[0].round_no == 1);
        CHECK(st.same_hand_streak == 1);      // 直前はパー 1 回だけ
    }

    // (d) baseline はそのまま持ち回る（Jev が使えなくてもこれで戦える）
    {
        Stats s;
        Probs p;
        CHECK(d::statsPrediction(s, 1, 1, p));
        d::buildJevState(s, 1, 1, p, st);
        for (size_t i = 0; i < 3; ++i) {
            CHECK(st.stats_baseline.p[i] == p.p[i]);
        }
    }

    std::printf("  直近 12 件・対戦の境目・切れている印・連続回数・条件つき集計を確かめた\n");
}

// ---------------------------------------------------------------------------
int main()
{
    std::printf("=== AI DUEL ロジックの試験 ===\n");
    std::printf("正解データ: %s（種 %d）\n\n", kGoldenSource, kGoldenSeed);

    checkRules();
    std::printf("\n");
    checkGreedyCounterexample();
    std::printf("\n");
    checkGolden();
    std::printf("\n");
    checkAppendGuards();
    std::printf("\n");
    checkSerialization();
    std::printf("\n");
    checkProviderOutcomes();
    std::printf("\n");
    checkHabits();
    std::printf("\n");
    checkJevState();
    std::printf("\n");

    std::printf("=== 確認 %d 件 / 失敗 %d 件 ===\n", g_checks, g_fail);
    if (g_fail == 0) std::printf("ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
