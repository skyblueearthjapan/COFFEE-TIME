// POKER TABLE（CAFE CARDS）のロジック試験。PC 上で走らせる（tools/run_cards_checks.py が組み立てる）。
//
//   0. 設計一式のコア（cards_core.hpp / local_policy.hpp）が無改変か … run_cards_checks.py が SHA-256 で確認
//   1. 原本 tests/core_test.cpp 相当（札・役・ベットの全経路・交換 1024 通り・GOPS・31・バカラ）
//   2. 原本 tests/local_test.cpp 相当（端末 AI が必ず合法な行動を返すか）
//   3. 3 枚の手 22,100 通りを独立実装と突き合わせ
//   4. バカラの追加札の表を独立に書いた表と全件突き合わせ
//   5. 4 ゲームを「端末 AI 同士」「でたらめ 対 端末 AI」で多数完走し、不変条件を毎手確かめる
//   6. その対局から観測 JSON を採り、argv[1] に書き出す（Node が gas/CardsGate.gs に通す）
//   7. 役のキーを argv[2] に書き出す（Node が gas/CardsContract.gs の pokerKey と突き合わせる）
//
// **すべての判定は CHECK（失敗しても走り切って最後に数える）**。assert は使わない。

#include "../firmware/src/app/games/cards/core/cards_extra.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#ifdef NDEBUG
#error "NDEBUG がついた状態では試験になりません（run_cards_checks.py は -UNDEBUG を付けます）"
#endif

using namespace cafe_cards;
namespace ct = coffee::cards;

static std::mt19937 g_rng(20260923);
static uint32_t next32(void *p) { return (*static_cast<std::mt19937 *>(p))(); }
static Card C(int r, int s) { return Card(s * 13 + r - 2); }

static int g_groups = 0;
static int g_failures = 0;
static int g_group_mark = 0;        // その群に入ったときの失敗数

#define CHECK(cond, msg) \
    do { if (!(cond)) { ++g_failures; std::printf("  **FAIL** %s\n", msg); } } while (false)

// 群の合否は「その群のあいだに失敗が増えたか」で決める（PASS を勝手に出さない）
#define GROUP(name)                                                              \
    do {                                                                         \
        if (g_failures == g_group_mark) {                                        \
            ++g_groups;                                                          \
            std::printf("  PASS %s\n", name);                                    \
        } else {                                                                 \
            std::printf("  **FAIL** %s（%d 件）\n", name, g_failures - g_group_mark); \
        }                                                                        \
        g_group_mark = g_failures;                                               \
    } while (false)

static constexpr int kExpectedGroups = 23;      // ＋ ホールデムの 7 枚評価 ＋ ホールデムの試合

// ---------------------------------------------------------------------------
// 1. 原本 tests/core_test.cpp 相当
// ---------------------------------------------------------------------------
static std::vector<BetAction> actionsOf(const Street &s)
{
    std::vector<BetAction> a;
    for (int i = 0; i < 5; ++i) {
        if (s.legal(BetAction(i))) {
            a.push_back(BetAction(i));
        }
    }
    return a;
}

static long long g_street_paths = 0;
static void walk(Street s, int depth)
{
    CHECK(depth <= 5, "ベットの枝が 5 手を超えた");
    if (depth > 5) {
        return;
    }
    if (s.closed) {
        CHECK(s.paid[0] <= 3 * s.unit && s.paid[1] <= 3 * s.unit, "拠出が単位の 3 倍を超えた");
        if (s.fold_winner < 0) {
            CHECK(s.paid[0] == s.paid[1], "降りていないのに拠出が揃っていない");
        }
        ++g_street_paths;
        return;
    }
    const auto as = actionsOf(s);
    CHECK(!as.empty(), "合法なベットが 1 つもない");
    for (auto a : as) {
        Street n = s;
        int delta = -1;
        const bool ok = n.apply(s.actor, a, delta);
        CHECK(ok, "合法なはずのベットを適用できない");
        CHECK(delta >= 0, "支払いが負になった");
        if (ok) {
            walk(n, depth + 1);
        }
    }
}

// 独立に書き起こしたバンカーの追加表（行 = バンカーの初期合計、列 = PLAYER の 3 枚目）
static const char *TABLE[8] = {"1111111111", "1111111111", "1111111111", "1111111101",
                               "0011111100", "0000111100", "0000001100", "0000000000"};

static BaccaratResult reference_bac(const std::array<int, 6> &v)
{
    BaccaratResult r;
    r.valid = true;
    r.p = {v[0], v[2], -1};
    r.b = {v[1], v[3], -1};
    r.np = r.nb = 2;
    r.used = 4;
    const int p0 = (v[0] + v[2]) % 10, b0 = (v[1] + v[3]) % 10;
    r.pt = p0;
    r.bt = b0;
    r.natural = p0 > 7 || b0 > 7;
    if (!r.natural) {
        const bool phit = p0 < 6;
        if (phit) { r.p[2] = v[4]; r.np = 3; r.pt = (p0 + v[4]) % 10; ++r.used; }
        const bool bhit = phit ? TABLE[b0][v[4]] == '1' : b0 < 6;
        if (bhit) { r.b[2] = v[phit ? 5 : 4]; r.nb = 3; r.bt = (b0 + r.b[2]) % 10; ++r.used; }
    }
    r.winner = r.pt == r.bt ? 2 : (r.pt > r.bt ? 0 : 1);
    return r;
}

// ---------------------------------------------------------------------------
// 7. 役のキー（Node が gas/CardsContract.gs の pokerKey と突き合わせる）
// ---------------------------------------------------------------------------
struct KeyCase { std::string cards, key; };
static std::vector<KeyCase> g_keys;
static unsigned g_key_categories = 0;

static void addKeyCase(const std::array<Card, 5> &h)
{
    const PokerValue v = poker_value(h);
    CHECK(v.valid, "役のキーを作れない手が混ざっている");
    if (!v.valid) {
        return;
    }
    g_key_categories |= 1u << v.key[0];
    std::string cards = "[";
    for (int i = 0; i < 5; ++i) {
        char name[ct::kCardNameMax];
        ct::cardName(h[i], name);
        cards += i ? ",\"" : "\"";
        cards += name;
        cards += "\"";
    }
    cards += "]";
    char key[64];
    std::snprintf(key, sizeof(key), "[%d,%d,%d,%d,%d,%d]", v.key[0], v.key[1], v.key[2], v.key[3],
                  v.key[4], v.key[5]);
    g_keys.push_back({cards, key});
}

static void collectPokerKeys()
{
    std::printf("[7] 役のキーを集める（Node が gas/CardsContract.gs の pokerKey と照合する）\n");
    // 9 つの役ぜんぶと、比べ方が効く例（ホイール・キッカー違い）を手で並べる
    addKeyCase({C(10, 3), C(11, 3), C(12, 3), C(13, 3), C(14, 3)});   // ロイヤル
    addKeyCase({C(14, 1), C(2, 1), C(3, 1), C(4, 1), C(5, 1)});       // 5 が上のストレートフラッシュ
    addKeyCase({C(9, 0), C(10, 0), C(11, 0), C(12, 0), C(13, 0)});    // 並のストレートフラッシュ
    addKeyCase({C(13, 0), C(13, 1), C(13, 2), C(13, 3), C(14, 0)});   // フォーカード
    addKeyCase({C(12, 0), C(12, 1), C(12, 2), C(7, 0), C(7, 1)});     // フルハウス
    addKeyCase({C(2, 2), C(5, 2), C(9, 2), C(11, 2), C(13, 2)});      // フラッシュ
    addKeyCase({C(5, 0), C(6, 1), C(7, 2), C(8, 3), C(9, 0)});        // ストレート
    addKeyCase({C(14, 0), C(2, 1), C(3, 2), C(4, 3), C(5, 0)});       // ホイール（フラッシュでない）
    addKeyCase({C(8, 0), C(8, 1), C(8, 2), C(14, 3), C(2, 0)});       // スリーカード
    addKeyCase({C(14, 0), C(14, 1), C(13, 2), C(13, 3), C(12, 0)});   // ツーペア（キッカー Q）
    addKeyCase({C(14, 0), C(14, 1), C(13, 2), C(13, 3), C(11, 0)});   // ツーペア（キッカー J）
    addKeyCase({C(9, 0), C(9, 1), C(14, 2), C(7, 3), C(3, 0)});       // ワンペア
    addKeyCase({C(14, 0), C(13, 1), C(9, 2), C(5, 3), C(3, 0)});      // ハイカード
    // でたらめな手も混ぜる（9 つの役の出方に偏りがあるので数を多めに）
    std::mt19937 rng(31337);
    for (int i = 0; i < 600; ++i) {
        Deck d;
        if (!d.init(1, next32, &rng)) {
            CHECK(false, "山札を作れない");
            return;
        }
        std::array<Card, 5> h{};
        for (auto &c : h) {
            d.take(c);
        }
        addKeyCase(h);
    }
    CHECK(g_key_categories == 0x1FFu, "9 つの役が全部そろっていない");
    CHECK(g_keys.size() >= 600, "役のキーの例が少なすぎる");
    GROUP("役のキーの見本（9 つの役すべて）");
}

// ---------------------------------------------------------------------------
// ホールデム: 7 枚から最良の 5 枚（計画 §5b）を、まったく別の書き方の評価器と比べる。
// 参照側は 5 枚の組み合わせを一切作らず、rank / suit の数え上げだけで役を決める
// ---------------------------------------------------------------------------
static int referenceCategory7(const Card *c, int n)
{
    int rank_count[15] = {};
    int suit_count[4] = {};
    bool by_suit[4][15] = {};
    for (int i = 0; i < n; ++i) {
        const int r = rank(c[i]), s = suit(c[i]);
        ++rank_count[r];
        ++suit_count[s];
        by_suit[s][r] = true;
    }
    auto straightHigh = [](const bool *has) {
        for (int hi = 14; hi >= 5; --hi) {
            bool all = true;
            for (int k = 0; k < 5; ++k) {
                const int r = hi - k;
                all = all && has[r == 1 ? 14 : r];
            }
            if (all) {
                return hi;
            }
        }
        // A2345（A を 1 として扱う）
        if (has[14] && has[2] && has[3] && has[4] && has[5]) {
            return 5;
        }
        return 0;
    };
    for (int s = 0; s < 4; ++s) {
        if (suit_count[s] >= 5 && straightHigh(by_suit[s]) > 0) {
            return 8;       // ストレートフラッシュ
        }
    }
    int quads = 0, trips = 0, pairs = 0;
    for (int r = 2; r <= 14; ++r) {
        if (rank_count[r] == 4) ++quads;
        else if (rank_count[r] == 3) ++trips;
        else if (rank_count[r] == 2) ++pairs;
    }
    if (quads) return 7;
    if (trips >= 2 || (trips == 1 && pairs >= 1)) return 6;     // フルハウス
    for (int s = 0; s < 4; ++s) {
        if (suit_count[s] >= 5) return 5;
    }
    bool has[15] = {};
    for (int r = 2; r <= 14; ++r) {
        has[r] = rank_count[r] > 0;
    }
    if (straightHigh(has) > 0) return 4;
    if (trips == 1) return 3;
    if (pairs >= 2) return 2;
    if (pairs == 1) return 1;
    return 0;
}

// 参照側の「6 つの数字」まるごと。5 枚の組み合わせを一切作らず、
// rank / suit の数え上げだけで poker_value と同じキーを組み立てる
static std::array<int, 6> referenceKey7(const Card *c, int n)
{
    int rank_count[15] = {};
    int suit_count[4] = {};
    bool by_suit[4][15] = {};
    for (int i = 0; i < n; ++i) {
        const int r = rank(c[i]), s = suit(c[i]);
        ++rank_count[r];
        ++suit_count[s];
        by_suit[s][r] = true;
    }
    auto straightHigh = [](const bool *has) {
        for (int hi = 14; hi >= 6; --hi) {
            bool all = true;
            for (int k = 0; k < 5; ++k) {
                all = all && has[hi - k];
            }
            if (all) {
                return hi;
            }
        }
        return (has[14] && has[2] && has[3] && has[4] && has[5]) ? 5 : 0;   // ホイール
    };
    // 8: ストレートフラッシュ
    for (int s = 0; s < 4; ++s) {
        if (suit_count[s] >= 5) {
            const int hi = straightHigh(by_suit[s]);
            if (hi > 0) {
                return {8, hi, 0, 0, 0, 0};
            }
        }
    }
    int quad = 0, trips_hi = 0, trips_lo = 0, pair_hi = 0, pair_lo = 0;
    for (int r = 14; r >= 2; --r) {
        if (rank_count[r] == 4 && !quad) quad = r;
        else if (rank_count[r] == 3) { if (!trips_hi) trips_hi = r; else if (!trips_lo) trips_lo = r; }
        else if (rank_count[r] == 2) { if (!pair_hi) pair_hi = r; else if (!pair_lo) pair_lo = r; }
    }
    auto topOther = [&](int skip_a, int skip_b, int nth) {
        int seen = 0;
        for (int r = 14; r >= 2; --r) {
            if (r == skip_a || r == skip_b || rank_count[r] == 0) continue;
            if (++seen == nth) return r;
        }
        return 0;
    };
    // 7: フォーカード
    if (quad) {
        return {7, quad, topOther(quad, 0, 1), 0, 0, 0};
    }
    // 6: フルハウス（3 枚組が 2 つなら高いほうを 3 枚に、低いほうを 2 枚に使う）
    if (trips_hi && (trips_lo || pair_hi)) {
        const int p = trips_lo > pair_hi ? trips_lo : pair_hi;
        return {6, trips_hi, p, 0, 0, 0};
    }
    // 5: フラッシュ（そのスートの高い 5 枚）
    for (int s = 0; s < 4; ++s) {
        if (suit_count[s] >= 5) {
            std::array<int, 6> key = {5, 0, 0, 0, 0, 0};
            int at = 1;
            for (int r = 14; r >= 2 && at < 6; --r) {
                if (by_suit[s][r]) key[at++] = r;
            }
            return key;
        }
    }
    // 4: ストレート
    {
        bool has[15] = {};
        for (int r = 2; r <= 14; ++r) has[r] = rank_count[r] > 0;
        const int hi = straightHigh(has);
        if (hi > 0) return {4, hi, 0, 0, 0, 0};
    }
    // 3: スリーカード
    if (trips_hi) {
        return {3, trips_hi, topOther(trips_hi, 0, 1), topOther(trips_hi, 0, 2), 0, 0};
    }
    // 2: ツーペア（余りの最高位が kicker。3 組目のペアでもよい）
    if (pair_hi && pair_lo) {
        return {2, pair_hi, pair_lo, topOther(pair_hi, pair_lo, 1), 0, 0};
    }
    // 1: ワンペア
    if (pair_hi) {
        return {1, pair_hi, topOther(pair_hi, 0, 1), topOther(pair_hi, 0, 2),
                topOther(pair_hi, 0, 3), 0};
    }
    // 0: ハイカード
    return {0, topOther(0, 0, 1), topOther(0, 0, 2), topOther(0, 0, 3), topOther(0, 0, 4),
            topOther(0, 0, 5)};
}

// 選び方のコードとは**別に** 21 通りを数え直し、選ばれた 5 枚より強い組み合わせが
// 無いことを確かめる（bestFive のループ範囲や比べ方の取りこぼしをここで捕まえる）
static bool noBetterCombo(const Card *c, int n, const std::array<int, 6> &best)
{
    for (int a = 0; a < n - 4; ++a) {
        for (int b = a + 1; b < n - 3; ++b) {
            for (int d = b + 1; d < n - 2; ++d) {
                for (int e = d + 1; e < n - 1; ++e) {
                    for (int f = e + 1; f < n; ++f) {
                        const PokerValue v =
                            poker_value({c[a], c[b], c[d], c[e], c[f]});
                        if (v.valid && v.key > best) {
                            return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}

static void holdemEvaluatorTests()
{
    std::printf("[8] ホールデム: 7 枚の最良役を独立実装と突き合わせる\n");
    std::mt19937 rng(987654321u);
    long long boards = 0;
    for (int i = 0; i < 20000; ++i) {
        Deck d;
        if (!d.init(1, next32, &rng)) {
            CHECK(false, "山札を作れない");
            break;
        }
        Card seven[7];
        for (auto &c : seven) {
            d.take(c);
        }
        const ct::Best5 best = ct::bestFive(seven, 7);
        CHECK(best.valid, "7 枚から 5 枚を選べない");
        if (!best.valid) {
            break;
        }
        CHECK(best.value.key[0] == referenceCategory7(seven, 7),
              "7 枚の役の種類が独立実装と違う");
        // 同点の決め方（6 つの数字）まで独立実装と合うか
        CHECK(best.value.key == referenceKey7(seven, 7), "7 枚の役のキーが独立実装と違う");
        // 21 通りを数え直して、これより強い組み合わせが無いか
        CHECK(noBetterCombo(seven, 7, best.value.key), "もっと強い 5 枚の組み合わせがある");
        // 選ばれた 5 枚が本当に 7 枚の部分集合か
        for (auto picked : best.cards) {
            bool found = false;
            for (auto c : seven) {
                found = found || c == picked;
            }
            CHECK(found, "選んだ 5 枚が元の 7 枚に無い");
        }
        ++boards;
        if (g_failures != g_group_mark) {
            break;
        }
    }
    CHECK(boards == 20000, "7 枚の手を 20,000 通り試していない");

    // 端の場合を名指しで
    {
        // 場が A-2-3-4、手札に 5 → 5 が上のストレート
        const std::array<Card, 2> hole = {C(5, 0), C(13, 1)};
        const Card board[5] = {C(14, 2), C(2, 3), C(3, 0), C(4, 1), C(9, 2)};
        const ct::Best5 b = ct::holdemBest(hole, board, 5);
        CHECK(b.valid && b.value.key[0] == 4 && b.value.key[1] == 5, "場のホイールを拾えない");
    }
    {
        // 6 枚同スート → いちばん高い 5 枚のフラッシュ
        const std::array<Card, 2> hole = {C(3, 3), C(4, 3)};
        const Card board[5] = {C(7, 3), C(9, 3), C(11, 3), C(13, 3), C(2, 0)};
        const ct::Best5 b = ct::holdemBest(hole, board, 5);
        CHECK(b.valid && b.value.key[0] == 5 && b.value.key[1] == 13 && b.value.key[2] == 11 &&
                  b.value.key[3] == 9 && b.value.key[4] == 7 && b.value.key[5] == 4,
              "6 枚同スートから高い 5 枚を選べない");
    }
    {
        // フルハウスが 2 通り（KKK+99 と KKK+77）→ 高いペアを選ぶ
        const std::array<Card, 2> hole = {C(13, 0), C(13, 1)};
        const Card board[5] = {C(13, 2), C(9, 0), C(9, 1), C(7, 2), C(7, 3)};
        const ct::Best5 b = ct::holdemBest(hole, board, 5);
        CHECK(b.valid && b.value.key[0] == 6 && b.value.key[1] == 13 && b.value.key[2] == 9,
              "フルハウスの組み方を間違えている");
    }
    {
        // 場だけで決まる（双方とも場のストレート）→ 引き分け
        const std::array<Card, 2> a = {C(2, 0), C(3, 1)};
        const std::array<Card, 2> b = {C(2, 2), C(3, 3)};
        const Card board[5] = {C(10, 0), C(11, 1), C(12, 2), C(13, 3), C(14, 0)};
        const ct::Best5 ba = ct::holdemBest(a, board, 5);
        const ct::Best5 bb = ct::holdemBest(b, board, 5);
        CHECK(ba.valid && bb.valid && ba.value.key == bb.value.key,
              "場だけで決まる局面が引き分けにならない");
    }
    std::printf("  7 枚の手 %lld 通り ＋ 端の場合 4 件\n", boards);
    GROUP("ホールデムの 7 枚評価");
}

static void portableCoreTests()
{
    std::printf("[1] 原本 tests/core_test.cpp 相当\n");
    for (int i = 0; i < 416; ++i) {
        CHECK(rank(Card(i)) >= 2 && rank(Card(i)) <= 14, "rank が範囲外");
        CHECK(suit(Card(i)) >= 0 && suit(Card(i)) < 4, "suit が範囲外");
    }
    CHECK(baccarat_value(C(14, 0)) == 1, "バカラの A は 1 点");
    CHECK(value31(C(14, 0)) == 11, "31 の A は 11 点");
    CHECK(baccarat_value(C(10, 0)) == 0, "バカラの 10 は 0 点");
    GROUP("札の値");

    for (int copies : {1, 8}) {
        for (int k = 0; k < 60; ++k) {
            Deck d;
            CHECK(d.init(copies, next32, &g_rng), "山札を作れない");
            CHECK(d.valid(), "山札に重複がある");
            Card c;
            for (int i = 0; i < d.size; ++i) {
                CHECK(d.take(c), "山札から引けない");
            }
            CHECK(!d.take(c), "引き切った山札から引けてしまう");
        }
    }
    GROUP("シャッフルの重複なしと引き切り");

    {
        Deck bad;
        CHECK(!bad.init(2, next32, &g_rng), "2 組の山札を断らない");
        CHECK(!bad.init(1, nullptr, nullptr), "乱数なしの山札を断らない");
        Card dup[2] = {0, 0};
        CHECK(!valid_cards(dup, 2), "同じ札 2 枚を断らない");
    }
    GROUP("壊れた山札を断る");

    {
        const auto royal = poker_value({C(10, 0), C(11, 0), C(12, 0), C(13, 0), C(14, 0)});
        CHECK(royal.key[0] == 8 && royal.key[1] == 14, "ロイヤルの判定が違う");
        const auto wheel = poker_value({C(14, 0), C(2, 1), C(3, 2), C(4, 3), C(5, 0)});
        CHECK(wheel.key[0] == 4 && wheel.key[1] == 5, "A2345 は 5 が上のストレート");
        CHECK(!poker_value({0, 0, 1, 2, 3}).valid, "同じ札を含む手を断らない");
        CHECK(!poker_value({0, 1, 2, 3, 52}).valid, "範囲外の札を断らない");
        const std::array<Card, 5> a = {C(8, 0), C(8, 1), C(14, 2), C(9, 0), C(7, 1)};
        auto b = a;
        b[4] = C(6, 1);
        CHECK(poker_compare(a, b) == 1, "キッカーの比べ方が違う");
        b = a;
        for (auto &c : b) {
            c = Card((c + 13) % 52);
        }
        CHECK(poker_compare(a, b) == 0, "スートで優劣が付いてしまう");
    }
    GROUP("役・キッカー・ホイール・スートの同値");

    for (int who = 0; who < 2; ++who) {
        for (int unit : {2, 4}) {
            Street s;
            s.actor = who;
            s.unit = unit;
            walk(s, 0);
        }
    }
    CHECK(g_street_paths > 0, "ベットの経路を 1 つも歩いていない");
    std::printf("  ベットの終端 %lld 通り\n", g_street_paths);
    GROUP("ベットの全経路");

    {
        Street s;
        int debit = 0;
        CHECK(!s.apply(1, BetAction::Bet, debit), "手番でない側のベットを通してしまう");
        CHECK(!s.apply(0, BetAction::Call, debit), "owed=0 の CALL を通してしまう");
        CHECK(!s.apply(0, BetAction::Fold, debit), "owed=0 の FOLD を通してしまう");
        CHECK(s.apply(0, BetAction::Bet, debit), "最初の BET ができない");
        CHECK(!s.legal(BetAction::Check), "BET のあとに CHECK ができてしまう");
    }
    GROUP("違法なベットを断る");

    {
        Deck d;
        CHECK(d.init(1, next32, &g_rng), "山札を作れない");
        int mask_pairs = 0;
        for (int m0 = 0; m0 < 32; ++m0) {
            for (int m1 = 0; m1 < 32; ++m1) {
                PokerHand p;
                CHECK(p.start(d, 0, {100, 100}), "ハンドを始められない");
                CHECK(p.bet(1, BetAction::Check), "CHECK できない");
                CHECK(p.bet(0, BetAction::Check), "CHECK できない");
                const auto old = p.hands;
                CHECK(p.draw(0, m0), "交換を受け付けない");
                CHECK(p.hands == old, "片方だけの交換で札が動いた");
                CHECK(!p.draw(0, m0), "同じ側が 2 回交換できてしまう");
                CHECK(p.draw(1, m1), "交換を受け付けない");
                Card all[20];
                int k = 0;
                for (auto &h : p.hands) {
                    for (auto c : h) {
                        all[k++] = c;
                    }
                }
                for (int i = 0; i < p.discard_count; ++i) {
                    all[k++] = p.discards[i];
                }
                CHECK(valid_cards(all, k), "交換で札が重複した");
                CHECK(p.deck.cursor == 10 + __builtin_popcount(unsigned(m0)) +
                                           __builtin_popcount(unsigned(m1)),
                      "交換で引いた枚数が合わない");
                CHECK(p.phase == PokerPhase::Post, "交換後の段階へ進んでいない");
                ++mask_pairs;
            }
        }
        CHECK(mask_pairs == 1024, "交換の組み合わせが 1024 通りではない");
        PokerHand p;
        CHECK(p.start(d, 0, {100, 100}), "ハンドを始められない");
        CHECK(p.bet(1, BetAction::Bet), "BET できない");
        CHECK(p.bet(0, BetAction::Fold), "FOLD できない");
        CHECK(p.folded && p.winner == 1 && p.pot == 0 && p.stack[0] + p.stack[1] == 200,
              "フォールドの精算が違う");
    }
    GROUP("交換 1024 通りとフォールドの精算");

    {
        std::array<int, 13> ord{};
        for (int i = 0; i < 13; ++i) {
            ord[i] = i + 1;
        }
        Gops g;
        CHECK(!g.init(8, ord), "8 枚の GOPS を断らない");
        CHECK(g.init(7, ord), "7 枚の GOPS を始められない");
        for (int i = 1; i <= 7; ++i) {
            g.choose(0, i);
            g.choose(1, i);
        }
        CHECK(g.burned == 28, "同じ札のとき得点が破棄されていない");
        CHECK(g.winner() == DRAW, "全部同じ札なら引き分け");
    }
    GROUP("GOPS の同じ札は得点を破棄");

    CHECK(score31({C(14, 3), C(13, 3), C(12, 3)}) == 31, "A+K+Q 同スートは 31");
    CHECK(score31({C(13, 0), C(13, 1), C(13, 2)}) == 10, "3 枚組の特典はない");
    CHECK(score31({0, 0, 1}) == -1, "重複した手を断らない");
    GROUP("31 の得点に 3 枚組の特典なし");

    {
        ThirtyOne t;
        const std::array<Card, 9> natural = {C(14, 0), C(14, 1), C(13, 0), C(13, 1),
                                             C(12, 0), C(12, 1), 0, 1, 2};
        CHECK(t.init(natural, 0), "初期配布を受け付けない");
        CHECK(t.finished && t.winner == DRAW, "双方 31 は即引き分け");
        const std::array<Card, 9> simple = {C(2, 0), C(3, 1), C(4, 2), C(5, 3), C(6, 0),
                                            C(7, 1), C(8, 2), C(9, 3), C(10, 0)};
        CHECK(t.init(simple, 0), "初期配布を受け付けない");
        CHECK(t.knock(0), "ノックできない");
        CHECK(!t.knock(1), "再ノックができてしまう");
        CHECK(t.stand(1), "最後の応答の STAND ができない");
        CHECK(t.finished && t.turns == 2 && t.end == ThirtyOne::Knocked, "ノックの終わり方が違う");

        CHECK(t.init(simple, 0), "初期配布を受け付けない");
        const Card in = t.market[2];
        CHECK(t.swap(0, 0, 2), "交換できない");
        CHECK(t.publicly_known[0] & (uint64_t(1) << in), "取った札が公開既知にならない");
        CHECK(t.swap(1, 0, 0), "交換できない");
        CHECK(t.swap(0, 0, 2), "交換できない");
        CHECK(!(t.publicly_known[0] & (uint64_t(1) << in)), "戻した札が公開既知から消えない");

        CHECK(t.init(simple, 0), "初期配布を受け付けない");
        for (int i = 0; i < 19; ++i) {
            CHECK(t.swap(t.actor, 0, 0), "19 手目までの交換ができない");
        }
        CHECK(!t.finished, "19 手で終わってしまった");
        CHECK(t.knock(t.actor), "20 手目のノックができない");
        CHECK(t.turns == 20 && !t.finished, "20 手目のノックで終わってしまった");
        CHECK(t.stand(t.actor), "21 手目の応答ができない");
        CHECK(t.turns == 21 && t.finished, "21 手目で終わらない");

        CHECK(t.init(simple, 0), "初期配布を受け付けない");
        for (int i = 0; i < 20; ++i) {
            CHECK(t.swap(t.actor, 0, 0), "20 手目までの交換ができない");
        }
        CHECK(t.finished && t.end == ThirtyOne::Limit, "20 手の上限で終わらない");
    }
    GROUP("31 の初期 31・ノック・公開既知札・20 手目ノック＋21 手目応答・上限");

    {
        int cases = 0;
        for (int human = 0; human < 3; ++human) {
            for (int ai = 0; ai < 3; ++ai) {
                for (int outcome = 0; outcome < 3; ++outcome) {
                    BaccaratPredictions q;
                    CHECK(q.choose(1, ai), "AI の予想を受け付けない");
                    CHECK(!q.ready(), "片方だけで揃ったことになる");
                    CHECK(!q.choose(1, ai), "同じ側が 2 回予想できてしまう");
                    CHECK(q.choose(0, human), "人間の予想を受け付けない");
                    const auto pts = q.points(outcome);
                    CHECK(pts[0] == (human == outcome) && pts[1] == (ai == outcome),
                          "的中の数え方が違う");
                    ++cases;
                }
            }
        }
        CHECK(cases == 27, "予想の組み合わせが 27 通りではない");
        CHECK(baccarat_from_values({8, 2, 0, 1, 9, 9}).used == 4, "ナチュラルで追加を引いた");
        CHECK(baccarat_from_values({6, 5, 0, 0, 2, 8}).used == 5, "5 枚目の行先が違う");
        CHECK(!baccarat_from_values({-1, 0, 0, 0, 0, 0}).valid, "範囲外の値を断らない");
    }
    GROUP("バカラの予想 27 通りとナチュラル・5 枚目の行先");
}

// ---------------------------------------------------------------------------
// 2. 原本 tests/local_test.cpp 相当
// ---------------------------------------------------------------------------
static void localPolicyTests()
{
    std::printf("[2] 原本 tests/local_test.cpp 相当\n");
    std::mt19937 rng(74107);
    long long actions = 0, swaps = 0, draws = 0, bids = 0;
    for (int k = 0; k < 5000; ++k) {
        Deck d;
        CHECK(d.init(1, next32, &rng), "山札を作れない");
        std::array<Card, 5> h{};
        for (auto &c : h) {
            CHECK(d.take(c), "札を引けない");
        }
        const int m = local_poker_draw(h);
        CHECK(m >= 0 && m <= 31, "交換 mask が範囲外");
        ++draws;
        for (int r = 0; r < 3; ++r) {
            Street s;
            s.actor = 1;
            s.paid = {2 * r, 0};
            s.raises = r;
            const auto a = local_poker_bet(h, s, uniform(next32, &rng, 100));
            CHECK(s.legal(a), "端末 AI が違法なベットを返した");
            ++actions;
        }
        std::array<Card, 3> own{}, market{};
        for (auto &c : own) { d.take(c); }
        for (auto &c : market) { d.take(c); }
        for (bool final_reply : {false, true}) {
            const auto a = local_thirty_one(own, market, final_reply);
            if (a.knock) {
                CHECK(!final_reply, "最後の応答でノックを返した");
            } else if (a.stand) {
                CHECK(final_reply, "通常ターンで STAND を返した");
            } else {
                CHECK(a.hand_slot >= 0 && a.hand_slot < 3 && a.market_slot >= 0 &&
                          a.market_slot < 3,
                      "交換の slot が範囲外");
                ++swaps;
            }
        }
    }
    for (int n : {7, 13}) {
        std::array<int, 13> rem{};
        for (int i = 0; i < n; ++i) {
            rem[i] = i + 1;
        }
        for (int count = 1; count <= n; ++count) {
            for (int prize = 1; prize <= n; ++prize) {
                for (int jitter = -1; jitter <= 1; ++jitter) {
                    const int x = local_gops(rem, count, prize, n, jitter);
                    CHECK(x >= 1 && x <= count, "GOPS の端末 AI が範囲外の札を返した");
                    ++bids;
                }
            }
        }
    }
    // 数えた回数が 0 のままなら、そもそも試していない
    CHECK(actions == 15000, "ベットの試行回数が合わない");
    CHECK(draws == 5000, "交換の試行回数が合わない");
    CHECK(swaps > 0, "31 の交換を 1 回も試していない");
    CHECK(bids == (7 * 7 + 13 * 13) * 3, "GOPS の試行回数が合わない");
    std::printf("  ベット %lld ・ 交換 %lld ・ 31 の交換 %lld ・ GOPS %lld 回\n", actions, draws,
                swaps, bids);
    GROUP("端末 AI は必ず合法な行動を返す");
}

// ---------------------------------------------------------------------------
// 3. 3 枚の手 22,100 通り / 4. バカラの追加札の表
// ---------------------------------------------------------------------------
static void exhaustiveTests()
{
    std::printf("[3] 3 枚の手 22,100 通りを独立実装と突き合わせる\n");
    long long hands31 = 0;
    bool stop = false;
    for (int i = 0; i < 50 && !stop; ++i) {
        for (int j = i + 1; j < 51 && !stop; ++j) {
            for (int k = j + 1; k < 52 && !stop; ++k) {
                const std::array<Card, 3> h = {Card(i), Card(j), Card(k)};
                int best = 0;
                for (int s = 0; s < 4; ++s) {
                    int sum = 0;
                    for (auto c : h) {
                        if (int((c % 52) / 13) == s) {
                            const int r = int(c % 13) + 2;
                            sum += (r == 14) ? 11 : (r >= 10 ? 10 : r);
                        }
                    }
                    if (sum > best) {
                        best = sum;
                    }
                }
                CHECK(score31(h) == best, "score31 が独立実装と違う");
                ++hands31;
                stop = g_failures != g_group_mark;      // 1 件でも違えば止める
            }
        }
    }
    CHECK(hands31 == 22100, "3 枚の手が 22,100 通りではない");
    GROUP("3 枚の手 22,100 通り");

    std::printf("[4] バカラの追加札の表と値の流れ\n");
    for (int b0 = 0; b0 < 8; ++b0) {
        CHECK(banker_draws(b0, -1) == (b0 < 6), "PLAYER が止まったときのバンカーの表");
        for (int p3 = 0; p3 < 10; ++p3) {
            CHECK(banker_draws(b0, p3) == (TABLE[b0][p3] == '1'), "バンカーの追加表");
        }
    }
    CHECK(!banker_draws(-1, 0) && !banker_draws(10, 0) && !banker_draws(0, 10),
          "範囲外の入力を断らない");
    long long cases = 0;
    for (int x = 0; x < 1000000; ++x) {
        std::array<int, 6> v{};
        int t = x;
        for (int j = 0; j < 6; ++j) {
            v[j] = t % 10;
            t /= 10;
        }
        const auto r = baccarat_from_values(v), rr = reference_bac(v);
        CHECK(r.valid && r.pt == rr.pt && r.bt == rr.bt && r.used == rr.used &&
                  r.winner == rr.winner && r.natural == rr.natural,
              "バカラの配布が独立実装と違う");
        CHECK(r.np >= 2 && r.np <= 3 && r.nb >= 2 && r.nb <= 3 && r.used >= 4 && r.used <= 6,
              "バカラの枚数が 2..3 / 4..6 の外");
        ++cases;
        if (g_failures != g_group_mark) {
            break;
        }
    }
    CHECK(cases == 1000000, "バカラの値の流れを 100 万通り試していない");
    std::printf("  値の流れ %lld 通り\n", cases);
    GROUP("バカラの追加札の表（全 80 組）と 100 万通りの値の流れ");
}

// ---------------------------------------------------------------------------
// 5 / 6. 4 ゲームの完走と観測の採取
// ---------------------------------------------------------------------------
struct Sample {
    std::string game, phase, observation, legal;
};
static std::vector<Sample> g_samples;
static int g_sample_count[4][8] = {};
static const int kSamplesPerPhase = 60;

static int phaseSlot(ct::Phase p)
{
    switch (p) {
    case ct::Phase::PokerBetPre:   return 0;
    case ct::Phase::PokerDraw:     return 1;
    case ct::Phase::PokerBetPost:  return 2;
    case ct::Phase::HoldemPreflop: return 3;
    case ct::Phase::HoldemFlop:    return 4;
    case ct::Phase::HoldemTurn:    return 5;
    case ct::Phase::HoldemRiver:   return 6;
    case ct::Phase::ThirtyLast:    return 1;
    default:                       return 0;
    }
}

static size_t g_observation_peak = 0;

// **AI が判断するすべての局面で観測を作ってみる**（採るのは段階ごとに 60 件だけだが、
// 長さの最大値は全部の局面から採らないと、いちばん長い 31 の履歴を見逃す）
static void maybeSample(const ct::Match &m)
{
    static char obs[ct::kObservationMax];
    static char legal[512];
    const size_t n = ct::writeObservation(m, obs, sizeof(obs));
    const size_t l = ct::writeLegal(m, 1, legal, sizeof(legal));
    CHECK(n != 0, "観測 JSON を書き出せなかった（枠が足りない）");
    CHECK(l != 0, "合法 ID の一覧を書き出せなかった");
    if (n == 0 || l == 0) {
        return;
    }
    if (n > g_observation_peak) {
        g_observation_peak = n;
    }
    const int g = (int)m.game;
    const int slot = (m.game == ct::Game::Baccarat) ? (m.variant == 1 ? 1 : 0) : phaseSlot(m.phase);
    if (g_sample_count[g][slot] >= kSamplesPerPhase) {
        return;
    }
    ++g_sample_count[g][slot];
    g_samples.push_back({ct::isHoldem(m) ? "holdem" : ct::gameId(m.game), ct::phaseId(m.phase),
                         obs, legal});
}

struct GopsWatch {
    int score0 = 0, score1 = 0, burned = 0, rows = 0;
};

static bool playMatch(ct::Game game, uint8_t variant, bool human_random, std::mt19937 &rng)
{
    static ct::Match m;
    if (!ct::startMatch(m, game, variant, next32, &rng)) {
        std::printf("  **FAIL** 試合を始められませんでした\n");
        return false;
    }
    GopsWatch gw;
    char ids[ct::kMaxActions][ct::kActionIdMax];
    int guard = 0;

    while (!m.finished) {
        if (++guard > 4000) {
            std::printf("  **FAIL** 試合が終わりません (%s)\n", ct::gameId(game));
            return false;
        }
        if (m.phase == ct::Phase::UnitResult) {
            if (game == ct::Game::Thirty) {
                if (m.t31.turns > 21) {
                    std::printf("  **FAIL** 31 が 21 行動を超えました\n");
                    return false;
                }
                if (m.t31.end == ThirtyOne::Natural && score31(m.t31.hands[0]) != 31 &&
                    score31(m.t31.hands[1]) != 31) {
                    std::printf("  **FAIL** 31 でないのに Natural で終わりました\n");
                    return false;
                }
            }
            if (!ct::nextUnit(m, next32, &rng)) {
                std::printf("  **FAIL** 次のハンド / ラウンドへ進めません\n");
                return false;
            }
            continue;
        }
        // 進み具合の番号は必ず 1..総数（GOPS の決着後に総数を超えないこと）
        if (ct::unitNo(m) < 1 || ct::unitNo(m) > ct::totalUnits(m)) {
            std::printf("  **FAIL** unit 番号が範囲外 (%u/%d)\n", (unsigned)ct::unitNo(m),
                        ct::totalUnits(m));
            return false;
        }
        const int who = ct::canAct(m, 1) ? 1 : 0;   // 同時選択は **AI を先に確定**
        if (!ct::canAct(m, who)) {
            std::printf("  **FAIL** だれも行動できない局面 (%s)\n", ct::phaseId(m.phase));
            return false;
        }
        const int n = ct::legalActions(m, who, ids);
        if (n < 1) {
            std::printf("  **FAIL** 合法な行動がありません\n");
            return false;
        }
        for (int i = 1; i < n; ++i) {
            if (std::strcmp(ids[i - 1], ids[i]) >= 0) {
                std::printf("  **FAIL** 合法 ID の並びが昇順ではありません\n");
                return false;
            }
        }
        if (m.phase == ct::Phase::ThirtyTurn || m.phase == ct::Phase::ThirtyLast) {
            const bool last = m.phase == ct::Phase::ThirtyLast;
            bool has_stand = false, has_knock = false;
            for (int i = 0; i < n; ++i) {
                has_stand |= std::strcmp(ids[i], "STAND") == 0;
                has_knock |= std::strcmp(ids[i], "KNOCK") == 0;
            }
            if (n != 10 || has_stand != last || has_knock == last) {
                std::printf("  **FAIL** 31 の行動一覧が違います (%s)\n", ct::phaseId(m.phase));
                return false;
            }
        }
        if (who == 1 && n >= 2) {
            maybeSample(m);     // 長さは毎回測り、見本として残すのは段階ごとに 60 件
        }

        char chosen[ct::kActionIdMax];
        const bool random_side = human_random && who == 0;
        if (random_side) {
            std::snprintf(chosen, sizeof(chosen), "%s", ids[rng() % (unsigned)n]);
        } else if (!ct::localActionId(m, who, ct::drawRoll(next32, &rng), chosen,
                                      sizeof(chosen))) {
            std::printf("  **FAIL** 端末 AI が行動を返しません (%s)\n", ct::phaseId(m.phase));
            return false;
        }
        if (!ct::isLegalId(m, who, chosen)) {
            std::printf("  **FAIL** 端末 AI が違法な行動 %s を返しました\n", chosen);
            return false;
        }

        Card swap_out = 0, swap_in = 0;
        int swap_slot = -1;
        if (game == ct::Game::Thirty && std::strncmp(chosen, "SWAP:", 5) == 0) {
            swap_slot = chosen[5] - '0';
            swap_out = m.t31.hands[who][swap_slot];
            swap_in = m.t31.market[chosen[7] - '0'];
        }
        const int prize = (game == ct::Game::Gops) ? m.gops.prizes[m.gops.index] : 0;
        const bool resolves_gops = (game == ct::Game::Gops) && m.gops.sealed[1 - who] != -1;
        const int other_bid = (game == ct::Game::Gops) ? m.gops.sealed[1 - who] : 0;

        if (!ct::applyAction(m, who, chosen)) {
            std::printf("  **FAIL** 行動 %s を適用できません\n", chosen);
            return false;
        }

        if (swap_slot >= 0) {
            const bool has_in = (m.t31.publicly_known[who] & (uint64_t(1) << swap_in)) != 0;
            const bool has_out = (m.t31.publicly_known[who] & (uint64_t(1) << swap_out)) != 0;
            if (!has_in || has_out) {
                std::printf("  **FAIL** 31 の公開既知札の出入りが合いません\n");
                return false;
            }
            for (int c = 0; c < 52; ++c) {
                if (m.t31.publicly_known[who] & (uint64_t(1) << c)) {
                    bool held = false;
                    for (auto x : m.t31.hands[who]) {
                        held |= (int)x == c;
                    }
                    if (!held) {
                        std::printf("  **FAIL** 公開既知札が手札に無い\n");
                        return false;
                    }
                }
            }
        }
        if (resolves_gops) {
            const int my_bid = std::atoi(chosen + 5);
            const bool tie = my_bid == other_bid;
            const int d0 = m.gops.score[0] - gw.score0;
            const int d1 = m.gops.score[1] - gw.score1;
            const int db = m.gops.burned - gw.burned;
            if (tie ? (db != prize || d0 != 0 || d1 != 0) : (db != 0 || d0 + d1 != prize)) {
                std::printf("  **FAIL** GOPS の得点の付き方が違います\n");
                return false;
            }
            gw.score0 = m.gops.score[0];
            gw.score1 = m.gops.score[1];
            gw.burned = m.gops.burned;
            ++gw.rows;
        }

        const char *why = nullptr;
        if (!ct::invariants(m, &why)) {
            std::printf("  **FAIL** 不変条件: %s\n", why != nullptr ? why : "?");
            return false;
        }
    }

    switch (game) {
    case ct::Game::Poker: {
        const int total = ct::isHoldem(m) ? 400 : 200;
        if (m.completed_units != 5 || m.scores[0] + m.scores[1] != total) {
            std::printf("  **FAIL** ポーカーの試合が 5 ハンド %d 点で終わっていません\n", total);
            return false;
        }
        break;
    }
    case ct::Game::Gops: {
        int total = 0;
        for (int i = 0; i < m.gops.n; ++i) {
            total += m.gops.prizes[i];
        }
        if (m.gops.score[0] + m.gops.score[1] + m.gops.burned != total ||
            m.completed_units != m.gops.n) {
            std::printf("  **FAIL** GOPS の得点の合計が合いません\n");
            return false;
        }
        if (gw.rows != m.gops.n) {
            std::printf("  **FAIL** GOPS の決着したラウンド数が %d ではなく %d\n", m.gops.n,
                        gw.rows);
            return false;
        }
        break;
    }
    case ct::Game::Thirty:
        if (m.t31_wins[0] + m.t31_wins[1] + m.t31_wins[2] != 3 || m.completed_units != 3) {
            std::printf("  **FAIL** 31 の 3 ハンドが揃っていません\n");
            return false;
        }
        break;
    default:
        if (m.completed_units != 5 || m.bac_hits[0] > 5 || m.bac_hits[1] > 5) {
            std::printf("  **FAIL** バカラの 5 ラウンドが揃っていません\n");
            return false;
        }
        break;
    }
    if (ct::unitNo(m) != (uint8_t)ct::totalUnits(m)) {
        std::printf("  **FAIL** 決着後の unit 番号が総数と違う (%u/%d)\n", (unsigned)ct::unitNo(m),
                    ct::totalUnits(m));
        return false;
    }
    return true;
}

static void matchTests(int per_game)
{
    std::printf("[5] 4 ゲームを端末 AI 同士 / でたらめ 対 端末 AI で完走する（各 %d 試合）\n",
                per_game * 2);
    std::mt19937 rng(20260924);
    struct Entry { ct::Game game; uint8_t variant; const char *name; };
    const Entry kEntries[] = {
        {ct::Game::Poker, 0, "POKER HOLDEM"},
        {ct::Game::Poker, 1, "POKER DRAW"},
        {ct::Game::Gops, 7, "GOPS 7"},
        {ct::Game::Gops, 13, "GOPS 13"},
        {ct::Game::Thirty, 0, "THIRTY-ONE"},
        {ct::Game::Baccarat, 0, "BACCARAT OPEN"},
        {ct::Game::Baccarat, 1, "BACCARAT CLASSIC"},
    };
    for (const auto &e : kEntries) {
        int ok = 0;
        for (int i = 0; i < per_game * 2; ++i) {
            if (!playMatch(e.game, e.variant, (i % 2) == 1, rng)) {
                ++g_failures;
                break;
            }
            ++ok;
        }
        std::printf("  %-18s %d 試合\n", e.name, ok);
        CHECK(ok == per_game * 2, "完走できなかった試合がある");
        GROUP(e.name);
    }
}

static void writeSamples(const char *path)
{
    FILE *f = std::fopen(path, "wb");
    if (f == nullptr) {
        CHECK(false, "観測を書き出せない");
        return;
    }
    std::fprintf(f, "[\n");
    for (size_t i = 0; i < g_samples.size(); ++i) {
        std::fprintf(f, "%s{\"game\":\"%s\",\"phase\":\"%s\",\"observation\":%s,\"legal\":%s}",
                     i ? ",\n" : "", g_samples[i].game.c_str(), g_samples[i].phase.c_str(),
                     g_samples[i].observation.c_str(), g_samples[i].legal.c_str());
    }
    std::fprintf(f, "\n]\n");
    std::fclose(f);
    std::printf("[6] 観測 %u 件を書き出しました（いちばん長い観測 %u バイト / 上限 %u）\n",
                (unsigned)g_samples.size(), (unsigned)g_observation_peak,
                (unsigned)ct::kObservationMax);
    CHECK(g_observation_peak < ct::kObservationMax, "観測が上限に届いている");
}

static void writePokerKeys(const char *path)
{
    FILE *f = std::fopen(path, "wb");
    if (f == nullptr) {
        CHECK(false, "役のキーを書き出せない");
        return;
    }
    std::fprintf(f, "[\n");
    for (size_t i = 0; i < g_keys.size(); ++i) {
        std::fprintf(f, "%s{\"cards\":%s,\"key\":%s}", i ? ",\n" : "", g_keys[i].cards.c_str(),
                     g_keys[i].key.c_str());
    }
    std::fprintf(f, "\n]\n");
    std::fclose(f);
    std::printf("[7] 役のキー %u 件を書き出しました（9 つの役すべてを含む）\n",
                (unsigned)g_keys.size());
}

int main(int argc, char **argv)
{
    portableCoreTests();
    localPolicyTests();
    exhaustiveTests();
    holdemEvaluatorTests();
    collectPokerKeys();
    matchTests(400);

    if (argc > 1) {
        writeSamples(argv[1]);
    }
    if (argc > 2) {
        writePokerKeys(argv[2]);
    }

    // 群を飛ばしていないか（途中で return していないか）を最後に確かめる
    CHECK(g_groups == kExpectedGroups, "合格した群の数が想定と違う");
    std::printf("\n合格した項目 %d / %d ・ 不合格 %d\n", g_groups, kExpectedGroups, g_failures);
    std::printf("{\"suite\":\"cards\",\"groups_passed\":%d,\"groups_expected\":%d,"
                "\"failures\":%d,\"observations\":%u,\"poker_keys\":%u}\n",
                g_groups, kExpectedGroups, g_failures, (unsigned)g_samples.size(),
                (unsigned)g_keys.size());
    return g_failures == 0 ? 0 : 1;
}
