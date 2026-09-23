#include "CardsGame.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_random.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "../../CupState.h"
#include "../../Display.h"
#include "../../HomeScreen.h"
#include "../../NetService.h"
#include "../../ui/ScreenManager.h"
#include "../../ui/Thinking.h"
#include "../../ui/UiKit.h"
#include "CardsStore.h"
#include "core/cards_extra.hpp"

LV_FONT_DECLARE(ct_font_jp_20);
LV_FONT_DECLARE(ct_font_jp_22);
LV_FONT_DECLARE(ct_font_jp_40);
// アイコン 8 人 ＋ ゲスト（Material Icons Round）。AI DUEL と同じフォント・同じ字
LV_FONT_DECLARE(ct_font_icons_54);

namespace cards {
namespace {

namespace ct = coffee::cards;
namespace core = cafe_cards;
using ui::Rect;

// ---------------------------------------------------------------------------
// 色（設計一式 data/theme.json の table_lounge。ほかのゲームの色は変えない）
// ---------------------------------------------------------------------------
#define CD_BACKDROP  lv_color_hex(0x0D1713)
#define CD_WALNUT_L  lv_color_hex(0x513725)
#define CD_BRASS     lv_color_hex(0xC6A66A)
#define CD_FELT      lv_color_hex(0x123B30)
#define CD_FELT_DARK lv_color_hex(0x0C2A22)
#define CD_TEXT      lv_color_hex(0xF7EFDF)
#define CD_MUTED     lv_color_hex(0xD8CEB9)
#define CD_PAPER     lv_color_hex(0xF7EFE2)
#define CD_INK       lv_color_hex(0x17261E)
#define CD_RED       lv_color_hex(0xA32638)
#define CD_BUTTON    lv_color_hex(0x183D31)
#define CD_BUTTON_HI lv_color_hex(0x23513F)
#define CD_GOLD      lv_color_hex(0xE0BD78)
#define CD_GOLD_INK  lv_color_hex(0x241C10)
#define CD_DANGER    lv_color_hex(0x762537)
#define CD_DISABLED  lv_color_hex(0x2D3D34)
#define CD_DIS_TEXT  lv_color_hex(0xC1C8BD)

// ---------------------------------------------------------------------------
// 画面の配置
//
// 数値は設計一式 v1.1 の data/screens.json（docs/02A_ALL_COORDINATES.md）をそのまま使い、
// この作業台に無い画面（本人選び・種類選び・相手選び・きろく）だけ足した。
// 四隅すべてが中心 (240,240)・半径 228px の円の内側に入ることを座標ごとに確かめてある。
// ---------------------------------------------------------------------------
namespace layout {

constexpr Rect kTitle   {128,  42, 224, 28};    // 設計一式 02A の共通見出し
constexpr Rect kMeta    {100,  74, 280, 24};
constexpr Rect kCafeBtn {176, 414, 128, 44};    // 卓の画面の「カフェへ」

// --- 入口（lobby）------------------------------------------------------------
// 「説明をもう一度」を足したぶん、4 つの卓を少し上げて縮めた
constexpr Rect kTile[4] = {{100, 100, 132, 88}, {248, 100, 132, 88},
                           {100, 194, 132, 88}, {248, 194, 132, 88}};
constexpr Rect kEntryResume{108, 290, 124, 44};
constexpr Rect kEntryHow   {248, 290, 124, 44};
constexpr Rect kEntryAgain {130, 340, 220, 40};
constexpr Rect kEntryNote  { 98, 384, 284, 20};
constexpr Rect kEntryBack  {176, 410, 128, 44};

// --- 本人選び（AI DUEL と同じ位置）-------------------------------------------
constexpr Rect kPlayerNote {104,  66, 272, 24};   // 四隅が半径 228px の円に入るよう DUEL より少し狭い
constexpr Rect kAvatar[8]  = {{ 73, 102, 78, 88}, {158, 102, 78, 88},
                              {244, 102, 78, 88}, {329, 102, 78, 88},
                              { 73, 198, 78, 88}, {158, 198, 78, 88},
                              {244, 198, 78, 88}, {329, 198, 78, 88}};
constexpr Rect kGuestBtn   {140, 296, 200, 46};
constexpr Rect kPlayerBack {150, 350, 180, 44};

// --- きろく（プロフィール）----------------------------------------------------
constexpr Rect kRecIcon  {200,  52,  80, 64};
constexpr Rect kRecName  {140, 118, 200, 26};
constexpr Rect kRecRow[4] = {{84, 150, 312, 26}, {84, 178, 312, 26},
                             {84, 206, 312, 26}, {84, 234, 312, 26}};
constexpr Rect kRecNote  { 84, 262, 312, 24};
constexpr Rect kRecPlay  {130, 292, 220, 52};
constexpr Rect kRecBack  {150, 352, 180, 46};

// --- 種類選び / 相手選び ------------------------------------------------------
constexpr Rect kPickBtn[2] = {{100, 124, 280, 74}, {100, 210, 280, 74}};
constexpr Rect kPickNote   { 84, 294, 312, 52};
constexpr Rect kPickBack   {150, 356, 180, 46};
constexpr Rect kPickHow    {108, 356, 124, 46};     // 相手選びだけ 2 ボタン
constexpr Rect kPickGo     {248, 356, 124, 46};

// --- 遊び方 / はじめての説明 --------------------------------------------------
// 「この卓の流れ」は 5 行あるので本文を 6 行ぶん（156px）取ってある
constexpr Rect kTutTitle { 88, 110, 304, 34};
constexpr Rect kTutBody  { 84, 152, 312, 156};
constexpr Rect kTutNote  { 90, 312, 300, 38};   // 最後のページは「次回から表示しない」
constexpr Rect kTutPrev  {108, 356, 124, 46};
constexpr Rect kTutNext  {248, 356, 124, 46};
constexpr Rect kTutBack  {176, 406, 128, 42};   // スキップ / 閉じる

// --- 共通の操作行（設計一式 02A）---------------------------------------------
constexpr Rect kAct2[2] = {{108, 350, 124, 52}, {248, 350, 124, 52}};
constexpr Rect kAct3[3] = {{ 88, 350,  96, 52}, {192, 350, 96, 52}, {296, 350, 96, 52}};

// --- POKER -------------------------------------------------------------------
// 設計一式は 150,110,180,22 だが、交換後は「交換 n 枚」も同じ行に出す（設計書 P4 の 5）。
// 180px には入らないので、上の帯が空いているぶんだけ横に広げた（四隅は円の内側 200px）
constexpr Rect kPokOpp     { 88, 110, 304, 22};
constexpr Rect kPokOppCard[5] = {{169, 132, 26, 34}, {198, 132, 26, 34}, {227, 132, 26, 34},
                                 {256, 132, 26, 34}, {285, 132, 26, 34}};
// 同じく、交換後は双方の交換枚数をここに出すので広げてある
constexpr Rect kPokPotLabel{ 88, 173, 304, 20};
constexpr Rect kPokPot     {150, 194, 180, 36};
constexpr Rect kPokHuman   { 80, 232, 320, 22};
constexpr Rect kPokHand[5] = {{72, 262, 64, 76}, {140, 262, 64, 76}, {208, 262, 64, 76},
                              {276, 262, 64, 76}, {344, 262, 64, 76}};
// ショーダウン
constexpr Rect kPokRole    {116, 108, 248, 22};
constexpr Rect kPokShowOpp[5] = {{96, 138, 52, 64}, {155, 138, 52, 64}, {214, 138, 52, 64},
                                 {273, 138, 52, 64}, {332, 138, 52, 64}};
constexpr Rect kPokResult  { 88, 211, 304, 32};
constexpr Rect kPokDetail  {100, 246, 280, 24};
constexpr Rect kPokShowYou[5] = {{96, 276, 52, 62}, {155, 276, 52, 62}, {214, 276, 52, 62},
                                 {273, 276, 52, 62}, {332, 276, 52, 62}};
// フォールドで終わったハンド（札は出さない）
constexpr Rect kFoldResult {88, 145, 304, 36};
constexpr Rect kFoldDetail {100, 192, 280, 28};
constexpr Rect kFoldPrivacy{ 90, 248, 300, 28};
constexpr Rect kFoldNoProb { 90, 280, 300, 26};

// --- HOLDEM（計画 §5b。上 = 相手の 2 枚、中 = 場の 5 枚、下 = 自分の 2 枚）----
constexpr Rect kHdOpp      { 88, 102, 304, 22};
constexpr Rect kHdOppCard[2] = {{212, 128, 26, 34}, {242, 128, 26, 34}};
constexpr Rect kHdHead     { 88, 166, 304, 20};     // 段階名・単位・POT
constexpr Rect kHdBoard[5] = {{ 96, 190, 52, 62}, {155, 190, 52, 62}, {214, 190, 52, 62},
                              {273, 190, 52, 62}, {332, 190, 52, 62}};
constexpr Rect kHdHuman    { 80, 254, 320, 22};
constexpr Rect kHdHand[2]  = {{176, 278, 60, 68}, {244, 278, 60, 68}};
// ショーダウン: 場を上に、双方の 2 枚を左右に並べる（4 段は丸い画面に入らない）
constexpr Rect kHdShowHead { 88, 100, 304, 18};
constexpr Rect kHdShowBoard[5] = {{102, 120, 48, 58}, {159, 120, 48, 58}, {216, 120, 48, 58},
                                  {273, 120, 48, 58}, {330, 120, 48, 58}};
// 名前と役は 2 行（「スリーカード」まで入れると 1 行では横に入らない）
constexpr Rect kHdOppTag   { 44, 182, 196, 52};
constexpr Rect kHdYouTag   {240, 182, 196, 52};
constexpr Rect kHdOppShow[2] = {{ 62, 238, 48, 58}, {114, 238, 48, 58}};
constexpr Rect kHdYouShow[2] = {{318, 238, 48, 58}, {370, 238, 48, 58}};
constexpr Rect kHdResult   { 88, 298, 304, 28};

// --- GOPS --------------------------------------------------------------------
constexpr Rect kGopsOpp   { 98, 105, 284, 24};
// はじめての人むけの一言をここに足すので、設計一式の 142,132,196,22 より横に広い
constexpr Rect kGopsPrizeL{ 88, 132, 304, 22};
constexpr Rect kGopsYou   { 66, 157,  94, 62};
constexpr Rect kGopsAi    {320, 157,  94, 62};
constexpr Rect kGopsPrize {204, 158,  72, 62};
constexpr Rect kGopsInfo  {104, 226, 272, 22};
constexpr Rect kGopsBid[4] = {{96, 254, 68, 82}, {172, 254, 68, 82},
                              {248, 254, 68, 82}, {324, 254, 68, 82}};
constexpr Rect kGopsPrev  { 40, 268, 44, 52};
constexpr Rect kGopsNext  {400, 268, 44, 52};
// ラウンドの結果
constexpr Rect kGopsResult{ 92, 118, 296, 32};
constexpr Rect kGopsBurn  { 92, 158, 296, 28};
constexpr Rect kGopsYouTag{ 90, 202, 136, 26};
constexpr Rect kGopsAiTag {254, 202, 136, 26};
constexpr Rect kGopsYouBid{128, 238,  66, 88};
constexpr Rect kGopsAiBid {286, 238,  66, 88};
// 残り札
constexpr Rect kRemYouT   {102, 118, 276, 26};
constexpr Rect kRemYou    { 80, 158, 320, 52};
constexpr Rect kRemAiT    {102, 226, 276, 26};
constexpr Rect kRemAi     { 80, 264, 320, 52};
constexpr Rect kRemBack   {160, 348, 160, 52};

// --- THIRTY-ONE --------------------------------------------------------------
constexpr Rect kT31Opp    {112, 100, 256, 24};
// はじめての人むけの一言をここに足すので、設計一式の 108,128,264,22 より横に広い
constexpr Rect kT31MarketT{ 88, 128, 304, 22};
constexpr Rect kT31Market[3] = {{124, 160, 64, 70}, {208, 160, 64, 70}, {292, 160, 64, 70}};
constexpr Rect kT31Human  { 80, 238, 320, 24};
constexpr Rect kT31Hand[3] = {{124, 268, 64, 70}, {208, 268, 64, 70}, {292, 268, 64, 70}};
constexpr Rect kT31OppScore{116, 106, 248, 26};
constexpr Rect kT31OppCard[3] = {{124, 140, 64, 70}, {208, 140, 64, 70}, {292, 140, 64, 70}};
// 得点と勝敗で 1 行、この試合の勝ち数で 1 行（ct_font_jp_20 は 1 行 26px なので 52px 取る）。
// 上は相手の札の下端 210、下は自分の札の上端 268 の内側
constexpr Rect kT31Result {92, 212, 296, 52};

// --- BACCARAT ----------------------------------------------------------------
constexpr Rect kBacPlayerL{ 90, 105, 300, 26};
constexpr Rect kBacPlayer[3] = {{124, 140, 64, 70}, {208, 140, 64, 70}, {292, 140, 64, 70}};
constexpr Rect kBacBankerL{ 90, 224, 300, 26};
constexpr Rect kBacBanker[3] = {{124, 260, 64, 70}, {208, 260, 64, 70}, {292, 260, 64, 70}};
constexpr Rect kBacNote   { 88, 326, 304, 22};

// --- 確認シート（設計一式 02 §6）---------------------------------------------
constexpr Rect kAskTitle {108, 138, 264, 32};
constexpr Rect kAskBody  { 98, 184, 284, 134};

// --- 行動の内訳 --------------------------------------------------------------
constexpr Rect kDetMean  { 90, 116, 300, 32};
constexpr Rect kDetValues{104, 162, 272, 112};
constexpr Rect kDetNote  { 88, 288, 304, 28};
constexpr Rect kDetBack  {160, 344, 160, 52};

// --- 通信の状態（JEV REVERSI と同じ並び）--------------------------------------
constexpr Rect kNetBody  { 84,  92, 312, 92};
constexpr Rect kNetBtn[3]= {{110, 196, 260, 54}, {110, 260, 260, 54}, {110, 324, 260, 54}};

// --- 試合の結果 --------------------------------------------------------------
constexpr Rect kResTitle { 90, 126, 300, 40};
constexpr Rect kResGame  { 86, 176, 308, 28};
constexpr Rect kResScore { 92, 208, 296, 60};
constexpr Rect kResProv  { 92, 272, 296, 24};
constexpr Rect kResLeft  {108, 304, 124, 50};
constexpr Rect kResRight {248, 304, 124, 50};
constexpr Rect kResBack  {160, 366, 160, 46};

// --- カフェ / ひと休み（ほかのゲームと同じ並び）--------------------------------
constexpr Rect kPanelBody{ 94, 138, 292, 58};
constexpr Rect kPanel[3] = {{110, 214, 260, 54}, {110, 278, 260, 54}, {110, 342, 260, 54}};

}  // namespace layout

// ---------------------------------------------------------------------------
// 画面と操作の種類
// ---------------------------------------------------------------------------
enum class View : uint8_t {
    Entry,      // 4 つのテーブル・つづきから・遊び方
    Player,     // アイコン 8 人 ＋ ゲスト
    Records,    // 選んだ人のきろく（ここから始める）
    Variant,    // GOPS 7/13・BACCARAT OPEN/CLASSIC
    Opponent,   // JEV / 端末AI
    Tutorial,   // 遊び方（tutorials.ja.json の 22 ページ）
    Table,      // 4 つの卓（局面ごとに中身が変わる）＋ ハンド / 試合の結果
    Confirm,    // 確認シート
    Detail,     // Jev の判断の内訳
    Remaining,  // GOPS の公開済みの残り札
    Network,    // 通信の状態
    Paused,     // 無操作でひと休み
    Cafe,       // 共通カフェパネル
};

// 卓の操作ボタンの役目
enum class Role : uint8_t {
    None, ActionId, DraftPoker, DraftGops, DraftThirty, ClearSel,
    Remaining, Detail, Next, Rematch, Records, Leave, Cafe, PagePrev, PageNext,
};

enum class Act : int {
    EntryTable0 = 1, EntryTable1, EntryTable2, EntryTable3,
    EntryResume, EntryHow, EntryAgain, EntryBack,
    TutToggle,
    PlayerGuest, PlayerBack,
    RecPlay, RecBack,
    Variant0, Variant1, VariantBack,
    OppJev, OppLocal, OppBack,
    TutPrev, TutNext, TutBack,
    AskYes, AskNo,
    DetailBack, RemainBack,
    NetRetry, NetLocal, NetCafe,
    PausedResume, PausedQuit,
    CafeCoffee, CafeBack, CafeHome,
};

enum class AskKind : uint8_t { Action, GoLocal, NewMatch };
enum class Step : uint8_t { Idle, Think, Wait };

constexpr uint32_t kIdlePauseMs = 120 * 1000;   // 無操作 120 秒でひと休み（rules.json）
constexpr uint32_t kJevWaitMs = 12000;          // ここで見切る（計画 §2）
constexpr uint8_t kJevAttempts = 2;             // 1 回だけ黙って送り直す
constexpr uint32_t kSendBusyMs = 8000;
constexpr uint32_t kInputLatchMs = 250;
constexpr uint32_t kThinkMs = 320;              // 端末 AI の「考えています」の見せ場
constexpr uint8_t kDetailRows = 6;              // 内訳に並べる候補の数

// ---------------------------------------------------------------------------
// アイコン 8 人 ＋ ゲスト（AI DUEL の DuelGame.cpp からそのまま）
// ---------------------------------------------------------------------------
struct Avatar {
    const char *glyph;
    const char *name;
};
constexpr Avatar kAvatars[cards::store::kSlots] = {
    {"\xEE\xBF\xAF", "カップ"},     // U+EFEF coffee
    {"\xEE\x8F\xAA", "まめ"},        // U+E3EA grain
    {"\xEE\xA0\xB8", "ほし"},        // U+E838 star
    {"\xEE\x94\x9C", "つき"},        // U+E51C dark_mode
    {"\xEE\x90\xB0", "たいよう"},    // U+E430 wb_sunny
    {"\xEE\xA8\xB5", "はっぱ"},      // U+EA35 eco
    {"\xEE\x8A\xBD", "くも"},        // U+E2BD cloud
    {"\xEE\xA2\xB8", "はぐるま"},    // U+E8B8 settings
};
constexpr const char *kGuestGlyph = "\xEE\x9F\xBD";     // U+E7FD person

// ---------------------------------------------------------------------------
// 文言（設計一式 data/content.ja.json・dialogs.ja.json・tutorials.ja.json を正本に、
// 端末の幅（全角 15 文字）で改行を入れてある。意味は変えていない）
// ---------------------------------------------------------------------------
constexpr const char *kTableName[4] = {"POKER", "GOPS", "THIRTY-ONE", "BACCARAT"};
constexpr const char *kTableCatch[4] = {"POKER\n強気を、読む", "GOPS\n切り札を読む",
                                        "THIRTY-ONE\n引き際が勝負", "BACCARAT\n予想が分かれる"};
constexpr const char *kTableTitle[4] = {"CAFE POKER", "GOPS", "THIRTY-ONE", "BACCARAT"};

// 役の名前（設計書 P5 の序列と同じ並び）。**いちばん狭い枠は 248px = 全角 12 文字**
// なので、ストレートフラッシュだけは通り名の「ストフラ」にしてある
constexpr const char *kHandName[9] = {"ハイカード", "ワンペア", "ツーペア", "スリーカード",
                                      "ストレート", "フラッシュ", "フルハウス", "フォーカード",
                                      "ストフラ"};
// スートは形で見分けられるようにする（色だけに頼らない。設計一式 01 §6）。
// この 4 字はゲーム用フォントに足してある（U+2663 / 2666 / 2665 / 2660）
constexpr const char *kSuitGlyph[4] = {"♣", "♦", "♥", "♠"};   // C D H S

struct TutorialPage {
    const char *title;
    const char *body;
};
constexpr TutorialPage kTutorial[22] = {
    {"5枚で役を作ろう", "手札5枚を受け取ります。強い役\nを作るか、相手を降ろすと勝ちで\nす。"},
    {"押しただけでは確定しません", "札や行動を選んでから「これで決\n定」。迷ったら選び直せます。"},
    {"上乗せは決まった額", "交換前は2点ずつ、交換後は4点ず\nつ。上乗せは各段階2回までです。"},
    {"交換は1回だけ", "0〜5枚を選びます。相手も同時に\n決め、双方が確定してから交換し\nます。"},
    {"Aは二つの使い方", "A・2・3・4・5は5が上のストレー\nト。Q・K・A・2・3はつながりま\nせん。"},
    {"持ち点で決着", "100点ずつで始め、5回終わったと\nきに多い側が勝ち。点数に金銭価\n値はありません。"},
    {"同じ札でスタート", "双方に1〜7、または1〜13の札。\n使った札は戻りません。"},
    {"得点を取り合う", "場に出た得点を見て、自分の札を\n一つ選びます。相手の今回の札は\n秘密です。"},
    {"同時に開く", "大きな札を出した側が場の得点を\n獲得。同じ数字なら、両方の札と\n得点を使い切ります。"},
    {"最後まで考えよう", "小さな得点に大きな札を使うと、\n後で困るかもしれません。最後は\n合計点で勝負。"},
    {"同じマークを集める", "手札は3枚。4種類のマークごとに\n足し、一番高い合計があなたの点\n数です。"},
    {"Aは11、絵札は10", "Aは11点、J・Q・Kは10点。同じ数\n字3枚の特別点はありません。"},
    {"場と1枚ずつ交換", "自分の1枚と場の1枚を選んで\n交換。場へ出した札は相手にも\n見えます。"},
    {"勝負を仕掛ける", "交換の代わりにノックすると、相\n手に最後の交換チャンスが1回あ\nります。"},
    {"31なら即決着", "31点になったらすぐに公開。同点\nは引き分け。長く続いたら20手で\n比べます。"},
    {"この端末だけの短期戦", "通常の流派とは一部違う\nカフェルールです。\n3回の勝ち数で決めます。"},
    {"二つの側を予想", "PLAYER側とBANKER側は手札の呼び\n名。人間とJevの呼び名ではあり\nません。"},
    {"9点に近い方が勝ち", "Aは1点、10・J・Q・Kは0点。足し\nた数字の一の位だけを比べます。"},
    {"引くかどうかは自動", "追加カードは決まったルールで配\nられます。人間もJevも引き方を\n変更できません。"},
    {"1枚ずつ見て予想", "OPENでは各側の最初の1枚が見え\nます。残りのカードは、人間にも\nJevにも分かりません。"},
    {"当たれば1点", "引き分けを当てても1点です。5回\nの的中数を比べましょう。"},
    {"未来は分かりません", "毎回新しい山札を使うため、前の\n勝敗の並びで次が決まるわけでは\nありません。"},
};
constexpr uint8_t kTutStart[4] = {0, 6, 10, 16};
constexpr uint8_t kTutCount[4] = {6, 4, 6, 6};

// はじめての人むけの「この卓の流れ」。何が見える / 何を決める / どう勝つ /
// いちばん間違えやすいこと、を 5 行で。1 行は全角 15 文字（312px）まで
constexpr TutorialPage kSummary[4] = {
    {"この卓の流れ",
     "手札5枚だけが見えます。\n出す・受ける・降りるを選ぶ。\n交換は1回だけ、0〜5枚。\n"
     "上乗せは各段階2回まで。\n5ハンド後の持ち点で決着。"},
    {"この卓の流れ",
     "同じ札を1組ずつ持ちます。\n場の得点札を見て1枚を選ぶ。\n大きい札を出した側が得点。\n"
     "同じ数字なら得点は消えます。\n合計点が多い側の勝ちです。"},
    {"この卓の流れ",
     "手札3枚と場の3枚を見ます。\n自分の1枚と場の1枚を交換。\n同じマークの合計が点数です。\n"
     "通常ターンにパスはありません。\nここで勝負＝相手に最後の1手。"},
    {"この卓の流れ",
     "PLAYERとBANKERは手札の呼び名。\nあなたもJevも同じ側を予想。\n追加の札は規則で自動です。\n"
     "当たれば1点、5回で比べます。\n前の結果は次に影響しません。"},
};

// ホールデムは原本のチュートリアルに無いので、ここで書く（計画 §5b）
constexpr TutorialPage kHoldemSummary =
    {"この卓の流れ",
     "自分の2枚だけが見えます。\n場の5枚は共有。合わせて\n最良の5枚が役です。\n"
     "上乗せは各段階2回まで。\n5ハンド後の持ち点で決着。"};

constexpr TutorialPage kHoldemTut[6] = {
    {"手札2枚と場の5枚", "配られるのは2枚だけ。場の5枚\nは共有で、合わせた7枚から\n"
                         "最良の5枚を選びます。"},
    {"押しただけでは確定しません", "行動を選んでから「決定」。\n迷ったら選び直せます。"},
    {"4回のベット", "プリフロップ・フロップ・\nターン・リバーの4回。\n"
                    "先手は毎回ディーラーでない側。"},
    {"上乗せは決まった額", "最初の2回は2点ずつ、ターンと\nリバーは4点ずつ。上乗せは\n"
                           "各段階2回までです。"},
    {"Aは二つの使い方", "A・2・3・4・5は5が上のストレー\nト。Q・K・A・2・3はつながりま\nせん。"},
    {"持ち点で決着", "200点ずつで始め、5回終わった\nときに多い側が勝ち。点数に\n"
                    "金銭価値はありません。"},
};

// POKER 卓はホールデムとドローで説明が別。そのほかの卓は 1 種類
bool s_tut_holdem = true;

int guideTablePages(int game)
{
    if (game == 0) {
        return s_tut_holdem ? (int)(1 + (sizeof(kHoldemTut) / sizeof(kHoldemTut[0])))
                            : (int)(1 + kTutCount[0]);
    }
    return 1 + kTutCount[game];
}

// 説明のページ数（卓ごと: 流れ 1 ＋ その卓のページ。game < 0 は 4 卓ぶん）
int guideCount(int game)
{
    int n = 0;
    for (int g = (game < 0 ? 0 : game); g <= (game < 0 ? 3 : game); ++g) {
        n += guideTablePages(g);
    }
    return n;
}

// 通し番号からページを取り出す（何番目の卓のページかも返す）
bool guidePage(int game, int index, const char *&title, const char *&body, int &of_game)
{
    for (int g = (game < 0 ? 0 : game); g <= (game < 0 ? 3 : game); ++g) {
        const bool holdem = (g == 0 && s_tut_holdem);
        const int pages = guideTablePages(g);
        if (index == 0) {
            const TutorialPage &p = holdem ? kHoldemSummary : kSummary[g];
            title = p.title;
            body = p.body;
            of_game = g;
            return true;
        }
        --index;
        if (index < pages - 1) {
            const TutorialPage &p = holdem ? kHoldemTut[index]
                                           : kTutorial[kTutStart[g] + index];
            title = p.title;
            body = p.body;
            of_game = g;
            return true;
        }
        index -= pages - 1;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 電源が入っている間ずっと持つ試合（計画 §4。PSRAM に 1 個だけ）
// **手札は NVS・SD・シリアルに書かない。** 電源が切れたら消える
// ---------------------------------------------------------------------------
struct Session {
    ct::Match match;

    bool in_match = false;          // 途中の試合があるか（「つづきから」の対象）
    bool counted = false;           // きろくに数えたか
    uint8_t slot = cards::store::kGuestSlot;
    uint8_t game = 0;
    uint8_t variant = 0;
    bool want_jev = false;          // 相手として JEV を選んだ
    bool local_only = false;        // 端末AIへ切替済み（もう Jev に戻らない）
    bool used_jev = false;          // Jev の返事を 1 回でも使った
    char match_id[33] = {0};

    // この決定のために 1 回だけ引いたくじ（設計書 I6）
    ct::SavedRoll roll;
    bool roll_drawn = false;

    // 直前の Jev の判断（内訳の画面用）
    bool jev_valid = false;
    bool jev_cached = false;
    bool jev_hidden = false;        // フォールドで終わったハンドは出さない（設計書 P6）
    float jev_conf = 0;
    uint8_t jev_unit = 0;
    char jev_action[ct::kActionIdMax] = {0};
    uint8_t jev_count = 0;
    uint8_t jev_total = 0;
    char jev_names[kDetailRows][ct::kActionIdMax] = {};
    uint8_t jev_percent[kDetailRows] = {};

    // 通信の作業領域（内蔵メモリを使わないよう、ここに置く）
    net::GasResult reply;
    char obs[ct::kObservationMax] = {};
    char legal[512] = {};
    char body[net::kGasRequestMax] = {};
};

Session *s_sess = nullptr;          // **画面を閉じても手放さない**（同じ電源セッション内で再開）

// ---------------------------------------------------------------------------
// 画面の状態（画面を閉じたら白紙に戻す）
// ---------------------------------------------------------------------------
lv_obj_t *s_screen = nullptr;
lv_obj_t *s_content = nullptr;
lv_timer_t *s_tick = nullptr;

View s_view = View::Entry;
View s_cafe_return = View::Entry;
View s_paused_return = View::Table;
View s_ask_return = View::Table;
View s_tut_return = View::Entry;
View s_records_return = View::Player;
Step s_step = Step::Idle;

bool s_dirty = true;
uint32_t s_view_ms = 0;
uint32_t s_step_ms = 0;

uint8_t s_pick_game = 0;            // これから始めるテーブル
uint8_t s_pick_variant = 0;

// 遊び方 / はじめての説明
uint8_t s_tut_page = 0;             // 通し番号（0 から）
int8_t s_tut_game = -1;             // -1 = 4 卓ぶん、0〜3 = その卓だけ
bool s_tut_first_play = false;      // 初回の自動表示（スキップ・次回から表示しない つき）
bool s_tut_dont_show = true;        // 「次回から表示しない」の既定はオン
uint8_t s_guided = 0;               // この電源セッションで説明を出した卓（最初の 1 局だけ一言を出す）
uint8_t s_page = 0;                 // GOPS の候補のページ

// 人間の仮選択（draft）。確定ボタンを押すまでゲームは 1 ミリも動かない
int s_draft_mask = 0;               // ポーカーの交換
int s_draft_bid = -1;               // GOPS の札
int s_draft_hand = -1, s_draft_market = -1;   // 31 の交換

// 確認シート
AskKind s_ask_kind = AskKind::Action;
char s_ask_id[ct::kActionIdMax] = {0};
uint32_t s_ask_rev = 0;             // **同じ revision は二度と確定できない**
bool s_busy = false;                // 確定を受け付けてから画面を作り直すまで

// 通信
uint32_t s_req_no = 0;
uint32_t s_req_ms = 0;
uint32_t s_req_rev = 0;
uint8_t s_attempt = 0;
bool s_send_pending = false;
uint32_t s_send_since_ms = 0;
char s_req_reason[40] = {0};
char s_result_json[240] = {0};      // 送りっぱなしの結果（空なら送るものがない）
bool s_result_pending = false;
uint32_t s_result_since_ms = 0;

// 卓のボタン（役目と、確定する行動 ID）
struct TableBtn {
    Role role = Role::None;
    char id[ct::kActionIdMax] = {0};
};
TableBtn s_btn[6];

// 私的な表示（手札）。カフェ・HOME・ひと休みへ移る前に必ず消す（設計書 I5）
constexpr size_t kPrivateMax = 16;
lv_obj_t *s_private[kPrivateMax] = {};
uint8_t s_private_count = 0;

// ポーカーの交換の仮選択は、画面ごと作り直さずにその場で描き替える
// （タップしたコールバックの中で lv_obj_clean はできない。作り直しを待つと 1 目盛り遅れる）
lv_obj_t *s_draw_card[5] = {};
lv_obj_t *s_draw_mark[5] = {};
lv_obj_t *s_draw_count = nullptr;      // 「n 枚」
lv_obj_t *s_draw_confirm = nullptr;    // 「交換する」/「交換しない」のラベル

void forgetDrawWidgets()
{
    for (int i = 0; i < 5; ++i) {
        s_draw_card[i] = nullptr;
        s_draw_mark[i] = nullptr;
    }
    s_draw_count = nullptr;
    s_draw_confirm = nullptr;
}

// ---------------------------------------------------------------------------
// 小道具
// ---------------------------------------------------------------------------
uint32_t hwRandom(void *)
{
    return esp_random();
}

ct::Match &match()
{
    return s_sess->match;
}

bool isGuest()
{
    return s_sess->slot >= cards::store::kGuestSlot;
}

// 「つづきから」の対象になる試合（終わった試合は対象外）
bool resumable()
{
    return s_sess != nullptr && s_sess->in_match && !s_sess->match.finished;
}

// 説明の枠（卓ごと。POKER だけホールデムとドローで別）
size_t seenSlot(int game, int variant)
{
    return (game == 0 && variant == 1) ? cards::store::kSeenPokerDraw : (size_t)game;
}

// 相手の返事を待っているあいだの一言（卓ごと。ui::Thinking が 1.5 秒で入れ替える）
constexpr const char *const kThinkPoker[3] = {
    "手札を読んでいます…", "場を読んでいます…", "賭け方を決めています…",
};
constexpr const char *const kThinkGops[2] = {
    "札の価値をくらべています…", "今回の得点札を見ています…",
};
constexpr const char *const kThinkThirty[2] = {
    "場の札をくらべています…", "交換かノックか考えています…",
};
constexpr const char *const kThinkBaccarat[1] = {
    "見えている札から考えます…",
};

// Jev の返事を待っている（pending=1）あいだだけ、止まった字ではなく動く表示にする
bool waitingForJev()
{
    return s_step == Step::Wait;
}

// 卓の上に出す「はじめての人むけの一言」。その卓の最初のハンド / ラウンドだけ。
// もう説明を見た（印が立っている）卓では出さないが、その場で説明を見たばかりの
// 卓では最初の 1 局だけ出す（説明の最後に印を立てるので、印だけでは判定できない）
bool showHint()
{
    if (s_sess == nullptr || !s_sess->in_match || s_sess->match.finished) {
        return false;
    }
    const size_t slot = seenSlot((int)s_sess->game, (int)s_sess->variant);
    const bool first_time = !cards::store::seenFlag(slot) || (s_guided & (1u << slot)) != 0;
    return first_time && ct::unitNo(s_sess->match) == 1;
}

const char *playerName()
{
    return isGuest() ? "ゲスト" : kAvatars[s_sess->slot].name;
}

const char *playerGlyph()
{
    return isGuest() ? kGuestGlyph : kAvatars[s_sess->slot].glyph;
}

// 相手の呼び名。**Jev が動いていないのに JEV と書かない**（設計書 I7）
const char *opponentName()
{
    if (!s_sess->want_jev) {
        return "端末AI";
    }
    return s_sess->local_only ? "Jev＋端末AI" : "JEV";
}

// 狭い枠（相手の行・GOPS の得点札）むけの短い呼び名。最長 8 単位 = 80px
const char *shortOpponentName()
{
    if (!s_sess->want_jev) {
        return "端末AI";
    }
    return s_sess->local_only ? "JEV/端末" : "JEV";
}

uint16_t lineUnits(const char *begin, const char *end)
{
    uint16_t cells = 0;
    for (const char *p = begin; p < end; ++p) {
        if (((unsigned char)*p & 0xC0) == 0x80) {
            continue;
        }
        cells = (uint16_t)(cells + (((unsigned char)*p < 0x80) ? 1 : 2));
    }
    return cells;
}

uint16_t widestUnits(const char *t)
{
    uint16_t widest = 0;
    const char *line = t;
    for (const char *p = t;; ++p) {
        if (*p == '\n' || *p == '\0') {
            const uint16_t u = lineUnits(line, p);
            if (u > widest) {
                widest = u;
            }
            line = p + 1;
            if (*p == '\0') {
                break;
            }
        }
    }
    return widest;
}

uint16_t lineCount(const char *t)
{
    uint16_t n = 1;
    for (const char *p = t; *p != '\0'; ++p) {
        if (*p == '\n') {
            ++n;
        }
    }
    return n;
}

const lv_font_t *fitFont(const char *t, int16_t width)
{
    return (int16_t)(widestUnits(t) * 11) <= width ? &ct_font_jp_22 : &ct_font_jp_20;
}

lv_obj_t *rectLabel(const Rect &r, const lv_font_t *font, lv_color_t color, const char *t)
{
    lv_obj_t *l = lv_label_create(s_content);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    // 日本語は自動折返しが効かない。改行は文言側に入れてあるので幅で切る
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, r.w);
    lv_label_set_text(l, t);
    const uint16_t lines = lineCount(t);
    const int16_t h = (int16_t)(font->line_height * lines);
    if (h > r.h && lines > 1) {
        lv_obj_set_height(l, r.h);
        lv_obj_set_pos(l, r.x, r.y);
    } else {
        lv_obj_set_pos(l, r.x, (int16_t)(r.h > h ? r.y + (r.h - h) / 2 : r.y));
    }
    return l;
}

void actionCb(lv_event_t *e);
void tableBtnCb(lv_event_t *e);
void avatarCb(lv_event_t *e);
void pokerCardCb(lv_event_t *e);
void gopsCardCb(lv_event_t *e);
void t31HandCb(lv_event_t *e);
void t31MarketCb(lv_event_t *e);

lv_obj_t *makeButton(const Rect &r, const char *t, lv_event_cb_t cb, void *user_data,
                     bool enabled, bool primary, const lv_font_t *font = nullptr)
{
    lv_obj_t *btn = lv_btn_create(s_content);
    lv_obj_set_pos(btn, r.x, r.y);
    lv_obj_set_size(btn, r.w, r.h);
    lv_obj_set_style_radius(btn, r.h / 2 > 16 ? 16 : r.h / 2, 0);
    lv_obj_set_style_bg_color(btn, !enabled ? CD_DISABLED : (primary ? CD_GOLD : CD_BUTTON), 0);
    lv_obj_set_style_bg_color(btn, CD_BUTTON_HI, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, enabled ? CD_BRASS : CD_DISABLED, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    // 既定の内側余白が大きく、座標での配置が効かなくなるので 0 にする
    lv_obj_set_style_pad_all(btn, 0, 0);

    if (t != nullptr && t[0] != '\0') {
        lv_obj_t *l = lv_label_create(btn);
        lv_obj_set_style_text_font(l, font != nullptr ? font : fitFont(t, (int16_t)(r.w - 8)), 0);
        lv_obj_set_style_text_color(l, !enabled ? CD_DIS_TEXT : (primary ? CD_GOLD_INK : CD_TEXT), 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(l, (int16_t)(r.w - 6));
        lv_label_set_text(l, t);
        lv_obj_center(l);
    }
    if (enabled) {
        // 確定は LV_EVENT_CLICKED の 1 回だけ（設計一式 03 §4）
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    } else {
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    }
    return btn;
}

lv_obj_t *rectButton(const Rect &r, const char *t, Act act, bool enabled = true,
                     bool primary = false)
{
    return makeButton(r, t, actionCb, (void *)(intptr_t)act, enabled, primary);
}

// 卓の操作ボタン（役目つき）
lv_obj_t *tableButton(uint8_t index, const Rect &r, const char *t, Role role, const char *id,
                      bool enabled, bool primary)
{
    if (index >= 6) {
        return nullptr;
    }
    s_btn[index].role = role;
    std::snprintf(s_btn[index].id, sizeof(s_btn[index].id), "%s", id != nullptr ? id : "");
    return makeButton(r, t, tableBtnCb, (void *)(intptr_t)index, enabled, primary);
}

// ボタンの中の文字ラベル（makeButton は文言があるとき 1 つだけ作る）
lv_obj_t *buttonLabel(lv_obj_t *btn)
{
    if (btn == nullptr || lv_obj_get_child_cnt(btn) == 0) {
        return nullptr;
    }
    lv_obj_t *child = lv_obj_get_child(btn, 0);
    return lv_obj_check_type(child, &lv_label_class) ? child : nullptr;
}

void makeTitle(const char *t)
{
    rectLabel(layout::kTitle, &ct_font_jp_22, CD_GOLD, t);
}

void makeMeta(const char *t)
{
    rectLabel(layout::kMeta, &ct_font_jp_20, CD_MUTED, t);
}

// 卓の共通枠（TableFrame。木のレール・金線・フェルト。設計一式 01 §3）
void buildFrame()
{
    struct Layer { Rect r; int radius; lv_color_t color; bool filled; };
    static const Layer kLayers[3] = {
        {{26, 98, 428, 296}, 148, CD_WALNUT_L, true},
        {{33, 105, 414, 282}, 141, CD_BRASS, false},
        {{40, 112, 400, 268}, 134, CD_FELT, true},
    };
    for (const Layer &layer : kLayers) {
        lv_obj_t *o = lv_obj_create(s_content);
        lv_obj_remove_style_all(o);
        lv_obj_set_pos(o, layer.r.x, layer.r.y);
        lv_obj_set_size(o, layer.r.w, layer.r.h);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(o, layer.radius, 0);
        if (layer.filled) {
            lv_obj_set_style_bg_color(o, layer.color, 0);
            lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_color(o, layer.color, 0);
            lv_obj_set_style_border_width(o, 1, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// カード（画像は使わない。LVGL の角丸と文字だけで描く。設計一式 01 §6）
// ---------------------------------------------------------------------------
enum class Face : uint8_t { Absent, Back, Front };

// private = 本人だけの札。カフェへ戻る前に消す対象として控える
lv_obj_t *makeCard(const Rect &r, Face face, int rank, int suit, bool selected, bool is_private,
                   lv_event_cb_t cb = nullptr, void *user_data = nullptr)
{
    if (face == Face::Absent) {
        return nullptr;     // まだ配られていない札は「無い」（設計一式 01 §6）
    }
    lv_obj_t *card = lv_obj_create(s_content);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, r.x, (int16_t)(r.y - (selected ? 4 : 0)));
    lv_obj_set_size(card, r.w, r.h);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, face == Face::Back ? CD_FELT_DARK : CD_PAPER, 0);
    lv_obj_set_style_border_color(card, selected ? CD_GOLD : (face == Face::Back ? CD_BRASS : CD_INK), 0);
    lv_obj_set_style_border_width(card, selected ? 3 : 1, 0);
    lv_obj_set_style_pad_all(card, 0, 0);

    if (face == Face::Back) {
        // 裏模様（このパッケージのもの。実在のブランドの裏面は使わない）
        lv_obj_t *inner = lv_obj_create(card);
        lv_obj_remove_style_all(inner);
        lv_obj_set_size(inner, (int16_t)(r.w - 12), (int16_t)(r.h - 12));
        lv_obj_center(inner);
        lv_obj_clear_flag(inner, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(inner, 4, 0);
        lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(inner, CD_BRASS, 0);
        lv_obj_set_style_border_width(inner, 1, 0);
    } else {
        char text[16];
        if (suit < 0) {
            std::snprintf(text, sizeof(text), "%d", rank);     // GOPS は 1〜13 の数字だけ
        } else {
            std::snprintf(text, sizeof(text), "%s\n%s", ct::rankText(rank), kSuitGlyph[suit]);
        }
        const lv_font_t *font = (suit < 0 && r.h >= 78) ? &ct_font_jp_40
                              : (r.h >= 66 ? &ct_font_jp_22 : &ct_font_jp_20);
        lv_obj_t *l = lv_label_create(card);
        lv_obj_set_style_text_font(l, font, 0);
        lv_obj_set_style_text_color(l, (suit == 1 || suit == 2) ? CD_RED : CD_INK, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(l, (int16_t)(r.w - 4));
        lv_label_set_text(l, text);
        lv_obj_center(l);
    }
    if (cb != nullptr) {
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, cb, LV_EVENT_CLICKED, user_data);
    }
    if (is_private && s_private_count < kPrivateMax) {
        s_private[s_private_count++] = card;
    }
    return card;
}

// 選んだ札の印（金の小さな四角）。金の 3px 枠と 4px 持ち上げに足す「形」の目印
// （設計一式 01 §6 の「チェック印」。64×76 の札に rank＋スートと文字の札を
//  同時に入れると読めなくなるので、文字ではなく印にした）
lv_obj_t *makeSelectMark(lv_obj_t *card, bool on)
{
    lv_obj_t *mark = lv_obj_create(card);
    lv_obj_remove_style_all(mark);
    lv_obj_set_size(mark, 12, 12);
    lv_obj_align(mark, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_clear_flag(mark, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(mark, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(mark, 3, 0);
    lv_obj_set_style_bg_color(mark, CD_GOLD, 0);
    lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(mark, CD_GOLD_INK, 0);
    lv_obj_set_style_border_width(mark, 1, 0);
    if (!on) {
        lv_obj_add_flag(mark, LV_OBJ_FLAG_HIDDEN);
    }
    return mark;
}

// 手札から作った文字（役の名前・自分の得点）も私的情報。カードと同じ扱いで控える
void registerPrivate(lv_obj_t *obj)
{
    if (obj != nullptr && s_private_count < kPrivateMax) {
        s_private[s_private_count++] = obj;
    }
}

// 私的な札と文字を**先に**消してから次の画面へ（設計書 I5 / 設計一式 03 §7）
void privacyCover()
{
    bool any = false;
    for (uint8_t i = 0; i < s_private_count; ++i) {
        lv_obj_t *obj = s_private[i];
        if (obj == nullptr) {
            continue;
        }
        if (lv_obj_check_type(obj, &lv_label_class)) {
            lv_label_set_text(obj, "");     // 役の名前・得点そのもの
        } else {
            const uint32_t n = lv_obj_get_child_cnt(obj);
            for (uint32_t k = 0; k < n; ++k) {
                lv_obj_t *child = lv_obj_get_child(obj, k);
                if (lv_obj_check_type(child, &lv_label_class)) {
                    lv_label_set_text(child, "");
                }
            }
        }
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        any = true;
    }
    s_private_count = 0;
    if (any) {
        lv_refr_now(nullptr);       // 塗り終わってから次の画面へ
    }
}

void resetDrafts();

void setView(View v)
{
    if (v == View::Cafe || v == View::Paused) {
        // 私的な札を消してから移り、仮選択も捨てる（設計一式 02 §4.2 / 03 §7）
        privacyCover();
        resetDrafts();
    }
    s_view = v;
    s_dirty = true;
    s_view_ms = millis();
}

void setStep(Step s)
{
    s_step = s;
    s_step_ms = millis();
    s_dirty = true;
}

void leaveScreen(bool home)
{
    privacyCover();
    if (home) {
        ui::goHome();
    } else {
        ui::pop();
    }
}

// ---------------------------------------------------------------------------
// 通信（計画 §5）
//
// **LVGL のコールバックの中で通信しない。** net:: の依頼箱に預けるだけで、
// 実際の送受信はメインループ（net::poll）が行う。
// ログに URL・合言葉・本文・手札は絶対に出さない（依頼番号と行動 ID と所要時間だけ）
// ---------------------------------------------------------------------------
// この試合が Jev で進む約束か。**ここで通信できるかは見ない。**
// 見てしまうと、電波が切れた瞬間に黙って端末 AI に変わり（本人は JEV と対戦している
// つもりのまま）、きろくも「jev」のままになる。つながらないことは beginJevRequest が
// 「通信の状態」画面で本人に知らせ、切り替えるかどうかは本人が選ぶ（設計一式 02 §6）
bool useJev()
{
    return s_sess->want_jev && !s_sess->local_only;
}

void newMatchId()
{
    uint8_t id[16];
    esp_fill_random(id, sizeof(id));
    for (int i = 0; i < 16; ++i) {
        std::snprintf(s_sess->match_id + i * 2, 3, "%02x", id[i]);
    }
}

bool buildRequest(uint32_t req)
{
    if (ct::writeObservation(match(), s_sess->obs, sizeof(s_sess->obs)) == 0) {
        return false;
    }
    if (ct::writeLegal(match(), 1, s_sess->legal, sizeof(s_sess->legal)) == 0) {
        return false;
    }
    const int n = std::snprintf(s_sess->body, sizeof(s_sess->body),
                                "{\"event\":\"cards\",\"req\":%lu,\"match\":\"%s\",\"rev\":%lu,"
                                "\"observation\":%s,\"legal\":%s}",
                                (unsigned long)req, s_sess->match_id,
                                (unsigned long)match().revision, s_sess->obs, s_sess->legal);
    return n > 0 && (size_t)n < sizeof(s_sess->body);
}

void cancelJevRequest()
{
    if (s_step == Step::Wait || s_send_pending) {
        net::gasCancel();
    }
    s_send_pending = false;
    s_attempt = 0;
}

void tryPostJev()
{
    const uint32_t req = s_req_no + 1;
    if (!buildRequest(req)) {
        // 依頼箱のふさがりとは別の失敗。ねばっても直らないのですぐ本人に知らせる
        std::snprintf(s_req_reason, sizeof(s_req_reason), "too large");
        Serial.println("[CARDS] req=- -> failed 0ms (request does not fit)");
        s_send_pending = false;
        setStep(Step::Idle);
        setView(View::Network);
        return;
    }
    if (net::gasRequest(req, s_sess->body)) {
        s_req_no = req;
        s_req_ms = millis();
        s_req_rev = match().revision;
        s_send_pending = false;
        s_req_reason[0] = '\0';
        return;
    }
    s_send_pending = true;
    if (millis() - s_send_since_ms >= kSendBusyMs) {
        s_send_pending = false;
        std::snprintf(s_req_reason, sizeof(s_req_reason), "busy");
        Serial.printf("[CARDS] req=- -> failed %lums (mailbox busy)\n",
                      (unsigned long)(millis() - s_send_since_ms));
        setStep(Step::Idle);
        setView(View::Network);
    }
}

void beginJevRequest(uint8_t attempt)
{
    if (!net::gasReady()) {
        std::snprintf(s_req_reason, sizeof(s_req_reason), "offline");
        Serial.println("[CARDS] req=- -> failed 0ms (offline)");
        setStep(Step::Idle);
        setView(View::Network);
        return;
    }
    s_attempt = attempt;
    s_send_since_ms = millis();
    setStep(Step::Wait);
    tryPostJev();
}

// 終わった試合を 1 行だけ送る（投げっぱなし。届かなければ捨てる）
void trySendResult()
{
    if (s_result_json[0] == '\0') {
        return;
    }
    const uint32_t req = s_req_no + 1;
    const int n = std::snprintf(s_sess->body, sizeof(s_sess->body),
                                "{\"event\":\"cards\",\"req\":%lu,\"result\":%s}",
                                (unsigned long)req, s_result_json);
    if (n > 0 && (size_t)n < sizeof(s_sess->body) && net::gasRequest(req, s_sess->body, true)) {
        s_req_no = req;
        s_send_pending = false;
        s_result_pending = false;
        Serial.printf("[CARDS] result queued req=%lu\n", (unsigned long)req);
        s_result_json[0] = '\0';
        return;
    }
    s_result_pending = true;
    if (millis() - s_result_since_ms >= kSendBusyMs) {
        s_result_pending = false;
        Serial.println("[CARDS] result dropped (mailbox busy)");
        s_result_json[0] = '\0';
    }
}

// きろみとシートに残す相手の区分。**Jev が 1 回も答えていない試合は「混合」ではなく
// 「端末AI」**（JEV を選んだが一度もつながらなかった試合を Jev 戦に数えない）
const char *opponentClass()
{
    if (!s_sess->want_jev || !s_sess->used_jev) {
        return "local";
    }
    return s_sess->local_only ? "mixed" : "jev";
}

void sendResult(const char *end_reason)
{
    s_result_pending = false;
    s_result_json[0] = '\0';
    if (!net::gasReady()) {
        Serial.printf("[CARDS] result %s not sent (offline)\n", end_reason);
        return;
    }
    const ct::Match &m = match();
    char player[8];
    if (isGuest()) {
        std::snprintf(player, sizeof(player), "guest");
    } else {
        std::snprintf(player, sizeof(player), "p%u", (unsigned)s_sess->slot);
    }
    // 途中で終わった試合に勝敗は付けない。点数はそのときの実際の値を送る
    // （m.scores は決着したときにしか入らないので、中止だと 0-0 の引き分けに見えてしまう）
    const bool completed = std::strcmp(end_reason, "completed") == 0;
    const char winner = !completed ? '-'
                      : (m.winner == core::H ? 'H' : (m.winner == core::AI ? 'A' : 'D'));
    int human_score = 0, ai_score = 0;
    ct::liveScores(m, human_score, ai_score);
    std::snprintf(s_result_json, sizeof(s_result_json),
                  "{\"game\":\"%s\",\"variant\":\"%s\",\"opponent\":\"%s\",\"player\":\"%s\","
                  "\"human_score\":%d,\"opponent_score\":%d,\"winner\":\"%c\","
                  "\"completed_units\":%u,\"end_reason\":\"%s\"}",
                  ct::gameId(m.game), ct::variantId(m), opponentClass(), player, human_score,
                  ai_score, winner, (unsigned)m.completed_units, end_reason);
    s_result_since_ms = millis();
    trySendResult();
}

// 返事を読む。合法な行動を取り出せたら true（**行動は端末で必ず検証する**。計画 §5）
bool parseJevReply(const net::GasResult &r, char *action, size_t action_size, char *reason,
                   size_t reason_size)
{
    if (r.req != s_req_no) {
        std::snprintf(reason, reason_size, "stale reply");
        return false;
    }
    if (!r.ok) {
        std::snprintf(reason, reason_size, "http %d", r.status);
        return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, r.body) != DeserializationError::Ok ||
        doc["ok"].as<bool>() != true || doc["req"].as<uint32_t>() != s_req_no) {
        std::snprintf(reason, reason_size, "bad reply");
        return false;
    }
    const char *status = doc["status"] | "failed";
    if (std::strcmp(status, "ready") != 0) {
        const char *why = doc["reason"] | "none";
        std::snprintf(reason, reason_size, "none:%.24s", why);
        return false;
    }
    // 局面が進んでいたら古い返事。適用しない（設計一式 03 §5）
    if (match().revision != s_req_rev) {
        std::snprintf(reason, reason_size, "stale reply");
        return false;
    }
    const char *chosen = doc["action"] | "";
    if (!ct::isLegalId(match(), 1, chosen)) {
        std::snprintf(reason, reason_size, "illegal reply");
        return false;
    }
    std::snprintf(action, action_size, "%s", chosen);

    Session &st = *s_sess;
    st.jev_valid = true;
    st.jev_hidden = false;
    st.jev_unit = ct::unitNo(match());
    std::snprintf(st.jev_action, sizeof(st.jev_action), "%s", chosen);
    st.jev_conf = doc["confidence"] | 0.0f;
    st.jev_cached = doc["cached"] | false;
    st.jev_count = 0;
    st.jev_total = 0;
    JsonObjectConst probs = doc["p"].as<JsonObjectConst>();
    for (JsonPairConst kv : probs) {
        if (st.jev_total < 255) {
            ++st.jev_total;
        }
        const uint8_t percent = (uint8_t)(kv.value().as<float>() * 100.0f + 0.5f);
        // 大きい順に挿し込み、上位 kDetailRows 件だけ残す
        uint8_t at = 0;
        while (at < st.jev_count && st.jev_percent[at] >= percent) {
            ++at;
        }
        if (at >= kDetailRows) {
            continue;
        }
        const uint8_t last = st.jev_count < kDetailRows ? st.jev_count
                                                        : (uint8_t)(kDetailRows - 1);
        for (uint8_t j = last; j > at; --j) {
            st.jev_percent[j] = st.jev_percent[j - 1];
            std::memcpy(st.jev_names[j], st.jev_names[j - 1], ct::kActionIdMax);
        }
        st.jev_percent[at] = percent;
        std::snprintf(st.jev_names[at], ct::kActionIdMax, "%s", kv.key().c_str());
        if (st.jev_count < kDetailRows) {
            ++st.jev_count;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// 試合の始末
// ---------------------------------------------------------------------------
void countMatch(const char *end_reason)
{
    if (s_sess->counted) {
        return;
    }
    s_sess->counted = true;
    const ct::Match &m = match();
    const bool completed = std::strcmp(end_reason, "completed") == 0;
    cards::store::Outcome outcome = cards::store::Outcome::Aborted;
    if (completed) {
        outcome = m.winner == core::H    ? cards::store::Outcome::Win
                : m.winner == core::AI   ? cards::store::Outcome::Loss
                                         : cards::store::Outcome::Draw;
    }
    const uint32_t hits = (m.game == ct::Game::Baccarat && completed) ? m.bac_hits[0] : 0;
    cards::store::noteResult(s_sess->slot, (size_t)m.game, outcome, hits);
    if (completed) {
        // 数えるのは回数だけ。手札も勝敗も SD には残さない
        char note[40];
        std::snprintf(note, sizeof(note), "cards %s %s", ct::gameId(m.game), opponentClass());
        cup::stats::gamePlayed(cup::GameId::Cards, note);
    }
    sendResult(end_reason);
}

// 途中の試合をやめる（新しい試合を始める前）
void abortMatch()
{
    if (!s_sess->in_match) {
        return;
    }
    if (!match().finished) {
        countMatch("aborted");      // 途中終了も「中止」として数える（回数には数えない）
    }
    cancelJevRequest();
    s_sess->in_match = false;
}

// 行動が 1 つ確定したあとの共通処理。
// **フォールドで終わったハンドは、確率内訳も出さない**（設計書 P6）
void afterAction()
{
    ct::Match &m = match();
    // 起こらないはずの失敗でコアが引き分けにしたときは、1 行だけ残して消す
    if (m.fault != nullptr) {
        Serial.printf("[CARDS] %s\n", m.fault);
        m.fault = nullptr;
    }
    if (m.game == ct::Game::Poker && m.phase == ct::Phase::UnitResult && m.last_folded) {
        s_sess->jev_hidden = true;
    }
    // 不変条件は実機でも 1 手ごとに見る（PC 上の試験と同じ関数。数十命令で終わる）。
    // 破れたらログに 1 行だけ残す（盤面は動かさない。手札は出さない）
    const char *why = nullptr;
    if (!ct::invariants(m, &why)) {
        Serial.printf("[CARDS] invariant broken: %s\n", why != nullptr ? why : "?");
    }
}

void resetDrafts()
{
    s_draft_mask = 0;
    s_draft_bid = -1;
    s_draft_hand = -1;
    s_draft_market = -1;
    s_page = 0;
}

void beginMatch()
{
    Session &st = *s_sess;
    abortMatch();
    st.game = s_pick_game;
    st.variant = s_pick_variant;
    st.local_only = false;
    st.used_jev = false;
    st.counted = false;
    st.jev_valid = false;
    st.roll_drawn = false;
    newMatchId();
    resetDrafts();
    const uint8_t variant = st.game == 1 ? (uint8_t)(st.variant == 1 ? 13 : 7) : st.variant;
    if (!ct::startMatch(match(), (ct::Game)st.game, variant, hwRandom, nullptr)) {
        ui::showToast(s_screen, "はじめられませんでした");
        setView(View::Entry);
        return;
    }
    st.in_match = true;
    Serial.printf("[CARDS] new match %s %s vs %s\n", ct::gameId(match().game),
                  ct::variantId(match()), st.want_jev ? "jev" : "local");
    setStep(Step::Idle);
    setView(View::Table);
}

// ---------------------------------------------------------------------------
// 相手（AI）の進行
// ---------------------------------------------------------------------------
// ログに出してよい行動 ID。**同時選択の段階は、公開前の相手の選択を出さない**
// （GOPS の入札・ポーカーの交換 mask・バカラの予想は、人間が確定するまで秘密）
const char *logId(const char *id)
{
    return ct::simultaneous(match()) ? "sealed" : id;
}

void applyAiAction(const char *id, const char *provider, uint32_t ms)
{
    if (!ct::applyAction(match(), 1, id)) {
        Serial.println("[CARDS] the core refused the opponent action");
        setStep(Step::Idle);
        return;
    }
    s_sess->roll_drawn = false;
    if (std::strcmp(provider, "jev") == 0) {
        s_sess->used_jev = true;
    } else {
        s_sess->jev_valid = false;      // 端末 AI や一択の手を Jev の確率として見せない
    }
    (void)ms;
    afterAction();
    setStep(Step::Idle);
    s_dirty = true;
}

void startAiDecision()
{
    ct::Match &m = match();
    char ids[ct::kMaxActions][ct::kActionIdMax];
    const int n = ct::legalActions(m, 1, ids);
    if (n <= 0) {
        setStep(Step::Idle);
        return;
    }
    if (n == 1) {
        // 一択は端末で確定し、Jev には送らない（設計書 G2 の forced）
        Serial.printf("[CARDS] req=- -> forced %s\n", logId(ids[0]));
        applyAiAction(ids[0], "forced", 0);
        return;
    }
    if (!s_sess->roll_drawn) {
        s_sess->roll = ct::drawRoll(hwRandom, nullptr);
        s_sess->roll_drawn = true;
    }
    if (useJev()) {
        beginJevRequest(1);
        return;
    }
    setStep(Step::Think);
}

// 相手が動くべき局面なら動かす
void syncAi()
{
    if (s_sess == nullptr || !s_sess->in_match) {
        return;
    }
    ct::Match &m = match();
    if (m.finished || m.phase == ct::Phase::UnitResult || !ct::canAct(m, 1)) {
        if (s_step != Step::Idle) {
            cancelJevRequest();     // 依頼箱を空けてから止める
            setStep(Step::Idle);
        }
        return;
    }
    if (s_step != Step::Idle) {
        return;
    }
    startAiDecision();
}

void pollJev()
{
    if (s_step != Step::Wait) {
        return;
    }
    if (s_send_pending) {
        tryPostJev();
        return;
    }
    if (net::gasTakeResult(s_sess->reply)) {
        char action[ct::kActionIdMax];
        if (parseJevReply(s_sess->reply, action, sizeof(action), s_req_reason,
                          sizeof(s_req_reason))) {
            Serial.printf("[CARDS] req=%lu -> jev %s %lums%s%s\n",
                          (unsigned long)s_sess->reply.req, logId(action),
                          (unsigned long)s_sess->reply.elapsed_ms,
                          s_sess->jev_cached ? " (cached" : "",
                          s_sess->jev_cached ? (s_attempt > 1 ? ", auto-retry)" : ")")
                                             : (s_attempt > 1 ? " (auto-retry)" : ""));
            s_attempt = 0;
            applyAiAction(action, "jev", s_sess->reply.elapsed_ms);
            return;
        }
        if (s_attempt < kJevAttempts) {
            Serial.printf("[CARDS] req=%lu -> retry %lums (%s)\n", (unsigned long)s_sess->reply.req,
                          (unsigned long)s_sess->reply.elapsed_ms, s_req_reason);
            beginJevRequest((uint8_t)(s_attempt + 1));
            return;
        }
        Serial.printf("[CARDS] req=%lu -> failed %lums (%s)\n", (unsigned long)s_sess->reply.req,
                      (unsigned long)s_sess->reply.elapsed_ms, s_req_reason);
        setStep(Step::Idle);
        setView(View::Network);
        return;
    }
    if (millis() - s_req_ms >= kJevWaitMs) {
        const uint32_t waited = millis() - s_req_ms;
        net::gasCancel();
        std::snprintf(s_req_reason, sizeof(s_req_reason), "timeout");
        if (s_attempt < kJevAttempts) {
            Serial.printf("[CARDS] req=%lu -> retry %lums (timeout)\n", (unsigned long)s_req_no,
                          (unsigned long)waited);
            beginJevRequest((uint8_t)(s_attempt + 1));
            return;
        }
        Serial.printf("[CARDS] req=%lu -> failed %lums (timeout)\n", (unsigned long)s_req_no,
                      (unsigned long)waited);
        setStep(Step::Idle);
        setView(View::Network);
    }
}

// ---------------------------------------------------------------------------
// 人間の確定
// ---------------------------------------------------------------------------
void askAction(AskKind kind, const char *id)
{
    s_ask_kind = kind;
    s_ask_rev = match().revision;
    std::snprintf(s_ask_id, sizeof(s_ask_id), "%s", id != nullptr ? id : "");
    s_ask_return = s_view;
    setView(View::Confirm);
}

void commitHuman(const char *id)
{
    ct::Match &m = match();
    if (s_busy || m.revision != s_ask_rev) {
        // 同じ revision は二度と確定できない（設計一式 03 §4）
        ui::showToast(s_screen, "もう一度選んでください");
        setView(View::Table);
        return;
    }
    if (ct::simultaneous(m) && !ct::sealed(m, 1)) {
        ui::showToast(s_screen, "相手の選択を待っています");
        setView(View::Table);
        return;
    }
    if (!ct::applyAction(m, 0, id)) {
        ui::showToast(s_screen, "いまは選べません");
        setView(View::Table);
        return;
    }
    s_busy = true;
    afterAction();
    resetDrafts();
    setView(View::Table);
    syncAi();
}

// ---------------------------------------------------------------------------
// 画面づくり
// ---------------------------------------------------------------------------
void buildEntry()
{
    buildFrame();
    const bool resume = resumable();
    makeTitle("POKER TABLE");
    makeMeta(resume ? "つづきの試合があります" : "今日は、どの勝負にしますか？");
    static const Act kActs[4] = {Act::EntryTable0, Act::EntryTable1, Act::EntryTable2,
                                 Act::EntryTable3};
    for (int i = 0; i < 4; ++i) {
        rectButton(layout::kTile[i], kTableCatch[i], kActs[i], true,
                   resume && (int)s_sess->game == i);
    }
    rectButton(layout::kEntryResume, "つづきから", Act::EntryResume, resume, resume);
    rectButton(layout::kEntryHow, "遊び方", Act::EntryHow);
    // はじめての説明をまた自動で出すようにする（4 卓ぶんの「見た」印を消す）
    const bool any_seen = cards::store::seenFlags() != 0;
    rectButton(layout::kEntryAgain, "説明をもう一度", Act::EntryAgain, any_seen);
    rectLabel(layout::kEntryNote, &ct_font_jp_20, CD_MUTED, "対戦内の点数のみ・換金なし");
    rectButton(layout::kEntryBack, "もどる", Act::EntryBack);
}

void buildPlayer()
{
    buildFrame();
    makeTitle("プレイヤー選択");
    rectLabel(layout::kPlayerNote, &ct_font_jp_20, CD_MUTED, "だれが遊びますか？");
    for (size_t i = 0; i < cards::store::kSlots; ++i) {
        const Rect &r = layout::kAvatar[i];
        // アイコンと名前はボタンの上に重ねる（ボタンの中の余白は 0 にしてある）
        lv_obj_t *btn = makeButton(r, "", avatarCb, (void *)(intptr_t)i, true, i == s_sess->slot);
        lv_obj_t *icon = lv_label_create(btn);
        lv_obj_set_style_text_font(icon, &ct_font_icons_54, 0);
        lv_obj_set_style_text_color(icon, i == s_sess->slot ? CD_GOLD_INK : CD_GOLD, 0);
        lv_label_set_long_mode(icon, LV_LABEL_LONG_CLIP);
        lv_label_set_text(icon, kAvatars[i].glyph);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 4);
        lv_obj_t *name = lv_label_create(btn);
        lv_obj_set_style_text_font(name, &ct_font_jp_20, 0);
        lv_obj_set_style_text_color(name, i == s_sess->slot ? CD_GOLD_INK : CD_TEXT, 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(name, (int16_t)(r.w - 4));
        lv_label_set_text(name, kAvatars[i].name);
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -4);
    }
    rectButton(layout::kGuestBtn, "ゲスト（記録なし）", Act::PlayerGuest, true, isGuest());
    rectButton(layout::kPlayerBack, "もどる", Act::PlayerBack);
}

void buildRecords()
{
    buildFrame();
    makeTitle("きろく");
    rectLabel(layout::kRecIcon, &ct_font_icons_54, CD_GOLD, playerGlyph());
    rectLabel(layout::kRecName, &ct_font_jp_22, CD_TEXT, playerName());
    for (size_t g = 0; g < 4; ++g) {
        uint32_t w, l, d, a;
        cards::store::totals(s_sess->slot, g, w, l, d, a);
        char line[64];
        if (g == 3) {
            std::snprintf(line, sizeof(line), "%-11s %lu-%lu-%lu　的中 %lu", kTableName[g],
                          (unsigned long)w, (unsigned long)l, (unsigned long)d,
                          (unsigned long)cards::store::stats().bac_hits[s_sess->slot]);
        } else {
            std::snprintf(line, sizeof(line), "%-11s %lu-%lu-%lu　中止 %lu", kTableName[g],
                          (unsigned long)w, (unsigned long)l, (unsigned long)d, (unsigned long)a);
        }
        rectLabel(layout::kRecRow[g], &ct_font_jp_20, g == s_pick_game ? CD_GOLD : CD_MUTED, line);
    }
    rectLabel(layout::kRecNote, &ct_font_jp_20, CD_MUTED,
              isGuest() ? "ゲストの記録は残りません" : "勝-負-分");
    char play[40];
    std::snprintf(play, sizeof(play), "%s ではじめる", kTableName[s_pick_game]);
    rectButton(layout::kRecPlay, play, Act::RecPlay, true, true);
    rectButton(layout::kRecBack, "もどる", Act::RecBack);
}

void buildVariant()
{
    buildFrame();
    makeTitle(kTableName[s_pick_game]);
    makeMeta("どちらで遊びますか？");
    if (s_pick_game == 0) {
        // ホールデムを既定に（ユーザーはホールデムしか知らない。計画 §5b）
        rectButton(layout::kPickBtn[0], "ホールデム\n手札2枚と場の5枚", Act::Variant0, true,
                   s_pick_variant == 0);
        rectButton(layout::kPickBtn[1], "ドロー\n5枚を1回交換", Act::Variant1, true,
                   s_pick_variant == 1);
        rectLabel(layout::kPickNote, &ct_font_jp_20, CD_MUTED,
                  "ホールデムは200点、\nドローは100点から始めます");
    } else if (s_pick_game == 1) {
        rectButton(layout::kPickBtn[0], "7枚勝負\n1〜7の札で7ラウンド", Act::Variant0, true,
                   s_pick_variant == 0);
        rectButton(layout::kPickBtn[1], "13枚勝負\n1〜13の札で13ラウンド", Act::Variant1, true,
                   s_pick_variant == 1);
        rectLabel(layout::kPickNote, &ct_font_jp_20, CD_MUTED,
                  "同じ数字を出したら、その回の\n得点はなくなります");
    } else {
        rectButton(layout::kPickBtn[0], "OPEN\n各側の1枚目が見える", Act::Variant0, true,
                   s_pick_variant == 0);
        rectButton(layout::kPickBtn[1], "CLASSIC\n札を見ないで予想", Act::Variant1, true,
                   s_pick_variant == 1);
        rectLabel(layout::kPickNote, &ct_font_jp_20, CD_MUTED,
                  "PLAYER・BANKERは手札の側の\n名前。人間とJevではありません");
    }
    rectButton(layout::kPickBack, "もどる", Act::VariantBack);
}

void buildOpponent()
{
    buildFrame();
    makeTitle(kTableName[s_pick_game]);
    makeMeta("だれと勝負しますか？");
    const bool online = net::gasReady();
    rectButton(layout::kPickBtn[0], "JEV\n通信して1手を選んでもらう", Act::OppJev, online,
               online && s_sess->want_jev);
    rectButton(layout::kPickBtn[1], "端末AI\n通信なしで遊ぶ", Act::OppLocal, true,
               !s_sess->want_jev || !online);
    // Jev が選べない理由を出す。**黙って端末 AI に変えない**
    rectLabel(layout::kPickNote, &ct_font_jp_20, online ? CD_MUTED : CD_GOLD,
              online ? "JEVの1手は3〜7秒かかります"
                     : "いまは通信できないので\nJEVとは遊べません");
    // この卓の遊び方（tutorials.ja.json のそのゲームのページ）へはここから入る
    rectButton(layout::kPickHow, "遊び方", Act::EntryHow);
    rectButton(layout::kPickGo, "もどる", Act::OppBack);
}

void buildTutorial()
{
    buildFrame();
    const int total = guideCount(s_tut_game);
    int page = s_tut_page < total ? (int)s_tut_page : 0;
    const char *title = "";
    const char *body = "";
    int of_game = s_tut_game < 0 ? 0 : s_tut_game;
    if (!guidePage(s_tut_game, page, title, body, of_game)) {
        page = 0;
        guidePage(s_tut_game, 0, title, body, of_game);
    }
    const bool last = page + 1 >= total;

    makeTitle(s_tut_first_play ? "はじめての方へ" : "遊び方");
    char meta[48];
    std::snprintf(meta, sizeof(meta), "%s ・ %d / %d", kTableName[of_game], page + 1, total);
    makeMeta(meta);
    rectLabel(layout::kTutTitle, &ct_font_jp_22, CD_GOLD, title);
    rectLabel(layout::kTutBody, &ct_font_jp_20, CD_TEXT, body);

    if (s_tut_first_play && last) {
        // 最後のページだけ「次回から表示しない」。既定はオン（2 回目からは出ない）
        char toggle[64];
        std::snprintf(toggle, sizeof(toggle), "次回から表示しない：%s",
                      s_tut_dont_show ? "オン" : "オフ");
        rectButton(layout::kTutNote, toggle, Act::TutToggle, true, s_tut_dont_show);
    } else {
        rectLabel(layout::kTutNote, &ct_font_jp_20, CD_MUTED, "現金・景品交換はありません");
    }
    rectButton(layout::kTutPrev, "前へ", Act::TutPrev, page > 0);
    rectButton(layout::kTutNext, last ? (s_tut_first_play ? "はじめる" : "閉じる") : "次へ",
               Act::TutNext, true, true);
    // スキップはどのページからでも押せる（2 回目以降の人がすぐ始められるように）
    rectButton(layout::kTutBack, s_tut_first_play ? "スキップ" : "閉じる", Act::TutBack);
}

// ベットの操作行（ドローとホールデムで同じ。設計書 P3 の行動そのまま）
void buildBetButtons(const core::Street &s)
{
    const int owed = s.paid[1] - s.paid[0];
    if (owed > 0) {
        char call[24], raise[24];
        std::snprintf(call, sizeof(call), "同額で\n+%d pt", owed);
        std::snprintf(raise, sizeof(raise), "上乗せ\n+%d pt", owed + s.unit);
        if (s.raises < 2) {
            tableButton(0, layout::kAct3[0], "降りる", Role::ActionId, "FOLD", true, false);
            tableButton(1, layout::kAct3[1], call, Role::ActionId, "CALL", true, true);
            tableButton(2, layout::kAct3[2], raise, Role::ActionId, "RAISE", true, false);
        } else {
            tableButton(0, layout::kAct2[0], "降りる", Role::ActionId, "FOLD", true, false);
            tableButton(1, layout::kAct2[1], call, Role::ActionId, "CALL", true, true);
        }
    } else {
        char bet[24];
        std::snprintf(bet, sizeof(bet), "%d点出す", s.unit);
        tableButton(0, layout::kAct2[0], "続ける", Role::ActionId, "CHECK", true, false);
        tableButton(1, layout::kAct2[1], bet, Role::ActionId, "BET", true, true);
    }
}

// --- HOLDEM ------------------------------------------------------------------
const char *holdemPhaseName(ct::Phase p)
{
    switch (p) {
    case ct::Phase::HoldemFlop:  return "フロップ";
    case ct::Phase::HoldemTurn:  return "ターン";
    case ct::Phase::HoldemRiver: return "リバー";
    default:                     return "プリフロップ";
    }
}

void buildHoldemTable()
{
    ct::Match &m = match();
    const core::Street &s = m.hd_street;
    makeTitle(kTableTitle[0]);
    char meta[64];
    std::snprintf(meta, sizeof(meta), "ハンド %u / 5 ・ %s", (unsigned)m.hand_no, playerName());
    makeMeta(meta);

    char opp[64];
    std::snprintf(opp, sizeof(opp), "%s  %d pt", shortOpponentName(), m.hd_stack[1]);
    rectLabel(layout::kHdOpp, &ct_font_jp_20, CD_MUTED, opp);
    for (int i = 0; i < 2; ++i) {
        makeCard(layout::kHdOppCard[i], Face::Back, 0, 0, false, false);
    }

    char head[64];
    if (showHint() && m.phase == ct::Phase::HoldemPreflop) {
        // はじめての人へ（最初のハンドのプリフロップだけ）
        std::snprintf(head, sizeof(head), "%s ・ 場の5枚は共有", holdemPhaseName(m.phase));
    } else {
        std::snprintf(head, sizeof(head), "%s %d点 ・ POT %dpt", holdemPhaseName(m.phase), s.unit,
                      m.hd_pot);
    }
    rectLabel(layout::kHdHead, &ct_font_jp_20, CD_MUTED, head);

    // 配られた場の札は表、まだの枠は裏（「あと何枚来るか」が見えるように）
    for (int i = 0; i < 5; ++i) {
        if (i < m.hd_board_n) {
            const core::Card c = m.hd_board[i];
            makeCard(layout::kHdBoard[i], Face::Front, core::rank(c), core::suit(c), false, false);
        } else {
            makeCard(layout::kHdBoard[i], Face::Back, 0, 0, false, false);
        }
    }

    char human[96];
    if (waitingForJev()) {
        // 止まった字だと固まって見えるので、動く「考え中」に差し替える（枠と座標は同じ）
        ui::thinkingCreate(s_content, Rect{80, 252, 320, 26}, kThinkPoker, 3);
    } else if (!ct::canAct(m, 0)) {
        std::snprintf(human, sizeof(human), "相手が考えています");
        registerPrivate(rectLabel(layout::kHdHuman, &ct_font_jp_20, CD_TEXT, human));
    } else {
        const int owed = s.paid[1] - s.paid[0];
        if (owed > 0) {
            std::snprintf(human, sizeof(human), "あなた %d pt ／ 同額には%d点", m.hd_stack[0],
                          owed);
        } else if (m.hd_board_n == 0) {
            std::snprintf(human, sizeof(human), "あなた %d pt", m.hd_stack[0]);
        } else {
            const ct::Best5 best = ct::holdemBest(m.hd_hands[0], m.hd_board.data(), m.hd_board_n);
            std::snprintf(human, sizeof(human), "あなた %d pt / %s", m.hd_stack[0],
                          best.valid ? kHandName[best.value.key[0]] : "");
        }
        registerPrivate(rectLabel(layout::kHdHuman, fitFont(human, layout::kHdHuman.w), CD_TEXT,
                                  human));
    }
    for (int i = 0; i < 2; ++i) {
        const core::Card c = m.hd_hands[0][i];
        makeCard(layout::kHdHand[i], Face::Front, core::rank(c), core::suit(c), false, true);
    }

    if (ct::canAct(m, 0)) {
        buildBetButtons(s);
    } else {
        tableButton(0, layout::kAct2[0], "遊び方", Role::None, nullptr, false, false);
        tableButton(1, layout::kAct2[1], "相手の番", Role::None, nullptr, false, false);
    }
}

void buildHoldemResult()
{
    ct::Match &m = match();
    makeTitle(kTableTitle[0]);
    char meta[64];
    std::snprintf(meta, sizeof(meta), "ハンド %u / 5 ・ %s", (unsigned)m.hand_no,
                  m.last_folded ? "フォールド" : "手札公開");
    makeMeta(meta);

    const char *verdict = m.last_winner == core::H  ? "あなたの勝ち"
                        : m.last_winner == core::AI ? "相手の勝ち"
                                                    : "引き分け";
    if (m.last_folded) {
        // **降りたハンドは双方の札も確率内訳も出さない**（設計書 P6 と同じ扱い）
        rectLabel(layout::kFoldResult, &ct_font_jp_22, CD_TEXT,
                  m.last_winner == core::H ? "相手が降りました" : "あなたが降りました");
        char detail[64];
        std::snprintf(detail, sizeof(detail), "場の%d点を獲得", m.last_pot);
        rectLabel(layout::kFoldDetail, &ct_font_jp_20, CD_GOLD, detail);
        rectLabel(layout::kFoldPrivacy, &ct_font_jp_20, CD_MUTED, "双方の札は公開しません");
        rectLabel(layout::kFoldNoProb, &ct_font_jp_20, CD_MUTED, "行動の確率内訳も非表示");
        char score[64];
        std::snprintf(score, sizeof(score), "あなた%dpt ／ 相手%dpt", m.hd_stack[0], m.hd_stack[1]);
        rectLabel(Rect{100, 220, 280, 24}, &ct_font_jp_20, CD_TEXT, score);
    } else {
        const ct::Best5 mine = ct::holdemBest(m.hd_hands[0], m.hd_board.data(), m.hd_board_n);
        const ct::Best5 theirs = ct::holdemBest(m.hd_hands[1], m.hd_board.data(), m.hd_board_n);
        char head[64];
        std::snprintf(head, sizeof(head), "場の5枚　あなた%dpt", m.hd_stack[0]);
        rectLabel(layout::kHdShowHead, &ct_font_jp_20, CD_MUTED, head);
        for (int i = 0; i < m.hd_board_n && i < 5; ++i) {
            const core::Card c = m.hd_board[i];
            makeCard(layout::kHdShowBoard[i], Face::Front, core::rank(c), core::suit(c), false,
                     false);
        }
        // 名前と、7 枚から選ばれた最良の 5 枚の役名（2 行）
        char opp_tag[64], you_tag[64];
        std::snprintf(opp_tag, sizeof(opp_tag), "%s\n%s", shortOpponentName(),
                      theirs.valid ? kHandName[theirs.value.key[0]] : "");
        std::snprintf(you_tag, sizeof(you_tag), "あなた\n%s",
                      mine.valid ? kHandName[mine.value.key[0]] : "");
        registerPrivate(rectLabel(layout::kHdOppTag, &ct_font_jp_20, CD_MUTED, opp_tag));
        registerPrivate(rectLabel(layout::kHdYouTag, &ct_font_jp_20, CD_GOLD, you_tag));
        for (int i = 0; i < 2; ++i) {
            const core::Card o = m.hd_hands[1][i];
            makeCard(layout::kHdOppShow[i], Face::Front, core::rank(o), core::suit(o), false, true);
            const core::Card y = m.hd_hands[0][i];
            makeCard(layout::kHdYouShow[i], Face::Front, core::rank(y), core::suit(y), false, true);
        }
        char result[64];
        std::snprintf(result, sizeof(result), "%s ・ 場の%d点", verdict, m.last_pot);
        rectLabel(layout::kHdResult, fitFont(result, layout::kHdResult.w), CD_TEXT, result);
    }

    const bool show_detail = s_sess->jev_valid && !s_sess->jev_hidden && !m.last_folded;
    tableButton(0, layout::kAct2[0], "内訳", Role::Detail, nullptr, show_detail, false);
    tableButton(1, layout::kAct2[1], m.hand_no >= 5 ? "結果へ" : "次のハンド", Role::Next, nullptr,
                true, true);
}

// --- POKER -------------------------------------------------------------------
void buildPokerTable()
{
    ct::Match &m = match();
    const core::Street &s = m.ph.street;
    makeTitle(kTableTitle[0]);
    char meta[64];
    std::snprintf(meta, sizeof(meta), "ハンド %u / 5 ・ %s", (unsigned)m.hand_no, playerName());
    makeMeta(meta);

    // 交換後は相手の交換枚数も出す（**双方が確定してから初めて公開**。設計書 P4 の 5）
    const bool after_draw = m.phase == ct::Phase::PokerBetPost;
    char opp[64];
    if (after_draw && m.draw_counts[1] >= 0) {
        std::snprintf(opp, sizeof(opp), "%s  %d pt ・ 交換%d枚", shortOpponentName(),
                      m.ph.stack[1], (int)m.draw_counts[1]);
    } else {
        std::snprintf(opp, sizeof(opp), "%s  %d pt", shortOpponentName(), m.ph.stack[1]);
    }
    rectLabel(layout::kPokOpp, &ct_font_jp_20, CD_MUTED, opp);
    for (int i = 0; i < 5; ++i) {
        makeCard(layout::kPokOppCard[i], Face::Back, 0, 0, false, false);
    }

    const bool drawing = m.phase == ct::Phase::PokerDraw;
    char pot_head[64];
    if (!drawing && !after_draw && showHint() && ct::canAct(m, 0)) {
        // はじめての人へ、最初のハンドだけ（ボタンには重ねない。POT の数字は下の行に残る）
        std::snprintf(pot_head, sizeof(pot_head), "続ける＝追加なし／出す＝%d点", s.unit);
    } else if (drawing) {
        std::snprintf(pot_head, sizeof(pot_head), "交換する札を選択");
    } else if (after_draw && m.draw_counts[0] >= 0 && m.draw_counts[1] >= 0) {
        // 双方の交換枚数は、両方が確定してから初めてここに出る（設計書 P4 の 5）
        std::snprintf(pot_head, sizeof(pot_head), "交換　あなた%d枚 ・ 相手%d枚",
                      (int)m.draw_counts[0], (int)m.draw_counts[1]);
    } else {
        std::snprintf(pot_head, sizeof(pot_head), "POT");
    }
    rectLabel(layout::kPokPotLabel, &ct_font_jp_20, CD_MUTED, pot_head);
    char pot[24];
    if (drawing) {
        std::snprintf(pot, sizeof(pot), "%d 枚", __builtin_popcount((unsigned)s_draft_mask));
    } else {
        std::snprintf(pot, sizeof(pot), "%d pt", m.ph.pot);
    }
    lv_obj_t *pot_label = rectLabel(layout::kPokPot, &ct_font_jp_22, CD_GOLD, pot);
    if (drawing) {
        s_draw_count = pot_label;       // 仮選択の枚数はここだけ描き替える
    }

    // 自分の行 ＋ 役（コードの判定。推定ではない）
    char human[96];
    const core::PokerValue v = core::poker_value(m.ph.hands[0]);
    const char *role = v.valid ? kHandName[v.key[0]] : "";
    if (waitingForJev()) {
        ui::thinkingCreate(s_content, Rect{80, 230, 320, 26}, kThinkPoker, 3);
        human[0] = '\0';
    } else if (drawing) {
        std::snprintf(human, sizeof(human), "%s",
                      ct::sealed(m, 1) ? "相手の選択は確定済み" : "相手の交換を待っています");
    } else if (!ct::canAct(m, 0)) {
        std::snprintf(human, sizeof(human), "相手が考えています");
    } else {
        const int owed = s.paid[1] - s.paid[0];
        if (owed > 0) {
            std::snprintf(human, sizeof(human), "あなた %d pt ／ 同額には%d点", m.ph.stack[0], owed);
        } else {
            std::snprintf(human, sizeof(human), "あなた %d pt / %s", m.ph.stack[0], role);
        }
    }
    // 役の名前は手札から作った私的情報。カードと同じく、カフェへ移る前に消す
    if (human[0] != '\0') {
        registerPrivate(rectLabel(layout::kPokHuman, fitFont(human, layout::kPokHuman.w), CD_TEXT,
                                  human));
    }

    for (int i = 0; i < 5; ++i) {
        const core::Card c = m.ph.hands[0][i];
        const bool picked = drawing && (s_draft_mask & (1 << i));
        lv_obj_t *card = makeCard(layout::kPokHand[i], Face::Front, core::rank(c), core::suit(c),
                                  picked, true, drawing ? pokerCardCb : nullptr,
                                  (void *)(intptr_t)i);
        if (drawing) {
            s_draw_card[i] = card;
            s_draw_mark[i] = makeSelectMark(card, picked);
        }
    }

    if (drawing) {
        const bool ready = ct::sealed(m, 1);
        // 「選択解除」は枚数にかかわらず押せるままにする（押せる / 押せないを
        //  その場で描き替えるとイベントの付け外しが要るため。0 枚のときは空振り）
        tableButton(0, layout::kAct2[0], "選択解除", Role::ClearSel, nullptr, true, false);
        lv_obj_t *btn = tableButton(1, layout::kAct2[1],
                                    s_draft_mask == 0 ? "交換しない" : "交換する",
                                    Role::DraftPoker, nullptr, ready, ready);
        s_draw_confirm = buttonLabel(btn);
    } else if (ct::canAct(m, 0)) {
        buildBetButtons(s);
    } else {
        tableButton(0, layout::kAct2[0], "遊び方", Role::None, nullptr, false, false);
        tableButton(1, layout::kAct2[1], "相手の番", Role::None, nullptr, false, false);
    }
}

void buildPokerResult()
{
    ct::Match &m = match();
    makeTitle(kTableTitle[0]);
    char meta[64];
    std::snprintf(meta, sizeof(meta), "ハンド %u / 5 ・ %s", (unsigned)m.hand_no,
                  m.last_folded ? "フォールド" : "手札公開");
    makeMeta(meta);

    const char *verdict = m.last_winner == core::H    ? "あなたの勝ち"
                        : m.last_winner == core::AI   ? "相手の勝ち"
                                                      : "引き分け";
    char detail[64];
    std::snprintf(detail, sizeof(detail), "場の%d点を獲得", m.last_pot);

    if (m.last_folded) {
        // **フォールドで終わったハンドは、双方の札も確率内訳も出さない**（設計書 P6）
        rectLabel(layout::kFoldResult, &ct_font_jp_22, CD_TEXT,
                  m.last_winner == core::H ? "相手が降りました" : "あなたが降りました");
        rectLabel(layout::kFoldDetail, &ct_font_jp_20, CD_GOLD, detail);
        rectLabel(layout::kFoldPrivacy, &ct_font_jp_20, CD_MUTED, "双方の札は公開しません");
        rectLabel(layout::kFoldNoProb, &ct_font_jp_20, CD_MUTED, "行動の確率内訳も非表示");
        char score[64];
        std::snprintf(score, sizeof(score), "あなた%dpt ／ 相手%dpt", m.stacks[0], m.stacks[1]);
        rectLabel(Rect{100, 220, 280, 24}, &ct_font_jp_20, CD_TEXT, score);
    } else {
        const core::PokerValue vo = core::poker_value(m.ph.hands[1]);
        const core::PokerValue vy = core::poker_value(m.ph.hands[0]);
        char role[48];
        std::snprintf(role, sizeof(role), "%s：%s", shortOpponentName(),
                      vo.valid ? kHandName[vo.key[0]] : "");
        registerPrivate(rectLabel(layout::kPokRole, &ct_font_jp_20, CD_MUTED, role));
        for (int i = 0; i < 5; ++i) {
            const core::Card c = m.ph.hands[1][i];
            makeCard(layout::kPokShowOpp[i], Face::Front, core::rank(c), core::suit(c), false, true);
        }
        // 勝ち負けと自分の役を 1 行に、獲得した場の点と残りを次の行に
        char top[80];
        std::snprintf(top, sizeof(top), "%s ・ %s", verdict, vy.valid ? kHandName[vy.key[0]] : "");
        registerPrivate(rectLabel(layout::kPokResult, fitFont(top, layout::kPokResult.w), CD_TEXT,
                                  top));
        char line[64];
        std::snprintf(line, sizeof(line), "%s　あなた%dpt", detail, m.stacks[0]);
        rectLabel(layout::kPokDetail, &ct_font_jp_20, CD_GOLD, line);
        for (int i = 0; i < 5; ++i) {
            const core::Card c = m.ph.hands[0][i];
            makeCard(layout::kPokShowYou[i], Face::Front, core::rank(c), core::suit(c), false, true);
        }
    }
    const bool show_detail = s_sess->jev_valid && !s_sess->jev_hidden && !m.last_folded;
    tableButton(0, layout::kAct2[0], "内訳", Role::Detail, nullptr, show_detail, false);
    tableButton(1, layout::kAct2[1], m.hand_no >= 5 ? "結果へ" : "次のハンド", Role::Next, nullptr,
                true, true);
}

// --- GOPS --------------------------------------------------------------------
void buildGopsTable()
{
    ct::Match &m = match();
    makeTitle(kTableName[1]);
    char meta[64];
    std::snprintf(meta, sizeof(meta), "%d枚勝負 ・ ラウンド %d / %d", m.gops.n, m.gops.index + 1,
                  m.gops.n);
    makeMeta(meta);

    if (waitingForJev()) {
        ui::thinkingCreate(s_content, Rect{88, 104, 304, 26}, kThinkGops, 2);
    } else {
        rectLabel(layout::kGopsOpp, &ct_font_jp_20, CD_MUTED,
                  ct::sealed(m, 1) ? "相手の選択は確定済み" : "相手が選んでいます");
    }
    rectLabel(layout::kGopsPrizeL, &ct_font_jp_20, CD_MUTED,
              showHint() ? "今回の得点・同じ数字なら消滅" : "今回の得点");
    char you[32], ai[32];
    std::snprintf(you, sizeof(you), "あなた\n%d pt", m.gops.score[0]);
    std::snprintf(ai, sizeof(ai), "%s\n%d pt", shortOpponentName(), m.gops.score[1]);
    rectLabel(layout::kGopsYou, &ct_font_jp_20, CD_TEXT, you);
    rectLabel(layout::kGopsAi, &ct_font_jp_20, CD_TEXT, ai);
    makeCard(layout::kGopsPrize, Face::Front, m.gops.prizes[m.gops.index], -1, false, false);

    // 未使用の札を小さい順に 4 枚ずつ（設計書 G4）
    int cards_left[13];
    int count = 0;
    for (int v = 1; v <= m.gops.n; ++v) {
        if (m.gops.remaining[0] & (uint16_t)(1u << (v - 1))) {
            cards_left[count++] = v;
        }
    }
    const int pages = count > 0 ? (count + 3) / 4 : 1;
    if (s_page >= pages) {
        s_page = 0;
    }
    char info[48];
    if (s_draft_bid > 0) {
        std::snprintf(info, sizeof(info), "%d を選択　ページ %d / %d", s_draft_bid, s_page + 1,
                      pages);
    } else {
        std::snprintf(info, sizeof(info), "札を選んでから確定　%d / %d", s_page + 1, pages);
    }
    rectLabel(layout::kGopsInfo, &ct_font_jp_20, CD_MUTED, info);
    for (int i = 0; i < 4; ++i) {
        const int index = s_page * 4 + i;
        if (index >= count) {
            break;
        }
        const int value = cards_left[index];
        makeCard(layout::kGopsBid[i], Face::Front, value, -1, value == s_draft_bid, false,
                 gopsCardCb, (void *)(intptr_t)value);
    }
    tableButton(2, layout::kGopsPrev, "<", Role::PagePrev, nullptr, pages > 1, false);
    tableButton(3, layout::kGopsNext, ">", Role::PageNext, nullptr, pages > 1, false);

    const bool ready = ct::sealed(m, 1) && s_draft_bid > 0;
    tableButton(0, layout::kAct2[0], "残り札", Role::Remaining, nullptr, true, false);
    tableButton(1, layout::kAct2[1], "この札で\n勝負", Role::DraftGops, nullptr, ready, ready);
}

void buildGopsResult()
{
    ct::Match &m = match();
    makeTitle(kTableName[1]);
    const uint8_t row = (uint8_t)(m.gops_rows > 0 ? m.gops_rows - 1 : 0);
    char meta[64];
    std::snprintf(meta, sizeof(meta), "%d枚勝負 ・ ラウンド %u / %d", m.gops.n, (unsigned)(row + 1),
                  m.gops.n);
    makeMeta(meta);

    const ct::Match::GopsRow &r = m.gops_hist[row];
    const bool tie = r.human == r.ai;
    rectLabel(layout::kGopsResult, &ct_font_jp_22, CD_TEXT,
              tie ? "同じ札です" : (r.human > r.ai ? "あなたの勝ち" : "相手の勝ち"));
    char burn[64];
    if (tie) {
        std::snprintf(burn, sizeof(burn), "今回の%u点は破棄", (unsigned)r.prize);
    } else {
        std::snprintf(burn, sizeof(burn), "%s が %u点を獲得", r.human > r.ai ? "あなた" : "相手",
                      (unsigned)r.prize);
    }
    rectLabel(layout::kGopsBurn, &ct_font_jp_20, CD_GOLD, burn);
    char you[32], ai[32];
    std::snprintf(you, sizeof(you), "あなた %dpt", m.gops.score[0]);
    std::snprintf(ai, sizeof(ai), "%s %dpt", shortOpponentName(), m.gops.score[1]);
    rectLabel(layout::kGopsYouTag, &ct_font_jp_20, CD_MUTED, you);
    rectLabel(layout::kGopsAiTag, &ct_font_jp_20, CD_MUTED, ai);
    makeCard(layout::kGopsYouBid, Face::Front, r.human, -1, false, false);
    makeCard(layout::kGopsAiBid, Face::Front, r.ai, -1, false, false);

    tableButton(0, layout::kAct2[0], "残り札", Role::Remaining, nullptr, true, false);
    tableButton(1, layout::kAct2[1], m.gops.done() ? "結果へ" : "次の\nラウンド", Role::Next,
                nullptr, true, true);
}

void buildRemaining()
{
    buildFrame();
    ct::Match &m = match();
    makeTitle(kTableName[1]);
    makeMeta("公開済みの残り札");
    char you[64] = {0}, ai[64] = {0};
    size_t ay = 0, aa = 0;
    for (int v = 1; v <= m.gops.n; ++v) {
        // snprintf の戻り値は「入り切っていれば書いた長さ」なので、そのまま足す前に
        // 収まったかを見る（切れたまま足すと次の残り長さが桁あふれする）
        if ((m.gops.remaining[0] & (uint16_t)(1u << (v - 1))) && ay + 8 < sizeof(you)) {
            ay += (size_t)std::snprintf(you + ay, sizeof(you) - ay, "%s%d", ay ? "  " : "", v);
        }
        if ((m.gops.remaining[1] & (uint16_t)(1u << (v - 1))) && aa + 8 < sizeof(ai)) {
            aa += (size_t)std::snprintf(ai + aa, sizeof(ai) - aa, "%s%d", aa ? "  " : "", v);
        }
    }
    rectLabel(layout::kRemYouT, &ct_font_jp_20, CD_MUTED, "あなたの残り");
    rectLabel(layout::kRemYou, &ct_font_jp_22, CD_TEXT, you);
    char tag[32];
    std::snprintf(tag, sizeof(tag), "%sの残り", opponentName());
    rectLabel(layout::kRemAiT, &ct_font_jp_20, CD_MUTED, tag);
    rectLabel(layout::kRemAi, &ct_font_jp_22, CD_TEXT, ai);
    rectButton(layout::kRemBack, "閉じる", Act::RemainBack, true, true);
}

// --- THIRTY-ONE ----------------------------------------------------------------
void buildThirtyTable()
{
    ct::Match &m = match();
    makeTitle(kTableName[2]);
    char meta[64];
    if (m.phase == ct::Phase::ThirtyLast) {
        std::snprintf(meta, sizeof(meta), "ハンド %u / 3 ・ 最後の1手", (unsigned)m.t31_hand_no);
    } else {
        std::snprintf(meta, sizeof(meta), "ハンド %u / 3 ・ %d / 20手", (unsigned)m.t31_hand_no,
                      m.t31.turns);
    }
    makeMeta(meta);

    int known = 0;
    for (int c = 0; c < 52; ++c) {
        if (m.t31.publicly_known[1] & (uint64_t(1) << c)) {
            ++known;
        }
    }
    // 「Jev＋端末AI」でも 256px に収まる長さにしてある（最長 23 単位 = 230px）
    char opp[64];
    std::snprintf(opp, sizeof(opp), "%s　非公開 %d枚", opponentName(), 3 - known);
    rectLabel(layout::kT31Opp, &ct_font_jp_20, CD_MUTED, opp);
    rectLabel(layout::kT31MarketT, &ct_font_jp_20, CD_MUTED,
              showHint() ? "場の3枚（共有）・交換は1枚だけ" : "場のカード（共有）");
    for (int j = 0; j < 3; ++j) {
        const core::Card c = m.t31.market[j];
        makeCard(layout::kT31Market[j], Face::Front, core::rank(c), core::suit(c),
                 j == s_draft_market, false, t31MarketCb, (void *)(intptr_t)j);
    }

    const int score = core::score31(m.t31.hands[0]);
    char human[64];
    if (waitingForJev()) {
        ui::thinkingCreate(s_content, Rect{80, 237, 320, 26}, kThinkThirty, 2);
    } else {
        if (!ct::canAct(m, 0) && s_step != Step::Idle) {
            std::snprintf(human, sizeof(human), "相手が考えています");
        } else {
            std::snprintf(human, sizeof(human), "あなた：%d点", score);
        }
        // 自分の得点も手札から作った私的情報
        registerPrivate(rectLabel(layout::kT31Human, &ct_font_jp_20, CD_TEXT, human));
    }
    for (int i = 0; i < 3; ++i) {
        const core::Card c = m.t31.hands[0][i];
        makeCard(layout::kT31Hand[i], Face::Front, core::rank(c), core::suit(c), i == s_draft_hand,
                 true, t31HandCb, (void *)(intptr_t)i);
    }

    const bool mine = ct::canAct(m, 0);
    const bool can_swap = mine && s_draft_hand >= 0 && s_draft_market >= 0;
    tableButton(0, layout::kAct2[0], "交換する", Role::DraftThirty, nullptr, can_swap, can_swap);
    if (m.phase == ct::Phase::ThirtyLast) {
        tableButton(1, layout::kAct2[1], "このまま\n比べる", Role::ActionId, "STAND", mine, false);
    } else {
        tableButton(1, layout::kAct2[1], "ここで勝負", Role::ActionId, "KNOCK", mine, false);
    }
}

void buildThirtyResult()
{
    ct::Match &m = match();
    makeTitle(kTableName[2]);
    // 見出しの行は 280px（半角 28 個ぶん）まで。終わり方は 5 文字に揃えてある
    const char *reason = m.t31.end == core::ThirtyOne::Natural ? "31点で終了"
                       : m.t31.end == core::ThirtyOne::Knocked ? "ノック終了"
                                                               : "20手で終了";
    char meta[72];
    std::snprintf(meta, sizeof(meta), "ハンド %u / 3 ・ %s", (unsigned)m.t31_hand_no, reason);
    makeMeta(meta);

    char opp[48];
    std::snprintf(opp, sizeof(opp), "%s：%d点", opponentName(), core::score31(m.t31.hands[1]));
    registerPrivate(rectLabel(layout::kT31OppScore, &ct_font_jp_20, CD_MUTED, opp));
    for (int i = 0; i < 3; ++i) {
        const core::Card c = m.t31.hands[1][i];
        makeCard(layout::kT31OppCard[i], Face::Front, core::rank(c), core::suit(c), false, true);
    }
    // 1 行に詰めると両端が切れるので 2 行にする（上 = このハンド、下 = 3 ハンドの勝ち数）
    char result[96];
    std::snprintf(result, sizeof(result), "あなた %d点 ・ %s\nこの試合 %u勝 %u敗 %u分",
                  core::score31(m.t31.hands[0]),
                  m.t31.winner == core::H ? "勝ち" : (m.t31.winner == core::AI ? "負け" : "引き分け"),
                  (unsigned)m.t31_wins[0], (unsigned)m.t31_wins[1], (unsigned)m.t31_wins[2]);
    registerPrivate(rectLabel(layout::kT31Result, &ct_font_jp_20, CD_TEXT, result));
    for (int i = 0; i < 3; ++i) {
        const core::Card c = m.t31.hands[0][i];
        makeCard(layout::kT31Hand[i], Face::Front, core::rank(c), core::suit(c), false, true);
    }

    const bool show_detail = s_sess->jev_valid && !s_sess->jev_hidden;
    tableButton(0, layout::kAct2[0], "内訳", Role::Detail, nullptr, show_detail, false);
    tableButton(1, layout::kAct2[1], m.t31_hand_no >= 3 ? "結果へ" : "次のハンド", Role::Next,
                nullptr, true, true);
}

// --- BACCARAT ------------------------------------------------------------------
const char *sideName(int outcome)
{
    return outcome == 0 ? "PLAYER" : (outcome == 1 ? "BANKER" : "引き分け");
}

void buildBaccaratTable()
{
    ct::Match &m = match();
    const bool open = m.variant == 0;
    const bool revealed = m.phase == ct::Phase::UnitResult;
    makeTitle(kTableName[3]);
    const char *outcome = m.bac_res.winner == core::DRAW ? "引き分け"
                        : (m.bac_res.winner == 0 ? "PLAYER勝" : "BANKER勝");
    // 公開後は「どちらが勝ったか」を見出しの行に上げ、下の 1 行は予想の当たり外れだけにする
    // （札の下端 330 と操作の行 350 のあいだは 20px しかないので、下は 1 行しか置けない）
    char meta[64];
    if (revealed) {
        std::snprintf(meta, sizeof(meta), "%s ・ %u/5 ・ %s", open ? "OPEN" : "CLASSIC",
                      (unsigned)m.bac_round, outcome);
    } else {
        std::snprintf(meta, sizeof(meta), "%s ・ ラウンド %u / 5", open ? "OPEN" : "CLASSIC",
                      (unsigned)m.bac_round);
    }
    makeMeta(meta);

    char plabel[48], blabel[48];
    if (revealed) {
        std::snprintf(plabel, sizeof(plabel), "PLAYER  /  手札A：%d点", m.bac_res.pt);
        std::snprintf(blabel, sizeof(blabel), "BANKER  /  手札B：%d点", m.bac_res.bt);
    } else {
        std::snprintf(plabel, sizeof(plabel), "PLAYER  /  手札A");
        std::snprintf(blabel, sizeof(blabel), "BANKER  /  手札B");
    }
    rectLabel(layout::kBacPlayerL, &ct_font_jp_20, CD_MUTED, plabel);
    rectLabel(layout::kBacBankerL, &ct_font_jp_20, CD_MUTED, blabel);

    // 配る順は P1 B1 P2 B2 (P3) (B3)。予想前は OPEN の 1 枚目だけ表向き
    const int pslot[3] = {0, 2, 4};
    const int bslot[3] = {1, 3, 5};
    for (int i = 0; i < 3; ++i) {
        Face pf = Face::Absent, bf = Face::Absent;
        if (revealed) {
            pf = i < m.bac_res.np ? Face::Front : Face::Absent;
            bf = i < m.bac_res.nb ? Face::Front : Face::Absent;
        } else if (i < 2) {
            pf = (open && i == 0) ? Face::Front : Face::Back;
            bf = (open && i == 0) ? Face::Front : Face::Back;
        }
        // 3 枚目は PLAYER が引いたときだけ 5 枚目が P3、そうでなければ 5 枚目が B3
        int pindex = pslot[i], bindex = bslot[i];
        if (revealed && i == 2) {
            pindex = 4;
            bindex = (m.bac_res.np == 3) ? 5 : 4;
        }
        if (pf != Face::Absent) {
            const core::Card c = m.bac_six[pindex];
            makeCard(layout::kBacPlayer[i], pf, core::rank(c), core::suit(c), false, false);
        }
        if (bf != Face::Absent) {
            const core::Card c = m.bac_six[bindex];
            makeCard(layout::kBacBanker[i], bf, core::rank(c), core::suit(c), false, false);
        }
    }

    if (revealed) {
        // 予想の当たり外れだけの短い 1 行（最長 22 単位 = 220px。枠は 304px）。
        // **人間を PLAYER、相手を BANKER の側に並べて書かない**（設計書 B1）
        char note[96];
        std::snprintf(note, sizeof(note), "あなた %s　相手 %s",
                      m.bac_pred.guess[0] == m.bac_res.winner ? "的中" : "外れ",
                      m.bac_pred.guess[1] == m.bac_res.winner ? "的中" : "外れ");
        rectLabel(layout::kBacNote, &ct_font_jp_20, CD_GOLD, note);
        const bool show_detail = s_sess->jev_valid && !s_sess->jev_hidden;
        tableButton(0, layout::kAct2[0], "予想の内訳", Role::Detail, nullptr, show_detail, false);
        tableButton(1, layout::kAct2[1], m.bac_round >= 5 ? "結果へ" : "次の\nラウンド", Role::Next,
                    nullptr, true, true);
        return;
    }

    const bool ready = ct::sealed(m, 1);
    const char *ready_note = showHint() ? "PLAYER/BANKER は札の側の名前"
                                        : "同じ札を見て、勝敗を予想";
    if (!ready && waitingForJev()) {
        ui::thinkingCreate(s_content, Rect{88, 324, 304, 26}, kThinkBaccarat, 1);
    } else {
        rectLabel(layout::kBacNote, &ct_font_jp_20, CD_MUTED,
                  ready ? ready_note : "相手が予想しています");
    }
    tableButton(0, layout::kAct3[0], "PLAYER\n勝ち", Role::ActionId, "PLAYER", ready, false);
    tableButton(1, layout::kAct3[1], "BANKER\n勝ち", Role::ActionId, "BANKER", ready, false);
    tableButton(2, layout::kAct3[2], "引き分け", Role::ActionId, "TIE", ready, false);
}

// --- 試合の結果 ----------------------------------------------------------------
void buildMatchResult()
{
    ct::Match &m = match();
    buildFrame();
    makeTitle("POKER TABLE");
    makeMeta("今回の対戦結果");
    const char *verdict = m.winner == core::H ? "あなたの勝ち"
                        : (m.winner == core::AI ? "相手の勝ち" : "引き分け");
    rectLabel(layout::kResTitle, &ct_font_jp_22, CD_TEXT, verdict);
    char line[64];
    std::snprintf(line, sizeof(line), "%s / %u%s完了", kTableName[(int)m.game],
                  (unsigned)m.completed_units,
                  m.game == ct::Game::Poker ? "ハンド"
                  : m.game == ct::Game::Thirty ? "ハンド" : "ラウンド");
    rectLabel(layout::kResGame, &ct_font_jp_20, CD_MUTED, line);

    char score[96];
    switch (m.game) {
    case ct::Game::Poker:
        std::snprintf(score, sizeof(score), "あなた %d pt\n相手 %d pt", m.scores[0], m.scores[1]);
        break;
    case ct::Game::Gops:
        std::snprintf(score, sizeof(score), "あなた %d 点 ／ 相手 %d 点\n消えた点 %d",
                      m.scores[0], m.scores[1], m.gops.burned);
        break;
    case ct::Game::Thirty:
        std::snprintf(score, sizeof(score), "あなた %d 勝 ／ 相手 %d 勝\n引き分け %u",
                      m.scores[0], m.scores[1], (unsigned)m.t31_wins[2]);
        break;
    default:
        std::snprintf(score, sizeof(score), "あなた %d 的中 ／ 相手 %d 的中\n5回中",
                      m.scores[0], m.scores[1]);
        break;
    }
    rectLabel(layout::kResScore, &ct_font_jp_22, CD_GOLD, score);
    const char *cls = opponentClass();
    rectLabel(layout::kResProv, &ct_font_jp_20, CD_MUTED,
              std::strcmp(cls, "mixed") == 0 ? "Jev＋端末AI  /  混合対戦"
              : (std::strcmp(cls, "jev") == 0 ? "JEV との対戦" : "端末AI との対戦"));
    tableButton(0, layout::kResLeft, "きろく", Role::Records, nullptr, true, false);
    tableButton(1, layout::kResRight, "もう一度", Role::Rematch, nullptr, true, true);
    tableButton(2, layout::kResBack, "ゲーム一覧へ", Role::Leave, nullptr, true, false);
}

void buildTable()
{
    if (s_sess == nullptr || !s_sess->in_match) {
        setView(View::Entry);
        return;
    }
    ct::Match &m = match();
    if (m.finished) {
        buildMatchResult();
        return;
    }
    buildFrame();
    const bool result = m.phase == ct::Phase::UnitResult;
    switch (m.game) {
    case ct::Game::Poker:
        if (ct::isHoldem(m)) {
            if (result) {
                buildHoldemResult();
            } else {
                buildHoldemTable();
            }
        } else if (result) {
            buildPokerResult();
        } else {
            buildPokerTable();
        }
        break;
    case ct::Game::Gops:
        if (result) {
            buildGopsResult();
        } else {
            buildGopsTable();
        }
        break;
    case ct::Game::Thirty:
        if (result) {
            buildThirtyResult();
        } else {
            buildThirtyTable();
        }
        break;
    default:
        buildBaccaratTable();
        break;
    }
    tableButton(5, layout::kCafeBtn, "カフェへ", Role::Cafe, nullptr, true, false);
}

// --- 確認・内訳・通信・カフェ ---------------------------------------------------
void confirmBody(char *out, size_t size)
{
    const ct::Match &m = match();
    if (s_ask_kind == AskKind::GoLocal) {
        std::snprintf(out, size, "残りの対戦は端末AIを使い、\n"
                                 "結果は混合対戦と記録します。\nJevには戻りません。");
        return;
    }
    if (s_ask_kind == AskKind::NewMatch) {
        std::snprintf(out, size, "いまの試合を終了して、\n新しく始めます。\n"
                                 "途中の手札は残りません。");
        return;
    }
    if (std::strncmp(s_ask_id, "DRAW:", 5) == 0) {
        const int count = __builtin_popcount((unsigned)std::atoi(s_ask_id + 5));
        if (count == 0) {
            std::snprintf(out, size, "今の5枚をすべて残します。\nカードは交換しません。");
        } else {
            std::snprintf(out, size, "%d枚を一度だけ交換します。\n捨てた札は今回戻りません。\n"
                                     "確定後は選び直せません。", count);
        }
        return;
    }
    if (std::strncmp(s_ask_id, "PLAY:", 5) == 0) {
        std::snprintf(out, size, "%sの札を出します。\nこの札は今回で使い切りです。\n"
                                 "相手の札は変更されません。", s_ask_id + 5);
        return;
    }
    if (std::strncmp(s_ask_id, "SWAP:", 5) == 0) {
        const int hi = s_ask_id[5] - '0', mi = s_ask_id[7] - '0';
        char a[ct::kCardNameMax], b[ct::kCardNameMax];
        ct::cardName(m.t31.hands[0][hi], a);
        ct::cardName(m.t31.market[mi], b);
        std::snprintf(out, size, "自分の%sと場の%sを交換します。\n"
                                 "場へ出した札は相手にも見えます。", a, b);
        return;
    }
    if (std::strcmp(s_ask_id, "KNOCK") == 0) {
        std::snprintf(out, size, "交換せずに勝負を宣言します。\n相手に最後の1手を渡した後、\n"
                                 "手札を比べます。");
        return;
    }
    if (std::strcmp(s_ask_id, "STAND") == 0) {
        std::snprintf(out, size, "交換せず、この手札のまま\n相手と得点を比べます。");
        return;
    }
    if (std::strcmp(s_ask_id, "FOLD") == 0) {
        std::snprintf(out, size, "このハンドを降ります。\n場の点数は相手が獲得し、\n"
                                 "双方の札は公開しません。");
        return;
    }
    if (std::strcmp(s_ask_id, "CHECK") == 0) {
        std::snprintf(out, size, "点を追加せずに続けます。");
        return;
    }
    if (std::strcmp(s_ask_id, "BET") == 0 || std::strcmp(s_ask_id, "CALL") == 0 ||
        std::strcmp(s_ask_id, "RAISE") == 0) {
        bool ok = false;
        const core::BetAction a = ct::betOf(s_ask_id, ok);
        const core::Street *street = ct::betStreet(m);   // ドローとホールデムで別物
        const int debit = (ok && street != nullptr) ? ct::betDebit(*street, 0, a) : 0;
        std::snprintf(out, size, "今回、追加する点数：%d点\n確定後の変更はできません。", debit);
        return;
    }
    // バカラの 3 択
    const int outcome = std::strcmp(s_ask_id, "PLAYER") == 0 ? 0
                      : (std::strcmp(s_ask_id, "BANKER") == 0 ? 1 : 2);
    std::snprintf(out, size, "%sと予想します。\nPLAYER・BANKERは\n手札の側の名前です。\n"
                             "人間・Jevとは別です。", sideName(outcome));
}

void buildConfirm()
{
    buildFrame();
    makeTitle("内容を確認");
    char body[224];
    confirmBody(body, sizeof(body));
    rectLabel(layout::kAskTitle, &ct_font_jp_22, CD_GOLD,
              s_ask_kind == AskKind::Action ? "この内容で決定しますか？" : "よろしいですか？");
    rectLabel(layout::kAskBody, &ct_font_jp_20, CD_TEXT, body);
    rectButton(layout::kAct2[0], "選び直す", Act::AskNo);
    rectButton(layout::kAct2[1], "決定", Act::AskYes, true, true);
}

void buildDetail()
{
    buildFrame();
    makeTitle("JEV の判断");
    makeMeta("公開可能なハンドのみ");
    const Session &st = *s_sess;
    if (!st.jev_valid || st.jev_hidden) {
        rectLabel(layout::kDetMean, &ct_font_jp_22, CD_TEXT, "内訳はありません");
        rectLabel(layout::kDetValues, &ct_font_jp_20, CD_MUTED,
                  "端末AIが選んだ手、または\n一択で自動的に\n決まった手です。");
    } else {
        rectLabel(layout::kDetMean, &ct_font_jp_20, CD_MUTED, "行動候補の選択値");
        char list[224];
        size_t at = 0;
        for (uint8_t i = 0; i < st.jev_count && at + 24 < sizeof(list); ++i) {
            at += (size_t)std::snprintf(list + at, sizeof(list) - at, "%s  %u%%%s\n",
                                        st.jev_names[i], (unsigned)st.jev_percent[i],
                                        std::strcmp(st.jev_names[i], st.jev_action) == 0 ? " <" : "");
        }
        if (st.jev_total > st.jev_count) {
            std::snprintf(list + at, sizeof(list) - at, "ほか %u 件",
                          (unsigned)(st.jev_total - st.jev_count));
        }
        rectLabel(layout::kDetValues, &ct_font_jp_20, CD_GOLD, list);
    }
    rectLabel(layout::kDetNote, &ct_font_jp_20, CD_MUTED, "勝率や心の声ではありません");
    rectButton(layout::kDetBack, "戻る", Act::DetailBack, true, true);
}

void buildNetwork()
{
    buildFrame();
    makeTitle("接続を確認してください");
    const char *why = nullptr;
    if (std::strcmp(s_req_reason, "offline") == 0) {
        why = "いまは通信できません";
    } else if (std::strcmp(s_req_reason, "busy") == 0) {
        why = "ほかの通信が終わりません";
    } else if (std::strcmp(s_req_reason, "too large") == 0) {
        why = "依頼が大きすぎました";
    } else if (std::strncmp(s_req_reason, "none:DAILY_LIMIT", 16) == 0) {
        why = "今日のJevの上限に達しました";
    } else if (std::strncmp(s_req_reason, "none:NO_KEY", 11) == 0) {
        why = "Jevの鍵が設定されていません";
    } else if (std::strcmp(s_req_reason, "timeout") == 0) {
        why = "返事が間に合いませんでした";
    } else if (std::strcmp(s_req_reason, "illegal reply") == 0 ||
               std::strcmp(s_req_reason, "bad reply") == 0 ||
               std::strncmp(s_req_reason, "none:", 5) == 0) {
        why = "返事の内容を確認できません";
    }
    char body[224];
    std::snprintf(body, sizeof(body), "応答を確認できません。\n"
                                      "同じ依頼を照会します。\n札や点数は変更しません。%s%s",
                  why != nullptr ? "\n" : "", why != nullptr ? why : "");
    rectLabel(layout::kNetBody, &ct_font_jp_20, CD_TEXT, body);
    rectButton(layout::kNetBtn[0], "もう一度", Act::NetRetry, true, true);
    rectButton(layout::kNetBtn[1], "端末AIで続行", Act::NetLocal);
    rectButton(layout::kNetBtn[2], "カフェへ", Act::NetCafe);
}

void buildPaused()
{
    makeTitle("一時停止中");
    rectLabel(layout::kPanelBody, &ct_font_jp_22, CD_TEXT,
              "手札を隠しています。\nカフェへ戻れます。");
    rectButton(layout::kPanel[0], "再開する", Act::PausedResume, true, true);
    rectButton(layout::kPanel[1], "＋1杯", Act::CafeCoffee);
    rectButton(layout::kPanel[2], "やめる", Act::PausedQuit);
}

void buildCafe()
{
    makeTitle("カフェ");
    rectLabel(layout::kPanelBody, &ct_font_jp_22, CD_TEXT,
              "コーヒーを取りましたか？\n試合はこのまま残ります。");
    rectButton(layout::kPanel[0], "＋1杯", Act::CafeCoffee, true, true);
    rectButton(layout::kPanel[1], "もどる", Act::CafeBack);
    rectButton(layout::kPanel[2], "HOMEへ", Act::CafeHome);
}

// ---------------------------------------------------------------------------
// 作り直し
// ---------------------------------------------------------------------------
void rebuild()
{
    if (s_content == nullptr || s_sess == nullptr) {
        return;
    }
    s_private_count = 0;
    s_busy = false;     // 画面を作り直したら、また 1 回だけ確定できる
    forgetDrawWidgets();
    for (auto &b : s_btn) {
        b.role = Role::None;
        b.id[0] = '\0';
    }
    lv_obj_clean(s_content);
    switch (s_view) {
    case View::Entry:     buildEntry(); break;
    case View::Player:    buildPlayer(); break;
    case View::Records:   buildRecords(); break;
    case View::Variant:   buildVariant(); break;
    case View::Opponent:  buildOpponent(); break;
    case View::Tutorial:  buildTutorial(); break;
    case View::Table:     buildTable(); break;
    case View::Confirm:   buildConfirm(); break;
    case View::Detail:    buildDetail(); break;
    case View::Remaining: buildRemaining(); break;
    case View::Network:   buildNetwork(); break;
    case View::Paused:    buildPaused(); break;
    case View::Cafe:      buildCafe(); break;
    }
}

// ---------------------------------------------------------------------------
// 入力
// ---------------------------------------------------------------------------
void avatarCb(lv_event_t *e)
{
    if (millis() - s_view_ms < kInputLatchMs) {
        return;
    }
    s_sess->slot = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    s_records_return = View::Player;
    setView(View::Records);
}

void pokerCardCb(lv_event_t *e)
{
    if (match().phase != ct::Phase::PokerDraw || millis() - s_view_ms < kInputLatchMs) {
        return;
    }
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= 5) {
        return;
    }
    // 仮選択を変えるだけ。交換も得点も変わらない（設計一式 01 §6）
    s_draft_mask ^= 1 << i;
    const bool sel = (s_draft_mask & (1 << i)) != 0;

    // **この場で描き替える**（画面ごと作り直すと、押したカード自身を消してしまう）。
    // 押した札の枠・持ち上げ・印、枚数、確定ボタンの文言が同じ描画で揃う
    if (s_draw_card[i] != nullptr) {
        lv_obj_set_y(s_draw_card[i], (lv_coord_t)(layout::kPokHand[i].y - (sel ? 4 : 0)));
        lv_obj_set_style_border_color(s_draw_card[i], sel ? CD_GOLD : CD_INK, 0);
        lv_obj_set_style_border_width(s_draw_card[i], sel ? 3 : 1, 0);
    }
    if (s_draw_mark[i] != nullptr) {
        if (sel) {
            lv_obj_clear_flag(s_draw_mark[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_draw_mark[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_draw_count != nullptr) {
        char n[16];
        std::snprintf(n, sizeof(n), "%d 枚", __builtin_popcount((unsigned)s_draft_mask));
        lv_label_set_text(s_draw_count, n);
    }
    if (s_draw_confirm != nullptr) {
        lv_label_set_text(s_draw_confirm, s_draft_mask == 0 ? "交換しない" : "交換する");
        lv_obj_center(s_draw_confirm);
    }
}

void gopsCardCb(lv_event_t *e)
{
    if (match().phase != ct::Phase::GopsBid || millis() - s_view_ms < kInputLatchMs) {
        return;
    }
    s_draft_bid = (int)(intptr_t)lv_event_get_user_data(e);
    s_dirty = true;
}

void t31HandCb(lv_event_t *e)
{
    if (!ct::canAct(match(), 0) || millis() - s_view_ms < kInputLatchMs) {
        return;
    }
    s_draft_hand = (int)(intptr_t)lv_event_get_user_data(e);
    s_dirty = true;
}

void t31MarketCb(lv_event_t *e)
{
    if (!ct::canAct(match(), 0) || millis() - s_view_ms < kInputLatchMs) {
        return;
    }
    s_draft_market = (int)(intptr_t)lv_event_get_user_data(e);
    s_dirty = true;
}

// 読み物として開く（前へ / 次へ / 閉じる だけ）
void startTutorial(int game, View back)
{
    s_tut_game = (int8_t)game;
    // POKER 卓はいま選んでいる種類の説明を出す（入口から開いたときは既定のホールデム）
    s_tut_holdem = (game == 0) ? (s_pick_variant == 0) : true;
    s_tut_page = 0;
    s_tut_first_play = false;
    s_tut_return = back;
    setView(View::Tutorial);
}

// 初めてその卓を始めるときの説明（スキップ・次回から表示しない つき）。
// 終わったら（はじめる でも スキップ でも）そのまま試合が始まる
void startFirstPlayGuide(int game)
{
    s_tut_game = (int8_t)game;
    s_tut_holdem = (game == 0) ? (s_pick_variant == 0) : true;
    s_tut_page = 0;
    s_tut_first_play = true;
    s_tut_dont_show = true;     // 既定はオン
    s_tut_return = View::Opponent;
    setView(View::Tutorial);
}

// 説明を終えて試合へ。「次回から表示しない」がオンならその卓の印を立てる
void finishFirstPlayGuide()
{
    const size_t slot = seenSlot((int)s_pick_game, (int)s_pick_variant);
    if (s_tut_dont_show) {
        cards::store::markSeen(slot);
    }
    // この電源セッションでは、最初の 1 局だけ卓の上に一言を出す
    s_guided = (uint8_t)(s_guided | (1u << slot));
    s_tut_first_play = false;
    beginMatch();
}

void startChosenMatch()
{
    // はじめての卓（ホールデムとドローは別々）なら、配る前に説明をはさむ
    if (!cards::store::seenFlag(seenSlot((int)s_pick_game, (int)s_pick_variant))) {
        startFirstPlayGuide(s_pick_game);
        return;
    }
    beginMatch();
}

void nextStep()
{
    // 種類のあるゲームは種類 → 相手、無ければ相手だけ
    // 種類を選ぶのは POKER（ホールデム / ドロー）・GOPS（7 / 13）・BACCARAT（OPEN / CLASSIC）。
    // THIRTY-ONE だけ種類がひとつ
    setView(s_pick_game != 2 ? View::Variant : View::Opponent);
}

void tableBtnCb(lv_event_t *e)
{
    const int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_sess == nullptr || index < 0 || index >= 6 || millis() - s_view_ms < kInputLatchMs) {
        return;
    }
    const TableBtn btn = s_btn[index];
    ct::Match &m = match();
    char id[ct::kActionIdMax];
    switch (btn.role) {
    case Role::ActionId:
        askAction(AskKind::Action, btn.id);
        break;
    case Role::DraftPoker:
        std::snprintf(id, sizeof(id), "DRAW:%d", s_draft_mask);
        askAction(AskKind::Action, id);
        break;
    case Role::DraftGops:
        if (s_draft_bid > 0) {
            std::snprintf(id, sizeof(id), "PLAY:%d", s_draft_bid);
            askAction(AskKind::Action, id);
        }
        break;
    case Role::DraftThirty:
        if (s_draft_hand >= 0 && s_draft_market >= 0) {
            std::snprintf(id, sizeof(id), "SWAP:%d:%d", s_draft_hand, s_draft_market);
            askAction(AskKind::Action, id);
        }
        break;
    case Role::ClearSel:
        s_draft_mask = 0;
        s_dirty = true;
        break;
    case Role::Remaining:
        setView(View::Remaining);
        break;
    case Role::Detail:
        setView(View::Detail);
        break;
    case Role::Next:
        if (ct::nextUnit(m, hwRandom, nullptr)) {
            if (m.fault != nullptr) {
                Serial.printf("[CARDS] %s\n", m.fault);
                m.fault = nullptr;
            }
            resetDrafts();
            s_sess->jev_valid = false;
            setView(View::Table);
            syncAi();
        }
        break;
    case Role::Rematch:
        s_sess->in_match = false;
        s_pick_game = s_sess->game;
        s_pick_variant = s_sess->variant;
        beginMatch();
        break;
    case Role::Records:
        s_records_return = View::Table;
        setView(View::Records);
        break;
    case Role::Leave:
        leaveScreen(false);
        break;
    case Role::Cafe:
        s_cafe_return = View::Table;
        setView(View::Cafe);
        break;
    case Role::PagePrev:
        s_page = (uint8_t)(s_page == 0 ? 0 : s_page - 1);
        s_dirty = true;
        break;
    case Role::PageNext:
        ++s_page;
        s_dirty = true;
        break;
    default:
        break;
    }
}

void actionCb(lv_event_t *e)
{
    const Act act = (Act)(intptr_t)lv_event_get_user_data(e);
    if (s_sess == nullptr) {
        return;
    }
    switch (act) {
    case Act::EntryTable0:
    case Act::EntryTable1:
    case Act::EntryTable2:
    case Act::EntryTable3:
        s_pick_game = (uint8_t)((int)act - (int)Act::EntryTable0);
        s_pick_variant = 0;
        if (resumable()) {
            askAction(AskKind::NewMatch, nullptr);
        } else {
            setView(View::Player);
        }
        break;
    case Act::EntryResume:
        if (resumable()) {
            // 再開は必ずひと休みを一度はさむ（手札をいきなり出さない）
            s_paused_return = View::Table;
            setView(View::Paused);
        }
        break;
    case Act::EntryHow:
        // 入口からは 4 卓ぶん全部、相手選びからはその卓のページだけ
        if (s_view == View::Opponent) {
            startTutorial((int)s_pick_game, View::Opponent);
        } else {
            startTutorial(-1, View::Entry);
        }
        break;
    case Act::EntryAgain:
        // 4 卓ぶんの「見た」印を消して、次に始めるときまた自動で説明を出す
        cards::store::clearSeen();
        s_guided = 0;
        ui::showToast(s_screen, "次から説明を表示します");
        s_dirty = true;
        break;
    case Act::EntryBack:
        leaveScreen(false);
        return;

    case Act::PlayerGuest:
        s_sess->slot = cards::store::kGuestSlot;
        s_records_return = View::Player;
        setView(View::Records);
        break;
    case Act::PlayerBack:
        setView(View::Entry);
        break;

    case Act::RecPlay:
        nextStep();
        break;
    case Act::RecBack:
        setView(s_records_return);
        break;

    case Act::Variant0:
        s_pick_variant = 0;
        setView(View::Opponent);
        break;
    case Act::Variant1:
        s_pick_variant = 1;
        setView(View::Opponent);
        break;
    case Act::VariantBack:
        setView(View::Records);
        break;

    case Act::OppJev:
        s_sess->want_jev = true;
        startChosenMatch();     // はじめての卓なら、配る前に説明をはさむ
        break;
    case Act::OppLocal:
        s_sess->want_jev = false;
        startChosenMatch();
        break;
    case Act::OppBack:
        setView(s_pick_game != 2 ? View::Variant : View::Records);
        break;

    case Act::TutPrev:
        if (s_tut_page > 0) {
            --s_tut_page;
        }
        s_dirty = true;
        break;
    case Act::TutNext:
        if (s_tut_page + 1 < guideCount(s_tut_game)) {
            ++s_tut_page;
            s_dirty = true;
        } else if (s_tut_first_play) {
            finishFirstPlayGuide();     // 「はじめる」
        } else {
            setView(s_tut_return);      // 「閉じる」
        }
        break;
    case Act::TutToggle:
        s_tut_dont_show = !s_tut_dont_show;
        s_dirty = true;
        break;
    case Act::TutBack:
        if (s_tut_first_play) {
            finishFirstPlayGuide();     // 「スキップ」もそのまま試合へ
        } else {
            setView(s_tut_return);
        }
        break;

    case Act::AskYes: {
        const AskKind kind = s_ask_kind;
        if (millis() - s_view_ms < kInputLatchMs) {
            break;
        }
        if (kind == AskKind::NewMatch) {
            abortMatch();
            setView(View::Player);
        } else if (kind == AskKind::GoLocal) {
            // 端末 AI への切替は不可逆（設計一式 02 §6）
            s_sess->local_only = true;
            cancelJevRequest();
            Serial.println("[CARDS] switched to the on-device AI for this match");
            setView(View::Table);
            setStep(Step::Idle);
            syncAi();
        } else {
            commitHuman(s_ask_id);
        }
        break;
    }
    case Act::AskNo:
        setView(s_ask_return);
        break;

    case Act::DetailBack:
        setView(View::Table);
        break;
    case Act::RemainBack:
        setView(View::Table);
        break;

    case Act::NetRetry:
        setView(View::Table);
        beginJevRequest(1);     // 同じ観測を送り直す（GAS は控えの行動を返す）
        break;
    case Act::NetLocal:
        askAction(AskKind::GoLocal, nullptr);
        break;
    case Act::NetCafe:
        s_cafe_return = View::Network;
        setView(View::Cafe);
        break;

    case Act::PausedResume:
        setView(s_paused_return);
        syncAi();
        break;
    case Act::PausedQuit:
        leaveScreen(false);
        return;

    case Act::CafeCoffee:
        // HOME の「+1」と同じ処理（記録も通知も同じ経路を通る）
        home::addOneCup();
        ui::showToast(s_screen, "1杯を記録しました");
        break;
    case Act::CafeBack:
        setView(s_cafe_return);
        syncAi();
        break;
    case Act::CafeHome:
        leaveScreen(true);
        return;
    }
}

// ---------------------------------------------------------------------------
// 50ms ごとの処理
// ---------------------------------------------------------------------------
bool playingView(View v)
{
    return v != View::Cafe && v != View::Paused;
}

// 卓が見えていない画面の間は試合を進めない（設計一式 03 §5）。
// Jev の返事は依頼箱に置いたまま（gasTakeResult を呼ばない）なので消えない
bool gameSuspended()
{
    return s_view == View::Cafe || s_view == View::Paused || s_view == View::Detail ||
           s_view == View::Remaining || s_view == View::Tutorial || s_view == View::Confirm;
}

void tickCb(lv_timer_t *t)
{
    (void)t;
    if (s_sess == nullptr) {
        return;
    }

    if (gameSuspended()) {
        // 時計だけ進めないでおく（戻ったところから続く）
        s_step_ms = millis();
        s_req_ms = millis();
        s_send_since_ms = millis();
        s_result_since_ms = millis();
    } else {
        switch (s_step) {
        case Step::Think:
            if (millis() - s_step_ms >= kThinkMs) {
                char id[ct::kActionIdMax];
                if (ct::localActionId(match(), 1, s_sess->roll, id, sizeof(id))) {
                    Serial.printf("[CARDS] req=- -> local %s\n", logId(id));
                    applyAiAction(id, "local", 0);
                } else {
                    setStep(Step::Idle);
                }
            }
            break;
        case Step::Wait:
            pollJev();
            break;
        default:
            break;
        }
        if (s_sess->in_match && match().finished && !s_sess->counted) {
            countMatch("completed");
            s_dirty = true;
        }
        if (s_view == View::Table) {
            syncAi();
        }
        if (s_result_pending) {
            trySendResult();
        }
    }

    if (s_dirty) {
        rebuild();
        s_dirty = false;
        return;
    }
    // 誰も待っていない返事（見切ったあとの依頼・結果の投げっぱなし）は受け取って捨てる
    if (s_step != Step::Wait && !gameSuspended()) {
        if (net::gasTakeResult(s_sess->reply)) {
            Serial.printf("[CARDS] req=%lu reply %s %lums (unwatched)\n",
                          (unsigned long)s_sess->reply.req, s_sess->reply.ok ? "ok" : "failed",
                          (unsigned long)s_sess->reply.elapsed_ms);
        }
    }

    // 120 秒さわられなければ「ひと休み」を重ねる（相手を待っている間は重ねない）
    if (playingView(s_view) && s_step == Step::Idle &&
        lv_disp_get_inactive_time(nullptr) > kIdlePauseMs) {
        s_paused_return = s_view;
        setView(View::Paused);
    }
}

// ---------------------------------------------------------------------------
// 画面の生成・破棄
// ---------------------------------------------------------------------------
void screenDeletedCb(lv_event_t *e)
{
    // 画面の破棄は遷移アニメーションの完了時なので、その間に次の画面が
    // 作られていることがある。古い画面の後始末で新しい画面を壊さないよう照合する
    if (lv_event_get_target(e) != s_screen) {
        return;
    }
    display::setGameActive(false);
    if (s_tick != nullptr) {
        lv_timer_del(s_tick);
        s_tick = nullptr;
    }
    net::gasCancel();
    // **試合（PSRAM）は手放さない。** 同じ電源セッションの中なら「つづきから」で戻れる
    s_step = Step::Idle;
    s_send_pending = false;
    s_result_pending = false;
    s_result_json[0] = '\0';
    s_attempt = 0;
    s_dirty = false;
    s_private_count = 0;
    forgetDrawWidgets();
    s_screen = nullptr;
    s_content = nullptr;
}

const char *viewName(View v)
{
    switch (v) {
    case View::Entry:     return "entry";
    case View::Player:    return "player";
    case View::Records:   return "records";
    case View::Variant:   return "variant";
    case View::Opponent:  return "opponent";
    case View::Tutorial:  return "tutorial";
    case View::Table:     return "table";
    case View::Confirm:   return "confirm";
    case View::Detail:    return "detail";
    case View::Remaining: return "remaining";
    case View::Network:   return "network";
    case View::Paused:    return "paused";
    default:              return "cafe";
    }
}

}  // namespace

// ---------------------------------------------------------------------------
lv_obj_t *createGameScreen()
{
    if (s_tick != nullptr) {
        lv_timer_del(s_tick);
        s_tick = nullptr;
    }
    display::setGameActive(true);

    lv_obj_t *scr = ui::makeScreen();
    lv_obj_set_style_bg_color(scr, CD_BACKDROP, 0);
    s_screen = scr;
    s_content = lv_obj_create(scr);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_pos(s_content, 0, 0);
    lv_obj_set_size(s_content, ui::kScreenSize, ui::kScreenSize);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, screenDeletedCb, LV_EVENT_DELETE, nullptr);

    if (s_sess == nullptr) {
        // 試合は PSRAM に 1 回だけ確保し、電源が入っている間ずっと持つ（計画 §4）
        void *raw = heap_caps_malloc(sizeof(Session), MALLOC_CAP_SPIRAM);
        bool on_psram = raw != nullptr;
        if (raw == nullptr) {
            raw = heap_caps_malloc(sizeof(Session), MALLOC_CAP_8BIT);
        }
        if (raw == nullptr) {
            Serial.println("[CARDS] no memory for the match");
            ui::makeRectLabel(s_content, layout::kNetBody, &ct_font_jp_22, CD_TEXT,
                              "メモリーが足りません");
            ui::makeBackButton(scr, "もどる");
            return scr;
        }
        s_sess = new (raw) Session();
        Serial.printf("[CARDS] session %u bytes on %s\n", (unsigned)sizeof(Session),
                      on_psram ? "PSRAM" : "internal RAM");
    }

    cards::store::loadStats();
    cards::store::loadSeen();
    // 前回の残り（遅れて届いた返事）を捨ててから始める
    net::gasCancel();
    net::gasTakeResult(s_sess->reply);

    s_view = View::Entry;
    s_cafe_return = View::Entry;
    s_paused_return = View::Table;
    s_ask_return = View::Table;
    s_tut_return = View::Entry;
    s_step = Step::Idle;
    s_busy = false;
    s_attempt = 0;
    s_send_pending = false;
    s_result_pending = false;
    s_result_json[0] = '\0';
    s_req_reason[0] = '\0';
    s_pick_game = s_sess->in_match ? s_sess->game : 0;
    s_pick_variant = s_sess->in_match ? s_sess->variant : 0;
    s_tut_game = -1;
    s_tut_page = 0;
    s_tut_first_play = false;
    s_tut_dont_show = true;
    resetDrafts();
    s_view_ms = millis();

    rebuild();
    s_dirty = false;

    s_tick = lv_timer_create(tickCb, 50, nullptr);
    return scr;
}

// いま画面に私的な札（ポーカー・31 の手札、公開した双方の手札）が出ているか。
// main.cpp の開発用コマンドが、出ている間はスクリーンショットと画面切替を断る
bool privateOnScreen()
{
    return s_screen != nullptr && s_private_count > 0;
}

void debugPrintPublicState()
{
    if (s_screen == nullptr || s_sess == nullptr) {
        Serial.println("[CARDS] view=-");
        return;
    }
    const ct::Match &m = match();
    char slot[8];
    if (isGuest()) {
        std::snprintf(slot, sizeof(slot), "guest");
    } else {
        std::snprintf(slot, sizeof(slot), "p%u", (unsigned)s_sess->slot);
    }
    // 試合がないあいだは前の試合の数字を出さない（すべて 0 / '-'）
    const bool live = s_sess->in_match;
    int human_score = 0, ai_score = 0;
    if (live) {
        ct::liveScores(m, human_score, ai_score);      // 途中でも読める、いまの得点
    }
    // はじめての説明を見た卓（poker|gops|31|bac の順に 1 / 0）
    // poker-holdem | gops | 31 | bac | poker-draw の 5 桁
    const uint8_t seen = cards::store::seenFlags();
    char seen_text[cards::store::kSeenSlots + 1];
    for (size_t i = 0; i < cards::store::kSeenSlots; ++i) {
        seen_text[i] = (seen & (1u << i)) ? '1' : '0';
    }
    seen_text[cards::store::kSeenSlots] = '\0';
    // **手札と、公開前の相手の選択は出さない。** 公開されている数字だけを出す
    Serial.printf("[CARDS] view=%s game=%s variant=%s slot=%s unit=%u/%d phase=%s rev=%lu "
                  "score=%d-%d provider=%s pending=%u try=%u local=%u seen=%s\n",
                  viewName(s_view), live ? ct::gameId(m.game) : "-",
                  live ? ct::variantId(m) : "-", slot,
                  (unsigned)(live ? ct::unitNo(m) : 0), live ? ct::totalUnits(m) : 0,
                  live ? ct::phaseId(m.phase) : "-", (unsigned long)(live ? m.revision : 0),
                  human_score, ai_score, live ? opponentClass() : "-",
                  s_step == Step::Wait ? 1u : 0u, (unsigned)s_attempt,
                  s_sess->local_only ? 1u : 0u, seen_text);
}

}  // namespace cards
