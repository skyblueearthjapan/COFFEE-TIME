// エスパー対決（AI ESPER）の推論コア。
//
// Arduino も LVGL も使わない C++17 のヘッダーだけの実装なので、そのまま PC で
// 試験できる（tools/esper_checks.cpp / tools/run_esper_checks.py）。
// 画面側（EsperGame.cpp）はここへ「はい／いいえ／わからない／1 つ戻る」を渡し、
// 出題する質問・残り候補数・最終予想を受け取るだけにする。
//
// ---------------------------------------------------------------------------
// 設計書のどこを写したか（COFFEE_TIME_AI_ESPER_Design_v1.0.md）
// ---------------------------------------------------------------------------
//   3.2 候補集合の更新   … Yes なら S ∩ yes、No なら S − yes、skip は S を変えない
//   3.3 出題候補の作り方 … S 全体が適用範囲内／Yes 側も No 側も空でない／skip 済みでない
//   3.4 情報量と問数ガード … H(q) と greedy_height による「安全な質問」
//   3.6 最終予想         … |S|=1 ならそれ、そうでなければ決定的に 1 つ選ぶ
//   1.4 詰まりを避ける操作 … わからない（skip）最大 2 回・1 つ戻る（undo）最大 2 回
//   6.3 オフライン方式   … 基準質問（情報量最大・同点は表示文の短い順→ID 順）を使う
//
// ここは設計書付録 B の Python 参考実装 `verify_catalog.py` の移植である。
// **同じ質問列・同じ残り候補数・同じ予想を出すこと**が試験の合格条件なので、
// 比較の順序や丸めを「良かれと思って」変えてはいけない。
//
// ---------------------------------------------------------------------------
// 「安全な質問」とは何か（設計書 3.4）
// ---------------------------------------------------------------------------
// 情報量 H(q) が大きい質問は今の 1 手としては良いが、その後の質問が足りなくなる
// ことがある。そこで、その質問に答えた後の両側について
//
//     G(S) = 0                              （|S| <= 1）
//     G(S) = 999                            （分けられる質問が 1 つも無い）
//     G(S) = 1 + max(G(S_yes), G(S_no))     （情報量最大の「基準質問」で分けたとき）
//
// という「基準ルートをたどったときの最悪の深さ」を数え、
//
//     1 + max(G(S_yes), G(S_no)) <= 残り質問数
//
// を満たす質問だけを「安全な質問」と呼ぶ。これは最適な質問木の証明ではなく、
// 実際にたどれる 1 本のルートの深さである（設計書が明記している）。
// 安全な質問が 1 つも無ければ、今いちばん良い質問をそのまま使い、
// 最後に当てられなくても「分かったふり」をしない（certainty=情報不足）。
//
// ---------------------------------------------------------------------------
// スタックについて（LVGL タスクは 8KB しかない）
// ---------------------------------------------------------------------------
// 146 問ぶんの作業表と再帰の覚え書き（メモ）は Engine が丸ごと抱えている。
// **Engine をローカル変数にしてはいけない。** 端末では PSRAM に置いて使い回す。
// 再帰 greedyHeight の 1 段は 100 バイト弱・深さは実測 10 段ほどなので問題ない。
#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>

#include "../EsperContent.h"

namespace coffee {
namespace esper {

namespace content = coffee::esp::content;

// ---------------------------------------------------------------------------
// 128 候補の集合
//
// 並びは catalog.items の配列順（D01 が bit0、T32 が bit127／設計書 6.3）。
// ---------------------------------------------------------------------------
struct Mask {
    uint64_t lo = 0;
    uint64_t hi = 0;

    static Mask from(const content::Mask128 &m) { return Mask{m.lo, m.hi}; }

    bool empty() const { return (lo | hi) == 0; }
    uint32_t count() const {
        return (uint32_t)(__builtin_popcountll(lo) + __builtin_popcountll(hi));
    }
    bool test(uint16_t i) const {
        return i < 64 ? ((lo >> i) & 1u) != 0 : ((hi >> (i - 64)) & 1u) != 0;
    }
    void set(uint16_t i) {
        if (i < 64) { lo |= (uint64_t)1 << i; } else { hi |= (uint64_t)1 << (i - 64); }
    }
    Mask operator&(const Mask &o) const { return Mask{lo & o.lo, hi & o.hi}; }
    Mask operator|(const Mask &o) const { return Mask{lo | o.lo, hi | o.hi}; }
    // this から o を取り除く（S − yes）
    Mask without(const Mask &o) const { return Mask{lo & ~o.lo, hi & ~o.hi}; }
    // this が o に完全に含まれるか（S ⊆ scope）
    bool subsetOf(const Mask &o) const { return (lo & ~o.lo) == 0 && (hi & ~o.hi) == 0; }
    bool operator==(const Mask &o) const { return lo == o.lo && hi == o.hi; }
    bool operator!=(const Mask &o) const { return !(*this == o); }

    // n 番目（0 起点）に立っているビットの位置。無ければ 0xFFFF
    uint16_t nth(uint32_t n) const {
        for (uint16_t i = 0; i < content::kItemCount; ++i) {
            if (test(i)) {
                if (n == 0) { return i; }
                --n;
            }
        }
        return 0xFFFF;
    }
    // いちばん小さい番号のビット。無ければ 0xFFFF
    uint16_t first() const { return nth(0); }
};

// 質問の集合（skip でふさいだ質問）。146 問 → 192bit
struct QuestionSet {
    uint64_t w[3] = {0, 0, 0};
    bool test(uint16_t i) const { return ((w[i >> 6] >> (i & 63)) & 1u) != 0; }
    void set(uint16_t i) { w[i >> 6] |= (uint64_t)1 << (i & 63); }
    void clear() { w[0] = w[1] = w[2] = 0; }
    bool operator==(const QuestionSet &o) const {
        return w[0] == o.w[0] && w[1] == o.w[1] && w[2] == o.w[2];
    }
};

// ---------------------------------------------------------------------------
// セッションの状態
//
// ここに入っている値は段階 2（サーバー・Jev）でもそのままサーバーへ写せるもの
// だけにしてある。**プレイヤーが頭の中で決めた答えは端末のどこにも無い。**
// 候補一覧でどのカードを見たか、どれくらい迷ったかも持たない（設計書 4.2）。
// ---------------------------------------------------------------------------
enum class Answer : uint8_t { Yes = 0, No = 1, Skip = 2 };

enum class Phase : uint8_t {
    Idle = 0,     // まだ始まっていない
    Question,     // 質問に答えてもらう
    Guess,        // 最終予想を出した（まだ当たり外れは聞いていない）
    Result,       // 当たり外れが確定した
};

enum class Verdict : uint8_t { None = 0, AiWin, HumanWin };

// 最終予想をどういう根拠で出したか（設計書 3.6）
enum class GuessReason : uint8_t {
    Unique = 0,                 // 条件に合う候補が 1 つだけ
    InsufficientInformation,    // 問数上限・有効な質問なし。分かったふりをしない
};

// 履歴 1 行。取り消しても行は消さず active=false にする（設計書 8）
struct HistoryEntry {
    uint16_t question;
    Answer answer;
    bool active;
};

// 10 問 ＋ わからない 2 回 ＋ 取り消しで無効になった行。余裕をもって 32 行
constexpr uint8_t kMaxHistory = 32;
constexpr uint8_t kShortlistMax = 8;          // Jev へ渡す上限（設計書 3.4）
constexpr uint16_t kNoQuestion = 0xFFFF;
constexpr uint16_t kNoItem = 0xFFFF;
constexpr uint16_t kUnreachable = 999;        // G(S) の「分けられない」（設計書 3.4）
constexpr double kGainWindowBits = 0.20;      // 最大情報量からこのビット数以内を候補にする

struct Session {
    uint8_t mode = 0;
    uint32_t seed = 0;
    uint16_t revision = 0;        // 段階 2 で expected_revision に使う番号（設計書 7.1）

    Phase phase = Phase::Idle;
    Mask candidates;              // 残っている候補 S
    QuestionSet blocked;          // skip でふさいだ質問（設計書 3.3-3）

    HistoryEntry history[kMaxHistory] = {};
    uint8_t history_count = 0;

    uint8_t asked = 0;            // 有効な Yes/No の数＝消費した問数（skip は数えない）
    uint8_t skip_count = 0;       // わからないを押した累計。undo で戻さない（設計書 8）
    uint8_t undo_count = 0;       // 1 つ戻るを押した累計。undo で戻さない

    uint16_t current_question = kNoQuestion;
    uint16_t guess = kNoItem;
    GuessReason guess_reason = GuessReason::Unique;
    Verdict verdict = Verdict::None;

    // 外れたあとに本人が申告した本当の候補（設計書 5.3 の G70。任意・集計だけに使う）
    uint16_t revealed = kNoItem;

    const content::Mode &modeInfo() const { return content::kModes[mode]; }
    uint8_t maxQuestions() const { return modeInfo().max_questions; }
    uint8_t maxSkips() const { return modeInfo().max_skips; }
    uint8_t remainingQuestions() const {
        return asked >= maxQuestions() ? 0 : (uint8_t)(maxQuestions() - asked);
    }
    uint32_t remainingCount() const { return candidates.count(); }
    bool canSkip() const { return phase == Phase::Question && skip_count < maxSkips(); }
    bool canUndo() const {
        // 勝敗を確定したあとは回答履歴を変えない（設計書 1.4）。
        // 予想画面（Guess）からは戻れる＝「最終予想前のやり直し」
        if (phase != Phase::Question && phase != Phase::Guess) { return false; }
        if (undo_count >= content::kMaxUndo) { return false; }
        for (int8_t i = (int8_t)history_count - 1; i >= 0; --i) {
            if (history[i].active && history[i].answer != Answer::Skip) { return true; }
        }
        return false;
    }
};

// 安全な質問の短い一覧（設計書 3.4：最大 8 問を Jev へ渡す）
struct Shortlist {
    uint16_t question[kShortlistMax];
    double gain[kShortlistMax];
    uint8_t count = 0;
    // 安全条件を通った質問から選べたか。false なら今いちばん良い 1 問だけが入っている
    bool safe = false;
};

// ---------------------------------------------------------------------------
// 段階 2（Jev）への継ぎ目
//
// 段階 1 ではここに LocalAdvisor（＝コードの基準どおり）を挿す。段階 2 では
// 同じ関数を「GAS → TypeSafe Jev」へ問い合わせる実装に差し替えるだけでよく、
// 候補の絞り込み・情報量の計算・安全確認は今までどおりコードが行う
// （設計書 3.1「Jev は差し替え可能な判断部品」／3.5）。
//
// 遠隔版が送るもの（docs/AI_GAMES_DIGEST.md の疎通確認済み契約・設計書 4.4）:
//   POST → GAS → https://api.typesafe.ai/v1/systemone
//   {
//     "model": "jev-1.13.0",
//     "state": { "rules_version", "mode", "history":[{question:<meaning_en>, answer}],
//                "remaining_candidates": {id: "<name_en>. <definition_ja>"},
//                "remaining_questions": <残り問数>,
//                "scope": "Closed catalogue. The secret target is NOT provided." },
//     "questions": {
//       "guess":        {type:"choice", instructions, criteria:{候補 id → 説明}},
//       "next_question":{type:"choice", instructions, criteria:{質問 id → meaning_en}}  // 2 問以上あるときだけ
//     }
//   }
// 返ってくるもの:
//   { "model", "usage":{input_tokens,output_tokens},
//     "answers": { "<key>": {type:"choice", choice:"<id>", confidence:0..1,
//                            probabilities:{id: 0..1}} } }
//
// **絶対に送らないもの**（設計書 4.2）: 頭の中の答え、一覧で見たカード、
// 滞在時間、スクロール位置、社員名、コーヒーの個人別履歴。
// 応答は必ず検証する（choice が criteria に含まれる／probabilities のキーが一致／
// 有限で 0〜1／合計と 1 の差が 1e-3 以内）。失敗したら黙って基準（下の LocalAdvisor）へ戻す。
// 3 秒で切り上げ、コーヒーカウンターは絶対に止めない（設計書 4.6）。
// ---------------------------------------------------------------------------
class Advisor {
public:
    virtual ~Advisor() = default;

    // 安全な質問の一覧から 1 問選ぶ。戻り値は list の添字（0..count-1）。
    // 範囲外を返したら呼び出し側が 0（＝基準質問）に直す
    virtual uint8_t chooseQuestion(const Session &s, const Shortlist &list) = 0;

    // 残った候補から最終予想を 1 つ選ぶ。戻り値は候補の番号。
    // 残候補に入っていない番号を返したら呼び出し側が基準の選び方に戻す
    virtual uint16_t chooseGuess(const Session &s, const Mask &remaining) = 0;

    // 画面に「AI の予想 42%」を出してよいか。ルール処理では出さず候補数を出す（設計書 3.7）
    virtual bool hasProbabilities() const { return false; }

    // 画面と集計に出す処理系の名前（設計書 11：jev / mixed / rule を混ぜない）
    virtual const char *engineName() const = 0;
};

// ---------------------------------------------------------------------------
// 推論エンジン本体
// ---------------------------------------------------------------------------
class Engine {
public:
    // 覚え書きの大きさ。足りなくても答えは変わらない（入れ替えて計算し直すだけ）
    static constexpr uint32_t kMemoSlots = 4096;

    void setAdvisor(Advisor *a) { advisor_ = a; }
    Advisor *advisor() const { return advisor_; }

    const Session &session() const { return session_; }
    const Shortlist &shortlist() const { return shortlist_; }

    // --- 進行 --------------------------------------------------------------

    // モードを決めてゲームを始める。seed は最終予想が同率だったときの選び方にだけ使う
    // （同じセッションでは何度計算しても同じ答えになるようにするため。設計書 3.6）
    void start(uint8_t mode_index, uint32_t seed)
    {
        session_ = Session{};
        session_.mode = mode_index < content::kModeCount ? mode_index : 0;
        session_.seed = seed;
        session_.candidates = Mask::from(session_.modeInfo().items);
        session_.phase = Phase::Question;
        memoClear();
        advance();
    }

    // 「はい」「いいえ」「わからない」。受け付けたら true
    bool answer(Answer a)
    {
        if (session_.phase != Phase::Question || session_.current_question == kNoQuestion) {
            return false;
        }
        if (a == Answer::Skip && !session_.canSkip()) {
            return false;
        }
        if (session_.history_count >= kMaxHistory) {
            return false;   // ここに来ることは無い（10 問 + 救済 4 回が上限）
        }
        const uint16_t q = session_.current_question;
        session_.history[session_.history_count++] = HistoryEntry{q, a, true};
        ++session_.revision;

        if (a == Answer::Skip) {
            // 設計書 1.4：候補は変えない・問数も消費しない・同じ質問は再出題しない
            session_.blocked.set(q);
            ++session_.skip_count;
            memoClear();        // ふさぐ質問が変わると G(S) の値も変わる
        } else {
            const Mask yes = Mask::from(content::kQuestions[q].yes);
            session_.candidates = (a == Answer::Yes) ? (session_.candidates & yes)
                                                     : session_.candidates.without(yes);
            ++session_.asked;
        }
        advance();
        return true;
    }

    // 1 つ戻る（設計書 1.4 / 8）。最後の Yes/No を無効にし、その質問を出し直す
    bool undo()
    {
        if (!session_.canUndo()) {
            return false;
        }
        int8_t at = -1;
        for (int8_t i = (int8_t)session_.history_count - 1; i >= 0; --i) {
            if (session_.history[i].active && session_.history[i].answer != Answer::Skip) {
                at = i;
                break;
            }
        }
        if (at < 0) {
            return false;
        }
        // 取り消した回答より後ろにある skip もいっしょに無効にする（その時点へ戻す）。
        // 行そのものは消さない。無効にした skip の質問はまた出題できるようになる
        for (uint8_t i = (uint8_t)at; i < session_.history_count; ++i) {
            session_.history[i].active = false;
        }
        const uint16_t q = session_.history[at].question;
        ++session_.undo_count;      // 救済の回数は巻き戻さない（設計書 8）
        ++session_.revision;
        rebuild();
        // 当時の質問をそのまま出し直す（ここで Jev を呼び直さない／設計書 8）
        session_.phase = Phase::Question;
        session_.current_question = q;
        session_.guess = kNoItem;
        buildShortlist();           // 画面の補助情報（残り候補数）を作り直すため
        session_.current_question = q;
        return true;
    }

    // 残りの情報のまま最終予想へ進む（わからないを使い切ったときの逃げ道／設計書 1.4）
    void guessNow()
    {
        if (session_.phase == Phase::Question) {
            enterGuess();
        }
    }

    // 最終予想への「はい／いいえ」。1 回だけ確定する（設計書 1.2 / E09）
    bool verdict(bool ai_was_right)
    {
        if (session_.phase != Phase::Guess) {
            return false;
        }
        session_.verdict = ai_was_right ? Verdict::AiWin : Verdict::HumanWin;
        session_.phase = Phase::Result;
        ++session_.revision;
        return true;
    }

    // 外れたあとの任意の申告（設計書 5.3 の G70）。推論はやり直さない
    void reveal(uint16_t item)
    {
        if (session_.phase == Phase::Result) {
            session_.revealed = item;
        }
    }

    // --- 答え合わせの説明 ---------------------------------------------------

    // item に対して、その質問の正しい答えは何か（Y / N / U）
    enum class Cell : uint8_t { Yes = 0, No = 1, OutOfScope = 2 };
    static Cell cellFor(uint16_t question, uint16_t item)
    {
        const content::Question &q = content::kQuestions[question];
        if (!Mask::from(q.scope).test(item)) {
            return Cell::OutOfScope;      // U。No として消し込んではいけない（設計書 2.4 / E12）
        }
        return Mask::from(q.yes).test(item) ? Cell::Yes : Cell::No;
    }

    // 申告された候補と食い違う回答が何番目にあったかを拾う（外れたときの説明に使う）。
    // out には history の添字を古い順に入れる。戻り値は見つかった数
    uint8_t contradictions(uint16_t item, uint8_t *out, uint8_t out_max) const
    {
        uint8_t n = 0;
        if (item >= content::kItemCount) {
            return 0;
        }
        for (uint8_t i = 0; i < session_.history_count; ++i) {
            const HistoryEntry &h = session_.history[i];
            if (!h.active || h.answer == Answer::Skip) {
                continue;
            }
            const Cell truth = cellFor(h.question, item);
            const bool matches = (truth == Cell::Yes && h.answer == Answer::Yes) ||
                                 (truth == Cell::No && h.answer == Answer::No);
            if (!matches && n < out_max) {
                out[n++] = i;
            } else if (!matches) {
                ++n;
            }
        }
        return n;
    }

    // 申告された候補がそもそもこのモードの一覧に入っていたか（設計書 1.2 の invalid_target）
    bool revealedWasInMode(uint16_t item) const
    {
        return item < content::kItemCount && Mask::from(session_.modeInfo().items).test(item);
    }

    // --- 参考値（試験・開発用） --------------------------------------------
    uint32_t memoHits() const { return memo_hits_; }
    uint32_t memoMisses() const { return memo_misses_; }
    uint16_t maxDepth() const { return max_depth_; }
    void resetCounters() { memo_hits_ = memo_misses_ = 0; max_depth_ = 0; }

    // 外から G(S) を確かめたいとき（試験用）
    uint16_t greedyHeightOf(const Mask &s) { return greedyHeight(s, 0); }

private:
    // --- 出題候補（設計書 3.3） --------------------------------------------
    //
    // Python 参考実装の eligible() と同じ 3 条件。すでに答えた質問は S 上で
    // 片側しか残らないので、条件 2 で自然に落ちる（試験でそれを確かめている）。
    static bool isEligible(const Mask &s, const QuestionSet &blocked, uint16_t q,
                           Mask &yes_out, Mask &no_out)
    {
        if (blocked.test(q)) {
            return false;
        }
        const content::Question &info = content::kQuestions[q];
        if (!s.subsetOf(Mask::from(info.scope))) {
            return false;       // 残候補に U が 1 つでもあれば出さない（設計書 2.4）
        }
        const Mask y = s & Mask::from(info.yes);
        if (y.empty()) {
            return false;
        }
        const Mask n = s.without(Mask::from(info.yes));
        if (n.empty()) {
            return false;
        }
        yes_out = y;
        no_out = n;
        return true;
    }

    // H(q) = −r log2 r − (1−r) log2(1−r)。Python の
    //   -sum(v*log2(v) for v in (|Sy|/|S|, |Sn|/|S|) if v>0)
    // と同じ式・同じ順序で計算する（浮動小数の足し算は交換法則が成り立つので、
    // 表裏の質問は同じビット列になる。だからここでは丸めずそのまま比べてよい）
    static double entropy(uint32_t yes_count, uint32_t total)
    {
        const double p = (double)yes_count / (double)total;
        const double q = (double)(total - yes_count) / (double)total;
        return -(p * std::log2(p) + q * std::log2(q));
    }

    // 並び順（設計書 3.4）: 情報量降順 → 表示文の文字数昇順 → 質問 ID 昇順
    static bool better(double ha, uint16_t qa, double hb, uint16_t qb)
    {
        if (ha != hb) {
            return ha > hb;
        }
        const content::Question &a = content::kQuestions[qa];
        const content::Question &b = content::kQuestions[qb];
        if (a.text_len != b.text_len) {
            return a.text_len < b.text_len;
        }
        return a.sort_rank < b.sort_rank;
    }

    // 並びの先頭（＝基準質問）だけを返す。配列を作らないので再帰から呼んでよい
    uint16_t bestEligible(const Mask &s, const QuestionSet &blocked, Mask &yes_out,
                          Mask &no_out) const
    {
        uint16_t best = kNoQuestion;
        double best_h = 0.0;
        Mask best_y, best_n;
        const uint32_t total = s.count();
        for (uint16_t q = 0; q < content::kQuestionCount; ++q) {
            Mask y, n;
            if (!isEligible(s, blocked, q, y, n)) {
                continue;
            }
            const double h = entropy(y.count(), total);
            if (best == kNoQuestion || better(h, q, best_h, best)) {
                best = q;
                best_h = h;
                best_y = y;
                best_n = n;
            }
        }
        if (best != kNoQuestion) {
            yes_out = best_y;
            no_out = best_n;
        }
        return best;
    }

    // G(S)（設計書 3.4 / 付録 B の greedy_height）。
    // 同じ S を何度も聞かれるので覚え書き（メモ）に入れる。メモは blocked が変わったら捨てる
    uint16_t greedyHeight(const Mask &s, uint16_t depth)
    {
        if (s.count() <= 1) {
            return 0;
        }
        if (depth > max_depth_) {
            max_depth_ = depth;
        }
        if (depth >= kDepthGuard) {
            // 実測では 10 段ほどで終わる。ここに来るのはデータが壊れているときだけ。
            // LVGL タスクの 8KB スタックを割らないための保険
            return kUnreachable;
        }
        uint16_t slot = 0;
        if (memoFind(s, slot)) {
            ++memo_hits_;
            return memo_[slot].value;
        }
        ++memo_misses_;

        Mask y, n;
        const uint16_t q = bestEligible(s, session_.blocked, y, n);
        uint16_t result;
        if (q == kNoQuestion) {
            result = kUnreachable;
        } else {
            const uint16_t a = greedyHeight(y, (uint16_t)(depth + 1));
            const uint16_t b = greedyHeight(n, (uint16_t)(depth + 1));
            const uint16_t worse = a > b ? a : b;
            result = (uint16_t)(worse >= kUnreachable ? kUnreachable : worse + 1);
        }
        memoStore(s, result);
        return result;
    }

    // 安全な質問の一覧を作る（設計書 3.4 / 付録 B の shortlist）
    void buildShortlist()
    {
        shortlist_.count = 0;
        shortlist_.safe = false;
        session_.current_question = kNoQuestion;

        const Mask s = session_.candidates;
        const uint32_t total = s.count();
        if (total <= 1) {
            return;
        }
        const uint8_t remaining = session_.remainingQuestions();

        // 1) 出題できる質問を全部並べる（情報量降順・表示文の短い順・ID 順）
        scratch_count_ = 0;
        for (uint16_t q = 0; q < content::kQuestionCount; ++q) {
            Mask y, n;
            if (!isEligible(s, session_.blocked, q, y, n)) {
                continue;
            }
            Candidate c;
            c.question = q;
            c.gain = entropy(y.count(), total);
            c.yes = y;
            c.no = n;
            // 挿入整列（146 件までなので十分速い。並びは Python の sort と同じ規則）
            uint16_t at = scratch_count_;
            while (at > 0 && better(c.gain, c.question, scratch_[at - 1].gain,
                                    scratch_[at - 1].question)) {
                scratch_[at] = scratch_[at - 1];
                --at;
            }
            scratch_[at] = c;
            ++scratch_count_;
        }
        if (scratch_count_ == 0) {
            return;     // 分けられる質問が無い → 呼び出し側が最終予想へ進む
        }

        // 2) 安全条件 1 + max(G(S_yes), G(S_no)) <= 残り質問数 を満たすものを拾う
        int first_safe = -1;
        for (uint16_t i = 0; i < scratch_count_; ++i) {
            const uint16_t a = greedyHeight(scratch_[i].yes, 0);
            const uint16_t b = greedyHeight(scratch_[i].no, 0);
            const uint16_t worse = a > b ? a : b;
            const uint32_t depth = (worse >= kUnreachable) ? kUnreachable : (uint32_t)worse + 1;
            scratch_[i].safe = (depth <= remaining);
            if (scratch_[i].safe && first_safe < 0) {
                first_safe = (int)i;
            }
        }
        if (first_safe < 0) {
            // 安全な質問が無い。いちばん良い 1 問だけを使い、情報不足であることを隠さない
            shortlist_.question[0] = scratch_[0].question;
            shortlist_.gain[0] = scratch_[0].gain;
            shortlist_.count = 1;
            shortlist_.safe = false;
            return;
        }

        // 3) 最大情報量との差が 0.20bit 以内・Yes 側の分かれ方が同じものは 1 つにまとめる
        const double best = scratch_[first_safe].gain;
        Mask seen[kShortlistMax];
        uint8_t seen_count = 0;
        for (uint16_t i = (uint16_t)first_safe;
             i < scratch_count_ && shortlist_.count < kShortlistMax; ++i) {
            if (!scratch_[i].safe) {
                continue;
            }
            if (scratch_[i].gain < best - kGainWindowBits - 1e-12) {
                break;      // 並びは情報量降順なので、ここから先は全部対象外
            }
            bool duplicate = false;
            for (uint8_t k = 0; k < seen_count; ++k) {
                if (seen[k] == scratch_[i].yes) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }
            seen[seen_count++] = scratch_[i].yes;
            shortlist_.question[shortlist_.count] = scratch_[i].question;
            shortlist_.gain[shortlist_.count] = scratch_[i].gain;
            ++shortlist_.count;
        }
        shortlist_.safe = true;
    }

    // 回答のあと・undo のあとに、次の質問か最終予想かを決める
    void advance()
    {
        if (session_.candidates.count() <= 1 || session_.asked >= session_.maxQuestions()) {
            enterGuess();
            return;
        }
        buildShortlist();
        if (shortlist_.count == 0) {
            enterGuess();       // 分けられる質問がもう無い（設計書 3.4 の最後）
            return;
        }
        uint8_t pick = 0;
        if (advisor_ != nullptr && shortlist_.count > 1) {
            const uint8_t choice = advisor_->chooseQuestion(session_, shortlist_);
            // 範囲外が返ってきたら黙って基準質問に戻す（設計書 3.5）
            pick = choice < shortlist_.count ? choice : 0;
        }
        session_.current_question = shortlist_.question[pick];
        session_.phase = Phase::Question;
    }

    void enterGuess()
    {
        const Mask s = session_.candidates;
        session_.current_question = kNoQuestion;
        session_.phase = Phase::Guess;
        const uint32_t n = s.count();
        if (n == 1) {
            session_.guess = s.first();
            session_.guess_reason = GuessReason::Unique;
            return;
        }
        session_.guess_reason = GuessReason::InsufficientInformation;
        if (n == 0) {
            // S=0 はデータ不一致か状態の壊れ（設計書 3.2）。存在しない答えは作らない
            session_.guess = kNoItem;
            return;
        }
        // 同率のときはセッションの seed と候補の並びで 1 つに決める。
        // 何度計算しても同じ答えになる＝再送やり直しで予想が変わらない（設計書 3.6）
        uint16_t pick = s.nth(session_.seed % n);
        if (advisor_ != nullptr) {
            const uint16_t from_advisor = advisor_->chooseGuess(session_, s);
            if (from_advisor < content::kItemCount && s.test(from_advisor)) {
                pick = from_advisor;
            }
        }
        session_.guess = pick;
    }

    // 有効な履歴だけを使って候補集合・問数・ふさいだ質問を作り直す（設計書 3.2 / 8）
    void rebuild()
    {
        session_.candidates = Mask::from(session_.modeInfo().items);
        session_.blocked.clear();
        session_.asked = 0;
        for (uint8_t i = 0; i < session_.history_count; ++i) {
            const HistoryEntry &h = session_.history[i];
            if (!h.active) {
                continue;
            }
            if (h.answer == Answer::Skip) {
                session_.blocked.set(h.question);
                continue;
            }
            const Mask yes = Mask::from(content::kQuestions[h.question].yes);
            session_.candidates = (h.answer == Answer::Yes) ? (session_.candidates & yes)
                                                            : session_.candidates.without(yes);
            ++session_.asked;
        }
        memoClear();
    }

    // --- G(S) の覚え書き（開いた番地法。あふれたら古い物を上書きするだけ） ----
    struct MemoEntry {
        uint64_t lo;
        uint64_t hi;
        uint16_t value;
        uint8_t used;
    };

    static uint32_t hashOf(const Mask &s)
    {
        uint64_t h = s.lo * 0x9E3779B97F4A7C15ull;
        h ^= (s.hi + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2));
        h ^= h >> 29;
        h *= 0xBF58476D1CE4E5B9ull;
        h ^= h >> 32;
        return (uint32_t)(h & (kMemoSlots - 1));
    }

    bool memoFind(const Mask &s, uint16_t &slot_out) const
    {
        uint32_t at = hashOf(s);
        for (uint32_t probe = 0; probe < kProbeLimit; ++probe) {
            const MemoEntry &e = memo_[at];
            if (e.used == 0) {
                return false;
            }
            if (e.lo == s.lo && e.hi == s.hi) {
                slot_out = (uint16_t)at;
                return true;
            }
            at = (at + 1) & (kMemoSlots - 1);
        }
        return false;
    }

    void memoStore(const Mask &s, uint16_t value)
    {
        uint32_t at = hashOf(s);
        for (uint32_t probe = 0; probe < kProbeLimit; ++probe) {
            MemoEntry &e = memo_[at];
            if (e.used == 0 || (e.lo == s.lo && e.hi == s.hi)) {
                e.lo = s.lo;
                e.hi = s.hi;
                e.value = value;
                e.used = 1;
                return;
            }
            at = (at + 1) & (kMemoSlots - 1);
        }
        // 近くが全部埋まっていた。最初の場所を上書きする（答えは変わらない）
        MemoEntry &e = memo_[hashOf(s)];
        e.lo = s.lo;
        e.hi = s.hi;
        e.value = value;
        e.used = 1;
    }

    void memoClear()
    {
        for (uint32_t i = 0; i < kMemoSlots; ++i) {
            memo_[i].used = 0;
        }
    }

    struct Candidate {
        uint16_t question;
        bool safe;
        double gain;
        Mask yes;
        Mask no;
    };

    static constexpr uint16_t kDepthGuard = 64;
    static constexpr uint32_t kProbeLimit = 8;

    Session session_;
    Shortlist shortlist_;
    Advisor *advisor_ = nullptr;

    // 146 問ぶんの作業表と覚え書き。**この 2 つがあるので Engine は大きい**
    Candidate scratch_[content::kQuestionCount];
    uint16_t scratch_count_ = 0;
    MemoEntry memo_[kMemoSlots] = {};

    uint32_t memo_hits_ = 0;
    uint32_t memo_misses_ = 0;
    uint16_t max_depth_ = 0;
};

// ---------------------------------------------------------------------------
// 段階 1 の Advisor：コードの基準どおりに選ぶ（通信しない）
//
// 設計書 6.3「初期版のローカル処理は Jev の安全候補内ランダム選択を再現する必要は
// なく、決定的な基準質問を使う」。つまり毎回いちばん上の質問を選ぶ。
// ---------------------------------------------------------------------------
class LocalAdvisor : public Advisor {
public:
    uint8_t chooseQuestion(const Session &, const Shortlist &) override { return 0; }
    uint16_t chooseGuess(const Session &, const Mask &) override { return kNoItem; }
    bool hasProbabilities() const override { return false; }
    const char *engineName() const override { return "rule"; }
};

}  // namespace esper
}  // namespace coffee
