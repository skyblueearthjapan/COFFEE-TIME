#pragma once

// JEV REVERSI — 設計一式のコアに足りないものだけを集めた「継ぎ足し」のヘッダー。
//
// `core/reversi_core.hpp` と `core/reversi_session.hpp` は設計一式のものを
// **1 バイトも変えずに**置いてある（tools/run_reversi_checks.py がハッシュで見張る）。
// 座標の名前・棋譜の文字列・GAS へ送る JSON・反転の一覧（アニメーション用）は
// あちらには無いので、こちらに置く。
//
// ここも Arduino も LVGL も使わない純粋な C++（PC の検査でそのまま動く）。

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "reversi_core.hpp"
#include "reversi_session.hpp"

namespace coffee {
namespace rev {

namespace core = ct_rev;

// 座標は A1 が左上（設計書 2.1）。列は左から A〜H、行は上から 1〜8
constexpr size_t kCoordMax = 5;      // "PASS" + 終端
constexpr size_t kTokenMax = 8;      // "PASS:F" + 終端

// 盤上の番号 -> "C2" / パス(-1 または 255) -> "PASS"
inline void coordName(uint8_t n, int square, char *out)
{
    if (square < 0 || square >= n * n) {
        std::snprintf(out, kCoordMax, "PASS");
        return;
    }
    out[0] = (char)('A' + square % n);
    out[1] = (char)('1' + square / n);
    out[2] = '\0';
}

// "C2" -> 番号。"PASS" は core::PASS。読めなければ -2 を返す（PASS と区別する）
inline int squareOf(uint8_t n, const char *name)
{
    if (name == nullptr) {
        return -2;
    }
    if (std::strcmp(name, "PASS") == 0) {
        return core::PASS;
    }
    if (name[0] < 'A' || name[0] > 'H' || name[1] < '1' || name[1] > '8' || name[2] != '\0') {
        return -2;
    }
    const int c = name[0] - 'A', r = name[1] - '1';
    if (c >= n || r >= n) {
        return -2;
    }
    return r * n + c;
}

// 着手の出所（設計書 3.3）。H=人間 J=Jev L=端末AI F=選択の余地なし
inline char sourceChar(core::Source s)
{
    switch (s) {
    case core::Source::Human:  return 'H';
    case core::Source::Jev:    return 'J';
    case core::Source::Local:  return 'L';
    default:                   return 'F';
    }
}

inline bool sourceOf(char c, core::Source &out)
{
    switch (c) {
    case 'H': out = core::Source::Human;  return true;
    case 'J': out = core::Source::Jev;    return true;
    case 'L': out = core::Source::Local;  return true;
    case 'F': out = core::Source::Forced; return true;
    default:  return false;
    }
}

// 棋譜の 1 件を "C2:H" / "PASS:F" にする
inline void historyToken(uint8_t n, const core::Event &e, char *out)
{
    char name[kCoordMax];
    coordName(n, e.move == 255 ? core::PASS : (int)e.move, name);
    std::snprintf(out, kTokenMax, "%s:%c", name, sourceChar(e.source));
}

// 相手の種類の呼び名（GAS・NVS の統計と共通。設計書 1.2 / 計画 §4）
inline const char *modeId(core::Mode m)
{
    switch (m) {
    case core::Mode::Jev:    return "jev";
    case core::Mode::Pro:    return "jev_pro";
    case core::Mode::Casual: return "casual";
    default:                 return "local";
    }
}

// 16 バイトの局 ID を小文字 32 桁の 16 進にする（外部 JSON の形式。設計書 3.2）
inline void idHex(const std::array<uint8_t, 16> &id, char *out)
{
    static const char kDigits[] = "0123456789abcdef";
    for (size_t i = 0; i < 16; ++i) {
        out[2 * i] = kDigits[id[i] >> 4];
        out[2 * i + 1] = kDigits[id[i] & 0x0Fu];
    }
    out[32] = '\0';
}

// GAS へ送る snapshot（計画 §5）。
//   {"game_id":"…","n":6,"human":"B","mode":"jev","history":["C2:H","B2:J"]}
// 入り切らなければ 0 を返す（欠けた JSON は絶対に返さない）。
// **端末名・杯数・ほかのゲームの記録は入れない。**
inline size_t snapshotJson(const core::Session &s, char *out, size_t size)
{
    char id[33];
    idHex(s.id, id);
    int at = std::snprintf(out, size, "{\"game_id\":\"%s\",\"n\":%u,\"human\":\"%c\",\"mode\":\"%s\",\"history\":[",
                           id, (unsigned)s.pos.n, core::symbol(s.human), modeId(s.mode));
    if (at < 0 || (size_t)at >= size) {
        return 0;
    }
    for (uint16_t i = 0; i < s.count; ++i) {
        char token[kTokenMax];
        historyToken(s.pos.n, s.history[i], token);
        const int wrote = std::snprintf(out + at, size - (size_t)at, "%s\"%s\"", i ? "," : "", token);
        if (wrote < 0 || (size_t)(at + wrote) >= size) {
            return 0;
        }
        at += wrote;
    }
    const int tail = std::snprintf(out + at, size - (size_t)at, "]}");
    if (tail < 0 || (size_t)(at + tail) >= size) {
        return 0;
    }
    return (size_t)(at + tail);
}

// その一手で返る石の一覧（アニメーションと「返る石のプレビュー」用）。
// out には最大 63 件。返り値は枚数（置けない場所なら 0）
inline uint8_t flipList(const core::Position &p, int move, uint8_t *out)
{
    const uint64_t mask = move < 0 ? 0 : core::captures(p, move, p.side);
    uint8_t count = 0;
    for (int i = 0; i < p.n * p.n; ++i) {
        if (mask & (uint64_t(1) << i)) {
            out[count++] = (uint8_t)i;
        }
    }
    return count;
}

// 棋譜に Jev の手 / 端末 AI の手があるか（結果と統計の区分に使う）
inline bool usedJev(const core::Session &s)
{
    for (uint16_t i = 0; i < s.count; ++i) {
        if (s.history[i].source == core::Source::Jev) {
            return true;
        }
    }
    return false;
}

inline bool usedLocal(const core::Session &s)
{
    if (s.mode == core::Mode::Local || s.localOnly) {
        return true;
    }
    for (uint16_t i = 0; i < s.count; ++i) {
        if (s.history[i].source == core::Source::Local) {
            return true;
        }
    }
    return false;
}

// 統計の相手区分（計画 §4 の 5 つ）。並びは NVS に入るので変えない
enum class Opponent : uint8_t { Jev = 0, Pro = 1, Casual = 2, Local = 3, Mixed = 4 };
constexpr size_t kOpponentCount = 5;

// Jev も端末 AI も使ったら「混在」。端末 AI で続行を選んだ局は（まだ L を打って
// いなくても）Jev の手が無ければ「端末AI」。それ以外は選んだモードのまま
inline Opponent opponentOf(const core::Session &s)
{
    const bool jev = usedJev(s), local = usedLocal(s);
    if (jev && local) {
        return Opponent::Mixed;
    }
    if (local) {
        return Opponent::Local;
    }
    switch (s.mode) {
    case core::Mode::Pro:    return Opponent::Pro;
    case core::Mode::Casual: return Opponent::Casual;
    case core::Mode::Local:  return Opponent::Local;
    default:                 return Opponent::Jev;
    }
}

// 勝敗（人間から見て）。並びは NVS に入るので変えない
enum class Outcome : uint8_t { Win = 0, Loss = 1, Draw = 2 };
constexpr size_t kOutcomeCount = 3;

// 終局した局の勝敗。投了は人間の負け。aborted は数えない（設計書 2.5）
inline bool outcomeOf(const core::Session &s, Outcome &out)
{
    if (s.closure == core::Closure::Resigned) {
        out = Outcome::Loss;
        return true;
    }
    if (s.closure != core::Closure::Completed) {
        return false;
    }
    const char w = core::winner(s.pos);
    if (w == 'D') {
        out = Outcome::Draw;
    } else {
        out = (w == core::symbol(s.human)) ? Outcome::Win : Outcome::Loss;
    }
    return true;
}

}  // namespace rev
}  // namespace coffee
