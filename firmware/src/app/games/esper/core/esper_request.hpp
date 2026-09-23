// 端末 → GAS（Jev）の要求 JSON を組み立てるところ（docs/ESPER_STAGE2_PLAN.md §4）。
//
// esper_core.hpp と同じく Arduino も LVGL も使わない C++17 のヘッダーだけなので、
// 画面（EsperGame.cpp）と PC 上の試験（tools/esper_checks.cpp）の**両方が同じ関数**を
// 使う。「送ってよいものだけが入っている」ことを PC 側で確かめられるようにするため。
//
// ---------------------------------------------------------------------------
// 入れてよいもの（設計書 4.2／計画 §1）
// ---------------------------------------------------------------------------
//   モード・進行の番号（session / rev / req）・有効な回答履歴（質問 ID と yes/no）・
//   残っている候補の ID・安全な質問の ID・残り問数。
//
// **絶対に入れないもの**: 頭の中の答え、選んだもののメモ（EsperGame.cpp の s_memo）、
//   一覧で見たカード、外れたあとの申告（s_reveal）、滞在時間、社員名、コーヒーの履歴。
//   日本語の表示文・質問文も入れない（GAS が ID から英文に置き換える／計画 §2）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esper_core.hpp"

namespace coffee {
namespace esper {

// 依頼の本文の上限。net::kGasRequestMax と同じ値（EsperGame.cpp が static_assert で照合）。
// いちばん大きい要求は 128 候補 + 8 質問 + 10 問の履歴で 1KB ほどなので十分余る
constexpr size_t kRequestJsonMax = 3584;

namespace detail {

// 末尾に足す。入りきらなければ false（呼び出し側は 0 を返して送らない）
inline bool appendJson(char *out, size_t out_size, size_t &at, const char *text)
{
    const size_t n = std::strlen(text);
    if (at + n + 1 > out_size) {
        return false;
    }
    std::memcpy(out + at, text, n);
    at += n;
    out[at] = '\0';
    return true;
}

}  // namespace detail

// 要求 JSON を out に書く。書けた長さを返す（0 = 作れなかった＝送らない）。
// shortlist は phase が Question のときだけ入れる（最終予想の局面では中身が古い）
inline size_t buildRequestJson(const Session &s, const Shortlist &list, uint32_t req,
                               const char *session_id, char *out, size_t out_size)
{
    if (out == nullptr || out_size == 0) {
        return 0;
    }
    out[0] = '\0';
    size_t at = 0;

    char head[160];
    std::snprintf(head, sizeof(head),
                  "{\"event\":\"esper\",\"req\":%lu,\"session\":\"%.32s\",\"rev\":%u,"
                  "\"mode\":\"%s\",\"history\":[",
                  (unsigned long)req, session_id != nullptr ? session_id : "",
                  (unsigned)s.revision, s.modeInfo().id);
    if (!detail::appendJson(out, out_size, at, head)) {
        return 0;
    }

    // 有効な Yes/No だけ。取り消された行と「わからない」は入れない
    //（ふさいだ質問は shortlist に出てこないので、skip の情報は要らない）
    bool first = true;
    for (uint8_t i = 0; i < s.history_count && i < kMaxHistory; ++i) {
        const HistoryEntry &h = s.history[i];
        if (!h.active || h.answer == Answer::Skip) {
            continue;
        }
        if (h.question >= content::kQuestionCount) {
            return 0;
        }
        char row[48];
        std::snprintf(row, sizeof(row), "%s{\"q\":\"%s\",\"a\":\"%s\"}", first ? "" : ",",
                      content::kQuestions[h.question].id,
                      h.answer == Answer::Yes ? "yes" : "no");
        if (!detail::appendJson(out, out_size, at, row)) {
            return 0;
        }
        first = false;
    }

    if (!detail::appendJson(out, out_size, at, "],\"candidates\":[")) {
        return 0;
    }
    first = true;
    for (uint16_t i = 0; i < content::kItemCount; ++i) {
        if (!s.candidates.test(i)) {
            continue;
        }
        char row[16];
        std::snprintf(row, sizeof(row), "%s\"%s\"", first ? "" : ",", content::kItems[i].id);
        if (!detail::appendJson(out, out_size, at, row)) {
            return 0;
        }
        first = false;
    }

    if (!detail::appendJson(out, out_size, at, "],\"shortlist\":[")) {
        return 0;
    }
    first = true;
    if (s.phase == Phase::Question) {
        for (uint8_t i = 0; i < list.count && i < kShortlistMax; ++i) {
            if (list.question[i] >= content::kQuestionCount) {
                return 0;
            }
            char row[16];
            std::snprintf(row, sizeof(row), "%s\"%s\"", first ? "" : ",",
                          content::kQuestions[list.question[i]].id);
            if (!detail::appendJson(out, out_size, at, row)) {
                return 0;
            }
            first = false;
        }
    }

    char tail[48];
    std::snprintf(tail, sizeof(tail), "],\"remaining\":%u}", (unsigned)s.remainingQuestions());
    if (!detail::appendJson(out, out_size, at, tail)) {
        return 0;
    }
    return at;
}

// --- 返事に入っていた ID を番号に直す（画面側の検証で使う） -----------------
//
// どちらも「今の局面で使ってよいものか」まで見る。見つからなければ kNoQuestion / kNoItem

// 安全な質問一覧の中から探す（一覧外の質問は採用しない／設計書 3.5）
inline uint16_t questionInShortlist(const Shortlist &list, const char *id)
{
    if (id == nullptr || id[0] == '\0') {
        return kNoQuestion;
    }
    for (uint8_t i = 0; i < list.count && i < kShortlistMax; ++i) {
        const uint16_t q = list.question[i];
        if (q < content::kQuestionCount && std::strcmp(content::kQuestions[q].id, id) == 0) {
            return q;
        }
    }
    return kNoQuestion;
}

// 残っている候補の中から探す（候補外の予想は採用しない／設計書 4.5）
inline uint16_t itemInCandidates(const Mask &candidates, const char *id)
{
    if (id == nullptr || id[0] == '\0') {
        return kNoItem;
    }
    for (uint16_t i = 0; i < content::kItemCount; ++i) {
        if (candidates.test(i) && std::strcmp(content::kItems[i].id, id) == 0) {
            return i;
        }
    }
    return kNoItem;
}

}  // namespace esper
}  // namespace coffee
