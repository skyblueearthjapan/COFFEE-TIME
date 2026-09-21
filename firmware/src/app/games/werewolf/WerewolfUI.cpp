#include "WerewolfUI.h"

#include "../../ui/ScreenManager.h"
#include "../../ui/UiKit.h"
#include "../detective/DetectiveGame.h"
#include "../duel/DuelGame.h"
#include "../esper/EsperGame.h"
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

lv_obj_t *createEntryScreen()
{
    lv_obj_t *scr = makeScreen();
    makeTitle(scr, "ゲーム");

    // 並び順はユーザー指定（2026-09-21）: AI DUEL → エスパー → 探偵 → 人狼
    // （メニュー画面と同じ 4 段の配置）
    makeMenuItem(scr, "AI DUEL", openDuelCb, nullptr, -104, true);
    makeMenuItem(scr, "エスパー対決", openEsperCb, nullptr, -34, true);
    makeMenuItem(scr, "喫茶「余白」の事件簿", openDetectiveCb, nullptr, 36, true);
    makeMenuItem(scr, "閉店後の人狼会", openWerewolfCb, nullptr, 106, true);

    makeBackButton(scr);
    return scr;
}

}  // namespace werewolf
