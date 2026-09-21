#include "WerewolfUI.h"

#include "../../ui/ScreenManager.h"
#include "../../ui/UiKit.h"
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

lv_obj_t *createEntryScreen()
{
    lv_obj_t *scr = makeScreen();
    makeTitle(scr, "ゲーム");

    makeMenuItem(scr, "閉店後の人狼会", openWerewolfCb, nullptr, -60, true);
    makeMenuItem(scr, "喫茶「余白」の事件簿", nullptr, nullptr, 10, false);
    makeMenuItem(scr, "エスパー対決", nullptr, nullptr, 80, false);

    makeBackButton(scr);
    return scr;
}

}  // namespace werewolf
