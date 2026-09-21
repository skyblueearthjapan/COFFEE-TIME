#include "WerewolfUI.h"

#include "../../ui/ScreenManager.h"
#include "../../ui/UiKit.h"
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

lv_obj_t *createEntryScreen()
{
    lv_obj_t *scr = makeScreen();
    // 5 段になったので、見出しは大きな makeTitle ではなく makeHeading にした
    // （y=52〜82。makeTitle だと 1 段目の上端 89px と重なる）
    makeHeading(scr, "ゲーム");

    // 並び順はユーザー指定（2026-09-21 の 4 つ ＋ 2026-09-22 のリバーシ）:
    // AI DUEL → エスパー → 探偵 → 人狼 → リバーシ。
    // 行の高さ 54・間隔 62 で 5 段。上端 89px・下端 391px で半径 228px の円の内側
    constexpr int kStep = 62;
    constexpr int kHeight = 54;
    makeMenuItem(scr, "AI DUEL", openDuelCb, nullptr, -2 * kStep, true, kHeight);
    makeMenuItem(scr, "エスパー対決", openEsperCb, nullptr, -kStep, true, kHeight);
    makeMenuItem(scr, "喫茶「余白」の事件簿", openDetectiveCb, nullptr, 0, true, kHeight);
    makeMenuItem(scr, "閉店後の人狼会", openWerewolfCb, nullptr, kStep, true, kHeight);
    makeMenuItem(scr, "リバーシ", openReversiCb, nullptr, 2 * kStep, true, kHeight);

    makeBackButton(scr);
    return scr;
}

}  // namespace werewolf
