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
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../firmware/src/app/games/esper/core/esper_core.hpp"
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
    CHECK(ct::kModeCount == 3);

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
    for (uint8_t m = 0; m < ct::kModeCount; ++m) {
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

static void checkRandomSafe()
{
    std::printf("[4] 安全な質問から乱数で選ぶ（%d seed × 全候補。設計書 16 章と同じ条件）\n",
                ESPER_RANDOM_SEEDS);
    uint32_t total = 0;
    for (uint8_t m = 0; m < ct::kModeCount; ++m) {
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
        const uint8_t mode = (uint8_t)rng.below(ct::kModeCount);
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
    checkRandomSafe();
    std::printf("\n");
    checkSkipUndoFuzz();
    std::printf("\n");
    checkVerdictAndReveal();
    std::printf("\n");

    std::printf("=== 確認 %d 件 / 失敗 %d 件 ===\n", g_checks, g_fail);
    if (g_fail == 0) std::printf("ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
