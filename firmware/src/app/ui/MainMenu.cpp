#include "MainMenu.h"

#include "../games/werewolf/WerewolfUI.h"
#include "ScreenManager.h"
#include "UiKit.h"

namespace ui {

static void openGamesCb(lv_event_t *e)
{
    (void)e;
    push(werewolf::createEntryScreen);
}

lv_obj_t *createMainMenu()
{
    lv_obj_t *scr = makeScreen();
    makeTitle(scr, "メニュー");

    // 1〜3 は今後の実装（設計書の画面構成に合わせて枠だけ用意する）
    makeMenuItem(scr, "今日の状況", nullptr, nullptr, -104, false);
    makeMenuItem(scr, "履歴", nullptr, nullptr, -34, false);
    makeMenuItem(scr, "設定", nullptr, nullptr, 36, false);
    makeMenuItem(scr, "ゲーム", openGamesCb, nullptr, 106, true);

    makeBackButton(scr, "カフェへ");
    return scr;
}

}  // namespace ui
