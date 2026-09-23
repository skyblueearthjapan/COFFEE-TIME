#pragma once

// POKER TABLE（原本の CAFE CARDS）の**追加分**。
//
// 設計一式のコア `cards_core.hpp`（ルール）と `local_policy.hpp`（端末 AI）は
// **無改変**で使う（tools/run_cards_checks.py が SHA-256 で確かめる）。
// 原本に無くて端末に要るものだけを、このファイルに足す:
//
//   * 札の名前（`SA` / `H7` / `C10`）と画面に出す表示値
//   * 4 ゲームの「試合」の進行（Match）。原本のコアは 1 ハンド / 1 局ぶんしか持たない
//   * 合法な行動の ID 一覧。並びは GAS 側（`gas/CardsGate.gs` の criteria）と同じ**文字列の昇順**
//   * Jev へ渡す観測の JSON 書き出し（原本 `jev_contract.js` の `buildObservation` と同じ形）
//   * 公開イベントの記録（設計書 I14）。**配布順・手札・未来の札は入れない**
//   * 不変条件の検査（PC 上の試験と実機の両方から呼ぶ）
//
// ここには Arduino も LVGL も入れない（PC 上の試験 tools/cards_checks.cpp が同じものを使う）。

#include "cards_core.hpp"
#include "local_policy.hpp"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace coffee {
namespace cards {

namespace core = cafe_cards;

using core::BaccaratResult;
using core::BetAction;
using core::Card;
using core::Deck;
using core::Gops;
using core::PokerHand;
using core::PokerPhase;
using core::ThirtyOne;

// ---------------------------------------------------------------------------
// 共通の大きさ
// ---------------------------------------------------------------------------
constexpr size_t kActionIdMax = 12;      // "SWAP:2:2" / "DRAW:31" / "BANKER" ＋ 終端
constexpr size_t kMaxActions = 32;       // ポーカーの交換 32 通りが最大
constexpr size_t kCardNameMax = 5;       // "C10" ＋ 終端
constexpr size_t kEventMax = 32;         // 公開イベントの輪（設計書 I14 は 192 だが端末は直近だけ使う）
// 観測 JSON の上限。31 は公開交換の履歴 24 件（1 件あたり out/in の札名つき）で
// 実測 1633 バイトまで伸びるので、余裕をみて 2048 にしてある
// （依頼の本文 net::kGasRequestMax = 3584 の内訳: 前後 80 + 観測 2048 + 合法 ID 322）
constexpr size_t kObservationMax = 2048;

constexpr const char *kRulesVersion = "1.0.0";

enum class Game : uint8_t { Poker = 0, Gops = 1, Thirty = 2, Baccarat = 3 };
constexpr size_t kGameCount = 4;

// 原本 I3 のゲーム固有 phase を、この端末で必要なぶんだけ
enum class Phase : uint8_t {
    PokerBetPre,        // 交換前のベット
    PokerDraw,          // 交換（同時選択）
    PokerBetPost,       // 交換後のベット
    GopsBid,            // 入札（同時選択）
    ThirtyTurn,         // 通常ターン（SWAP か KNOCK）
    ThirtyLast,         // ノック後の最後の 1 手（SWAP か STAND）
    BaccaratPredict,    // 予想（同時選択）
    UnitResult,         // ハンド / ラウンドの結果
    MatchOver,          // 試合終了
};

// 設計書 I14 の公開イベント。**札の配布順と相手の手札は絶対に入れない**
enum class EventKind : uint8_t {
    HandStart,   // poker/thirty: ハンドの開始（手札は入れない）
    Bet,         // poker: CHECK / BET / CALL / RAISE / FOLD
    DrawCounts,  // poker: 交換枚数（双方が確定したあとだけ）
    HandEnd,     // poker/thirty: ハンドの決着
    RoundEnd,    // gops/baccarat: ラウンドの決着
    Swap,        // thirty: 場との 1 対 1 交換（出入りの札は公開情報）
    Knock,       // thirty
    Stand,       // thirty
    Fallback,    // 共通: Jev → 端末 AI への切替
};

struct PublicEvent {
    EventKind kind = EventKind::HandStart;
    int8_t actor = -1;          // 0 = 人間, 1 = AI, -1 = 進行（SYSTEM）
    uint8_t unit_no = 0;        // ハンド / ラウンド番号（1 から）
    uint8_t value = 0;          // Bet: 今回払った点 / DrawCounts: 枚数 / RoundEnd: 得点札
    uint8_t detail = 0;         // Bet: BetAction / HandEnd・RoundEnd: 勝者 +1（0 = 無し）
    Card out_card = 0;          // Swap: 出した札
    Card in_card = 0;           // Swap: 取った札
};

// ---------------------------------------------------------------------------
// 札の名前と表示値
// ---------------------------------------------------------------------------
inline constexpr char kSuitLetter[4] = {'C', 'D', 'H', 'S'};

// 表示用のランク（GOPS だけは 1〜13 の数字をそのまま出すので使わない）
inline const char *rankText(int r)
{
    static const char *const kNames[15] = {"", "", "2", "3", "4", "5", "6", "7",
                                           "8", "9", "10", "J", "Q", "K", "A"};
    return (r >= 2 && r <= 14) ? kNames[r] : "";
}

// 原本 jev_contract.js の cardName と同じ綴り（`SA` / `H7` / `C10`）。out は kCardNameMax
inline void cardName(Card c, char *out)
{
    out[0] = kSuitLetter[core::suit(c)];
    std::snprintf(out + 1, kCardNameMax - 1, "%s", rankText(core::rank(c)));
}

// ---------------------------------------------------------------------------
// バカラの計算予想（設計書 B5 / data/baccarat_probabilities.json の値を 100 万分率で）
// 端末 AI はこの 3 値の最大を選ぶ。完全同率は PLAYER → BANKER → TIE の順
// ---------------------------------------------------------------------------
inline constexpr uint32_t kBaccaratClassicPpm[3] = {446247, 458597, 95156};
inline constexpr uint32_t kBaccaratOpenPpm[10][10][3] = {
    {{450457,460143,89400},{446910,465060,88030},{443161,468844,87994},{440125,471891,87984},{433798,478608,87594},{420611,492636,86753},{402136,501382,96482},{371129,530169,98702},{328353,585060,86587},{311016,602328,86656}},
    {{452701,458206,89092},{447696,461750,90554},{443995,467002,89003},{440708,470324,88967},{432370,478871,88759},{418491,493913,87596},{398915,499848,101237},{370343,529007,100651},{327807,584043,88150},{311061,600667,88273}},
    {{454947,454980,90073},{451394,458562,90044},{446254,462270,91476},{442623,467497,89880},{432376,478366,89258},{417766,490356,91878},{400741,496813,102446},{371600,526735,101665},{329619,581161,89221},{312307,598397,89296}},
    {{457835,451111,91054},{454358,454699,90942},{450676,458421,90902},{445111,462791,92099},{432870,473483,93647},{420613,486265,93122},{403485,493715,102799},{374366,523100,102534},{331687,578141,90172},{314359,595395,90247}},
    {{461375,444351,94274},{457802,447810,94388},{454133,451928,93939},{449736,456162,94102},{437213,454465,108322},{424470,478970,96561},{406632,486551,106817},{376321,517838,105841},{334371,572666,92962},{317031,589348,93621}},
    {{470758,433917,95326},{467180,437534,95285},{463372,441382,95245},{458720,442524,98756},{458591,442873,98536},{434630,456805,108564},{415897,476160,107942},{384780,508212,107008},{342377,563040,94583},{324972,580363,94665}},
    {{489937,408767,101296},{487361,410688,101951},{484423,413481,102096},{482186,416017,101797},{476347,419622,104031},{465418,430630,103952},{414680,422264,163057},{388641,500893,110466},{354682,543726,101591},{337245,560504,102251}},
    {{520272,378428,101300},{517470,380362,102168},{514687,383187,102126},{511282,386389,102328},{506623,389424,103952},{495812,400376,103812},{493273,396084,110642},{414912,422366,162721},{380397,517468,102135},{363367,534612,102021}},
    {{575086,335711,89204},{572396,337864,89740},{568993,341273,89734},{566237,343758,90005},{561390,347497,91114},{550497,358018,91486},{536048,362143,101809},{509997,387833,102170},{423119,430776,146105},{406194,500694,93113}},
    {{592252,318448,89300},{588949,321188,89863},{586169,324001,89829},{583379,326520,90101},{577977,330325,91698},{567641,340853,91506},{552794,344749,102457},{526960,371005,102035},{492969,413917,93113},{422894,430706,146400}},
};

// 予想の 3 択（バカラ）。0 = PLAYER, 1 = BANKER, 2 = TIE。core の winner と同じ番号
inline const uint32_t *baccaratPpm(bool open, int p_first, int b_first)
{
    if (!open || p_first < 0 || p_first > 9 || b_first < 0 || b_first > 9) {
        return kBaccaratClassicPpm;
    }
    return kBaccaratOpenPpm[p_first][b_first];
}

// ---------------------------------------------------------------------------
// 試合（4 ゲームぶんを 1 つの型に。PSRAM に 1 個だけ置く）
// ---------------------------------------------------------------------------
struct Match {
    Game game = Game::Poker;
    uint8_t variant = 0;        // gops: 7 / 13、baccarat: 0 = OPEN, 1 = CLASSIC。ほかは 0
    Phase phase = Phase::MatchOver;
    uint32_t revision = 0;      // 行動が 1 つ確定するごとに増える（二重確定よけ）
    bool finished = false;
    int winner = core::NONE;    // H / AI / DRAW
    int scores[2] = {0, 0};     // 表示用（poker = 持ち点, gops = 得点, thirty = 勝ちハンド数, baccarat = 的中）
    uint8_t completed_units = 0;

    Deck deck;                  // ハンド / ラウンドごとに引き直す作業用

    // --- POKER ---------------------------------------------------------
    PokerHand ph;
    uint8_t hand_no = 1;                     // 1..5
    uint8_t dealer0 = 0;                     // 第 1 ハンドのディーラー
    int stacks[2] = {100, 100};              // ハンドをまたぐ持ち点
    int hand_start_stacks[2] = {100, 100};   // 拠出 19 点の上限を確かめる用
    std::array<std::array<Card, 5>, 2> pre_draw{};   // 交換直前の 5 枚（捨て札を出すため）
    Card discarded[2][5] = {};
    uint8_t discard_n[2] = {0, 0};
    int8_t draw_counts[2] = {-1, -1};
    int last_pot = 0;                        // 直前のハンドで動いた場の点
    bool last_folded = false;
    int last_winner = core::NONE;

    // --- GOPS ----------------------------------------------------------
    Gops gops;
    struct GopsRow { uint8_t prize, human, ai; };
    GopsRow gops_hist[13] = {};
    uint8_t gops_rows = 0;

    // --- THIRTY-ONE ----------------------------------------------------
    ThirtyOne t31;
    uint8_t t31_hand_no = 1;                 // 1..3
    uint8_t first0 = 0;                      // 第 1 ハンドの先手
    uint8_t t31_wins[3] = {0, 0, 0};         // 人間 / AI / 引き分け
    std::array<Card, 9> t31_deal{};

    // --- BACCARAT ------------------------------------------------------
    uint8_t bac_round = 1;                   // 1..5
    core::BaccaratPredictions bac_pred;      // 予想は原本の型で持つ（二重予想はここが断る）
    std::array<Card, 6> bac_six{};
    BaccaratResult bac_res;
    uint8_t bac_hits[2] = {0, 0};

    // --- 公開イベント（直近 kEventMax 件の輪）---------------------------
    PublicEvent events[kEventMax] = {};
    uint8_t event_count = 0;                 // 輪の中にある件数（0..kEventMax）
    uint8_t event_head = 0;                  // いちばん古い件の位置
};

// ---------------------------------------------------------------------------
// 小さな道具
// ---------------------------------------------------------------------------
inline void pushEvent(Match &m, const PublicEvent &e)
{
    const size_t at = (m.event_head + m.event_count) % kEventMax;
    m.events[at] = e;
    if (m.event_count < kEventMax) {
        ++m.event_count;
    } else {
        m.event_head = (uint8_t)((m.event_head + 1) % kEventMax);
    }
}

inline const PublicEvent &eventAt(const Match &m, size_t i)
{
    return m.events[(m.event_head + i) % kEventMax];
}

inline void clearEvents(Match &m)
{
    m.event_count = 0;
    m.event_head = 0;
}

// 合計の単位数（ポーカー 5 / GOPS 7・13 / 31 は 3 / バカラ 5）
inline int totalUnits(const Match &m)
{
    switch (m.game) {
    case Game::Poker:  return 5;
    case Game::Gops:   return m.variant == 13 ? 13 : 7;
    case Game::Thirty: return 3;
    default:           return 5;
    }
}

// いま何ハンド目 / 何ラウンド目か。**必ず 1〜totalUnits に収まる**。
// GOPS は決着した時点で index が n まで進むので、そのまま +1 すると 8/7 のような
// 総数超えになる（結果画面では「決着した回」を返す）
inline uint8_t unitNo(const Match &m)
{
    int n = 1;
    switch (m.game) {
    case Game::Poker:  n = m.hand_no; break;
    case Game::Gops:   n = (m.phase == Phase::GopsBid) ? m.gops.index + 1 : m.gops_rows; break;
    case Game::Thirty: n = m.t31_hand_no; break;
    default:           n = m.bac_round; break;
    }
    const int total = totalUnits(m);
    if (n < 1) {
        n = 1;
    }
    if (n > total) {
        n = total;
    }
    return (uint8_t)n;
}

// いまの時点の得点（試合の途中でも読める）。中止した試合の記録に使う。
// **m.scores は決着したときにしか入らない**ので、途中の試合で読んではいけない
inline void liveScores(const Match &m, int &human, int &ai)
{
    switch (m.game) {
    case Game::Poker:  human = m.ph.stack[0];   ai = m.ph.stack[1];   break;
    case Game::Gops:   human = m.gops.score[0]; ai = m.gops.score[1]; break;
    case Game::Thirty: human = m.t31_wins[0];   ai = m.t31_wins[1];   break;
    default:           human = m.bac_hits[0];   ai = m.bac_hits[1];   break;
    }
}

inline const char *gameId(Game g)
{
    switch (g) {
    case Game::Poker:  return "poker";
    case Game::Gops:   return "gops";
    case Game::Thirty: return "thirty_one";
    default:           return "baccarat";
    }
}

inline const char *variantId(const Match &m)
{
    switch (m.game) {
    case Game::Poker:  return "fixed5";
    case Game::Gops:   return m.variant == 13 ? "classic13" : "quick7";
    case Game::Thirty: return "market3";
    default:           return m.variant == 1 ? "classic" : "open";
    }
}

inline const char *phaseId(Phase p)
{
    switch (p) {
    case Phase::PokerBetPre:     return "bet_pre";
    case Phase::PokerDraw:       return "draw";
    case Phase::PokerBetPost:    return "bet_post";
    case Phase::GopsBid:         return "bid";
    case Phase::ThirtyTurn:      return "turn";
    case Phase::ThirtyLast:      return "last_reply";
    case Phase::BaccaratPredict: return "predict";
    case Phase::UnitResult:      return "unit_result";
    default:                     return "match_over";
    }
}

// 同時選択の段階か（AI を先に確定させてから人間の確定ボタンを有効にする）
inline bool simultaneous(const Match &m)
{
    return m.phase == Phase::PokerDraw || m.phase == Phase::GopsBid ||
           m.phase == Phase::BaccaratPredict;
}

inline bool sealed(const Match &m, int who)
{
    switch (m.phase) {
    case Phase::PokerDraw:       return m.ph.draw_masks[who] != -1;
    case Phase::GopsBid:         return m.gops.sealed[who] != -1;
    case Phase::BaccaratPredict: return m.bac_pred.guess[who] != -1;
    default:                     return false;
    }
}

// いま行動できるのはだれか。同時選択の段階は両方 true になりうる
inline bool canAct(const Match &m, int who)
{
    if (who < 0 || who > 1 || m.finished) {
        return false;
    }
    switch (m.phase) {
    case Phase::PokerBetPre:
    case Phase::PokerBetPost:    return m.ph.street.actor == who && !m.ph.street.closed;
    case Phase::PokerDraw:       return m.ph.draw_masks[who] == -1;
    case Phase::GopsBid:         return m.gops.sealed[who] == -1 && !m.gops.done();
    case Phase::ThirtyTurn:
    case Phase::ThirtyLast:      return m.t31.actor == who && !m.t31.finished;
    case Phase::BaccaratPredict: return m.bac_pred.guess[who] == -1;
    default:                     return false;
    }
}

// ---------------------------------------------------------------------------
// 合法な行動の ID
//
// 並びは GAS 側（CardsGate.gs が criteria のキーを `Object.keys().sort()` する）と
// **同じ文字列の昇順**にする。ここが 1 文字でも違うと GAS が `LEGAL_IDS` で断る
// ---------------------------------------------------------------------------
inline void sortIds(char ids[][kActionIdMax], int count)
{
    for (int i = 1; i < count; ++i) {
        char key[kActionIdMax];
        std::memcpy(key, ids[i], kActionIdMax);
        int j = i - 1;
        while (j >= 0 && std::strcmp(ids[j], key) > 0) {
            std::memcpy(ids[j + 1], ids[j], kActionIdMax);
            --j;
        }
        std::memcpy(ids[j + 1], key, kActionIdMax);
    }
}

inline int legalActions(const Match &m, int who, char out[][kActionIdMax])
{
    if (!canAct(m, who)) {
        return 0;
    }
    int n = 0;
    switch (m.phase) {
    case Phase::PokerBetPre:
    case Phase::PokerBetPost: {
        const core::Street &s = m.ph.street;
        const int owed = s.paid[1 - who] - s.paid[who];
        if (owed > 0) {
            std::snprintf(out[n++], kActionIdMax, "CALL");
            std::snprintf(out[n++], kActionIdMax, "FOLD");
            if (s.raises < 2) {
                std::snprintf(out[n++], kActionIdMax, "RAISE");
            }
        } else {
            std::snprintf(out[n++], kActionIdMax, "BET");
            std::snprintf(out[n++], kActionIdMax, "CHECK");
        }
        break;
    }
    case Phase::PokerDraw:
        for (int mask = 0; mask < 32; ++mask) {
            std::snprintf(out[n++], kActionIdMax, "DRAW:%d", mask);
        }
        break;
    case Phase::GopsBid:
        for (int v = 1; v <= m.gops.n; ++v) {
            if (m.gops.remaining[who] & (uint16_t)(1u << (v - 1))) {
                std::snprintf(out[n++], kActionIdMax, "PLAY:%d", v);
            }
        }
        break;
    case Phase::ThirtyTurn:
    case Phase::ThirtyLast:
        std::snprintf(out[n++], kActionIdMax, "%s",
                      m.phase == Phase::ThirtyLast ? "STAND" : "KNOCK");
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                std::snprintf(out[n++], kActionIdMax, "SWAP:%d:%d", i, j);
            }
        }
        break;
    case Phase::BaccaratPredict:
        std::snprintf(out[n++], kActionIdMax, "BANKER");
        std::snprintf(out[n++], kActionIdMax, "PLAYER");
        std::snprintf(out[n++], kActionIdMax, "TIE");
        break;
    default:
        break;
    }
    sortIds(out, n);
    return n;
}

inline bool isLegalId(const Match &m, int who, const char *id)
{
    char ids[kMaxActions][kActionIdMax];
    const int n = legalActions(m, who, ids);
    for (int i = 0; i < n; ++i) {
        if (std::strcmp(ids[i], id) == 0) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// 1 ハンド / 1 ラウンドを始める
// ---------------------------------------------------------------------------
inline bool startPokerHand(Match &m, core::Rng32 rng, void *ctx)
{
    if (!m.deck.init(1, rng, ctx)) {
        return false;
    }
    const int dealer = (m.dealer0 + m.hand_no - 1) % 2;
    if (!m.ph.start(m.deck, dealer, {m.stacks[0], m.stacks[1]})) {
        return false;
    }
    m.hand_start_stacks[0] = m.stacks[0];
    m.hand_start_stacks[1] = m.stacks[1];
    m.draw_counts[0] = m.draw_counts[1] = -1;
    m.discard_n[0] = m.discard_n[1] = 0;
    m.last_pot = 0;
    m.last_folded = false;
    m.last_winner = core::NONE;
    clearEvents(m);
    PublicEvent e;
    e.kind = EventKind::HandStart;
    e.actor = -1;
    e.unit_no = m.hand_no;
    e.detail = (uint8_t)(dealer + 1);
    pushEvent(m, e);
    m.phase = Phase::PokerBetPre;
    return true;
}

inline bool startThirtyHand(Match &m, core::Rng32 rng, void *ctx)
{
    if (!m.deck.init(1, rng, ctx)) {
        return false;
    }
    for (auto &c : m.t31_deal) {
        if (!m.deck.take(c)) {
            return false;
        }
    }
    const int first = (m.first0 + m.t31_hand_no - 1) % 2;
    if (!m.t31.init(m.t31_deal, first)) {
        return false;
    }
    clearEvents(m);
    PublicEvent e;
    e.kind = EventKind::HandStart;
    e.actor = -1;
    e.unit_no = m.t31_hand_no;
    e.detail = (uint8_t)(first + 1);
    pushEvent(m, e);
    m.phase = m.t31.finished ? Phase::UnitResult : Phase::ThirtyTurn;
    if (m.t31.finished) {
        // 初期配布で 31（設計書 T4 の 1）。勝ちハンドを数えてから結果へ
        const int w = m.t31.winner;
        m.t31_wins[w == core::DRAW ? 2 : w]++;
        ++m.completed_units;
        PublicEvent end;
        end.kind = EventKind::HandEnd;
        end.actor = -1;
        end.unit_no = m.t31_hand_no;
        end.detail = (uint8_t)(w + 1);
        pushEvent(m, end);
    }
    return true;
}

inline bool startBaccaratRound(Match &m, core::Rng32 rng, void *ctx)
{
    if (!m.deck.init(8, rng, ctx)) {
        return false;
    }
    for (auto &c : m.bac_six) {
        if (!m.deck.take(c)) {
            return false;
        }
    }
    m.bac_res = core::baccarat_from_cards(m.bac_six);
    if (!m.bac_res.valid) {
        return false;
    }
    m.bac_pred = core::BaccaratPredictions{};
    m.phase = Phase::BaccaratPredict;
    return true;
}

// 試合を始める。variant は GOPS が 7 / 13、バカラが 0 = OPEN / 1 = CLASSIC。
// **`m = Match{}` と書くと 2KB 級の一時変数がスタックに乗る**ので、項目ごとに戻す
inline void resetMatch(Match &m)
{
    m.phase = Phase::MatchOver;
    m.revision = 0;
    m.finished = false;
    m.winner = core::NONE;
    m.scores[0] = m.scores[1] = 0;
    m.completed_units = 0;
    m.hand_no = 1;
    m.dealer0 = 0;
    m.stacks[0] = m.stacks[1] = 100;
    m.hand_start_stacks[0] = m.hand_start_stacks[1] = 100;
    m.discard_n[0] = m.discard_n[1] = 0;
    m.draw_counts[0] = m.draw_counts[1] = -1;
    m.last_pot = 0;
    m.last_folded = false;
    m.last_winner = core::NONE;
    m.gops = Gops{};
    m.gops_rows = 0;
    m.t31 = ThirtyOne{};
    m.t31_hand_no = 1;
    m.first0 = 0;
    m.t31_wins[0] = m.t31_wins[1] = m.t31_wins[2] = 0;
    m.bac_round = 1;
    m.bac_pred = core::BaccaratPredictions{};
    m.bac_res = BaccaratResult{};
    m.bac_hits[0] = m.bac_hits[1] = 0;
    clearEvents(m);
}

inline bool startMatch(Match &m, Game game, uint8_t variant, core::Rng32 rng, void *ctx)
{
    resetMatch(m);
    m.game = game;
    m.variant = variant;
    switch (game) {
    case Game::Poker:
        m.variant = 0;
        m.stacks[0] = m.stacks[1] = 100;
        m.hand_no = 1;
        m.dealer0 = (uint8_t)core::uniform(rng, ctx, 2);
        return startPokerHand(m, rng, ctx);
    case Game::Gops: {
        const int n = variant == 13 ? 13 : 7;
        m.variant = (uint8_t)n;
        std::array<int, 13> order{};
        for (int i = 0; i < n; ++i) {
            order[i] = i + 1;
        }
        for (int i = n - 1; i > 0; --i) {
            const int j = (int)core::uniform(rng, ctx, (uint32_t)(i + 1));
            const int t = order[i];
            order[i] = order[j];
            order[j] = t;
        }
        if (!m.gops.init(n, order)) {
            return false;
        }
        m.gops_rows = 0;
        clearEvents(m);
        m.phase = Phase::GopsBid;
        return true;
    }
    case Game::Thirty:
        m.variant = 0;
        m.t31_hand_no = 1;
        m.first0 = (uint8_t)core::uniform(rng, ctx, 2);
        return startThirtyHand(m, rng, ctx);
    default:
        m.variant = variant == 1 ? 1 : 0;
        m.bac_round = 1;
        return startBaccaratRound(m, rng, ctx);
    }
}

// ---------------------------------------------------------------------------
// 試合の決着
// ---------------------------------------------------------------------------
inline void finishMatch(Match &m)
{
    m.finished = true;
    m.phase = Phase::MatchOver;
    switch (m.game) {
    case Game::Poker:
        m.scores[0] = m.stacks[0];
        m.scores[1] = m.stacks[1];
        break;
    case Game::Gops:
        m.scores[0] = m.gops.score[0];
        m.scores[1] = m.gops.score[1];
        break;
    case Game::Thirty:
        m.scores[0] = m.t31_wins[0];
        m.scores[1] = m.t31_wins[1];
        break;
    default:
        m.scores[0] = m.bac_hits[0];
        m.scores[1] = m.bac_hits[1];
        break;
    }
    m.winner = m.scores[0] > m.scores[1] ? core::H
             : (m.scores[0] < m.scores[1] ? core::AI : core::DRAW);
}

// 結果画面から次のハンド / ラウンドへ
inline bool nextUnit(Match &m, core::Rng32 rng, void *ctx)
{
    if (m.phase != Phase::UnitResult) {
        return false;
    }
    ++m.revision;
    switch (m.game) {
    case Game::Poker:
        if (m.hand_no >= 5) {
            finishMatch(m);
            return true;
        }
        ++m.hand_no;
        return startPokerHand(m, rng, ctx);
    case Game::Gops:
        if (m.gops.done()) {
            finishMatch(m);
            return true;
        }
        m.phase = Phase::GopsBid;
        return true;
    case Game::Thirty:
        if (m.t31_hand_no >= 3) {
            finishMatch(m);
            return true;
        }
        ++m.t31_hand_no;
        return startThirtyHand(m, rng, ctx);
    default:
        if (m.bac_round >= 5) {
            finishMatch(m);
            return true;
        }
        ++m.bac_round;
        return startBaccaratRound(m, rng, ctx);
    }
}

// ---------------------------------------------------------------------------
// 行動を適用する
// ---------------------------------------------------------------------------
inline BetAction betOf(const char *id, bool &ok)
{
    ok = true;
    if (std::strcmp(id, "CHECK") == 0) return BetAction::Check;
    if (std::strcmp(id, "BET") == 0)   return BetAction::Bet;
    if (std::strcmp(id, "CALL") == 0)  return BetAction::Call;
    if (std::strcmp(id, "RAISE") == 0) return BetAction::Raise;
    if (std::strcmp(id, "FOLD") == 0)  return BetAction::Fold;
    ok = false;
    return BetAction::Check;
}

inline const char *betId(BetAction a)
{
    switch (a) {
    case BetAction::Check: return "CHECK";
    case BetAction::Bet:   return "BET";
    case BetAction::Call:  return "CALL";
    case BetAction::Raise: return "RAISE";
    default:               return "FOLD";
    }
}

// 今回払う点（設計書 P3）。イベントの amount と、場の点の計算に使う
inline int betDebit(const core::Street &s, int who, BetAction a)
{
    const int owed = s.paid[1 - who] - s.paid[who];
    switch (a) {
    case BetAction::Bet:   return s.unit;
    case BetAction::Call:  return owed;
    case BetAction::Raise: return owed + s.unit;
    default:               return 0;
    }
}

inline void notePokerEnd(Match &m)
{
    m.last_folded = m.ph.folded;
    m.last_winner = m.ph.winner;
    m.stacks[0] = m.ph.stack[0];
    m.stacks[1] = m.ph.stack[1];
    ++m.completed_units;
    PublicEvent e;
    e.kind = EventKind::HandEnd;
    e.actor = -1;
    e.unit_no = m.hand_no;
    e.value = (uint8_t)(m.last_pot > 255 ? 255 : m.last_pot);
    e.detail = (uint8_t)(m.ph.winner + 1);
    pushEvent(m, e);
    m.phase = Phase::UnitResult;
}

inline bool applyAction(Match &m, int who, const char *id)
{
    if (!isLegalId(m, who, id)) {
        return false;
    }
    switch (m.phase) {
    case Phase::PokerBetPre:
    case Phase::PokerBetPost: {
        bool ok = false;
        const BetAction a = betOf(id, ok);
        if (!ok) {
            return false;
        }
        const int debit = betDebit(m.ph.street, who, a);
        const int pot_before = m.ph.pot;
        if (!m.ph.bet(who, a)) {
            return false;
        }
        PublicEvent e;
        e.kind = EventKind::Bet;
        e.actor = (int8_t)who;
        e.unit_no = m.hand_no;
        e.value = (uint8_t)debit;
        e.detail = (uint8_t)a;
        pushEvent(m, e);
        ++m.revision;
        m.last_pot = pot_before + debit;
        if (m.ph.phase == PokerPhase::Finished) {
            notePokerEnd(m);
        } else if (m.ph.phase == PokerPhase::Draw) {
            m.pre_draw = m.ph.hands;      // 交換前の 5 枚（捨て札を観測へ出すため）
            m.phase = Phase::PokerDraw;
        }
        return true;
    }
    case Phase::PokerDraw: {
        const int mask = std::atoi(id + 5);
        if (!m.ph.draw(who, mask)) {
            return false;
        }
        ++m.revision;
        if (m.ph.draw_masks[0] >= 0 && m.ph.draw_masks[1] >= 0) {
            for (int p = 0; p < 2; ++p) {
                m.discard_n[p] = 0;
                for (int i = 0; i < 5; ++i) {
                    if (m.ph.draw_masks[p] & (1 << i)) {
                        m.discarded[p][m.discard_n[p]++] = m.pre_draw[p][i];
                    }
                }
                m.draw_counts[p] = (int8_t)m.discard_n[p];
            }
            // 枚数は**双方が確定したあとだけ**公開する（設計書 P4 の 5）
            for (int p = 0; p < 2; ++p) {
                PublicEvent e;
                e.kind = EventKind::DrawCounts;
                e.actor = (int8_t)p;
                e.unit_no = m.hand_no;
                e.value = (uint8_t)m.draw_counts[p];
                pushEvent(m, e);
            }
            m.phase = Phase::PokerBetPost;
        }
        return true;
    }
    case Phase::GopsBid: {
        const int value = std::atoi(id + 5);
        const int prize = m.gops.prizes[m.gops.index];
        const int other = 1 - who;
        const bool resolves = m.gops.sealed[other] != -1;
        const int other_bid = m.gops.sealed[other];
        if (!m.gops.choose(who, value)) {
            return false;
        }
        ++m.revision;
        if (resolves) {
            const int human = who == 0 ? value : other_bid;
            const int ai = who == 1 ? value : other_bid;
            if (m.gops_rows < 13) {
                m.gops_hist[m.gops_rows++] = {(uint8_t)prize, (uint8_t)human, (uint8_t)ai};
            }
            PublicEvent e;
            e.kind = EventKind::RoundEnd;
            e.actor = -1;
            e.unit_no = (uint8_t)m.gops_rows;
            e.value = (uint8_t)prize;
            e.detail = (uint8_t)((human > ai ? core::H : (human < ai ? core::AI : core::DRAW)) + 1);
            pushEvent(m, e);
            ++m.completed_units;
            m.phase = Phase::UnitResult;
        }
        return true;
    }
    case Phase::ThirtyTurn:
    case Phase::ThirtyLast: {
        PublicEvent e;
        e.actor = (int8_t)who;
        e.unit_no = m.t31_hand_no;
        if (std::strncmp(id, "SWAP:", 5) == 0) {
            const int hi = id[5] - '0', mi = id[7] - '0';
            e.kind = EventKind::Swap;
            e.out_card = m.t31.hands[who][hi];
            e.in_card = m.t31.market[mi];
            if (!m.t31.swap(who, hi, mi)) {
                return false;
            }
        } else if (std::strcmp(id, "KNOCK") == 0) {
            e.kind = EventKind::Knock;
            if (!m.t31.knock(who)) {
                return false;
            }
        } else {
            e.kind = EventKind::Stand;
            if (!m.t31.stand(who)) {
                return false;
            }
        }
        pushEvent(m, e);
        ++m.revision;
        if (m.t31.finished) {
            const int w = m.t31.winner;
            m.t31_wins[w == core::DRAW ? 2 : w]++;
            ++m.completed_units;
            PublicEvent end;
            end.kind = EventKind::HandEnd;
            end.actor = -1;
            end.unit_no = m.t31_hand_no;
            end.detail = (uint8_t)(w + 1);
            pushEvent(m, end);
            m.phase = Phase::UnitResult;
        } else {
            m.phase = (m.t31.knocker != core::NONE) ? Phase::ThirtyLast : Phase::ThirtyTurn;
        }
        return true;
    }
    case Phase::BaccaratPredict: {
        const int outcome = std::strcmp(id, "PLAYER") == 0 ? 0
                          : (std::strcmp(id, "BANKER") == 0 ? 1 : core::DRAW);
        if (!m.bac_pred.choose(who, outcome)) {
            return false;
        }
        ++m.revision;
        if (m.bac_pred.ready()) {
            const std::array<int, 2> pts = m.bac_pred.points(m.bac_res.winner);
            for (int p = 0; p < 2; ++p) {
                m.bac_hits[p] = (uint8_t)(m.bac_hits[p] + pts[p]);
            }
            ++m.completed_units;
            PublicEvent e;
            e.kind = EventKind::RoundEnd;
            e.actor = -1;
            e.unit_no = m.bac_round;
            e.value = (uint8_t)m.bac_res.used;
            e.detail = (uint8_t)(m.bac_res.winner + 1);
            pushEvent(m, e);
            m.phase = Phase::UnitResult;
        }
        return true;
    }
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// 端末 AI（設計書 I7）。
//
// くじは**その決定のために 1 回だけ**引いて控える（設計書 I6）。通信のやり直しや
// 画面の作り直しで引き直すと、同じ局面で違う手が出てしまう。
// `%100` の偏りを避けるため、引く時点で 0〜99 / 0〜2 に均しておく
// ---------------------------------------------------------------------------
struct SavedRoll {
    uint8_t poker100 = 0;   // ポーカーのベット（0〜99）
    uint8_t gops3 = 0;      // GOPS の jitter の素（0〜2 → -1/0/+1）
};

inline SavedRoll drawRoll(core::Rng32 rng, void *ctx)
{
    SavedRoll r;
    r.poker100 = (uint8_t)core::uniform(rng, ctx, 100);
    r.gops3 = (uint8_t)core::uniform(rng, ctx, 3);
    return r;
}

inline bool localActionId(const Match &m, int who, const SavedRoll &roll, char *out, size_t size)
{
    if (!canAct(m, who)) {
        return false;
    }
    switch (m.phase) {
    case Phase::PokerBetPre:
    case Phase::PokerBetPost: {
        const BetAction a = core::local_poker_bet(m.ph.hands[who], m.ph.street, roll.poker100);
        std::snprintf(out, size, "%s", betId(a));
        return true;
    }
    case Phase::PokerDraw: {
        const int mask = core::local_poker_draw(m.ph.hands[who]);
        if (mask < 0) {
            return false;
        }
        std::snprintf(out, size, "DRAW:%d", mask);
        return true;
    }
    case Phase::GopsBid: {
        std::array<int, 13> rem{};
        int count = 0;
        for (int v = 1; v <= m.gops.n; ++v) {
            if (m.gops.remaining[who] & (uint16_t)(1u << (v - 1))) {
                rem[count++] = v;
            }
        }
        const int jitter = (int)roll.gops3 - 1;
        const int pick = core::local_gops(rem, count, m.gops.prizes[m.gops.index], m.gops.n, jitter);
        if (pick < 1) {
            return false;
        }
        std::snprintf(out, size, "PLAY:%d", pick);
        return true;
    }
    case Phase::ThirtyTurn:
    case Phase::ThirtyLast: {
        const core::Local31 a = core::local_thirty_one(m.t31.hands[who], m.t31.market,
                                                       m.phase == Phase::ThirtyLast);
        if (a.knock) {
            std::snprintf(out, size, "KNOCK");
        } else if (a.stand) {
            std::snprintf(out, size, "STAND");
        } else {
            std::snprintf(out, size, "SWAP:%d:%d", a.hand_slot, a.market_slot);
        }
        return true;
    }
    case Phase::BaccaratPredict: {
        const bool open = m.variant == 0;
        const uint32_t *p = baccaratPpm(open, core::baccarat_value(m.bac_six[0]),
                                        core::baccarat_value(m.bac_six[1]));
        int best = 0;
        for (int i = 1; i < 3; ++i) {
            if (p[i] > p[best]) {
                best = i;    // 完全同率は PLAYER → BANKER → TIE の順（設計書 B5）
            }
        }
        std::snprintf(out, size, "%s", best == 0 ? "PLAYER" : (best == 1 ? "BANKER" : "TIE"));
        return true;
    }
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// 観測 JSON（原本 jev_contract.js の buildObservation と同じ形）
//
// **相手の手札・山札・未来の札・人間の仮選択は 1 バイトも入れない。**
// 個人の傾向は計画 §2 のとおり `sample_n:0`（傾向なし）で送る
// ---------------------------------------------------------------------------
struct JsonOut {
    char *buf;
    size_t size;
    size_t at = 0;
    bool ok = true;

    void put(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
};

inline void JsonOut::put(const char *fmt, ...)
{
    if (!ok || at >= size) {
        ok = false;
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(buf + at, size - at, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= size - at) {
        ok = false;
        return;
    }
    at += (size_t)n;
}

inline void putCardArray(JsonOut &j, const Card *cards, int count)
{
    j.put("[");
    for (int i = 0; i < count; ++i) {
        char name[kCardNameMax];
        cardName(cards[i], name);
        j.put("%s\"%s\"", i ? "," : "", name);
    }
    j.put("]");
}

inline const char *actorName(int8_t actor)
{
    return actor == 1 ? "SELF" : "OPPONENT";
}

// ポーカー / 31 の公開行動履歴（最大 24 件。原本 actionHistory と同じ形）
inline void putPublicActions(JsonOut &j, const Match &m)
{
    // 先に対象の件数を数え、後ろから 24 件だけ出す
    uint8_t keep[kEventMax];
    int n = 0;
    for (size_t i = 0; i < m.event_count; ++i) {
        const PublicEvent &e = eventAt(m, i);
        const bool poker = (e.kind == EventKind::Bet || e.kind == EventKind::DrawCounts);
        const bool thirty = (e.kind == EventKind::Swap || e.kind == EventKind::Knock ||
                             e.kind == EventKind::Stand);
        if ((m.game == Game::Poker && poker) || (m.game == Game::Thirty && thirty)) {
            keep[n++] = (uint8_t)i;
        }
    }
    const int from = n > 24 ? n - 24 : 0;
    j.put("[");
    for (int k = from; k < n; ++k) {
        const PublicEvent &e = eventAt(m, keep[k]);
        j.put("%s{\"actor\":\"%s\"", k > from ? "," : "", actorName(e.actor));
        switch (e.kind) {
        case EventKind::Bet: {
            const BetAction a = (BetAction)e.detail;
            j.put(",\"type\":\"%s\"", betId(a));
            if (a == BetAction::Bet || a == BetAction::Call || a == BetAction::Raise) {
                j.put(",\"amount\":%u", (unsigned)e.value);
            }
            break;
        }
        case EventKind::DrawCounts:
            j.put(",\"type\":\"DRAW_COUNT\",\"count\":%u", (unsigned)e.value);
            break;
        case EventKind::Swap: {
            char a[kCardNameMax], b[kCardNameMax];
            cardName(e.out_card, a);
            cardName(e.in_card, b);
            j.put(",\"type\":\"SWAP\",\"out_card\":\"%s\",\"in_card\":\"%s\"", a, b);
            break;
        }
        case EventKind::Knock:
            j.put(",\"type\":\"KNOCK\"");
            break;
        default:
            j.put(",\"type\":\"STAND\"");
            break;
        }
        j.put("}");
    }
    j.put("]");
}

// AI（who = 1）から見た観測を書き出す。書けた長さ（終端を除く）を返す。0 なら失敗
inline size_t writeObservation(const Match &m, char *out, size_t size)
{
    JsonOut j{out, size};
    j.put("{\"game\":\"%s\",\"variant\":\"%s\",\"phase\":\"%s\",\"rules_version\":\"%s\"",
          gameId(m.game), variantId(m), phaseId(m.phase), kRulesVersion);

    switch (m.phase) {
    case Phase::PokerBetPre:
    case Phase::PokerDraw:
    case Phase::PokerBetPost: {
        const core::Street &s = m.ph.street;
        const core::PokerValue v = core::poker_value(m.ph.hands[1]);
        if (!v.valid) {
            return 0;
        }
        j.put(",\"own_cards\":");
        putCardArray(j, m.ph.hands[1].data(), 5);
        j.put(",\"own_poker_key\":[%d,%d,%d,%d,%d,%d]", v.key[0], v.key[1], v.key[2], v.key[3],
              v.key[4], v.key[5]);
        j.put(",\"own_discards\":");
        // 捨て札は交換後の段階でだけ出す（原本 gate の EARLY_DISCARDS）
        putCardArray(j, m.discarded[1], m.phase == Phase::PokerBetPost ? m.discard_n[1] : 0);
        j.put(",\"hand_no\":%u,\"max_hands\":5", (unsigned)m.hand_no);
        j.put(",\"stacks\":{\"self\":%d,\"opponent\":%d},\"pot\":%d", m.ph.stack[1], m.ph.stack[0],
              m.ph.pot);
        j.put(",\"contribution\":{\"self\":%d,\"opponent\":%d},\"unit\":%d,\"raises_left\":%d",
              s.paid[1], s.paid[0], s.unit, 2 - s.raises);
        j.put(",\"dealer\":\"%s\"", m.ph.dealer == 1 ? "SELF" : "OPPONENT");
        if (m.phase == Phase::PokerBetPost) {
            j.put(",\"draw_counts\":{\"self\":%d,\"opponent\":%d}", (int)m.draw_counts[1],
                  (int)m.draw_counts[0]);
        } else {
            j.put(",\"draw_counts\":null");
        }
        j.put(",\"public_actions\":");
        putPublicActions(j, m);
        j.put(",\"statistics\":{\"sample_n\":0}");
        break;
    }
    case Phase::GopsBid: {
        const int n = m.gops.n;
        j.put(",\"n\":%d,\"round_no\":%d,\"prize\":%d", n, m.gops.index + 1,
              m.gops.prizes[m.gops.index]);
        for (int side = 1; side >= 0; --side) {
            j.put(",\"%s\":[", side == 1 ? "own_remaining" : "opponent_remaining");
            bool first = true;
            for (int v = 1; v <= n; ++v) {
                if (m.gops.remaining[side] & (uint16_t)(1u << (v - 1))) {
                    j.put("%s%d", first ? "" : ",", v);
                    first = false;
                }
            }
            j.put("]");
        }
        j.put(",\"scores\":{\"self\":%d,\"opponent\":%d},\"history\":[", m.gops.score[1],
              m.gops.score[0]);
        for (uint8_t i = 0; i < m.gops_rows; ++i) {
            j.put("%s{\"prize\":%u,\"self\":%u,\"opponent\":%u}", i ? "," : "",
                  (unsigned)m.gops_hist[i].prize, (unsigned)m.gops_hist[i].ai,
                  (unsigned)m.gops_hist[i].human);
        }
        j.put("],\"previous_match_history\":[]");
        break;
    }
    case Phase::ThirtyTurn:
    case Phase::ThirtyLast: {
        const int score = core::score31(m.t31.hands[1]);
        if (score < 0 || score == 31) {
            return 0;
        }
        j.put(",\"own_cards\":");
        putCardArray(j, m.t31.hands[1].data(), 3);
        j.put(",\"own_score\":%d,\"market\":", score);
        putCardArray(j, m.t31.market.data(), 3);
        j.put(",\"opponent_known_cards\":[");
        int known = 0;
        for (int c = 0; c < 52; ++c) {
            if (m.t31.publicly_known[0] & (uint64_t(1) << c)) {
                char name[kCardNameMax];
                cardName((Card)c, name);
                j.put("%s\"%s\"", known ? "," : "", name);
                ++known;
            }
        }
        j.put("],\"opponent_unknown_count\":%d,\"turn_count\":%d,\"final_reply\":%s", 3 - known,
              m.t31.turns, m.phase == Phase::ThirtyLast ? "true" : "false");
        j.put(",\"public_actions\":");
        putPublicActions(j, m);
        j.put(",\"statistics\":{\"sample_n\":0}");
        break;
    }
    case Phase::BaccaratPredict: {
        j.put(",\"decks\":8,\"fresh_deck_each_round\":true,\"public_first_cards\":");
        if (m.variant == 0) {
            char p[kCardNameMax], b[kCardNameMax];
            cardName(m.bac_six[0], p);
            cardName(m.bac_six[1], b);
            j.put("{\"PLAYER\":\"%s\",\"BANKER\":\"%s\"}", p, b);
        } else {
            j.put("null");
        }
        break;
    }
    default:
        return 0;
    }
    j.put("}");
    return j.ok ? j.at : 0;
}

// 合法 ID の JSON 配列（昇順）。GAS は同じ並びでないと断る
inline size_t writeLegal(const Match &m, int who, char *out, size_t size)
{
    char ids[kMaxActions][kActionIdMax];
    const int n = legalActions(m, who, ids);
    if (n < 2) {
        return 0;   // 1 つしかない行動は Jev に聞かない（設計書 G2 の forced）
    }
    JsonOut j{out, size};
    j.put("[");
    for (int i = 0; i < n; ++i) {
        j.put("%s\"%s\"", i ? "," : "", ids[i]);
    }
    j.put("]");
    return j.ok ? j.at : 0;
}

// ---------------------------------------------------------------------------
// 不変条件（設計書 P2 / G3 / T4 / B3）。PC 上の試験と実機の両方から呼ぶ
// ---------------------------------------------------------------------------
inline bool invariants(const Match &m, const char **why)
{
    const char *dummy = nullptr;
    const char **w = why != nullptr ? why : &dummy;
    *w = nullptr;
    switch (m.game) {
    case Game::Poker: {
        if (m.ph.stack[0] < 0 || m.ph.stack[1] < 0 || m.ph.pot < 0) {
            *w = "poker: negative chips";
            return false;
        }
        if (m.ph.stack[0] + m.ph.stack[1] + m.ph.pot != 200) {
            *w = "poker: chips do not add up to 200";
            return false;
        }
        if (m.ph.pot > 38) {
            *w = "poker: pot above 38";
            return false;
        }
        for (int p = 0; p < 2; ++p) {
            if (m.hand_start_stacks[p] - m.ph.stack[p] > 19) {
                *w = "poker: more than 19 contributed in one hand";
                return false;
            }
        }
        Card all[20];
        int k = 0;
        for (const auto &h : m.ph.hands) {
            for (auto c : h) {
                all[k++] = c;
            }
        }
        for (int i = 0; i < m.ph.discard_count; ++i) {
            all[k++] = m.ph.discards[i];
        }
        if (!core::valid_cards(all, k)) {
            *w = "poker: a card appears twice";
            return false;
        }
        return true;
    }
    case Game::Gops: {
        int resolved = 0;
        for (int i = 0; i < m.gops.index; ++i) {
            resolved += m.gops.prizes[i];
        }
        if (m.gops.score[0] + m.gops.score[1] + m.gops.burned != resolved) {
            *w = "gops: scores plus burned do not match the resolved prizes";
            return false;
        }
        for (int p = 0; p < 2; ++p) {
            int left = 0;
            for (int v = 1; v <= m.gops.n; ++v) {
                if (m.gops.remaining[p] & (uint16_t)(1u << (v - 1))) {
                    ++left;
                }
            }
            if (left != m.gops.n - m.gops.index) {
                *w = "gops: the number of remaining cards is wrong";
                return false;
            }
        }
        return true;
    }
    case Game::Thirty: {
        if (m.t31.turns > 21) {
            *w = "thirty_one: more than 21 actions";
            return false;
        }
        Card all[9];
        int k = 0;
        for (const auto &h : m.t31.hands) {
            for (auto c : h) {
                all[k++] = c;
            }
        }
        for (auto c : m.t31.market) {
            all[k++] = c;
        }
        if (!core::valid_cards(all, k)) {
            *w = "thirty_one: a card appears twice";
            return false;
        }
        if (m.t31_wins[0] + m.t31_wins[1] + m.t31_wins[2] != m.completed_units) {
            *w = "thirty_one: hand wins do not match the completed hands";
            return false;
        }
        // 31 点ができたら、ほかの条件より先に即終了している（設計書 T4 の 1・2）
        for (int p = 0; p < 2; ++p) {
            if (core::score31(m.t31.hands[p]) == 31 &&
                !(m.t31.finished && m.t31.end == ThirtyOne::Natural)) {
                *w = "thirty_one: 31 did not end the hand immediately";
                return false;
            }
        }
        return true;
    }
    default: {
        if (!m.bac_res.valid || m.bac_res.used < 4 || m.bac_res.used > 6) {
            *w = "baccarat: used cards outside 4..6";
            return false;
        }
        if (m.bac_hits[0] > 5 || m.bac_hits[1] > 5) {
            *w = "baccarat: more than five correct predictions";
            return false;
        }
        if (m.bac_res.natural && m.bac_res.used != 4) {
            *w = "baccarat: a natural drew extra cards";
            return false;
        }
        return true;
    }
    }
}

}  // namespace cards
}  // namespace coffee
