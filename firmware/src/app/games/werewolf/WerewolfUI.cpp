#include "WerewolfUI.h"

#include "../../ui/ScreenManager.h"
#include "../../ui/UiKit.h"
#include "../cards/CardsGame.h"
#include "../detective/DetectiveGame.h"
#include "../duel/DuelGame.h"
#include "../esper/EsperGame.h"
#include "../reversi/ReversiGame.h"
#include "WerewolfGame.h"

namespace werewolf {

using namespace ui;

// ---- ゲーム一覧 -------------------------------------------------------------

static void openWerewolfCb(lv_event_t *e)
{
    (void)e;
    // W2 からは人数決め〜結果までを 1 枚の画面（WerewolfGame）で進める
    push(createGameScreen);
}

static void openDetectiveCb(lv_event_t *e)
{
    (void)e;
    // 段階 B からは話の一覧〜章のおわりまでを 1 枚の画面（DetectiveGame）で進める
    push(detective::createGameScreen);
}

static void openEsperCb(lv_event_t *e)
{
    (void)e;
    // 段階 1 は通信なし。むずかしさ選び〜結果までを 1 枚の画面（EsperGame）で進める
    push(esper::createGameScreen);
}

static void openDuelCb(lv_event_t *e)
{
    (void)e;
    // 入口〜10 回勝負〜癖の表示までを 1 枚の画面（DuelGame）で進める
    push(duel::createGameScreen);
}

static void openReversiCb(lv_event_t *e)
{
    (void)e;
    // 入口〜対局〜結果までを 1 枚の画面（ReversiGame）で進める
    push(reversi::createGameScreen);
}

static void openCardsCb(lv_event_t *e)
{
    (void)e;
    // 入口〜4 つの卓〜結果までを 1 枚の画面（CardsGame）で進める
    push(cards::createGameScreen);
}

lv_obj_t *createEntryScreen()
{
    lv_obj_t *scr = makeScreen();
    // 6 段になったので、見出しは大きな makeTitle ではなく makeHeading にした
    // （y=52〜82。makeTitle だと 1 段目の上端と重なる）
    makeHeading(scr, "ゲーム");

    // 並び順はユーザー指定（2026-09-21 の 4 つ）＋ リバーシ（第 5 枠）＋ POKER TABLE（第 6 枠）:
    // AI DUEL → エスパー → 探偵 → 人狼 → リバーシ → POKER TABLE。
    // **6 段にしたので行の高さと間隔を詰めた**（54/62 → 44/52）。
    // 中心は y=110/162/214/266/318/370。上端 88px・下端 392px・幅 300px の四隅すべてが
    // 中心 (240,240)・半径 228px の円の内側（いちばん遠い角で 214px）
    constexpr int kStep = 52;
    constexpr int kHeight = 44;
    makeMenuItem(scr, "AI DUEL", openDuelCb, nullptr, -5 * kStep / 2, true, kHeight);
    makeMenuItem(scr, "エスパー対決", openEsperCb, nullptr, -3 * kStep / 2, true, kHeight);
    makeMenuItem(scr, "喫茶「余白」の事件簿", openDetectiveCb, nullptr, -kStep / 2, true, kHeight);
    makeMenuItem(scr, "閉店後の人狼会", openWerewolfCb, nullptr, kStep / 2, true, kHeight);
    makeMenuItem(scr, "リバーシ", openReversiCb, nullptr, 3 * kStep / 2, true, kHeight);
    makeMenuItem(scr, "POKER TABLE", openCardsCb, nullptr, 5 * kStep / 2, true, kHeight);

    makeBackButton(scr);
    return scr;
}

}  // namespace werewolf
