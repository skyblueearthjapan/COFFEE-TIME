// JEV REVERSI の純粋ロジックの PC 上の試験。
//
// 実機は要らない。tools/run_reversi_checks.py が zig を呼んでこれを組み立て、実行する:
//   python tools/run_reversi_checks.py
//
// 対象は Arduino も LVGL も使わない 3 つのヘッダーだけ:
//   firmware/src/app/games/reversi/core/reversi_core.hpp     （設計一式・無改変）
//   firmware/src/app/games/reversi/core/reversi_session.hpp  （設計一式・無改変）
//   firmware/src/app/games/reversi/core/reversi_extra.hpp    （この実装の継ぎ足し）
//
// 確かめること:
//   1. 設計一式の golden_positions.json 41 件と完全一致（合法手・反転・次局面・
//      エラー・終局・勝者）
//   2. hit_test と、座標 "C2" ・棋譜 "C2:H" / "PASS:F" の相互変換
//   3. セッションの保存形式（REV1）の往復と、壊れた塊をすべて弾くこと
//   4. 端末 AI の手が、GAS と共通の JS（gas/ReversiShared.gs）の localMove と
//      6×6 / 8×8 の多数の局面で一致すること（期待値は Node が作る）
//   5. 端末AI 対 端末AI / でたらめ 対 端末AI を多数局（不正手 0・強制パスが正しい・
//      必ず終局する・石の枚数の不変条件）
//   6. 棋譜の書き出し（JSON）を吐き出し、Node の CTReversi.replay に通すこと
//      （実際の照合は run_reversi_checks.py が Node で行う）
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../firmware/src/app/games/reversi/core/reversi_extra.hpp"
#include "reversi_golden.inc"

namespace core = ct_rev;
namespace rev = coffee::rev;

using core::Cell;
using core::Closure;
using core::Error;
using core::Mode;
using core::Position;
using core::Session;
using core::Source;

// ---------------------------------------------------------------------------
// 試験の道具（AI DUEL の tools/duel_checks.cpp と同じ書き方）
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

static void note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
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

// 再現できるでたらめ（xorshift64*）。実機の esp_random とは別物で、試験の中だけ
static uint64_t g_rng = 0x9E3779B97F4A7C15ull;

static void seedRng(uint64_t seed)
{
    g_rng = seed ? seed : 1;
}

static uint32_t nextRandom()
{
    g_rng ^= g_rng >> 12;
    g_rng ^= g_rng << 25;
    g_rng ^= g_rng >> 27;
    return (uint32_t)((g_rng * 0x2545F4914F6CDD1Dull) >> 32);
}

// ---------------------------------------------------------------------------
// 局面の組み立て（手本データは cells / side / ply を直接持っている）
// ---------------------------------------------------------------------------
static Position fromCells(uint8_t n, const char *cells, char side, uint16_t ply)
{
    Position p;
    p.n = n;
    p.side = side == 'B' ? Cell::Black : Cell::White;
    p.ply = ply;
    for (int i = 0; i < n * n; ++i) {
        p.board[i] = cells[i] == 'B' ? Cell::Black : cells[i] == 'W' ? Cell::White : Cell::Empty;
    }
    return p;
}

static bool sameCells(const Position &p, const char *cells)
{
    for (int i = 0; i < p.n * p.n; ++i) {
        if (core::symbol(p.board[i]) != cells[i]) {
            return false;
        }
    }
    return true;
}

static const char *errorName(Error e)
{
    switch (e) {
    case Error::Ok:            return "";
    case Error::BadState:      return "BAD_STATE";
    case Error::OutOfRange:    return "OUT_OF_RANGE";
    case Error::Occupied:      return "OCCUPIED";
    case Error::NoCapture:     return "NO_CAPTURE";
    case Error::PassForbidden: return "PASS_FORBIDDEN";
    default:                   return "FINISHED";
    }
}

// ---------------------------------------------------------------------------
// 1. 設計一式の手本 41 件（golden_positions.json）
// ---------------------------------------------------------------------------
static void checkGolden()
{
    std::printf("[1] golden_positions.json %d 件（設計一式・無改変）\n", kGoldenCount);
    CHECK(kGoldenCount >= 40);      // 手本が空でも合格してしまわないように

    for (int k = 0; k < kGoldenCount; ++k) {
        const GoldenCase &g = kGolden[k];
        const Position start = fromCells(g.n, g.cells, g.side, g.ply);
        CHECK(core::valid(start));

        // 合法手（行優先の昇順）
        const auto moves = core::legal(start, start.side);
        CHECK(moves.count == g.legal_count);
        for (uint8_t i = 0; i < g.legal_count && i < moves.count; ++i) {
            CHECK(moves.cells[i] == g.legal[i]);
        }

        // 反転する石（昇順）
        uint8_t flips[64];
        const uint8_t flip_count = g.move < 0 ? 0 : rev::flipList(start, g.move, flips);
        CHECK(flip_count == g.flip_count);
        for (uint8_t i = 0; i < g.flip_count && i < flip_count; ++i) {
            CHECK(flips[i] == g.flips[i]);
        }

        // 終局・勝者
        CHECK(core::terminal(start) == g.terminal);
        CHECK(core::winner(start) == g.winner);

        // 着手（失敗したときは盤面・手番・ply を変えない）
        Position after = start;
        const auto t = core::apply(after, g.move);
        CHECK(std::strcmp(errorName(t.error), g.error) == 0);
        CHECK(after.n == g.next_n);
        CHECK(core::symbol(after.side) == g.next_side);
        CHECK(after.ply == g.next_ply);
        CHECK(sameCells(after, g.next_cells));
        if (t.error == Error::Ok && g.move >= 0) {
            CHECK(t.flipped == g.flip_count);
        }
        if (g.error[0] != '\0') {
            CHECK(sameCells(after, g.cells));   // 失敗時はまったく変わらない
        }
    }
    std::printf("  %d 件・合法手と反転と次局面まで一致\n", kGoldenCount);
}

// ---------------------------------------------------------------------------
// 2. タッチの当たり判定と、座標・棋譜の文字列
// ---------------------------------------------------------------------------
static void checkCoords()
{
    std::printf("[2] hit_test と座標・棋譜の文字列\n");

    for (uint8_t n : {6, 8}) {
        const int unit = 312 / n;
        for (int r = 0; r < n; ++r) {
            for (int c = 0; c < n; ++c) {
                const int square = r * n + c;
                // マスの中心・左上（含む）・右下の 1px 手前（含む）
                CHECK(core::hit_test(84 + c * unit + unit / 2, 84 + r * unit + unit / 2, n) == square);
                CHECK(core::hit_test(84 + c * unit, 84 + r * unit, n) == square);
                CHECK(core::hit_test(84 + c * unit + unit - 1, 84 + r * unit + unit - 1, n) == square);

                char name[rev::kCoordMax];
                rev::coordName(n, square, name);
                CHECK(name[0] == 'A' + c && name[1] == '1' + r && name[2] == '\0');
                CHECK(rev::squareOf(n, name) == square);
            }
        }
        // 盤の外（右端・下端の 396 は盤外＝2 つのマスで重複受理しない）
        CHECK(core::hit_test(83, 200, n) == -1);
        CHECK(core::hit_test(200, 83, n) == -1);
        CHECK(core::hit_test(396, 200, n) == -1);
        CHECK(core::hit_test(200, 396, n) == -1);
        // 6×6 は 312/6=52 でぴったり、8×8 は 312/8=39 でぴったり
        CHECK(unit * n == 312);
        // 盤の外の番号は PASS 扱い
        char pass[rev::kCoordMax];
        rev::coordName(n, core::PASS, pass);
        CHECK(std::strcmp(pass, "PASS") == 0);
        CHECK(rev::squareOf(n, "PASS") == core::PASS);
        CHECK(rev::squareOf(n, "Z9") == -2);
        CHECK(rev::squareOf(n, "A9") == -2);
        CHECK(rev::squareOf(n, "") == -2);
        CHECK(rev::squareOf(n, nullptr) == -2);
    }
    // 6×6 に H8 は無い
    CHECK(rev::squareOf(6, "H8") == -2);
    CHECK(rev::squareOf(8, "H8") == 63);

    // 棋譜の 1 件
    char token[rev::kTokenMax];
    rev::historyToken(6, core::Event{8, Source::Human}, token);
    CHECK(std::strcmp(token, "C2:H") == 0);
    rev::historyToken(6, core::Event{255, Source::Forced}, token);
    CHECK(std::strcmp(token, "PASS:F") == 0);
    rev::historyToken(8, core::Event{9, Source::Jev}, token);
    CHECK(std::strcmp(token, "B2:J") == 0);
    rev::historyToken(8, core::Event{0, Source::Local}, token);
    CHECK(std::strcmp(token, "A1:L") == 0);

    Source s;
    CHECK(rev::sourceOf('H', s) && s == Source::Human);
    CHECK(rev::sourceOf('J', s) && s == Source::Jev);
    CHECK(rev::sourceOf('L', s) && s == Source::Local);
    CHECK(rev::sourceOf('F', s) && s == Source::Forced);
    CHECK(!rev::sourceOf('X', s));

    std::array<uint8_t, 16> id{};
    for (size_t i = 0; i < 16; ++i) {
        id[i] = (uint8_t)(i * 17);
    }
    char hex[33];
    rev::idHex(id, hex);
    CHECK(std::strlen(hex) == 32);
    CHECK(std::strncmp(hex, "00112233", 8) == 0);
    for (const char *p = hex; *p; ++p) {
        CHECK((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'));
    }
    std::printf("  6×6 / 8×8 の全マスと盤外、座標・棋譜・局 ID の文字列を確認\n");
}

// ---------------------------------------------------------------------------
// 3. セッションの保存形式（REV1）
// ---------------------------------------------------------------------------
static bool sameSession(const Session &a, const Session &b)
{
    if (a.pos.n != b.pos.n || a.pos.side != b.pos.side || a.pos.ply != b.pos.ply) return false;
    for (int i = 0; i < a.pos.n * a.pos.n; ++i) {
        if (a.pos.board[i] != b.pos.board[i]) return false;
    }
    if (a.human != b.human || a.mode != b.mode || a.closure != b.closure) return false;
    if (a.localOnly != b.localOnly || a.generation != b.generation || a.count != b.count) return false;
    if (a.id != b.id) return false;
    for (uint16_t i = 0; i < a.count; ++i) {
        if (a.history[i].move != b.history[i].move || a.history[i].source != b.history[i].source) {
            return false;
        }
    }
    return true;
}

// 端末 AI 同士で 1 局進める。各手のあとに呼び出し側の処理を挟めるようにする。
// **commit が断ったらその場で止める**（黙って無限に回らないように）
template <typename OnPly>
static void playLocalGame(Session &s, OnPly &&on_ply)
{
    int guard = 0;
    while (s.closure == Closure::Active && s.count < 128) {
        if (++guard > 130) {
            report(false, "playLocalGame did not finish", __LINE__);
            break;
        }
        const auto moves = core::legal(s.pos, s.pos.side);
        const int move = moves.count == 0 ? core::PASS
                       : moves.count == 1 ? (int)moves.cells[0]
                                          : core::local_move(s.pos);
        const Source source = moves.count >= 2
                                  ? (s.pos.side == s.human ? Source::Human : Source::Local)
                                  : (s.pos.side == s.human && moves.count == 1 ? Source::Human
                                                                               : Source::Forced);
        if (!core::commit(s, move, source, s.pos.ply)) {
            report(false, "playLocalGame: the core refused a move", __LINE__);
            break;
        }
        on_ply(s);
        if (core::terminal(s.pos)) {
            break;
        }
    }
}

static void checkSnapshot()
{
    std::printf("[3] セッションの保存形式（REV1・最大 %u バイト）\n", (unsigned)core::SNAPSHOT_MAX);

    seedRng(20260922);
    int encoded = 0, corrupted = 0;

    for (int game = 0; game < 40; ++game) {
        const uint8_t n = (game % 2) ? 8 : 6;
        std::array<uint8_t, 16> id{};
        for (size_t i = 0; i < 16; ++i) {
            id[i] = (uint8_t)nextRandom();
        }
        Session s;
        CHECK(core::start(s, n, (game % 4 < 2) ? Cell::Black : Cell::White,
                          (Mode)(game % 4), id));

        playLocalGame(s, [&](const Session &now) {
            std::array<uint8_t, core::SNAPSHOT_MAX> blob{};
            const size_t bytes = core::encode(now, blob);
            CHECK(bytes == (size_t)(38 + 2 * now.count));
            CHECK(bytes <= core::SNAPSHOT_MAX);
            ++encoded;

            Session back;
            CHECK(core::decode(blob.data(), bytes, back));
            CHECK(sameSession(now, back));

            // 1 ビットでも変われば必ず弾く（CRC が全バイトを覆っているため）
            if ((nextRandom() & 7u) == 0) {
                std::array<uint8_t, core::SNAPSHOT_MAX> broken = blob;
                const size_t at = nextRandom() % bytes;
                broken[at] ^= (uint8_t)(1u << (nextRandom() % 8));
                Session ignored;
                CHECK(!core::decode(broken.data(), bytes, ignored));
                ++corrupted;
            }
        });
        // 終局した局は Completed になっている（強制パスで終わった局も含む）
        CHECK(core::terminal(s.pos) == (s.closure == Closure::Completed));
    }

    // 決まった形の壊し方も全部弾く
    std::array<uint8_t, 16> id{};
    Session s;
    CHECK(core::start(s, 6, Cell::Black, Mode::Jev, id));
    CHECK(core::commit(s, 8, Source::Human, 0));        // C2
    std::array<uint8_t, core::SNAPSHOT_MAX> blob{};
    const size_t bytes = core::encode(s, blob);
    Session out;
    CHECK(core::decode(blob.data(), bytes, out));

    // **CRC を計算し直してから**渡す。そうしないと「CRC が合わない」だけで弾かれてしまい、
    // 版・盤サイズ・出所などの検査そのものを通っていないことになる
    auto rejects = [&](const char *what, void (*mangle)(uint8_t *)) {
        std::array<uint8_t, core::SNAPSHOT_MAX> copy = blob;
        mangle(copy.data());
        core::put32(copy.data() + bytes - 4, core::crc32(copy.data(), bytes - 4));
        Session ignored;
        const bool rejected = !core::decode(copy.data(), bytes, ignored);
        report(rejected, what, __LINE__);
    };
    // CRC を直さない壊し方は、CRC だけで弾けること（決め打ちの 1 か所）
    {
        std::array<uint8_t, core::SNAPSHOT_MAX> copy = blob;
        copy[34] ^= 0x01;       // 棋譜の 1 手め。CRC は直さない
        Session ignored;
        CHECK(!core::decode(copy.data(), bytes, ignored));
    }

    rejects("magic", [](uint8_t *p) { p[0] = 'X'; });
    rejects("version", [](uint8_t *p) { p[4] = 2; });
    rejects("n", [](uint8_t *p) { p[5] = 7; });
    rejects("human", [](uint8_t *p) { p[6] = 0; });
    rejects("mode", [](uint8_t *p) { p[7] = 9; });
    rejects("closure", [](uint8_t *p) { p[8] = 4; });
    rejects("localOnly", [](uint8_t *p) { p[9] = 2; });
    rejects("reserved", [](uint8_t *p) { p[10] = 1; });
    rejects("generation=0", [](uint8_t *p) { for (int i = 0; i < 4; ++i) p[12 + i] = 0; });
    rejects("count", [](uint8_t *p) { p[32] = 5; });
    rejects("source", [](uint8_t *p) { p[35] = 4; });
    rejects("illegal move", [](uint8_t *p) { p[34] = 0; });
    // まだ終局していないのに「終局した」と書いてある（設計書 11.1）
    rejects("closure says completed", [](uint8_t *p) { p[8] = 1; });
    // 世代が棋譜から数え直した値より小さい（古い塊で新しい塊を上書きさせない）
    rejects("generation too low", [](uint8_t *p) { core::put32(p + 12, 1); });

    // 終局した塊に「まだ続いている」と書く / 投了・中断と書く → すべて拒否
    {
        Session done;
        CHECK(core::start(done, 6, Cell::Black, Mode::Local, id));
        playLocalGame(done, [](const Session &) {});
        CHECK(done.closure == Closure::Completed);
        std::array<uint8_t, core::SNAPSHOT_MAX> end_blob{};
        const size_t end_bytes = core::encode(done, end_blob);
        Session back;
        CHECK(core::decode(end_blob.data(), end_bytes, back) && sameSession(done, back));
        for (uint8_t closure : {0, 2, 3}) {
            std::array<uint8_t, core::SNAPSHOT_MAX> copy = end_blob;
            copy[8] = closure;
            core::put32(copy.data() + end_bytes - 4, core::crc32(copy.data(), end_bytes - 4));
            Session ignored;
            report(!core::decode(copy.data(), end_bytes, ignored),
                   "a finished game must stay Completed", __LINE__);
        }
    }

    // 長さ違い（短すぎ・長すぎ・件数と合わない）
    Session ignored;
    CHECK(!core::decode(blob.data(), 37, ignored));
    CHECK(!core::decode(blob.data(), bytes + 1, ignored));
    CHECK(!core::decode(blob.data(), core::SNAPSHOT_MAX + 1, ignored));
    CHECK(!core::decode(nullptr, bytes, ignored));

    // 出所のきまり（設計書 3.3）
    Session t;
    CHECK(core::start(t, 6, Cell::Black, Mode::Jev, id));
    CHECK(!core::commit(t, 8, Source::Jev, 0));         // 人間の手を J にはできない
    CHECK(!core::commit(t, 8, Source::Forced, 0));      // 人間の唯一の手も F ではない
    CHECK(core::commit(t, 8, Source::Human, 0));
    CHECK(!core::commit(t, 8, Source::Local, 1));       // すでに石がある
    Session u = t;
    CHECK(core::freezeLocal(u));
    CHECK(u.localOnly);
    const auto white = core::legal(u.pos, u.pos.side);
    CHECK(white.count >= 2);
    CHECK(!core::commit(u, white.cells[0], Source::Jev, u.pos.ply));   // 端末AIに切り替えた後は J 不可
    CHECK(core::commit(u, white.cells[0], Source::Local, u.pos.ply));

    // 端末 AI に切り替えた局で localOnly を落とす → 拒否
    {
        std::array<uint8_t, core::SNAPSHOT_MAX> local_blob{};
        const size_t local_bytes = core::encode(u, local_blob);
        CHECK(local_bytes > 0);
        Session back;
        CHECK(core::decode(local_blob.data(), local_bytes, back) && sameSession(u, back));
        local_blob[9] = 0;
        core::put32(local_blob.data() + local_bytes - 4,
                    core::crc32(local_blob.data(), local_bytes - 4));
        Session broken;
        report(!core::decode(local_blob.data(), local_bytes, broken),
               "localOnly must stay set once the on-device AI has moved", __LINE__);
    }

    // 投了・中断した局も往復できること（設計書 2.5：どちらも未終局でだけ成り立つ）
    for (Closure closure : {Closure::Resigned, Closure::Aborted}) {
        Session closed = t;
        CHECK(core::close(closed, closure));
        CHECK(closed.closure == closure);
        CHECK(!core::close(closed, closure));       // 二度は閉じられない
        CHECK(!core::commit(closed, 0, Source::Human, closed.pos.ply));   // 閉じた局は進まない
        std::array<uint8_t, core::SNAPSHOT_MAX> closed_blob{};
        const size_t closed_bytes = core::encode(closed, closed_blob);
        Session back;
        CHECK(core::decode(closed_blob.data(), closed_bytes, back));
        CHECK(sameSession(closed, back));
    }

    std::printf("  %d 局面を往復・%d 件のビット化けと 14 種の壊し方 ＋ 終局／localOnly の"
                "つじつま合わせを拒否\n", encoded, corrupted);
    CHECK(encoded > 500 && corrupted > 20);     // 数えていないのに合格しない
}

// ---------------------------------------------------------------------------
// 3b. 相手の区分と勝敗（画面と NVS の統計が使う判定。core/reversi_extra.hpp）
// ---------------------------------------------------------------------------
static void checkClassification()
{
    std::printf("[3b] 相手の区分と勝敗の判定\n");
    std::array<uint8_t, 16> id{};

    // C2（黒・人間）→ 白（AI）は合法手が 2 つ以上あるので J / L を選べる
    Session base;
    CHECK(core::start(base, 6, Cell::Black, Mode::Jev, id));
    CHECK(core::commit(base, 8, Source::Human, 0));
    const auto white = core::legal(base.pos, base.pos.side);
    CHECK(white.count >= 2);

    // Jev だけで打った局
    Session jev = base;
    CHECK(core::commit(jev, white.cells[0], Source::Jev, jev.pos.ply));
    CHECK(rev::usedJev(jev) && !rev::usedLocal(jev));
    CHECK(rev::opponentOf(jev) == rev::Opponent::Jev);

    // 途中で端末 AI に切り替えた局 → 混在
    Session mixed = jev;
    CHECK(core::freezeLocal(mixed));
    CHECK(rev::usedJev(mixed) && rev::usedLocal(mixed));
    CHECK(rev::opponentOf(mixed) == rev::Opponent::Mixed);

    // Jev が 1 手も打たないまま切り替えた局 → 端末AI
    Session frozen = base;
    CHECK(core::freezeLocal(frozen));
    CHECK(!rev::usedJev(frozen) && rev::usedLocal(frozen));
    CHECK(rev::opponentOf(frozen) == rev::Opponent::Local);

    // モードそのままの区分
    struct ModeCase { Mode mode; rev::Opponent opponent; };
    static const ModeCase kModeCases[] = {
        {Mode::Jev, rev::Opponent::Jev}, {Mode::Pro, rev::Opponent::Pro},
        {Mode::Casual, rev::Opponent::Casual}, {Mode::Local, rev::Opponent::Local},
    };
    for (const ModeCase &m : kModeCases) {
        Session fresh;
        CHECK(core::start(fresh, 8, Cell::White, m.mode, id));
        CHECK(rev::opponentOf(fresh) == m.opponent);
        CHECK(rev::usedLocal(fresh) == (m.mode == Mode::Local));
        CHECK(!rev::usedJev(fresh));
    }

    // 勝敗: 中断は数えない・投了は人間の負け
    rev::Outcome outcome = rev::Outcome::Draw;
    CHECK(!rev::outcomeOf(base, outcome));              // まだ続いている
    Session aborted = base;
    CHECK(core::close(aborted, Closure::Aborted));
    CHECK(!rev::outcomeOf(aborted, outcome));
    Session resigned = base;
    CHECK(core::close(resigned, Closure::Resigned));
    CHECK(rev::outcomeOf(resigned, outcome) && outcome == rev::Outcome::Loss);

    // 終局した局は盤面の枚数どおり（黒番・白番の両方で確かめる）
    seedRng(0xB0A2D);
    int wins = 0, losses = 0, draws = 0;
    for (int game = 0; game < 40; ++game) {
        Session s;
        CHECK(core::start(s, (game % 2) ? 8 : 6,
                          (game % 4 < 2) ? Cell::Black : Cell::White, Mode::Local, id));
        playLocalGame(s, [](const Session &) {});
        CHECK(s.closure == Closure::Completed);
        CHECK(rev::outcomeOf(s, outcome));
        const auto c = core::counts(s.pos);
        const int mine = s.human == Cell::Black ? c.black : c.white;
        const int theirs = s.human == Cell::Black ? c.white : c.black;
        CHECK(outcome == (mine > theirs    ? rev::Outcome::Win
                        : mine < theirs    ? rev::Outcome::Loss
                                           : rev::Outcome::Draw));
        (outcome == rev::Outcome::Win ? wins : outcome == rev::Outcome::Loss ? losses : draws)++;
    }
    std::printf("  40 局の終局を判定（勝 %d ・ 負 %d ・ 分 %d）\n", wins, losses, draws);
    CHECK(wins + losses + draws == 40);
}

// ---------------------------------------------------------------------------
// 4. 端末 AI の手が GAS と共通の JS（gas/ReversiShared.gs）と一致するか
// ---------------------------------------------------------------------------
static void checkLocalAgainstJs()
{
    std::printf("[4] 端末 AI の手 vs gas/ReversiShared.gs の localMove（%d 局面）\n", kLocalCount);

    int six = 0, eight = 0;
    for (int k = 0; k < kLocalCount; ++k) {
        const LocalCase &c = kLocal[k];
        const Position p = fromCells(c.n, c.cells, c.side, c.ply);
        CHECK(core::valid(p));
        const int got = core::local_move(p);
        if (got != c.expected) {
            note("局面 %d (%u×%u ply=%u): C++ %d / JS %d\n", k, (unsigned)c.n, (unsigned)c.n,
                 (unsigned)c.ply, got, c.expected);
        }
        CHECK(got == c.expected);
        (c.n == 6 ? six : eight)++;
    }
    std::printf("  6×6 %d 局面 / 8×8 %d 局面が完全一致\n", six, eight);
    // 期待値が 1 件も無いまま「一致」と言わない
    CHECK(kLocalCount >= 200);
    CHECK(six >= 100 && eight >= 100);
}

// ---------------------------------------------------------------------------
// 5. 多数の対局を通す（不正手 0・強制パス・必ず終局）
// ---------------------------------------------------------------------------
struct Tally {
    int games = 0;
    int plies = 0;
    int passes = 0;
    int wins[3] = {0, 0, 0};    // 黒勝 / 白勝 / 引き分け
    int longest = 0;
    int shortest = 999;
};

// random_human = true なら人間役をでたらめに、false なら端末 AI 同士。
// 端末 AI 同士は手が決まっているので、そのままでは同じ 1 局を 400 回並べるだけになる。
// 出だしの数手だけでたらめにして、局ごとに別の展開をたどらせる
static void runGames(uint8_t n, int count, bool random_human, Tally &tally)
{
    for (int game = 0; game < count; ++game) {
        const int opening = random_human ? 0 : (game % 9);
        std::array<uint8_t, 16> id{};
        for (size_t i = 0; i < 16; ++i) {
            id[i] = (uint8_t)nextRandom();
        }
        Session s;
        const Cell human = (nextRandom() & 1u) ? Cell::Black : Cell::White;
        CHECK(core::start(s, n, human, random_human ? Mode::Jev : Mode::Local, id));

        int guard = 0;
        while (s.closure == Closure::Active) {
            CHECK(++guard <= 128);
            if (guard > 128) break;

            const auto mine = core::legal(s.pos, s.pos.side);
            const auto theirs = core::legal(s.pos, core::other(s.pos.side));
            // 双方 0 手なら commit の前に終局しているはず（架空のパスを足さない）
            CHECK(!(mine.count == 0 && theirs.count == 0));

            const bool pick_random = random_human ? (s.pos.side == s.human)
                                                  : ((int)s.count < opening);
            int move;
            Source source;
            if (mine.count == 0) {
                // 強制パス: PASS 以外は絶対に通らない
                move = core::PASS;
                source = Source::Forced;
                Session probe = s;
                CHECK(!core::commit(probe, 0, Source::Forced, probe.pos.ply));
                ++tally.passes;
            } else if (s.pos.side == s.human) {
                move = pick_random ? (int)mine.cells[nextRandom() % mine.count]
                                   : core::local_move(s.pos);
                source = Source::Human;
            } else if (mine.count == 1) {
                move = mine.cells[0];
                source = Source::Forced;
            } else {
                move = pick_random ? (int)mine.cells[nextRandom() % mine.count]
                                   : core::local_move(s.pos);
                source = Source::Local;
            }

            // 打つ前の枚数（パスでは変わらない・着手では合計が 1 枚だけ増える）
            const auto before = core::counts(s.pos);
            uint8_t flips[64];
            const uint8_t expect_flips = move < 0 ? 0 : rev::flipList(s.pos, move, flips);
            CHECK(move < 0 || expect_flips > 0);        // 不正手を打っていない

            // ply と count は必ずそろっている（設計書 3.4）
            CHECK(s.count == s.pos.ply);
            CHECK(core::commit(s, move, source, s.pos.ply));
            ++tally.plies;

            const auto after = core::counts(s.pos);
            if (move < 0) {
                CHECK(before.black == after.black && before.white == after.white);
            } else {
                CHECK(before.black + before.white + 1 == after.black + after.white);
                CHECK(after.empty + 1 == before.empty);
            }
            if (core::terminal(s.pos)) {
                break;
            }
        }

        CHECK(s.closure == Closure::Completed);
        CHECK(core::terminal(s.pos));
        CHECK(s.count == s.pos.ply);
        // 配置の数は 6×6 で最大 32、8×8 で最大 60（設計書 3.4）
        int placements = 0;
        for (uint16_t i = 0; i < s.count; ++i) {
            if (s.history[i].move != 255) ++placements;
        }
        CHECK(placements <= n * n - 4);
        const auto c = core::counts(s.pos);
        CHECK(c.black + c.white == 4 + placements);
        const char w = core::winner(s.pos);
        CHECK(w == 'B' || w == 'W' || w == 'D');
        ++tally.wins[w == 'B' ? 0 : w == 'W' ? 1 : 2];
        if (s.count > tally.longest) {
            tally.longest = s.count;
        }
        if (s.count < tally.shortest) {
            tally.shortest = s.count;
        }
        ++tally.games;
    }
}

static void checkGames()
{
    std::printf("[5] 対局を多数（不正手 0・強制パス・必ず終局）\n");
    seedRng(0x5EED5EED);

    for (uint8_t n : {6, 8}) {
        Tally local_only, mixed;
        runGames(n, 400, false, local_only);
        runGames(n, 400, true, mixed);
        std::printf("  %u×%u  端末AI同士 %d 局（%d 手・パス %d・手数 %d〜%d・黒%d 白%d 分%d）\n"
                    "       でたらめ相手 %d 局（%d 手・パス %d・手数 %d〜%d・黒%d 白%d 分%d）\n",
                    (unsigned)n, (unsigned)n, local_only.games, local_only.plies,
                    local_only.passes, local_only.shortest, local_only.longest,
                    local_only.wins[0], local_only.wins[1], local_only.wins[2],
                    mixed.games, mixed.plies, mixed.passes, mixed.shortest, mixed.longest,
                    mixed.wins[0], mixed.wins[1], mixed.wins[2]);
        CHECK(local_only.games == 400 && mixed.games == 400);
        // 強制パスが 1 回も出ない試験は「パスの経路を通っていない」ので意味が薄い
        CHECK(local_only.passes + mixed.passes > 0);
    }
}

// ---------------------------------------------------------------------------
// 6. 棋譜の書き出し（Node の CTReversi.replay に通すための JSON）
// ---------------------------------------------------------------------------
static void writeSnapshots(const char *path)
{
    std::printf("[6] 棋譜の書き出し -> %s\n", path);
    std::FILE *f = std::fopen(path, "wb");
    if (f == nullptr) {
        std::printf("  FAIL: 書き出せません\n");
        ++g_fail;
        return;
    }
    std::fprintf(f, "[\n");
    seedRng(0xC0FFEE22);
    int written = 0;

    for (int game = 0; game < 24; ++game) {
        const uint8_t n = (game % 2) ? 8 : 6;
        std::array<uint8_t, 16> id{};
        for (size_t i = 0; i < 16; ++i) {
            id[i] = (uint8_t)nextRandom();
        }
        Session s;
        // 4 つのモードを順に使う。JEV / JEV PRO / CASUAL の局は相手の手を J として、
        // 端末 AI の局は L として記録する（CTReversi.replay はこの区別まで検査する）
        const Mode mode = (Mode)(game % 4);
        const bool as_jev = mode != Mode::Local;
        CHECK(core::start(s, n, (game % 4 < 2) ? Cell::Black : Cell::White, mode, id));

        int guard = 0;
        while (s.closure == Closure::Active && ++guard <= 128) {
            const auto mine = core::legal(s.pos, s.pos.side);
            int move;
            Source source;
            if (mine.count == 0) {
                move = core::PASS;
                source = Source::Forced;
            } else if (s.pos.side == s.human) {
                move = mine.cells[nextRandom() % mine.count];
                source = Source::Human;
            } else if (mine.count == 1) {
                move = mine.cells[0];
                source = Source::Forced;
            } else {
                move = core::local_move(s.pos);
                source = as_jev ? Source::Jev : Source::Local;
            }
            CHECK(core::commit(s, move, source, s.pos.ply));

            // 途中の局面も 1 局につき数件だけ吐く（replay は途中の棋譜でも通るはず）
            if ((nextRandom() & 15u) == 0 || core::terminal(s.pos)) {
                char snapshot[2048];
                const size_t bytes = rev::snapshotJson(s, snapshot, sizeof(snapshot));
                CHECK(bytes > 0);
                char cells[65] = {0};
                for (int i = 0; i < n * n; ++i) {
                    cells[i] = core::symbol(s.pos.board[i]);
                }
                std::fprintf(f, "%s  {\"snapshot\":%s,\"ply\":%u,\"side\":\"%c\",\"cells\":\"%s\"}",
                             written ? ",\n" : "", snapshot, (unsigned)s.pos.ply,
                             core::symbol(s.pos.side), cells);
                ++written;
            }
            if (core::terminal(s.pos)) {
                break;
            }
        }
    }
    std::fprintf(f, "\n]\n");
    std::fclose(f);
    std::printf("  %d 件の棋譜を書き出しました（Node で CTReversi.replay に通します）\n", written);
    CHECK(written > 100);
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv)
{
    std::printf("JEV REVERSI ロジック試験\n\n");
    checkGolden();
    checkCoords();
    checkSnapshot();
    checkClassification();
    checkLocalAgainstJs();
    checkGames();
    if (argc > 1) {
        writeSnapshots(argv[1]);
    }

    std::printf("\n%d 項目を確認しました。", g_checks);
    if (g_fail == 0) {
        std::printf("失敗なし。\n");
        return 0;
    }
    std::printf("**%d 件 失敗**\n", g_fail);
    return 1;
}
