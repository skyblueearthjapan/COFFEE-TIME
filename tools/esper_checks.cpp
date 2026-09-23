// エスパー対決の推論コア（games/esper/core/esper_core.hpp）と生成データの PC 上の試験。
//
// 実機は要らない。tools/run_esper_checks.py が zig を呼んでこれを組み立て、実行する:
//   python tools/run_esper_checks.py
//
// 確かめること:
//   1. 生成データがカタログの取り決めどおりか（128 候補 / 146 質問 / 4 分野 32 件ずつ /
//      yes ⊆ scope / 署名 128 種 / EASY ⊂ NORMAL ⊂ HARD / ビットの並び / 同点整列のキー）
//   2. **golden_cases.json の 168 局を 1 手ずつ再現**（質問の並び・残り候補数・最終予想が完全一致）
//   3. 各モードの全候補を正直な回答で最後まで（必ず上限内で言い当てる／問数の分布）
//   4. 安全な質問から乱数で選ぶ試験（設計書 16 章の 30 seed × 全候補 = 5,040 局）
//   5. わからない（skip）・1 つ戻る（undo）を混ぜた乱数試験と、その場その場の不変条件
//   6. 答え合わせまわり（食い違いの数え方・勝敗は 1 回だけ・申告の扱い）
//   7. **段階 2（Jev）の差し込み口 applyAdvice**（一覧外・候補外・revision 違いは無視／
//      Jev の返事をまねる ScriptedAdvisor で遊んでもルールが曲がらない）
//   8. **端末 → GAS の要求 JSON**（ID だけ・メモや答えが混ざらない・依頼箱に収まる）
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../firmware/src/app/games/esper/core/esper_core.hpp"
#include "../firmware/src/app/games/esper/core/esper_request.hpp"
#include "esper_golden.inc"

namespace es = coffee::esper;
namespace ct = coffee::esp::content;

using es::Answer;
using es::Engine;
using es::GuessReason;
using es::Mask;
using es::Phase;
using es::Verdict;

#ifndef ESPER_RANDOM_SEEDS
#define ESPER_RANDOM_SEEDS 30
#endif

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

// 再現できる疑似乱数（xorshift64*）。試験の種は固定する
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    uint32_t next()
    {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return (uint32_t)((s * 0x2545F4914F6CDD1Dull) >> 32);
    }
    uint32_t below(uint32_t n) { return n ? next() % n : 0; }
};

// Engine は 100KB 級なのでスタックに置かない（端末でも PSRAM に置く）
static Engine g_engine;
static es::LocalAdvisor g_local;

// 安全な質問の中から乱数で選ぶ Advisor（設計書 16 章の「安全候補からのランダム選択試験」）。
// 段階 2 で Jev に差し替える継ぎ目が本当に差し替え可能かの確認も兼ねる
class RandomAdvisor : public es::Advisor {
public:
    explicit RandomAdvisor(uint64_t seed) : rng_(seed) {}
    uint8_t chooseQuestion(const es::Session &, const es::Shortlist &list) override
    {
        return (uint8_t)rng_.below(list.count);
    }
    uint16_t chooseGuess(const es::Session &, const Mask &) override { return es::kNoItem; }
    const char *engineName() const override { return "rule-random"; }
private:
    Rng rng_;
};

// 正直な答え（出題される質問は必ず適用範囲内なので Yes か No のどちらか）
static Answer truthful(uint16_t question, uint16_t target)
{
    return Engine::cellFor(question, target) == Engine::Cell::Yes ? Answer::Yes : Answer::No;
}

// ---------------------------------------------------------------------------
// 1. 生成データの検査
// ---------------------------------------------------------------------------
static void checkContent()
{
    std::printf("[1] 生成データ（EsperContent）の検査\n");
    CHECK(ct::kItemCount == 128);
    CHECK(ct::kQuestionCount == 146);
    CHECK(ct::kRefModeCount == 3);
    CHECK(ct::kPlayModeFirst == 3);
    CHECK(ct::kModeCount == ct::kRefModeCount + ct::kPlayModeCount);

    uint16_t per_group[ct::kGroupCount] = {0, 0, 0, 0};
    for (uint16_t i = 0; i < ct::kItemCount; ++i) {
        const ct::Item &it = ct::kItems[i];
        CHECK(it.group < ct::kGroupCount);
        ++per_group[it.group];
        CHECK(it.id[0] == ct::kGroups[it.group].letter);
        CHECK(it.name[0] != '\0' && it.definition[0] != '\0');
        CHECK(Mask::from(ct::kGroups[it.group].items).test(i));
    }
    for (uint8_t g = 0; g < ct::kGroupCount; ++g) {
        CHECK(per_group[g] == 32);
    }
    // ビットの並びは catalog.items の配列順（設計書 6.3）。両端を固定で確かめる
    CHECK(std::strcmp(ct::kItems[0].id, "D01") == 0);
    CHECK(std::strcmp(ct::kItems[127].id, "T32") == 0);

    for (uint16_t q = 0; q < ct::kQuestionCount; ++q) {
        const ct::Question &info = ct::kQuestions[q];
        const Mask yes = Mask::from(info.yes);
        const Mask scope = Mask::from(info.scope);
        CHECK(yes.subsetOf(scope));
        CHECK(!yes.empty() && yes.count() < scope.count());
        CHECK(info.text_len > 0 && info.text_len <= 32);    // D06
        CHECK(info.text[0] != '\0' && info.help[0] != '\0');
        CHECK(info.scope_bits != 0 && info.scope_bits <= 0x0F);
        Mask from_bits;
        for (uint8_t g = 0; g < ct::kGroupCount; ++g) {
            if (info.scope_bits & (1u << g)) {
                from_bits = from_bits | Mask::from(ct::kGroups[g].items);
            }
        }
        CHECK(from_bits == scope);      // scope マスクと scope_bits が一致
    }

    // sort_rank は質問 ID の昇順の順位（0..145 の並べ替え）であること
    {
        std::vector<int> seen((size_t)ct::kQuestionCount, 0);
        for (uint16_t q = 0; q < ct::kQuestionCount; ++q) {
            const uint16_t r = ct::kQuestions[q].sort_rank;
            CHECK(r < ct::kQuestionCount);
            if (r < ct::kQuestionCount) ++seen[r];
        }
        for (uint16_t r = 0; r < ct::kQuestionCount; ++r) CHECK(seen[(size_t)r] == 1);
        for (uint16_t a = 0; a < ct::kQuestionCount; ++a) {
            const uint16_t b = (uint16_t)((a + 1) % ct::kQuestionCount);
            const bool id_less = std::strcmp(ct::kQuestions[a].id, ct::kQuestions[b].id) < 0;
            CHECK(id_less == (ct::kQuestions[a].sort_rank < ct::kQuestions[b].sort_rank));
        }
    }

    // D04: 128 候補の回答署名がすべて異なる（分離できない組が 0）
    int collisions = 0;
    for (uint16_t a = 0; a < ct::kItemCount; ++a) {
        for (uint16_t b = (uint16_t)(a + 1); b < ct::kItemCount; ++b) {
            bool same = true;
            for (uint16_t q = 0; q < ct::kQuestionCount && same; ++q) {
                if (Engine::cellFor(q, a) != Engine::cellFor(q, b)) same = false;
            }
            if (same) ++collisions;
        }
    }
    CHECK(collisions == 0);

    // D05: EASY ⊂ NORMAL ⊂ HARD
    const Mask easy = Mask::from(ct::kModes[0].items);
    const Mask normal = Mask::from(ct::kModes[1].items);
    const Mask hard = Mask::from(ct::kModes[2].items);
    CHECK(easy.count() == 8 && normal.count() == 32 && hard.count() == 128);
    CHECK(easy.subsetOf(normal) && normal.subsetOf(hard));
    CHECK(ct::kModes[0].max_questions == 5);
    CHECK(ct::kModes[1].max_questions == 7);
    CHECK(ct::kModes[2].max_questions == 10);
    for (uint8_t m = 0; m < ct::kModeCount; ++m) {
        CHECK(ct::kModes[m].item_count == Mask::from(ct::kModes[m].items).count());
        CHECK(ct::kModes[m].max_skips == 2);
    }

    std::printf("  候補 %u / 質問 %u / 対応 %u セル / 分離できない組 %d / カタログ %s\n",
                (unsigned)ct::kItemCount, (unsigned)ct::kQuestionCount,
                (unsigned)(ct::kItemCount * ct::kQuestionCount), collisions,
                ct::kCatalogVersion);
}

// ---------------------------------------------------------------------------
// 2. golden_cases.json の 168 局を 1 手ずつ再現
// ---------------------------------------------------------------------------
static void checkGolden()
{
    std::printf("[2] 参考実装の 168 局を再現（質問の並び・残り候補数・最終予想）\n");
    g_engine.setAdvisor(&g_local);

    int games = 0, steps = 0, mismatched = 0;
    for (int c = 0; c < kGoldenCaseCount; ++c) {
        const GoldenCase &gc = kGoldenCases[c];
        CHECK(std::strcmp(ct::kItems[gc.target].id, gc.tid) == 0);

        g_engine.start(gc.mode, 0);
        bool ok = true;
        for (uint8_t i = 0; i < gc.count; ++i) {
            const GoldenStep &gs = gc.steps[i];
            CHECK(std::strcmp(ct::kQuestions[gs.question].id, gs.qid) == 0);
            const es::Session &s = g_engine.session();
            if (s.phase != Phase::Question || s.current_question != gs.question) {
                if (mismatched < 5) {
                    note("局 %d（%s / %s）%u 手目: 出題が %s、期待は %s\n", c,
                         ct::kModes[gc.mode].id, gc.tid, (unsigned)(i + 1),
                         s.current_question == es::kNoQuestion
                             ? "(なし)" : ct::kQuestions[s.current_question].id,
                         gs.qid);
                }
                ++mismatched;
                ok = false;
                break;
            }
            // 記録された答えが、カタログから見ても正しいこと（記録そのものの検算）
            CHECK(truthful(gs.question, gc.target) == (gs.yes ? Answer::Yes : Answer::No));
            g_engine.answer(gs.yes ? Answer::Yes : Answer::No);
            if (g_engine.session().remainingCount() != gs.remaining) {
                if (mismatched < 5) {
                    note("局 %d（%s）%u 手目の残り候補が %u、期待は %u\n", c, gc.tid,
                         (unsigned)(i + 1), (unsigned)g_engine.session().remainingCount(),
                         (unsigned)gs.remaining);
                }
                ++mismatched;
                ok = false;
                break;
            }
            ++steps;
        }
        if (!ok) continue;
        const es::Session &s = g_engine.session();
        CHECK(s.phase == Phase::Guess);
        CHECK(s.asked == gc.count);
        CHECK(s.guess == gc.expected_guess);
        CHECK(s.guess_reason == GuessReason::Unique);
        CHECK(s.remainingCount() == 1);
        uint8_t contra[8];
        CHECK(g_engine.contradictions(gc.target, contra, 8) == 0);
        ++games;
    }
    CHECK(mismatched == 0);
    CHECK(games == kGoldenCaseCount);
    std::printf("  %d 局 / %d 手が完全一致（食い違い %d 件）\n", games, steps, mismatched);
}

// ---------------------------------------------------------------------------
// 3・4. モードごとの総当たり
// ---------------------------------------------------------------------------
struct Tally {
    uint32_t games = 0;
    uint32_t solved = 0;
    uint32_t q_sum = 0;
    uint32_t q_min = 999;
    uint32_t q_max = 0;
};

// 候補集合と「食い違いが 0 かどうか」が必ず一致すること（設計書 2.4 / 3.2）
static void checkContradictionsMatchSet(uint8_t mode, uint16_t target)
{
    const es::Session &s = g_engine.session();
    const Mask items = Mask::from(ct::kModes[mode].items);
    uint8_t buf[8];
    for (uint16_t i = 0; i < ct::kItemCount; ++i) {
        if (!items.test(i)) continue;
        const bool alive = s.candidates.test(i);
        const bool clean = g_engine.contradictions(i, buf, 8) == 0;
        CHECK(alive == clean);
    }
    CHECK(g_engine.contradictions(target, buf, 8) == 0);
}

static void play(uint8_t mode, uint16_t target, Tally &t, bool check_invariants)
{
    g_engine.start(mode, (uint32_t)target * 2654435761u);
    uint32_t guard = 0;
    while (g_engine.session().phase == Phase::Question) {
        const es::Session &s = g_engine.session();
        const uint16_t q = s.current_question;
        if (check_invariants) {
            // 出題される質問は必ず「残候補すべてが適用範囲内」（U を含まない／E12）
            CHECK(s.candidates.subsetOf(Mask::from(ct::kQuestions[q].scope)));
            // すでに答えた質問が再び出ることは無い（設計書 3.3 の防御的確認）
            for (uint8_t i = 0; i < s.history_count; ++i) {
                if (s.history[i].active) CHECK(s.history[i].question != q);
            }
            CHECK(s.candidates.test(target));   // 真の答えは必ず候補に残っている
        }
        g_engine.answer(truthful(q, target));
        if (++guard > 64) { CHECK(false); break; }
    }
    const es::Session &s = g_engine.session();
    ++t.games;
    CHECK(s.phase == Phase::Guess);
    CHECK(s.asked <= s.maxQuestions());         // E07: 5/7/10 を超えない
    CHECK((s.remainingCount() == 1) == (s.guess_reason == GuessReason::Unique));
    if (check_invariants) checkContradictionsMatchSet(mode, target);
    if (s.guess == target) ++t.solved;
    t.q_sum += s.asked;
    if (s.asked < t.q_min) t.q_min = s.asked;
    if (s.asked > t.q_max) t.q_max = s.asked;
}

static void checkExhaustive()
{
    std::printf("[3] 各モードの全候補を正直な回答で最後まで（基準ルート）\n");
    g_engine.setAdvisor(&g_local);
    for (uint8_t m = 0; m < ct::kRefModeCount; ++m) {
        const Mask items = Mask::from(ct::kModes[m].items);
        Tally t;
        for (uint16_t i = 0; i < ct::kItemCount; ++i) {
            if (!items.test(i)) continue;
            play(m, i, t, true);
        }
        CHECK(t.solved == t.games);
        std::printf("  %-6s %3u 局 / 全部当てた %s / 問数 %u〜%u（平均 %.3f・上限 %u）\n",
                    ct::kModes[m].id, t.games, t.solved == t.games ? "はい" : "いいえ",
                    t.q_min, t.q_max, (double)t.q_sum / (double)(t.games ? t.games : 1),
                    (unsigned)ct::kModes[m].max_questions);
    }
}

// 遊び用のモード: 質問数を絞っているので「必ず当たる」ではなく、AI の勝率が狙いの範囲に入ることを確かめる。
// 同点の質問や最後の予想は乱数の種で変わるので、種を変えて何度も回した平均で見る
static void checkPlayModes()
{
    const int kSeeds = 40;
    std::printf("[3b] 遊び用のモード: 全候補 × %d 通りの種・正直な回答（AI の勝率）\n", kSeeds);
    g_engine.setAdvisor(&g_local);
    // 狙い: ミニ 80% 以上 / レギュラー 70% 以上 / フル 50% 前後 / 超難関 25% 前後（2026-09-21 ユーザー指定）
    static const double kLow[4] = {0.78, 0.68, 0.42, 0.18};
    static const double kHigh[4] = {1.00, 0.92, 0.58, 0.32};
    for (uint8_t i = 0; i < ct::kPlayModeCount; ++i) {
        const uint8_t m = (uint8_t)(ct::kPlayModeFirst + i);
        const Mask items = Mask::from(ct::kModes[m].items);
        uint32_t games = 0, solved = 0, left_sum = 0;
        for (int seed = 0; seed < kSeeds; ++seed) {
            for (uint16_t k = 0; k < ct::kItemCount; ++k) {
                if (!items.test(k)) continue;
                g_engine.start(m, (uint32_t)k * 2654435761u + (uint32_t)seed * 40503u + 17u);
                uint32_t guard = 0;
                while (g_engine.session().phase == Phase::Question) {
                    g_engine.answer(truthful(g_engine.session().current_question, k));
                    if (++guard > 64) { CHECK(false); break; }
                }
                const es::Session &ss = g_engine.session();
                CHECK(ss.phase == Phase::Guess);
                CHECK(ss.asked <= ss.maxQuestions());
                CHECK(ss.candidates.test(k));
                ++games;
                left_sum += ss.remainingCount();
                if (ss.guess == k) ++solved;
            }
        }
        const double rate = (double)solved / (double)(games ? games : 1);
        if (i < 4) CHECK(rate >= kLow[i] && rate <= kHigh[i]);
        std::printf("  %-8s %3u こ / %u 問まで / %5u 局 / AI の勝率 %.1f%% / 最後に残る候補 平均 %.2f こ\n",
                    ct::kModes[m].id, (unsigned)ct::kModes[m].item_count,
                    (unsigned)ct::kModes[m].max_questions, games, rate * 100.0,
                    (double)left_sum / (double)(games ? games : 1));
    }
}

static void checkRandomSafe()
{
    std::printf("[4] 安全な質問から乱数で選ぶ（%d seed × 全候補。設計書 16 章と同じ条件）\n",
                ESPER_RANDOM_SEEDS);
    uint32_t total = 0;
    for (uint8_t m = 0; m < ct::kRefModeCount; ++m) {
        const Mask items = Mask::from(ct::kModes[m].items);
        Tally t;
        for (int seed = 0; seed < ESPER_RANDOM_SEEDS; ++seed) {
            RandomAdvisor advisor(0xE59E1200ull + (uint64_t)seed * 131u + m);
            g_engine.setAdvisor(&advisor);
            for (uint16_t i = 0; i < ct::kItemCount; ++i) {
                if (!items.test(i)) continue;
                play(m, i, t, false);
            }
        }
        CHECK(t.solved == t.games);
        total += t.games;
        std::printf("  %-6s %5u 局 / 全部当てた %s / 問数 %u〜%u（平均 %.3f）\n",
                    ct::kModes[m].id, t.games, t.solved == t.games ? "はい" : "いいえ",
                    t.q_min, t.q_max, (double)t.q_sum / (double)(t.games ? t.games : 1));
    }
    std::printf("  合計 %u 局\n", total);
    g_engine.setAdvisor(&g_local);
}

// ---------------------------------------------------------------------------
// 5. わからない（skip）・1 つ戻る（undo）を混ぜた乱数試験
// ---------------------------------------------------------------------------
struct Snapshot {
    Mask candidates;
    es::QuestionSet blocked;
    uint16_t question;
    uint8_t asked;
};

static void checkSkipUndoFuzz()
{
    std::printf("[5] わからない・1 つ戻るを混ぜた乱数試験\n");
    g_engine.setAdvisor(&g_local);

    Rng rng(0xE59E12345678ull);
    uint32_t games = 0, skips = 0, undos = 0, solved = 0, unsolved = 0;

    for (uint32_t trial = 0; trial < 4000; ++trial) {
        const uint8_t mode = (uint8_t)rng.below(ct::kRefModeCount);
        const Mask items = Mask::from(ct::kModes[mode].items);
        const uint16_t target = items.nth(rng.below(items.count()));

        g_engine.start(mode, rng.next());
        std::vector<Snapshot> stack;
        std::vector<uint16_t> skipped;
        uint32_t guard = 0;

        while (g_engine.session().phase == Phase::Question) {
            const es::Session &s = g_engine.session();
            const uint16_t q = s.current_question;

            // 不変条件（毎手）
            CHECK(q < ct::kQuestionCount);
            CHECK(s.candidates.test(target));
            CHECK(s.candidates.subsetOf(Mask::from(ct::kQuestions[q].scope)));
            CHECK(s.asked < s.maxQuestions());
            CHECK(s.skip_count <= s.maxSkips());
            CHECK(s.undo_count <= ct::kMaxUndo);
            for (uint16_t done : skipped) {
                CHECK(q != done);        // skip した質問は二度と出ない（設計書 1.4）
            }

            const uint32_t roll = rng.below(100);
            if (roll < 18 && s.canSkip()) {
                const Mask before = s.candidates;
                const uint8_t asked_before = s.asked;
                CHECK(g_engine.answer(Answer::Skip));
                // skip は候補も問数も動かさない（E05）
                CHECK(g_engine.session().candidates == before);
                CHECK(g_engine.session().asked == asked_before);
                CHECK(g_engine.session().blocked.test(q));
                skipped.push_back(q);
                ++skips;
            } else if (roll < 34 && s.canUndo() && !stack.empty()) {
                const Snapshot want = stack.back();
                stack.pop_back();
                // 取り消した回答より後ろの skip も無効になる（設計書 8）
                while (!skipped.empty() && g_engine.session().blocked.test(skipped.back()) &&
                       !want.blocked.test(skipped.back())) {
                    skipped.pop_back();
                }
                CHECK(g_engine.undo());
                const es::Session &after = g_engine.session();
                CHECK(after.phase == Phase::Question);
                CHECK(after.candidates == want.candidates);      // E06: 履歴の再生で戻る
                CHECK(after.asked == want.asked);
                CHECK(after.current_question == want.question);
                CHECK(after.blocked == want.blocked);
                ++undos;
            } else {
                stack.push_back(Snapshot{s.candidates, s.blocked, q, s.asked});
                CHECK(g_engine.answer(truthful(q, target)));
                CHECK(g_engine.session().asked == (uint8_t)(stack.back().asked + 1));
            }
            if (++guard > 400) { CHECK(false); break; }
        }

        const es::Session &s = g_engine.session();
        ++games;
        CHECK(s.phase == Phase::Guess);
        CHECK(s.asked <= s.maxQuestions());
        CHECK(s.skip_count <= s.maxSkips());
        CHECK(s.undo_count <= ct::kMaxUndo);
        CHECK(s.candidates.test(target));      // 正直に答えた以上、答えは必ず残っている
        checkContradictionsMatchSet(mode, target);
        if (s.guess == target) {
            ++solved;
        } else {
            // 外すのは「情報が足りなかった」ときだけ。分かったふりをしない（設計書 3.4）
            CHECK(s.guess_reason == GuessReason::InsufficientInformation);
            ++unsolved;
        }
        // 予想を出した後はもう答えられない（画面が残っていても二重に進まない）
        CHECK(!g_engine.answer(Answer::Yes));
        CHECK(!g_engine.session().canSkip());
    }

    std::printf("  %u 局（わからない %u 回 / 1 つ戻る %u 回）: 当たり %u / 情報不足で外れ %u\n",
                games, skips, undos, solved, unsolved);
    CHECK(skips > 0 && undos > 0);
    CHECK(solved > 0);
}

// ---------------------------------------------------------------------------
// 6. 答え合わせまわり
// ---------------------------------------------------------------------------
static void checkVerdictAndReveal()
{
    std::printf("[6] 勝敗の確定・食い違いの説明・申告の扱い\n");
    g_engine.setAdvisor(&g_local);

    // (a) 当たったとき
    const uint16_t target = Mask::from(ct::kModes[0].items).first();
    Tally t;
    play(0, target, t, false);
    CHECK(g_engine.session().guess == target);
    CHECK(g_engine.verdict(true));
    CHECK(g_engine.session().verdict == Verdict::AiWin);
    CHECK(g_engine.session().phase == Phase::Result);
    // 2 回目は受け付けない（E09: 2 番目の予想で取り返さない）
    CHECK(!g_engine.verdict(false));
    CHECK(g_engine.session().verdict == Verdict::AiWin);
    CHECK(!g_engine.answer(Answer::Yes));
    CHECK(!g_engine.undo());

    // (b) 外れたと申告 → 本当の候補を出してもらい、食い違った回答を数える
    play(2, 100, t, false);              // HARD の適当な候補で 1 局
    const uint16_t guessed = g_engine.session().guess;
    CHECK(g_engine.verdict(false));
    CHECK(g_engine.session().verdict == Verdict::HumanWin);

    // 予想と違う候補を申告したら、必ず 1 つ以上の食い違いが出る
    uint16_t other = (uint16_t)((guessed + 1) % ct::kItemCount);
    uint8_t buf[8];
    const uint8_t n = g_engine.contradictions(other, buf, 8);
    CHECK(n >= 1);
    for (uint8_t i = 0; i < n && i < 8; ++i) {
        CHECK(buf[i] < g_engine.session().history_count);
        const es::HistoryEntry &h = g_engine.session().history[buf[i]];
        CHECK(h.active && h.answer != Answer::Skip);
        const Engine::Cell truth = Engine::cellFor(h.question, other);
        const bool matched = (truth == Engine::Cell::Yes && h.answer == Answer::Yes) ||
                             (truth == Engine::Cell::No && h.answer == Answer::No);
        CHECK(!matched);
    }
    g_engine.reveal(other);
    CHECK(g_engine.session().revealed == other);
    CHECK(g_engine.revealedWasInMode(other));            // HARD は全 128 候補
    CHECK(!g_engine.revealedWasInMode(es::kNoItem));

    // EASY の一覧に無い候補を申告したら「集計対象外」（設計書 1.2 の invalid_target）
    play(0, target, t, false);
    const Mask easy = Mask::from(ct::kModes[0].items);
    uint16_t outsider = es::kNoItem;
    for (uint16_t i = 0; i < ct::kItemCount; ++i) {
        if (!easy.test(i)) { outsider = i; break; }
    }
    CHECK(outsider != es::kNoItem);
    CHECK(!g_engine.revealedWasInMode(outsider));

    std::printf("  勝敗は 1 回だけ確定・食い違いの行が取り出せる・一覧外の申告を区別できる\n");
}

// ---------------------------------------------------------------------------
// 7. 段階 2（Jev）の差し込み口 applyAdvice
//
// **ここが「Jev が何を返してもルールは曲がらない」ことの証明**。
// 候補の絞り込み・安全な質問の計算・唯一候補の予想はコードが決めたままで、
// applyAdvice が通すのは「安全な質問の中での選び直し」と
// 「情報不足のときの予想の選び直し」だけ。
// ---------------------------------------------------------------------------

// 今の shortlist に入っていない、出題できる質問を 1 つ探す（無ければ kNoQuestion）
static uint16_t questionOutsideShortlist()
{
    const es::Shortlist &list = g_engine.shortlist();
    for (uint16_t q = 0; q < ct::kQuestionCount; ++q) {
        bool inside = false;
        for (uint8_t i = 0; i < list.count; ++i) {
            if (list.question[i] == q) { inside = true; break; }
        }
        if (!inside) return q;
    }
    return es::kNoQuestion;
}

static void checkApplyAdvice()
{
    std::printf("[7] Jev の助言（applyAdvice）の受け付けと拒否\n");
    g_engine.setAdvisor(&g_local);

    // (a) 質問の局面で、一覧の中の質問なら差し替わる／一覧の外は無視される
    uint32_t swapped = 0, rejected_out = 0, rejected_rev = 0;
    for (uint8_t m = 0; m < ct::kRefModeCount; ++m) {
        g_engine.start(m, 12345u);
        const es::Session &s = g_engine.session();
        CHECK(s.phase == Phase::Question);
        CHECK(s.revision == 1);                 // start で 0 のままにしない（段階 2 の番号）
        const uint16_t baseline = s.current_question;
        const es::Shortlist &list = g_engine.shortlist();
        CHECK(list.count >= 1);
        CHECK(list.question[0] == baseline);    // 基準は「一覧の先頭」

        // 一覧の外の質問は通らない
        const uint16_t outside = questionOutsideShortlist();
        CHECK(outside != es::kNoQuestion);
        CHECK(!g_engine.applyAdvice(outside, es::kNoItem, s.revision));
        CHECK(s.current_question == baseline);
        ++rejected_out;

        // revision が違えば、正しい質問でも無視する
        if (list.count >= 2) {
            const uint16_t other = list.question[list.count - 1];
            CHECK(other != baseline);
            CHECK(!g_engine.applyAdvice(other, es::kNoItem, (uint16_t)(s.revision + 1)));
            CHECK(s.current_question == baseline);
            ++rejected_rev;

            // 正しい revision なら差し替わる。**そのあとも出題の条件を満たしている**
            CHECK(g_engine.applyAdvice(other, es::kNoItem, s.revision));
            CHECK(s.current_question == other);
            CHECK(s.candidates.subsetOf(Mask::from(ct::kQuestions[other].scope)));
            CHECK(!s.blocked.test(other));
            ++swapped;
        }
        // 予想の局面ではないので、候補の中の item でも無視される
        const uint16_t alive = s.candidates.first();
        CHECK(!g_engine.applyAdvice(es::kNoQuestion, alive, s.revision));
        CHECK(s.guess == es::kNoItem);
    }
    CHECK(swapped > 0 && rejected_out > 0 && rejected_rev > 0);

    // (b) 予想の局面：情報不足のときだけ、残っている候補に差し替えられる
    {
        // 超難関（5 問で 128 こ）は最後に候補が複数残る＝情報不足の局面が作れる
        const uint8_t mode = (uint8_t)(ct::kPlayModeFirst + 3);
        g_engine.start(mode, 7u);
        uint32_t guard = 0;
        while (g_engine.session().phase == Phase::Question) {
            g_engine.answer(truthful(g_engine.session().current_question, 100));
            if (++guard > 64) { CHECK(false); break; }
        }
        const es::Session &s = g_engine.session();
        CHECK(s.phase == Phase::Guess);
        CHECK(s.guess_reason == GuessReason::InsufficientInformation);
        CHECK(s.remainingCount() >= 2);
        const uint16_t baseline = s.guess;

        // 候補の外の item は無視される
        uint16_t dead = es::kNoItem;
        for (uint16_t i = 0; i < ct::kItemCount; ++i) {
            if (!s.candidates.test(i)) { dead = i; break; }
        }
        CHECK(dead != es::kNoItem);
        CHECK(!g_engine.applyAdvice(es::kNoQuestion, dead, s.revision));
        CHECK(s.guess == baseline);
        // 番号としてあり得ない値も無視される
        CHECK(!g_engine.applyAdvice(es::kNoQuestion, 9999, s.revision));
        CHECK(s.guess == baseline);

        // 残っている別の候補なら差し替わる
        uint16_t other = es::kNoItem;
        for (uint16_t i = 0; i < ct::kItemCount; ++i) {
            if (s.candidates.test(i) && i != baseline) { other = i; break; }
        }
        CHECK(other != es::kNoItem);
        CHECK(!g_engine.applyAdvice(es::kNoQuestion, other, (uint16_t)(s.revision + 7)));
        CHECK(s.guess == baseline);
        CHECK(g_engine.applyAdvice(es::kNoQuestion, other, s.revision));
        CHECK(s.guess == other);
        CHECK(s.candidates.test(s.guess));

        // 勝敗を確定したあとは受け付けない
        CHECK(g_engine.verdict(false));
        CHECK(!g_engine.applyAdvice(es::kNoQuestion, baseline, s.revision));
        CHECK(s.guess == other);
    }

    // (c) 唯一候補（|S|=1）の予想は絶対に上書きしない
    {
        const uint16_t target = Mask::from(ct::kModes[0].items).first();
        Tally t;
        play(0, target, t, false);
        const es::Session &s = g_engine.session();
        CHECK(s.guess_reason == GuessReason::Unique);
        CHECK(s.remainingCount() == 1);
        const uint16_t only = s.guess;
        uint16_t other = (uint16_t)((only + 1) % ct::kItemCount);
        CHECK(!g_engine.applyAdvice(es::kNoQuestion, other, s.revision));
        CHECK(s.guess == only);
    }

    // (d) revision が「回答・わからない・1 つ戻る」で必ず動く
    {
        g_engine.start(2, 99u);
        const es::Session &s = g_engine.session();
        uint16_t rev = s.revision;
        CHECK(g_engine.answer(Answer::Yes));
        CHECK(s.revision == (uint16_t)(rev + 1));
        rev = s.revision;
        CHECK(g_engine.answer(Answer::Skip));
        CHECK(s.revision == (uint16_t)(rev + 1));
        rev = s.revision;
        CHECK(g_engine.undo());
        CHECK(s.revision == (uint16_t)(rev + 1));
        rev = s.revision;
        g_engine.guessNow();
        CHECK(s.phase == Phase::Guess);
        CHECK(s.revision == (uint16_t)(rev + 1));
    }

    std::printf("  差し替え %u 件 / 一覧外を拒否 %u 件 / revision 違いを拒否 %u 件\n",
                swapped, rejected_out, rejected_rev);
}

// Jev の返事をまねる助言者。端末と同じ順序（コアが 1 手決める → 返事を applyAdvice）で
// 差し込み、**ときどきわざと壊れた助言**（一覧外・候補外・古い revision）を混ぜる
struct ScriptedAdvice {
    uint16_t question;
    uint16_t guess;
    uint16_t revision;
};

class ScriptedJev {
public:
    explicit ScriptedJev(uint64_t seed) : rng_(seed) {}

    // いまの局面に対する「返事」を作る
    ScriptedAdvice next(const es::Session &s, const es::Shortlist &list, bool &expect_applied)
    {
        ScriptedAdvice a{es::kNoQuestion, es::kNoItem, s.revision};
        const uint32_t roll = rng_.below(100);
        expect_applied = false;
        if (s.phase == Phase::Question) {
            if (roll < 15) {
                a.question = questionOutsideShortlist();    // 壊れた返事（一覧外）
            } else if (roll < 25) {
                a.question = list.count > 0 ? list.question[rng_.below(list.count)]
                                            : es::kNoQuestion;
                a.revision = (uint16_t)(s.revision + 1);    // 古い返事
            } else if (list.count > 0) {
                a.question = list.question[rng_.below(list.count)];
                expect_applied = true;
            }
        } else if (s.phase == Phase::Guess) {
            const uint32_t n = s.remainingCount();
            if (n == 0) {
                return a;
            }
            if (roll < 15) {
                for (uint16_t i = 0; i < ct::kItemCount; ++i) {
                    if (!s.candidates.test(i)) { a.guess = i; break; }   // 候補外
                }
            } else {
                a.guess = s.candidates.nth(rng_.below(n));
                expect_applied = (s.guess_reason == GuessReason::InsufficientInformation);
            }
        }
        return a;
    }

private:
    Rng rng_;
};

// 端末の流れをそのまま再現して 1 局遊ぶ（答えのあと → applyAdvice → 次の画面）
static void playWithJev(uint8_t mode, uint16_t target, ScriptedJev &jev, Tally &t,
                        uint32_t &applied_count)
{
    g_engine.start(mode, (uint32_t)target * 2654435761u + 13u);
    uint32_t guard = 0;
    for (;;) {
        const es::Session &s = g_engine.session();
        bool expect = false;
        const ScriptedAdvice a = jev.next(s, g_engine.shortlist(), expect);
        const uint16_t before_q = s.current_question;
        const uint16_t before_guess = s.guess;
        const bool applied = g_engine.applyAdvice(a.question, a.guess, a.revision);
        CHECK(applied == expect);
        if (applied) ++applied_count;

        if (s.phase == Phase::Question) {
            const uint16_t q = s.current_question;
            CHECK(q < ct::kQuestionCount);
            if (!applied) CHECK(q == before_q);
            // Jev が選んでも出題の条件は必ず満たす（E12・設計書 3.3）
            CHECK(s.candidates.subsetOf(Mask::from(ct::kQuestions[q].scope)));
            CHECK(!s.blocked.test(q));
            for (uint8_t i = 0; i < s.history_count; ++i) {
                if (s.history[i].active) CHECK(s.history[i].question != q);
            }
            CHECK(s.candidates.test(target));
            CHECK(s.asked < s.maxQuestions());
            g_engine.answer(truthful(q, target));
        } else {
            if (!applied) CHECK(s.guess == before_guess);
            break;
        }
        if (++guard > 64) { CHECK(false); break; }
    }
    const es::Session &s = g_engine.session();
    ++t.games;
    CHECK(s.phase == Phase::Guess);
    CHECK(s.asked <= s.maxQuestions());              // E07: 問数の上限は Jev でも動かない
    CHECK(s.candidates.test(target));
    CHECK(s.guess < ct::kItemCount && s.candidates.test(s.guess));
    CHECK((s.remainingCount() == 1) == (s.guess_reason == GuessReason::Unique));
    if (s.guess_reason == GuessReason::Unique) CHECK(s.guess == target);
    checkContradictionsMatchSet(mode, target);
    if (s.guess == target) ++t.solved;
    t.q_sum += s.asked;
    if (s.asked < t.q_min) t.q_min = s.asked;
    if (s.asked > t.q_max) t.q_max = s.asked;
}

static void checkScriptedJev()
{
    std::printf("[7b] Jev の返事をまねて遊ぶ（壊れた返事を混ぜてもルールは曲がらない）\n");
    g_engine.setAdvisor(&g_local);
    uint32_t applied = 0;
    for (uint8_t m = 0; m < ct::kRefModeCount; ++m) {
        const Mask items = Mask::from(ct::kModes[m].items);
        Tally t;
        ScriptedJev jev(0xE5E00001ull + m * 131u);
        for (uint16_t i = 0; i < ct::kItemCount; ++i) {
            if (!items.test(i)) continue;
            playWithJev(m, i, jev, t, applied);
        }
        CHECK(t.solved == t.games);      // 基準モードは Jev が選んでも必ず当たる
        std::printf("  %-6s %3u 局 / 全部当てた %s / 問数 %u〜%u（平均 %.3f）\n",
                    ct::kModes[m].id, t.games, t.solved == t.games ? "はい" : "いいえ",
                    t.q_min, t.q_max, (double)t.q_sum / (double)(t.games ? t.games : 1));
    }
    // 遊び用のモードは最後に候補が残る＝予想の差し替えも通る
    for (uint8_t i = 0; i < ct::kPlayModeCount; ++i) {
        const uint8_t m = (uint8_t)(ct::kPlayModeFirst + i);
        const Mask items = Mask::from(ct::kModes[m].items);
        Tally t;
        ScriptedJev jev(0xE5E00500ull + m * 131u);
        for (uint16_t k = 0; k < ct::kItemCount; ++k) {
            if (!items.test(k)) continue;
            playWithJev(m, k, jev, t, applied);
        }
        std::printf("  %-8s %3u 局 / 当たり %u（Jev の助言でも上限・出題条件は守られた）\n",
                    ct::kModes[m].id, t.games, t.solved);
    }
    CHECK(applied > 0);
    std::printf("  助言が通った回数 %u\n", applied);
}

// ---------------------------------------------------------------------------
// 8. 端末 → GAS の要求 JSON（docs/ESPER_STAGE2_PLAN.md §4）
// ---------------------------------------------------------------------------

// "<key>":[ … ] の中の "…" を順に取り出す（自分で作った形なので簡易でよい）
static std::vector<std::string> jsonIdArray(const std::string &json, const char *key)
{
    std::vector<std::string> out;
    const std::string needle = std::string("\"") + key + "\":[";
    const size_t at = json.find(needle);
    if (at == std::string::npos) return out;
    size_t i = at + needle.size();
    while (i < json.size() && json[i] != ']') {
        if (json[i] == '"') {
            const size_t end = json.find('"', i + 1);
            if (end == std::string::npos) break;
            out.push_back(json.substr(i + 1, end - i - 1));
            i = end + 1;
        } else {
            ++i;
        }
    }
    return out;
}

// 要求 JSON が「送ってよいものだけ」でできているか（設計書 4.2 / 計画 §1）
static void validateRequestJson(const std::string &json, const es::Session &s,
                                const es::Shortlist &list)
{
    CHECK(json.size() <= es::kRequestJsonMax);      // 依頼箱に入る
    CHECK(json.size() >= 2 && json.front() == '{' && json.back() == '}');

    // 端末は ID しか送らない。日本語（非 ASCII）が 1 バイトでもあれば不合格
    bool ascii = true;
    for (char ch : json) {
        if ((unsigned char)ch > 0x7E || (unsigned char)ch < 0x20) ascii = false;
    }
    CHECK(ascii);

    // 送ってはいけないものの名残が無いこと
    static const char *kForbidden[] = {"memo", "reveal", "target", "secret", "name",
                                       "definition", "text", "seed", "device", "token",
                                       "cup", "player"};
    for (const char *bad : kForbidden) {
        CHECK(json.find(std::string("\"") + bad) == std::string::npos);
    }

    // 括弧の対応
    int braces = 0, brackets = 0;
    for (char ch : json) {
        if (ch == '{') ++braces;
        if (ch == '}') --braces;
        if (ch == '[') ++brackets;
        if (ch == ']') --brackets;
        CHECK(braces >= 0 && brackets >= 0);
    }
    CHECK(braces == 0 && brackets == 0);

    CHECK(json.find("\"event\":\"esper\"") != std::string::npos);
    CHECK(json.find("\"req\":") != std::string::npos);
    CHECK(json.find("\"session\":\"") != std::string::npos);
    CHECK(json.find("\"history\":[") != std::string::npos);
    CHECK(json.find("\"candidates\":[") != std::string::npos);
    CHECK(json.find("\"shortlist\":[") != std::string::npos);
    char want[64];
    std::snprintf(want, sizeof(want), "\"rev\":%u,", (unsigned)s.revision);
    CHECK(json.find(want) != std::string::npos);
    std::snprintf(want, sizeof(want), "\"mode\":\"%s\"", s.modeInfo().id);
    CHECK(json.find(want) != std::string::npos);
    std::snprintf(want, sizeof(want), "\"remaining\":%u}", (unsigned)s.remainingQuestions());
    CHECK(json.find(want) != std::string::npos);

    // 残った候補：カタログにある ID で、重複なし・128 件以下・S と完全一致
    const std::vector<std::string> cands = jsonIdArray(json, "candidates");
    CHECK(cands.size() == (size_t)s.remainingCount());
    CHECK(cands.size() <= 128);
    Mask seen;
    for (const std::string &id : cands) {
        uint16_t at = es::kNoItem;
        for (uint16_t i = 0; i < ct::kItemCount; ++i) {
            if (id == ct::kItems[i].id) { at = i; break; }
        }
        CHECK(at != es::kNoItem);                   // カタログにある
        if (at == es::kNoItem) continue;
        CHECK(!seen.test(at));                      // 重複なし
        seen.set(at);
        CHECK(s.candidates.test(at));               // いま残っている候補だけ
    }
    CHECK(seen == s.candidates);

    // 安全な質問：質問の局面のときだけ入り、8 件以下・shortlist と完全一致
    const std::vector<std::string> shorts = jsonIdArray(json, "shortlist");
    CHECK(shorts.size() <= es::kShortlistMax);
    if (s.phase != Phase::Question) {
        CHECK(shorts.empty());
    } else {
        CHECK(shorts.size() == (size_t)list.count);
        for (size_t k = 0; k < shorts.size() && k < list.count; ++k) {
            CHECK(shorts[k] == ct::kQuestions[list.question[k]].id);
        }
    }

    // 履歴：有効な Yes/No の数と並びが合っている
    size_t active = 0;
    for (uint8_t i = 0; i < s.history_count; ++i) {
        if (s.history[i].active && s.history[i].answer != Answer::Skip) ++active;
    }
    // {"q":"Q001","a":"yes"} なので、1 行につき "q" / ID / "a" / 答え の 4 つ
    const std::vector<std::string> hist = jsonIdArray(json, "history");
    CHECK(hist.size() == active * 4);
    size_t at = 0;
    for (uint8_t i = 0; i < s.history_count; ++i) {
        const es::HistoryEntry &h = s.history[i];
        if (!h.active || h.answer == Answer::Skip) continue;
        if (at + 3 >= hist.size()) break;
        CHECK(hist[at] == "q");
        CHECK(hist[at + 1] == ct::kQuestions[h.question].id);
        CHECK(hist[at + 2] == "a");
        CHECK(hist[at + 3] == (h.answer == Answer::Yes ? "yes" : "no"));
        at += 4;
    }
}

static void checkRequestJson()
{
    std::printf("[8] 端末 → GAS の要求 JSON（ID だけ・依頼箱に収まる）\n");
    g_engine.setAdvisor(&g_local);
    static char buf[es::kRequestJsonMax];
    const char *session_id = "0123456789abcdef0123456789abcdef";

    size_t biggest = 0;
    uint32_t built = 0, skipped_guess = 0;
    std::string sample;

    for (uint8_t m = 0; m < ct::kModeCount; ++m) {
        const Mask items = Mask::from(ct::kModes[m].items);
        for (uint16_t k = 0; k < ct::kItemCount; k += 7) {
            if (!items.test(k)) continue;
            g_engine.start(m, (uint32_t)k * 2654435761u);
            uint32_t guard = 0;
            for (;;) {
                const es::Session &s = g_engine.session();
                const size_t n = es::buildRequestJson(s, g_engine.shortlist(),
                                                      (uint32_t)(built + 1), session_id,
                                                      buf, sizeof(buf));
                CHECK(n > 0);
                CHECK(n == std::strlen(buf));
                const std::string json(buf, n);
                validateRequestJson(json, s, g_engine.shortlist());
                if (n > biggest) { biggest = n; }
                if (sample.empty() && s.phase == Phase::Question && s.asked == 2) {
                    sample = json;
                }
                ++built;
                if (s.phase != Phase::Question) { ++skipped_guess; break; }
                g_engine.answer(truthful(s.current_question, k));
                if (++guard > 64) { CHECK(false); break; }
            }
        }
    }

    // 入りきらない大きさを渡したら、途中まで書いた文字列を送らない（0 を返す）
    {
        g_engine.start(2, 1u);
        char tiny[40];
        CHECK(es::buildRequestJson(g_engine.session(), g_engine.shortlist(), 1, session_id,
                                   tiny, sizeof(tiny)) == 0);
        CHECK(es::buildRequestJson(g_engine.session(), g_engine.shortlist(), 1, session_id,
                                   nullptr, 0) == 0);
    }

    std::printf("  %u 通ぶんを検査（最終予想の局面 %u 通）/ いちばん大きい要求 %u バイト（上限 %u）\n",
                built, skipped_guess, (unsigned)biggest, (unsigned)es::kRequestJsonMax);
    if (!sample.empty()) {
        std::printf("  例: %.400s%s\n", sample.c_str(), sample.size() > 400 ? "…" : "");
    }
}

// ---------------------------------------------------------------------------
int main()
{
    std::printf("=== エスパー対決 推論コアの試験 ===\n");
    std::printf("カタログ %s / SHA-256 %.16s…\n\n", ct::kCatalogVersion, ct::kCatalogSha256);

    checkContent();
    std::printf("\n");
    checkGolden();
    std::printf("\n");
    g_engine.resetCounters();
    checkExhaustive();
    std::printf("  覚え書き: 当たり %u / 外れ %u / 再帰の最大の深さ %u 段\n",
                g_engine.memoHits(), g_engine.memoMisses(), g_engine.maxDepth());
    std::printf("\n");
    checkPlayModes();
    std::printf("\n");
    checkRandomSafe();
    std::printf("\n");
    checkSkipUndoFuzz();
    std::printf("\n");
    checkVerdictAndReveal();
    std::printf("\n");
    checkApplyAdvice();
    std::printf("\n");
    checkScriptedJev();
    std::printf("\n");
    checkRequestJson();
    std::printf("\n");

    std::printf("=== 確認 %d 件 / 失敗 %d 件 ===\n", g_checks, g_fail);
    if (g_fail == 0) std::printf("ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
