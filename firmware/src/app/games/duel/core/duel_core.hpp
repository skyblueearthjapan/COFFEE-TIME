// AI DUEL（じゃんけん 10 回勝負）の純粋ロジック。
//
// Arduino も LVGL も使わない C++17 のヘッダーだけの実装なので、そのまま PC で
// 試験できる（tools/duel_checks.cpp / tools/run_duel_checks.py）。
// 画面側（DuelGame.cpp）はここへ「確定した 1 ラウンド」を渡し、
// 次の回の予測確率・AI が出すべき手・癖のカードを受け取るだけにする。
//
// ---------------------------------------------------------------------------
// 設計書のどこを写したか（COFFEE_TIME_AI_DUEL_Design_v1.0.md）
// ---------------------------------------------------------------------------
//   1.2 勝敗表         … resultOf()。あいこの再試合はしない
//   4.2 統計の全構造   … Stats（付録 E の zero_stats() と同じ項目・同じ並び）
//   4.3 対戦の境界     … appendResolved()。match_id が同じで round_no が 1 増えたときだけ遷移を数える
//   4.4 反映の手順     … appendResolved() の中の順番そのまま
//   4.5 癖の種類       … habitCards()。条件・優先順・最大 3 枚・重複除去
//   5.1 統計 AI の式   … statsPrediction()。重みと有効条件、第 1 手の 50:50 混合
//   5.2 Jev へ渡す状態 … buildJevState()（JSON 化は端末側 DuelGame.cpp の仕事）
//   5.4 勝つ手の選び方 … expectedMargins() / optimalActions()。同点は 1e-9 以内
//   6.2 確率の検証     … normalizeProbabilities()（3 つ・有限・0〜1・合計 1±0.001）
//
// ここは設計書付録 E の Python 参考実装 `duel_reference.py` の移植である。
// **同じ確率・同じ統計・同じ最善手の集合を出すこと**が試験の合格条件なので、
// 足し算の順序や丸めを「良かれと思って」変えてはいけない
// （Python の sum() は 0 から順に足すので、C++ 側も同じ順で足している）。
//
// ---------------------------------------------------------------------------
// 設計書から変えたところ（docs/AI_DUEL_PLAN.md §2・§4）
// ---------------------------------------------------------------------------
//   - round_id / match_id の 32 桁 16 進 ID は使わない。正本が端末なので、
//     対戦は端末内の通し番号 (uint16) で足りる。重複の検出も (match_id, round_no) で行う。
//   - tail は 50 件の固定配列。動的確保をしない（LVGL タスクのスタックは 8KB）。
//   - 途中終了 (aborted) は noteMatchAborted() で 1 回だけ数える。
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace coffee {
namespace duel {

// ---------------------------------------------------------------------------
// 手と勝敗（設計書 2.1。**配列の並びはこの順から動かさない**）
// ---------------------------------------------------------------------------
enum class Hand : uint8_t { Rock = 0, Scissors = 1, Paper = 2 };
enum class Result : uint8_t { HumanWin = 0, AiWin = 1, Draw = 2 };
enum class Provider : uint8_t { Jev = 0, Stats = 1 };

constexpr uint8_t kHandCount = 3;
constexpr uint8_t kResultCount = 3;
constexpr uint8_t kProviderCount = 2;

constexpr uint8_t kRoundsPerMatch = 10;      // 設計書 1.2：常に 10 回
constexpr uint8_t kTailCapacity = 50;        // 設計書 4.2：直近 50 件
constexpr uint8_t kRecentWindow = 20;        // 設計書 5.1：直近 20 手
constexpr uint8_t kSequenceWindow = 12;      // 設計書 5.2：Jev へ渡す直近 12 件
constexpr uint32_t kTransitionMinN = 5;      // 設計書 5.1：前の手に続く手を使う下限
constexpr uint32_t kResultTransitionMinN = 8;
constexpr uint32_t kFirstHandMinN = 5;
constexpr double kTieEpsilon = 1e-9;         // 設計書 5.4：期待得点差の同点判定
constexpr double kProbabilitySumTolerance = 0.001;   // 設計書 6.2

// ROCK が勝つ相手は SCISSORS、SCISSORS が勝つ相手は PAPER、PAPER が勝つ相手は ROCK
inline Hand beats(Hand h)
{
    return (Hand)(((uint8_t)h + 1u) % kHandCount);
}

// プレイヤー視点の勝敗（設計書 1.2 の表）
inline Result resultOf(Hand human, Hand ai)
{
    if (human == ai) {
        return Result::Draw;
    }
    return beats(human) == ai ? Result::HumanWin : Result::AiWin;
}

// ---------------------------------------------------------------------------
// 確率（ROCK / SCISSORS / PAPER の順）
// ---------------------------------------------------------------------------
struct Probs {
    double p[kHandCount] = {0.0, 0.0, 0.0};

    double &operator[](size_t i) { return p[i]; }
    const double &operator[](size_t i) const { return p[i]; }
    double &operator[](Hand h) { return p[(size_t)h]; }
    const double &operator[](Hand h) const { return p[(size_t)h]; }

    static Probs uniform()
    {
        Probs q;
        for (size_t i = 0; i < kHandCount; ++i) {
            q.p[i] = 1.0 / 3.0;
        }
        return q;
    }
};

// 設計書 6.2 / 参考実装 normalize_probabilities()。
// 3 つとも有限で 0〜1、合計が 1±0.001 であることを確かめ、合計で割り直す
inline bool normalizeProbabilities(const Probs &raw, Probs &out)
{
    double sum = 0.0;
    for (size_t i = 0; i < kHandCount; ++i) {
        const double x = raw.p[i];
        if (!std::isfinite(x) || x < 0.0 || x > 1.0) {
            return false;
        }
        sum += x;   // Python の sum() と同じく ROCK → SCISSORS → PAPER の順に足す
    }
    if (sum <= 0.0 || std::fabs(sum - 1.0) > kProbabilitySumTolerance + 1e-12) {
        return false;
    }
    for (size_t i = 0; i < kHandCount; ++i) {
        out.p[i] = raw.p[i] / sum;
    }
    return true;
}

// 設計書 5.4：AI の期待得点差（勝ち +1・負け −1・あいこ 0）。
// 係数が 0 の項も足す（Python の内包表記と同じ並び・同じ回数の加算にするため）
inline bool expectedMargins(const Probs &raw, Probs &out)
{
    Probs p;
    if (!normalizeProbabilities(raw, p)) {
        return false;
    }
    for (size_t a = 0; a < kHandCount; ++a) {
        double u = 0.0;
        for (size_t h = 0; h < kHandCount; ++h) {
            const double coef = (beats((Hand)a) == (Hand)h)   ?  1.0
                              : (beats((Hand)h) == (Hand)a)   ? -1.0
                                                              :  0.0;
            u += coef * p.p[h];
        }
        out.p[a] = u;
    }
    return true;
}

// 期待得点差が最大の手の集合（同率は 1e-9 以内）。戻り値は bit0=ROCK / bit1=SCISSORS / bit2=PAPER。
// **最多の予測手に勝つ手を出すだけでは最善にならないことがある**（設計書 5.4 の R.40/S.35/P.25）
inline uint8_t optimalActions(const Probs &raw)
{
    Probs u;
    if (!expectedMargins(raw, u)) {
        return 0;
    }
    double best = u.p[0];
    for (size_t i = 1; i < kHandCount; ++i) {
        if (u.p[i] > best) {
            best = u.p[i];
        }
    }
    uint8_t mask = 0;
    for (size_t i = 0; i < kHandCount; ++i) {
        if (std::fabs(u.p[i] - best) <= kTieEpsilon) {
            mask = (uint8_t)(mask | (1u << i));
        }
    }
    return mask;
}

// 最善手の集合から 1 つ選ぶ。random は等確率の乱数（端末では esp_random()）。
// **いちど決めた手は再抽選しない**のは呼び出し側の責任（設計書 5.4）
inline Hand pickFromActions(uint8_t mask, uint32_t random)
{
    uint8_t n = 0;
    Hand picks[kHandCount];
    for (uint8_t i = 0; i < kHandCount; ++i) {
        if ((mask & (1u << i)) != 0) {
            picks[n++] = (Hand)i;
        }
    }
    if (n == 0) {
        return Hand::Rock;      // 確率が壊れていた。存在しない手は作らない
    }
    return picks[random % n];
}

// ---------------------------------------------------------------------------
// 個人の統計（設計書 4.2・付録 E の zero_stats() と同じ項目）
// ---------------------------------------------------------------------------
struct TailEntry {
    uint16_t match_id = 0;      // 端末内の対戦通し番号（設計書の 32 桁 ID の代わり）
    uint8_t round_no = 0;       // 1..10
    Hand player_hand = Hand::Rock;
    Hand ai_hand = Hand::Rock;
    Result player_result = Result::Draw;
};

struct PredictionMetrics {
    uint32_t n = 0;
    uint32_t hits = 0;
    double brier_sum = 0.0;
    double nll_sum = 0.0;
};

struct MatchCounts {
    uint32_t completed = 0;
    uint32_t human_win = 0;
    uint32_t ai_win = 0;
    uint32_t draw = 0;
    uint32_t aborted = 0;
};

struct Stats {
    uint32_t rounds = 0;
    uint32_t hand_counts[kHandCount] = {0, 0, 0};
    uint32_t outcome_counts[kResultCount] = {0, 0, 0};
    uint32_t first_hand_counts[kHandCount] = {0, 0, 0};
    uint32_t transition_by_hand[kHandCount][kHandCount] = {};
    uint32_t transition_by_hand_result[kHandCount][kResultCount][kHandCount] = {};
    uint32_t repeat_after_result[kResultCount][2] = {};   // [0]=同じ手 [1]=変えた手
    TailEntry tail[kTailCapacity] = {};
    uint8_t tail_count = 0;
    // 参考実装の last_round_id の代わり。同じラウンドを二度数えないための見張り
    bool has_last = false;
    uint16_t last_match_id = 0;
    uint8_t last_round_no = 0;
    PredictionMetrics prediction[kProviderCount] = {};    // 0 = jev / 1 = stats
    MatchCounts match_counts;
};

// 確定した 1 ラウンド（appendResolved の入力）
struct ResolvedRound {
    uint16_t match_id = 0;
    uint8_t round_no = 0;
    Hand player_hand = Hand::Rock;
    Hand ai_hand = Hand::Rock;
    Result player_result = Result::Draw;
    Provider provider_used = Provider::Stats;
    Probs probabilities;        // その回に採用した予測確率（正規化前でよい）
};

// 参考実装 smooth()：p(h) = (c(h)+1) / (n+3)。0 確率を避ける（設計書 5.1）
inline Probs smooth(const uint32_t counts[kHandCount])
{
    const uint32_t total = counts[0] + counts[1] + counts[2];
    Probs out;
    for (size_t i = 0; i < kHandCount; ++i) {
        out.p[i] = (double)(counts[i] + 1u) / (double)(total + 3u);
    }
    return out;
}

// 参考実装 append_resolved()。手順は設計書 4.4 の 1〜7 のまま。
// 重複・勝敗の食い違い・確率の不正は false（呼び出し側が二重計上を防ぐ前提）
inline bool appendResolved(Stats &s, const ResolvedRound &r)
{
    if (r.round_no == 0 || r.round_no > kRoundsPerMatch) {
        return false;
    }
    if (s.has_last && s.last_match_id == r.match_id && s.last_round_no == r.round_no) {
        return false;   // 参考実装の DUPLICATE_STATS_UPDATE
    }
    if (resultOf(r.player_hand, r.ai_hand) != r.player_result) {
        return false;   // 参考実装の RESULT_MISMATCH
    }
    Probs p;
    if (!normalizeProbabilities(r.probabilities, p)) {
        return false;
    }

    const size_t hi = (size_t)r.player_hand;
    const size_t ri = (size_t)r.player_result;
    const TailEntry *previous = s.tail_count > 0 ? &s.tail[s.tail_count - 1] : nullptr;

    ++s.rounds;
    ++s.hand_counts[hi];
    ++s.outcome_counts[ri];
    if (r.round_no == 1) {
        ++s.first_hand_counts[hi];
    }
    // 設計書 4.3：同じ対戦で round_no が 1 だけ増えた組だけを遷移として数える
    if (previous != nullptr && previous->match_id == r.match_id &&
        (uint16_t)(previous->round_no + 1) == (uint16_t)r.round_no) {
        const size_t pi = (size_t)previous->player_hand;
        const size_t pri = (size_t)previous->player_result;
        ++s.transition_by_hand[pi][hi];
        ++s.transition_by_hand_result[pi][pri][hi];
        ++s.repeat_after_result[pri][hi == pi ? 0 : 1];
    }

    // 設計書 5.5：予測の良さは provider_used 別に測る
    PredictionMetrics &m = s.prediction[(size_t)r.provider_used];
    ++m.n;
    double top = p.p[0];
    for (size_t i = 1; i < kHandCount; ++i) {
        if (p.p[i] > top) {
            top = p.p[i];
        }
    }
    size_t predicted = 0;   // 同率の最大手は R/S/P の順で固定（設計書 5.5）
    for (size_t i = 0; i < kHandCount; ++i) {
        if (p.p[i] == top) {
            predicted = i;
            break;
        }
    }
    if (predicted == hi) {
        ++m.hits;
    }
    double brier = 0.0;
    for (size_t i = 0; i < kHandCount; ++i) {
        const double d = p.p[i] - (i == hi ? 1.0 : 0.0);
        brier += d * d;
    }
    m.brier_sum += brier;
    m.nll_sum += -std::log(p.p[hi] > 1e-6 ? p.p[hi] : 1e-6);

    // tail は古い→新しい順。51 件目からは先頭を捨てる
    if (s.tail_count >= kTailCapacity) {
        std::memmove(&s.tail[0], &s.tail[1], sizeof(TailEntry) * (kTailCapacity - 1));
        s.tail_count = (uint8_t)(kTailCapacity - 1);
    }
    TailEntry &t = s.tail[s.tail_count++];
    t.match_id = r.match_id;
    t.round_no = r.round_no;
    t.player_hand = r.player_hand;
    t.ai_hand = r.ai_hand;
    t.player_result = r.player_result;

    s.has_last = true;
    s.last_match_id = r.match_id;
    s.last_round_no = r.round_no;
    return true;
}

// 10 回そろって終わった対戦（設計書 4.4 の 8）。winner は 10 回の勝ち数の比較
inline void noteMatchCompleted(Stats &s, Result winner)
{
    ++s.match_counts.completed;
    switch (winner) {
    case Result::HumanWin: ++s.match_counts.human_win; break;
    case Result::AiWin:    ++s.match_counts.ai_win;    break;
    case Result::Draw:     ++s.match_counts.draw;      break;
    }
}

// 途中終了・放置終了（計画 §4）。確定済みラウンドは残し、勝敗には数えない
inline void noteMatchAborted(Stats &s)
{
    ++s.match_counts.aborted;
}

// 10 回の勝ち数から対戦の勝者を決める（設計書 1.2）
inline Result matchWinner(uint8_t human_wins, uint8_t ai_wins)
{
    if (human_wins > ai_wins) {
        return Result::HumanWin;
    }
    return human_wins < ai_wins ? Result::AiWin : Result::Draw;
}

// ---------------------------------------------------------------------------
// 統計 AI（設計書 5.1 / 参考実装 stats_prediction）
//
// 混ぜる分布と重み:
//   0.15 全期間の手                （常に）
//   0.25 直近 20 手                （tail に 1 件以上）
//   0.30 前の手に続く手            （同じ対戦の前回があり 5 ペア以上）
//   0.30 前の手＋勝敗に続く手      （同じ対戦の前回があり 8 ペア以上）
// 残った重みの和で割り直し、第 1 回で first_hand が 5 対戦以上あれば 50:50 で混ぜる
// ---------------------------------------------------------------------------
inline bool statsPrediction(const Stats &s, uint16_t match_id, uint8_t round_no, Probs &out)
{
    double weight[4];
    Probs comp[4];
    uint8_t n = 0;

    weight[n] = 0.15;
    comp[n] = smooth(s.hand_counts);
    ++n;

    if (s.tail_count > 0) {
        const uint8_t take = s.tail_count < kRecentWindow ? s.tail_count : kRecentWindow;
        uint32_t counts[kHandCount] = {0, 0, 0};
        for (uint8_t i = (uint8_t)(s.tail_count - take); i < s.tail_count; ++i) {
            ++counts[(size_t)s.tail[i].player_hand];
        }
        weight[n] = 0.25;
        comp[n] = smooth(counts);
        ++n;
    }

    if (round_no > 1 && s.tail_count > 0) {
        const TailEntry &prev = s.tail[s.tail_count - 1];
        if (prev.match_id == match_id && (uint16_t)(prev.round_no + 1) == (uint16_t)round_no) {
            const size_t hi = (size_t)prev.player_hand;
            const size_t ri = (size_t)prev.player_result;
            const uint32_t *hc = s.transition_by_hand[hi];
            const uint32_t *rc = s.transition_by_hand_result[hi][ri];
            if (hc[0] + hc[1] + hc[2] >= kTransitionMinN) {
                weight[n] = 0.30;
                comp[n] = smooth(hc);
                ++n;
            }
            if (rc[0] + rc[1] + rc[2] >= kResultTransitionMinN) {
                weight[n] = 0.30;
                comp[n] = smooth(rc);
                ++n;
            }
        }
    }

    double ws = 0.0;
    for (uint8_t i = 0; i < n; ++i) {
        ws += weight[i];
    }
    Probs q;
    for (size_t h = 0; h < kHandCount; ++h) {
        double acc = 0.0;
        for (uint8_t i = 0; i < n; ++i) {
            acc += weight[i] * comp[i].p[h];
        }
        q.p[h] = acc / ws;
    }
    if (round_no == 1 &&
        s.first_hand_counts[0] + s.first_hand_counts[1] + s.first_hand_counts[2] >= kFirstHandMinN) {
        const Probs f = smooth(s.first_hand_counts);
        for (size_t h = 0; h < kHandCount; ++h) {
            q.p[h] = 0.5 * q.p[h] + 0.5 * f.p[h];
        }
    }
    return normalizeProbabilities(q, out);
}

// ---------------------------------------------------------------------------
// 癖のカード（設計書 4.5）
//
// 自由文は作らない。テンプレートと「裏付けの件数」だけを返し、文字にするのは画面側。
// 条件は 20 件以上・最多が 60% 以上。60% の判定は整数のまま行う（hits*10 >= total*6）
// ので、12/20 のような「ちょうど 60%」が浮動小数の丸めで落ちることはない。
// ---------------------------------------------------------------------------
enum class HabitId : uint8_t {
    None = 0,
    RecentFavorite,       // 直近 20 回はパー：13/20 回
    ChangeAfterLoss,      // 負けた次は手を変更：17/24 回
    RepeatAfterWin,       // 勝った次は同じ手：18/25 回
    FirstHandFavorite,    // 最初はチョキが多め：14/21 対戦
    TransitionFavorite,   // グーの次はパー：19/30 回
    RepeatAfterDraw,      // あいこの次も同じ手：15/22 回
    OverallFavorite,      // これまでグーが多め：30/45 回
    InsufficientData,     // まだはっきりした偏りは見つかっていません
};

struct HabitCard {
    HabitId id = HabitId::None;
    Hand subject = Hand::Rock;   // favourite の手 / transition の「前の手」
    Hand object = Hand::Rock;    // transition の「次の手」（ほかでは使わない）
    uint32_t hits = 0;           // 分子
    uint32_t total = 0;          // 分母
};

constexpr uint8_t kMaxHabitCards = 3;
constexpr uint32_t kHabitMinSamples = 20;   // 設計書 4.5 の「20 件」
constexpr uint32_t kHabitShareNum = 6;      // 60% = 6/10
constexpr uint32_t kHabitShareDen = 10;

inline bool habitShareOk(uint32_t hits, uint32_t total)
{
    return total >= kHabitMinSamples &&
           (uint64_t)hits * kHabitShareDen >= (uint64_t)total * kHabitShareNum;
}

// 最多の手（同数は R → S → P の順）
inline size_t topHand(const uint32_t counts[kHandCount])
{
    size_t top = 0;
    for (size_t i = 1; i < kHandCount; ++i) {
        if (counts[i] > counts[top]) {
            top = i;
        }
    }
    return top;
}

// 優先順は設計書 4.5：recent → change_after_loss → repeat_after_win → first_hand
//                     → transition → repeat_after_draw → overall。最大 3 枚。
// 「同じ観察の重複は除く」= 直近と全期間が同じ手を指しているときは後ろ（全期間）を落とす
inline uint8_t habitCards(const Stats &s, HabitCard *out, uint8_t out_max)
{
    if (out == nullptr || out_max == 0) {
        return 0;
    }
    const uint8_t limit = out_max < kMaxHabitCards ? out_max : kMaxHabitCards;
    uint8_t count = 0;
    bool favorite_taken = false;
    Hand favorite_hand = Hand::Rock;

    auto add = [&](HabitId id, Hand subject, Hand object, uint32_t hits, uint32_t total) {
        if (count >= limit) {
            return;
        }
        out[count++] = HabitCard{id, subject, object, hits, total};
    };

    // 1) recent_favorite：直近が 20 件そろっていること
    if (s.tail_count >= kRecentWindow) {
        uint32_t counts[kHandCount] = {0, 0, 0};
        for (uint8_t i = (uint8_t)(s.tail_count - kRecentWindow); i < s.tail_count; ++i) {
            ++counts[(size_t)s.tail[i].player_hand];
        }
        const size_t top = topHand(counts);
        if (habitShareOk(counts[top], kRecentWindow)) {
            add(HabitId::RecentFavorite, (Hand)top, (Hand)top, counts[top], kRecentWindow);
            favorite_taken = true;
            favorite_hand = (Hand)top;
        }
    }

    // 2) change_after_loss / 3) repeat_after_win / 6) repeat_after_draw
    const uint32_t *after_loss = s.repeat_after_result[(size_t)Result::AiWin];
    const uint32_t loss_total = after_loss[0] + after_loss[1];
    if (habitShareOk(after_loss[1], loss_total)) {
        add(HabitId::ChangeAfterLoss, Hand::Rock, Hand::Rock, after_loss[1], loss_total);
    }
    const uint32_t *after_win = s.repeat_after_result[(size_t)Result::HumanWin];
    const uint32_t win_total = after_win[0] + after_win[1];
    if (habitShareOk(after_win[0], win_total)) {
        add(HabitId::RepeatAfterWin, Hand::Rock, Hand::Rock, after_win[0], win_total);
    }

    // 4) first_hand_favorite
    {
        const uint32_t total = s.first_hand_counts[0] + s.first_hand_counts[1] +
                               s.first_hand_counts[2];
        const size_t top = topHand(s.first_hand_counts);
        if (habitShareOk(s.first_hand_counts[top], total)) {
            add(HabitId::FirstHandFavorite, (Hand)top, (Hand)top, s.first_hand_counts[top], total);
        }
    }

    // 5) transition_favorite（前の手ごとに見る。並びは R → S → P）
    for (size_t prev = 0; prev < kHandCount; ++prev) {
        const uint32_t *row = s.transition_by_hand[prev];
        const uint32_t total = row[0] + row[1] + row[2];
        const size_t top = topHand(row);
        if (habitShareOk(row[top], total)) {
            add(HabitId::TransitionFavorite, (Hand)prev, (Hand)top, row[top], total);
        }
    }

    const uint32_t *after_draw = s.repeat_after_result[(size_t)Result::Draw];
    const uint32_t draw_total = after_draw[0] + after_draw[1];
    if (habitShareOk(after_draw[0], draw_total)) {
        add(HabitId::RepeatAfterDraw, Hand::Rock, Hand::Rock, after_draw[0], draw_total);
    }

    // 7) overall_favorite（直近と同じ手を指しているなら重複なので出さない）
    {
        const size_t top = topHand(s.hand_counts);
        if (s.rounds >= kHabitMinSamples && habitShareOk(s.hand_counts[top], s.rounds) &&
            !(favorite_taken && favorite_hand == (Hand)top)) {
            add(HabitId::OverallFavorite, (Hand)top, (Hand)top, s.hand_counts[top], s.rounds);
        }
    }

    if (count == 0) {
        out[count++] = HabitCard{HabitId::InsufficientData, Hand::Rock, Hand::Rock, 0, s.rounds};
    }
    return count;
}

// 記録量レベル（設計書 5.3：0 / 10 / 50 / 200 ラウンドの区切り。精度メーターは出さない）
inline uint8_t recordLevel(uint32_t rounds)
{
    if (rounds >= 200) {
        return 3;
    }
    if (rounds >= 50) {
        return 2;
    }
    return rounds >= 10 ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Jev へ渡す状態（設計書 5.2 / 計画 §6）
//
// ここは「素の構造体」まで。JSON にするのは端末側（ArduinoJson）の仕事。
// **入れてはいけないもの**: アイコン名・端末名・杯数・今回の手・未公開の AI の手。
// ---------------------------------------------------------------------------
struct SequenceItem {
    bool new_match = false;
    bool history_truncated = false;
    uint8_t round_no = 0;
    Hand player_hand = Hand::Rock;
    Hand ai_hand = Hand::Rock;
    Result player_result = Result::Draw;
};

struct JevState {
    uint8_t round_no = 1;
    uint32_t history_rounds = 0;
    uint32_t overall_counts[kHandCount] = {0, 0, 0};
    uint32_t recent20_counts[kHandCount] = {0, 0, 0};
    uint32_t first_hand_counts[kHandCount] = {0, 0, 0};

    bool has_previous = false;              // 同じ対戦の直前のラウンドがあるか
    Hand previous_player_hand = Hand::Rock;
    Hand previous_ai_hand = Hand::Rock;
    Result previous_player_result = Result::Draw;

    // 今回の条件（前の手 / 前の手＋勝敗）に続く手の回数。has_previous のときだけ意味がある
    uint32_t by_hand[kHandCount] = {0, 0, 0};
    uint32_t by_hand_sample_n = 0;
    uint32_t by_hand_result[kHandCount] = {0, 0, 0};
    uint32_t by_hand_result_sample_n = 0;

    SequenceItem sequence[kSequenceWindow] = {};
    uint8_t sequence_count = 0;
    uint32_t same_hand_streak = 0;          // 現在の対戦の中で同じ手が続いた回数
    Probs stats_baseline;
};

inline void buildJevState(const Stats &s, uint16_t match_id, uint8_t round_no,
                          const Probs &baseline, JevState &out)
{
    out = JevState{};
    out.round_no = round_no;
    out.history_rounds = s.rounds;
    for (size_t i = 0; i < kHandCount; ++i) {
        out.overall_counts[i] = s.hand_counts[i];
        out.first_hand_counts[i] = s.first_hand_counts[i];
    }
    out.stats_baseline = baseline;

    if (s.tail_count > 0) {
        const uint8_t take = s.tail_count < kRecentWindow ? s.tail_count : kRecentWindow;
        for (uint8_t i = (uint8_t)(s.tail_count - take); i < s.tail_count; ++i) {
            ++out.recent20_counts[(size_t)s.tail[i].player_hand];
        }
    }

    const TailEntry *prev = s.tail_count > 0 ? &s.tail[s.tail_count - 1] : nullptr;
    if (round_no > 1 && prev != nullptr && prev->match_id == match_id &&
        (uint16_t)(prev->round_no + 1) == (uint16_t)round_no) {
        out.has_previous = true;
        out.previous_player_hand = prev->player_hand;
        out.previous_ai_hand = prev->ai_hand;
        out.previous_player_result = prev->player_result;
        const size_t hi = (size_t)prev->player_hand;
        const size_t ri = (size_t)prev->player_result;
        for (size_t i = 0; i < kHandCount; ++i) {
            out.by_hand[i] = s.transition_by_hand[hi][i];
            out.by_hand_result[i] = s.transition_by_hand_result[hi][ri][i];
        }
        out.by_hand_sample_n = out.by_hand[0] + out.by_hand[1] + out.by_hand[2];
        out.by_hand_result_sample_n =
            out.by_hand_result[0] + out.by_hand_result[1] + out.by_hand_result[2];
    }

    // 直近 12 件（古い→新しい）。対戦の境目は new_match、窓の外で切れていたら history_truncated
    if (s.tail_count > 0) {
        const uint8_t take = s.tail_count < kSequenceWindow ? s.tail_count : kSequenceWindow;
        const uint8_t start = (uint8_t)(s.tail_count - take);
        for (uint8_t k = start; k < s.tail_count; ++k) {
            const TailEntry &e = s.tail[k];
            const bool boundary = (k == 0) || (s.tail[k - 1].match_id != e.match_id);
            SequenceItem &item = out.sequence[out.sequence_count++];
            item.round_no = e.round_no;
            item.player_hand = e.player_hand;
            item.ai_hand = e.ai_hand;
            item.player_result = e.player_result;
            if (k == start) {
                // 先頭は必ず「その対戦の始まりに見える」ので、本当の第 1 ラウンドでなければ断る
                item.new_match = true;
                item.history_truncated = !boundary || e.round_no != 1;
            } else {
                item.new_match = boundary;
                item.history_truncated = false;
            }
        }
    }

    // 同じ手の連続（現在の対戦の中だけ。設計書 5.2）
    if (prev != nullptr && prev->match_id == match_id) {
        uint32_t streak = 1;
        for (int i = (int)s.tail_count - 2; i >= 0; --i) {
            const TailEntry &a = s.tail[i];
            const TailEntry &b = s.tail[i + 1];
            if (a.match_id != match_id || (uint16_t)(a.round_no + 1) != (uint16_t)b.round_no ||
                a.player_hand != b.player_hand) {
                break;
            }
            ++streak;
        }
        out.same_hand_streak = streak;
    }
}

// ---------------------------------------------------------------------------
// NVS へ入れる塊（版数つき・固定長 488 バイト）
//
// 並び（数値はリトルエンディアン。double は IEEE-754 の 64bit をそのまま）:
//
//    0      : format_version = 1
//    1      : tail_count (0..50)
//    2      : has_last (0/1)
//    3      : last_round_no
//    4..5   : last_match_id (uint16)
//    6..7   : 予約（0）
//    8..11  : rounds (uint32)
//   12..23  : hand_counts[3]
//   24..35  : outcome_counts[3]
//   36..47  : first_hand_counts[3]
//   48..83  : transition_by_hand[3][3]
//   84..191 : transition_by_hand_result[3][3][3]
//  192..215 : repeat_after_result[3][2]
//  216..263 : prediction[2] × { n, hits, brier_sum(double), nll_sum(double) }
//  264..283 : match_counts { completed, human_win, ai_win, draw, aborted }
//  284..483 : tail[50] × { match_id(uint16), round_no, packed }
//             packed = player_hand | ai_hand<<2 | player_result<<4
//  484..487 : crc32（0..483 バイトに対する CRC-32/ISO-HDLC）
//
// 版番号・長さ・CRC・値の妥当性のどれかが合わなければ「記録なし」として扱う。
// CRC は破損検出用で、署名や改ざん対策ではない。
// ---------------------------------------------------------------------------
constexpr uint8_t kBlobVersion = 1;
constexpr size_t kBlobBytes = 488;

namespace detail {

inline void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

inline uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

inline void putDouble(uint8_t *p, double v)
{
    uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    for (size_t i = 0; i < 8; ++i) {
        p[i] = (uint8_t)((bits >> (8 * i)) & 0xFFu);
    }
}

inline double getDouble(const uint8_t *p)
{
    uint64_t bits = 0;
    for (size_t i = 0; i < 8; ++i) {
        bits |= (uint64_t)p[i] << (8 * i);
    }
    double v = 0.0;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

// CRC-32/ISO-HDLC。表を持たない実装（488 バイトしか通さないので速度は問題にならない）
inline uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

}  // namespace detail

inline void encodeStats(const Stats &s, uint8_t *out)
{
    std::memset(out, 0, kBlobBytes);
    out[0] = kBlobVersion;
    out[1] = s.tail_count > kTailCapacity ? kTailCapacity : s.tail_count;
    out[2] = s.has_last ? 1u : 0u;
    out[3] = s.last_round_no;
    out[4] = (uint8_t)(s.last_match_id & 0xFFu);
    out[5] = (uint8_t)((s.last_match_id >> 8) & 0xFFu);
    detail::put32(out + 8, s.rounds);

    size_t at = 12;
    for (size_t i = 0; i < kHandCount; ++i, at += 4) {
        detail::put32(out + at, s.hand_counts[i]);
    }
    for (size_t i = 0; i < kResultCount; ++i, at += 4) {
        detail::put32(out + at, s.outcome_counts[i]);
    }
    for (size_t i = 0; i < kHandCount; ++i, at += 4) {
        detail::put32(out + at, s.first_hand_counts[i]);
    }
    for (size_t a = 0; a < kHandCount; ++a) {
        for (size_t b = 0; b < kHandCount; ++b, at += 4) {
            detail::put32(out + at, s.transition_by_hand[a][b]);
        }
    }
    for (size_t a = 0; a < kHandCount; ++a) {
        for (size_t b = 0; b < kResultCount; ++b) {
            for (size_t c = 0; c < kHandCount; ++c, at += 4) {
                detail::put32(out + at, s.transition_by_hand_result[a][b][c]);
            }
        }
    }
    for (size_t a = 0; a < kResultCount; ++a) {
        for (size_t b = 0; b < 2; ++b, at += 4) {
            detail::put32(out + at, s.repeat_after_result[a][b]);
        }
    }
    for (size_t i = 0; i < kProviderCount; ++i) {
        detail::put32(out + at, s.prediction[i].n);
        detail::put32(out + at + 4, s.prediction[i].hits);
        detail::putDouble(out + at + 8, s.prediction[i].brier_sum);
        detail::putDouble(out + at + 16, s.prediction[i].nll_sum);
        at += 24;
    }
    detail::put32(out + at, s.match_counts.completed);      at += 4;
    detail::put32(out + at, s.match_counts.human_win);      at += 4;
    detail::put32(out + at, s.match_counts.ai_win);         at += 4;
    detail::put32(out + at, s.match_counts.draw);           at += 4;
    detail::put32(out + at, s.match_counts.aborted);        at += 4;

    for (size_t i = 0; i < kTailCapacity; ++i, at += 4) {
        if (i >= out[1]) {
            continue;
        }
        const TailEntry &t = s.tail[i];
        out[at + 0] = (uint8_t)(t.match_id & 0xFFu);
        out[at + 1] = (uint8_t)((t.match_id >> 8) & 0xFFu);
        out[at + 2] = t.round_no;
        out[at + 3] = (uint8_t)((uint8_t)t.player_hand | ((uint8_t)t.ai_hand << 2) |
                                ((uint8_t)t.player_result << 4));
    }
    detail::put32(out + (kBlobBytes - 4), detail::crc32(out, kBlobBytes - 4));
}

inline bool decodeStats(const uint8_t *in, Stats &s)
{
    if (in[0] != kBlobVersion || in[1] > kTailCapacity) {
        return false;   // 版が違う・長さが変。移行はしない（次の保存で新形式になる）
    }
    if (detail::get32(in + (kBlobBytes - 4)) != detail::crc32(in, kBlobBytes - 4)) {
        return false;
    }
    s = Stats{};
    s.tail_count = in[1];
    s.has_last = in[2] != 0;
    s.last_round_no = in[3];
    s.last_match_id = (uint16_t)((uint16_t)in[4] | ((uint16_t)in[5] << 8));
    s.rounds = detail::get32(in + 8);

    size_t at = 12;
    for (size_t i = 0; i < kHandCount; ++i, at += 4) {
        s.hand_counts[i] = detail::get32(in + at);
    }
    for (size_t i = 0; i < kResultCount; ++i, at += 4) {
        s.outcome_counts[i] = detail::get32(in + at);
    }
    for (size_t i = 0; i < kHandCount; ++i, at += 4) {
        s.first_hand_counts[i] = detail::get32(in + at);
    }
    for (size_t a = 0; a < kHandCount; ++a) {
        for (size_t b = 0; b < kHandCount; ++b, at += 4) {
            s.transition_by_hand[a][b] = detail::get32(in + at);
        }
    }
    for (size_t a = 0; a < kHandCount; ++a) {
        for (size_t b = 0; b < kResultCount; ++b) {
            for (size_t c = 0; c < kHandCount; ++c, at += 4) {
                s.transition_by_hand_result[a][b][c] = detail::get32(in + at);
            }
        }
    }
    for (size_t a = 0; a < kResultCount; ++a) {
        for (size_t b = 0; b < 2; ++b, at += 4) {
            s.repeat_after_result[a][b] = detail::get32(in + at);
        }
    }
    for (size_t i = 0; i < kProviderCount; ++i) {
        s.prediction[i].n = detail::get32(in + at);
        s.prediction[i].hits = detail::get32(in + at + 4);
        s.prediction[i].brier_sum = detail::getDouble(in + at + 8);
        s.prediction[i].nll_sum = detail::getDouble(in + at + 16);
        at += 24;
    }
    s.match_counts.completed = detail::get32(in + at);      at += 4;
    s.match_counts.human_win = detail::get32(in + at);      at += 4;
    s.match_counts.ai_win = detail::get32(in + at);         at += 4;
    s.match_counts.draw = detail::get32(in + at);           at += 4;
    s.match_counts.aborted = detail::get32(in + at);        at += 4;

    for (size_t i = 0; i < kTailCapacity; ++i, at += 4) {
        if (i >= s.tail_count) {
            continue;
        }
        TailEntry &t = s.tail[i];
        t.match_id = (uint16_t)((uint16_t)in[at] | ((uint16_t)in[at + 1] << 8));
        t.round_no = in[at + 2];
        const uint8_t packed = in[at + 3];
        const uint8_t hand = (uint8_t)(packed & 0x03u);
        const uint8_t ai = (uint8_t)((packed >> 2) & 0x03u);
        const uint8_t res = (uint8_t)((packed >> 4) & 0x03u);
        if (hand >= kHandCount || ai >= kHandCount || res >= kResultCount ||
            t.round_no == 0 || t.round_no > kRoundsPerMatch) {
            return false;   // 知らない値。壊れているとみなす
        }
        t.player_hand = (Hand)hand;
        t.ai_hand = (Hand)ai;
        t.player_result = (Result)res;
    }
    // 統計の意味が壊れていないかだけ軽く確かめる（数え間違いの取り込みを防ぐ）
    if (s.hand_counts[0] + s.hand_counts[1] + s.hand_counts[2] != s.rounds ||
        s.outcome_counts[0] + s.outcome_counts[1] + s.outcome_counts[2] != s.rounds) {
        return false;
    }
    return true;
}

}  // namespace duel
}  // namespace coffee
