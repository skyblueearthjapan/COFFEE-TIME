#include "WerewolfContent.h"

#include <cstring>

namespace coffee { namespace wolf { namespace content {

const StringEntry kStrings[] = {
    {"game.subtitle", "3〜10人で、ひと晩の読み合い。"},
    {"menu.enter", "みんなで人狼"},
    {"menu.start", "人数を選んで始める"},
    {"menu.help", "遊び方"},
    {"menu.back", "カフェへ"},
    {"menu.settings", "設定"},
    {"setup.title", "何人で遊びますか？"},
    {"setup.body", "{players}人で遊びます。\n1番から時計回りに着席。\nゲーム中は席を変えません。"},
    {"setup.check", "人数と番号を確認した"},
    {"setup.privacy", "画面は自分側に向けよう。\n覗かない・撮らない。\n役職はゲームの中だけ。"},
    {"handoff.night.title", "{seat}番の人の番です"},
    {"handoff.night.body", "前の画面は隠れています。\n{seat}番の人へ渡してください。\n画面を自分側に向けてね。"},
    {"handoff.receive", "受け取りました"},
    {"handoff.ready", "指を一度離してください"},
    {"role.title", "自分の役職を確認"},
    {"role.cover", "押している間だけ表示。\n指を離すと隠れます。"},
    {"role.hold", "押して見る"},
    {"role.next", "覚えました"},
    {"role.wolf.name", "人狼"},
    {"role.wolf.body", "選ばれずに逃げ切ろう。\nほかの役を名乗ってもOK。\n決選も同票なら引き分け。\n今夜の襲撃はありません。"},
    {"role.seer.name", "占い師"},
    {"role.seer.body", "人か伏せ札を1つ選び、\n人狼かどうか調べます。\n結果を話すかは自由です。\n画面で証明しないでね。"},
    {"role.villager.name", "村人"},
    {"role.villager.body", "人狼を見つければ勝ち。\n誰も人狼でないと思ったら\n「人狼はいない」に投票。\n夜の選択に能力はないよ。"},
    {"night.target.title", "調べる対象を選ぼう"},
    {"night.target.note", "全員が同じ操作をします"},
    {"night.target.confirm", "{target_label}を選びますか？\n決定後は変えられません。"},
    {"night.target.ok", "この対象にする"},
    {"common.change", "選び直す"},
    {"common.next", "次へ"},
    {"night.result.title", "夜の確認"},
    {"night.result.hold", "押して確認"},
    {"night.result.wolf", "{target_label}：\n人狼です。\n結果を覚えてください。"},
    {"night.result.not_wolf", "{target_label}：\n人狼ではありません。\n結果を覚えてください。"},
    {"night.result.none", "確認できました。\nあなたへの追加情報は\nありません。"},
    {"night.result.next", "確認しました"},
    {"night.done.title", "秘密を隠しました"},
    {"night.done.body", "画面が隠れたことを確認。\n次へ進んで渡してください。"},
    {"night.done.last", "端末をテーブルへ戻そう。\nここからは、みんなで会話。"},
    {"handoff.pass", "次の人へ"},
    {"night.done.day", "朝の案内へ"},
    {"day.ready.title", "朝になりました"},
    {"day.ready.body", "人狼は参加者か伏せ札に。\n画面を見せずに話そう。\n誰でも役を偽ってOK。"},
    {"day.start", "話し合いを始める"},
    {"day.time_up", "話し合いの時間です。\nまとまったら投票しよう。"},
    {"day.timer_ended", "時間になりました。\n投票を始めてください。"},
    {"day.extension", "+1分"},
    {"day.extension.used", "延長済み"},
    {"day.finish", "投票へ"},
    {"day.finish.confirm", "全員、投票してよいですか？\n戻れば話し合いを続けます。"},
    {"day.prompt.1", "順番に一言ずつ話そう。"},
    {"day.prompt.2", "聞いてみたいことはある？"},
    {"day.prompt.3", "投票する理由を考えよう。"},
    {"vote.ready.title", "秘密の投票をします"},
    {"vote.ready.body", "1番から順に回します。\n自分以外の人か、\n「人狼はいない」を選ぼう。\n全員が終わるまで秘密です。"},
    {"vote.begin", "1番から投票"},
    {"vote.handoff.title", "{seat}番の人へ渡そう"},
    {"vote.handoff.body", "ほかの人の画面を見ずに\n{seat}番の人が受け取ってね。"},
    {"vote.choose.title", "誰を選びますか？"},
    {"vote.confirm.cover", "押して投票先を確認"},
    {"vote.confirm.target", "{target_label}へ投票。\n決定すると変更できません。"},
    {"vote.confirm.commit", "投票を確定"},
    {"vote.done.title", "投票を隠しました"},
    {"vote.done.body", "投票先は表示しません。\n次の人へ渡してください。"},
    {"vote.done.last", "みんなの投票が揃いました。\n端末をテーブルに戻そう。"},
    {"vote.self", "自分"},
    {"vote.ineligible", "対象外"},
    {"vote.progress", "投票済み {count}/{players}"},
    {"runoff.title", "最多票が並びました"},
    {"runoff.body", "最多票の候補だけで決選。\n{seconds}秒、話し合おう。\n全員がもう一度投票します。"},
    {"runoff.candidates", "最多票の候補"},
    {"runoff.start", "決選の話し合い"},
    {"runoff.no_extend", "決選は延長なしです"},
    {"runoff.vote.note", "候補以外と自分は選べません"},
    {"final.ready.title", "みんなで答え合わせ"},
    {"final.ready.body", "端末を中央に置いてね。\n最後の人だけで見ないで\n一緒に結果を開こう。"},
    {"final.reveal", "みんなで結果を見る"},
    {"result.village.title", "村チームの勝ち！"},
    {"result.village.body", "人狼を見つけました。\n占い師と村人の勝ちです。"},
    {"result.wolf.title", "人狼チームの勝ち！"},
    {"result.wolf.body", "人狼が選ばれず、\n逃げ切りました。"},
    {"result.draw.title", "引き分け！"},
    {"result.draw.body", "決選でも票が並びました。\n最後まで分からなかったね。"},
    {"result.selected", "選ばれたのは {selected_label}"},
    {"result.no_selected", "決選でも一つに決まりませんでした"},
    {"result.roles", "参加者の役職"},
    {"result.votes", "投票のふり返り"},
    {"result.seer", "本当の占い結果"},
    {"result.vote_row", "{seat}番 → {target_label}"},
    {"result.role_row", "{seat}番：{role}"},
    {"result.seer_row", "{seat}番が{target_label}を確認"},
    {"result.again", "もう一回"},
    {"result.exit", "カフェへ"},
    {"result.rematch_confirm", "人数と番号を確認します。\n人数はここで変えられます。\n役職は新しく配ります。"},
    {"pause.title", "ゲームを一時停止"},
    {"pause.body", "秘密は隠れています。\n再開は同じ本人が操作。"},
    {"pause.resume", "ゲームへ戻る"},
    {"pause.coffee", "コーヒーを記録"},
    {"pause.home", "HOMEへ"},
    {"pause.abort", "無効にして終了"},
    {"pause.resume.owner", "{seat}番の本人ですか？\n指を離してから再開します。"},
    {"pause.home.notice", "人狼ゲームを中断中"},
    {"abort.confirm", "このゲームを無効にします。\n役職と投票は公開せずに\n消去します。よいですか？"},
    {"abort.title", "このゲームは無効です"},
    {"abort.body", "勝ち負けは付きません。\n全員で準備してやり直そう。"},
    {"abort.privacy", "秘密が見えてしまったら\n無理に続けずやり直そう。"},
    {"abort.reboot", "途中で電源が切れました。\n前のゲームは再開しません。\n役職を配り直して遊ぼう。"},
    {"abort.timeout", "45分でゲームを終えます。\n勝ち負けは付きません。"},
    {"error.storage", "開始の準備ができません。\n記録領域を確認してね。"},
    {"error.random", "役職を配る準備が必要です。\n担当者に知らせてください。"},
    {"error.display", "表示の安全確認が必要です。\n秘密を表示せず停止します。"},
    {"error.touch", "タッチを確認できません。\n秘密を隠して停止します。"},
    {"error.stale", "古い操作は受け付けません"},
    {"settings.title", "人狼の設定"},
    {"settings.sound", "公開画面の操作音"},
    {"settings.silent", "秘密の画面は常に無音"},
    {"settings.battery", "充電を確認してから始めよう"},
    {"settings.no_meter", "電池残量の数値は未確認です"},
    {"menu.active_other", "別のゲームを中断中です。\n保存して終了してください。"},
    {"menu.active_wolf", "人狼を再開するか、\n無効にして終了してね。"},
    {"help.return", "元の画面へ"},
    {"common.yes", "はい"},
    {"common.no", "戻る"},
    {"setup.range", "3〜10人で遊べます。\n2人以下では始められません。"},
    {"setup.pool", "人狼1枚、占い師1枚。\n村人{players}枚の計{pool}枚。\n1人1枚ずつ配り、\n残り2枚は伏せ札です。"},
    {"setup.count", "参加者 {players}人"},
    {"setup.minus", "−1人"},
    {"setup.plus", "+1人"},
    {"setup.begin", "役職を配る"},
    {"setup.no_change", "配った後の人数変更は\nできません。変更するなら\n無効にして始め直します。"},
    {"setup.old_rules", "5人専用版から変わりました。\n伏せ札が2枚あります。\n人狼がいない夜もあります。"},
    {"night.reserve_a", "伏せ札A"},
    {"night.reserve_b", "伏せ札B"},
    {"night.reserve_help", "AとBは配らなかった札。\n追加の参加者ではありません。"},
    {"vote.peace", "人狼はいない"},
    {"vote.peace.note", "棄権ではありません。\n全員が人狼ではない、\nという推理への1票です。"},
    {"vote.confirm.peace", "「人狼はいない」へ投票。\n人狼が実際にいた場合は\n逃げ切らせてしまいます。"},
    {"runoff.peace_candidate", "「人狼はいない」も候補。"},
    {"runoff.peace_disabled", "「人狼はいない」は\n決選候補ではありません。"},
    {"runoff.single_option", "選べる候補は1つです。\n内容を確認して投票してね。"},
    {"result.peace.title", "みんなで平和を見抜いた！"},
    {"result.peace.body", "人狼は伏せ札でした。\n参加者はみんな村チーム。\n全員の勝ちです。"},
    {"result.false.title", "今回は、全員惜しかった！"},
    {"result.false.body", "人狼は伏せ札でした。\n人狼でない人を選びました。\n今回は全員の不正解です。"},
    {"result.reserves", "伏せ札の答え合わせ"},
    {"result.reserve_row", "{reserve_label}：{role}"},
    {"result.seer_absent", "占い師は伏せ札でした。\n今回は本当の占い結果は\nありません。"},
    {"abort.roster", "人数や席が変わったため\nこのゲームは無効です。\n準備して新しく始めよう。"},
    {"settings.auto", "人数に合わせる"},
    {"settings.discussion", "話し合いの時間"},
    {"settings.seconds", "{seconds}秒"},
    {"common.prev", "前へ"},
    {"page.status", "{page}/{pages}"},
    {"common.selected", "選択中：{target_label}"},
    {"common.rule_brief", "始める前の約束"},
    {"common.understood", "全員で確認しました"},
    {"role.card_absent_note", "伏せ札の役は参加者にいません"},
    {"error.roster", "人数の設定を確認してください。"},
    {"error.target", "選べない対象です。\nもう一度選んでください。"},
    {"meta.migrate", "新しいルールへ更新します。\n前の設定は引き継ぎます。\n進行中の局は再開しません。"},
};
const size_t kStringCount = sizeof(kStrings) / sizeof(kStrings[0]);

const StringEntry kLocalStrings[] = {
    {"char.honorific", "{name}さん"},
    {"char.seat_no", "{seat}番"},
    {"roster.title", "あなたは だれ？"},
    {"roster.note", "番号より名前で呼ぼう"},
    {"roster.ok", "おぼえた"},
    {"story.skip", "スキップ"},
    {"handoff.night.title", "{name}さんの番です"},
    {"handoff.night.body", "前の画面は隠れています。\n{name}さんへ渡してね。\n画面を自分側に向けて。"},
    {"vote.handoff.title", "{name}さんへ渡そう"},
    {"vote.handoff.body", "ほかの人の画面を見ずに\n{name}さんが受け取ってね。"},
    {"pause.resume.owner", "{name}さんご本人ですか？\n指を離してから再開します。"},
    {"result.role_row", "{name}さん：{role}"},
    {"result.vote_row", "{name}さん → {target_label}"},
    {"result.seer_row", "{name}さんが調べたのは\n{target_label}"},
};
const size_t kLocalStringCount = sizeof(kLocalStrings) / sizeof(kLocalStrings[0]);

const char *findString(const char *key) {
    // 重ね書き（content.local.ja.json）を先に見る。設計書の正本は無改変のまま。
    for (size_t i = 0; i < kLocalStringCount; ++i) {
        if (std::strcmp(kLocalStrings[i].key, key) == 0) return kLocalStrings[i].value;
    }
    for (size_t i = 0; i < kStringCount; ++i) {
        if (std::strcmp(kStrings[i].key, key) == 0) return kStrings[i].value;
    }
    return nullptr;
}

const PagedEntry kTutorial[] = {
    {"T01", "3人から、1台で", "3〜10人で遊べます。\n人数を選んで番号を決め、\n端末を時計回りに渡します。"},
    {"T02", "役職は人数より2枚多い", "人狼1枚、占い師1枚。\n村人は参加人数と同じ枚数。\nこの中から1人1枚配ります。"},
    {"T03", "配らない2枚が伏せ札", "残った札をAとBにします。\n役職は最初に全部決まり、\n途中で変わりません。\n追加の人やAIではないよ。"},
    {"T04", "人狼がいない夜もある", "人狼が伏せ札にあると、\n参加者に人狼はいません。\n全員で平和を見抜こう。"},
    {"T05", "占い師がいない夜もある", "占い師が伏せ札にあると、\n本当の占いはありません。\n誰かが占い師と話しても、\n本当とは限りません。"},
    {"T06", "人狼の目的", "自分が選ばれずに\n逃げ切れば勝ちです。\n夜の襲撃はありません。"},
    {"T07", "占い師の能力", "他の人か、伏せ札1枚を\n選んで人狼か確認します。\n人と伏せ札の両方を\n調べることはできません。"},
    {"T08", "村人と人狼も同じ操作", "操作で役が分からないよう、\n全員が対象を選びます。\n占い師以外には、\n追加の情報は出ません。"},
    {"T09", "秘密は自分だけ", "押している間だけ表示。\n指を離したら隠れます。\n画面を見せて証明しない。\n覗き見や撮影もなしです。"},
    {"T10", "話し合いは声で", "みんなの前へ端末を戻し、\n自分の考えを話します。\nウソや黙ることも自由。\n録音や入力はしません。"},
    {"T11", "投票は一人一票", "自分以外の人、または\n「人狼はいない」を選択。\nこれは棄権ではなく、\n全員が村側という予想です。"},
    {"T12", "最多票で決まります", "一番票が多い候補を選択。\n全員一致や過半数は不要。\n全員が投票を終えるまでは\n誰の票も見えません。"},
    {"T13", "同票なら、一度だけ決選", "最多同票の候補から選び、\n全員がもう一度投票。\n自分には投票できません。\nまた並んだら引き分け。"},
    {"T14", "人狼がいたとき", "人狼を選べば村の勝ち。\n別の人や「人狼はいない」\nが選ばれると人狼の勝ち。\n決選同票は引き分けです。"},
    {"T15", "人狼がいなかったとき", "「人狼はいない」を選べば\n全員の勝ちです。\n誰かを選ぶと全員不正解。\n決選同票は引き分けです。"},
    {"T16", "人数が変わったら", "途中参加や交代はしません。\n無効にして始め直します。\n再起動したときも同じ。\n秘密は復元しません。"},
    {"T17", "ゲームの中だけの役", "人を疑うのは遊びの中だけ。\n仕事や性格を評価しません。\n参加は自由。困ったら\n無効にして終わって大丈夫。"},
};
const size_t kTutorialCount = sizeof(kTutorial) / sizeof(kTutorial[0]);

const char *const kIntroVariants[] = {
    "閉店後の喫茶「余白」。\n{players}人で囲む小さな卓。\n今夜は誰が、どんな役？",
    "ラテが二つの封筒を置く。\n「ここにも役職があるよ。\n誰に配られたかは秘密」",
    "カップは{players}個、伏せ札は2枚。\n人狼はこの席にいるのか、\n封筒で眠っているのか。",
    "モカが時計を置きました。\n「話し終わったら投票です。\n落ち着いて聞いてみてね」",
    "雨音の残るカフェ。\n集まった仲間と、\nひと晩だけの人狼会。",
    "チャイが窓を閉めました。\n「話は聞いても、\n画面は覗かない。それで」",
    "明日の準備はひと休み。\nいつもの顔に、\n今夜だけの秘密の役。",
    "砂糖とミルクも準備OK。\n足りないのは、\n今夜の答えを探す会話。",
    "前と同じ役になっても、\nこれは新しいゲーム。\n伏せ札も配り直します。",
    "少し灯りを落とした余白。\n「ウソはゲームの中だけ」\nそれが、今夜の約束。",
    "ポットの湯気が消える頃、\nいつものカフェが\nひと晩だけの村になります。",
    "「勝っても、負けても一杯」\nラテが笑いました。\nそれでは役職を配ります。",
};
const size_t kIntroVariantCount = sizeof(kIntroVariants) / sizeof(kIntroVariants[0]);

static const char *const kEndingLines_caught_wolf[] = {
    "話し合いの先に、人狼が見つかりました。",
    "役職を脱いだら、いつもの仲間へ。",
    "答え合わせも、このカフェのお楽しみ。",
};
static const char *const kEndingLines_wolf_escaped[] = {
    "カップの向こうで、人狼がそっと笑いました。",
    "最後まで役を演じた一人に、拍手。",
    "見抜けなかった夜も、話の種になります。",
};
static const char *const kEndingLines_peace_correct[] = {
    "封筒の中で、人狼はまだ眠っていました。",
    "疑うだけでなく、平和も見抜いた夜。",
    "今日はみんなで、勝利の一杯。",
};
static const char *const kEndingLines_false_accusation[] = {
    "人狼は席ではなく、伏せ札にいました。",
    "誰も人狼でない夜も、なかなか難しいね。",
    "役がなくても疑われる。それも、この遊び。",
};
static const char *const kEndingLines_draw[] = {
    "最後まで、一つの答えには決まりませんでした。",
    "どの話が気になった？答え合わせをしよう。",
    "結果を見たら、もう一杯の作戦会議。",
};
const EndingVariantGroup kEndingVariants[] = {
    {"caught_wolf", kEndingLines_caught_wolf, sizeof(kEndingLines_caught_wolf) / sizeof(kEndingLines_caught_wolf[0])},
    {"wolf_escaped", kEndingLines_wolf_escaped, sizeof(kEndingLines_wolf_escaped) / sizeof(kEndingLines_wolf_escaped[0])},
    {"peace_correct", kEndingLines_peace_correct, sizeof(kEndingLines_peace_correct) / sizeof(kEndingLines_peace_correct[0])},
    {"false_accusation", kEndingLines_false_accusation, sizeof(kEndingLines_false_accusation) / sizeof(kEndingLines_false_accusation[0])},
    {"draw", kEndingLines_draw, sizeof(kEndingLines_draw) / sizeof(kEndingLines_draw[0])},
};
const size_t kEndingVariantGroupCount = sizeof(kEndingVariants) / sizeof(kEndingVariants[0]);

const EndingVariantGroup *findEndingVariants(const char *outcome) {
    for (size_t i = 0; i < kEndingVariantGroupCount; ++i) {
        if (std::strcmp(kEndingVariants[i].outcome, outcome) == 0) return &kEndingVariants[i];
    }
    return nullptr;
}

const PagedEntry kMandatoryBrief[] = {
    {"B01", "人数と役職を確認", "参加者は{players}人です。\n人狼1・占い師1・村人{players}。\n合計{pool}枚から配り、\n2枚は伏せ札になります。"},
    {"B02", "どの役も伏せ札にあるかも", "人狼や占い師が\n参加者にいない夜もあります。\n人狼がいないと思ったら\n「人狼はいない」に投票。"},
    {"B03", "隠してから渡そう", "番号は本人確認ではないよ。\n受け取った人だけが見ます。\n画面を証拠に見せません。\n困ったら無効にして終了。"},
    {"B04", "投票の約束", "各自が1票。自分は選べず、\n最多票で決まります。\n同票は1回決選、再同票は\n引き分け。途中脱落なし。"},
};
const size_t kMandatoryBriefCount = sizeof(kMandatoryBrief) / sizeof(kMandatoryBrief[0]);

const SeatCharacter kSeatCharacters[] = {
    {"カップ", "\xEE\xBF\xAF"},   // 1 coffee
    {"パン", "\xEE\xA9\x93"},   // 2 bakery_dining
    {"スプーン", "\xEF\x80\x8C"},   // 3 flatware
    {"ポット", "\xEE\xBF\xB0"},   // 4 coffee_maker
    {"クッキー", "\xEE\xAA\xAC"},   // 5 cookie
    {"ほし", "\xEE\xA0\xB8"},   // 6 star
    {"はっぱ", "\xEE\xA8\xB5"},   // 7 eco
    {"ベル", "\xEE\x9F\xB4"},   // 8 notifications
    {"ほん", "\xEE\xA8\x99"},   // 9 menu_book
    {"つき", "\xEE\x94\x9C"},   // 10 dark_mode
};
const size_t kSeatCharacterCount = sizeof(kSeatCharacters) / sizeof(kSeatCharacters[0]);

const SeatCharacter *findSeatCharacter(int seat) {
    if (seat < 0 || (size_t)seat >= kSeatCharacterCount) return nullptr;
    return &kSeatCharacters[seat];
}

const StoryPage kStory[] = {
    {"S1", "閉店後の人狼会", "閉店後の喫茶「余白」。\n常連たちの中に、\n人に化けた「人狼」が\nまぎれているらしい。", "\xEE\xBD\x9E"},   // nightlight_round
    {"S2", "閉店後の人狼会", "人狼は、人のふりをして\nうそをつく。放っておくと\n常連がひとりずつ\n消えてしまう…という噂。", "\xEE\xA4\x9D"},   // pets
    {"S3", "閉店後の人狼会", "村人は、話し合いと投票で\n人狼を見つけ出す。\n見つけられなければ、\n人狼の勝ち。", "\xEF\x88\xB3"},   // groups
    {"S4", "閉店後の人狼会", "占い師は、夜のあいだに\nひとりだけ正体を\n占える。その結果が\n推理の手がかりになる。", "\xEE\xA3\xB4"},   // visibility
    {"S5", "閉店後の人狼会", "ただし役職の札は\n人数より2枚多い。\n人狼が誰の手にも\n渡っていない夜もある。", "\xEE\x99\xA6"},   // auto_stories
    {"S6", "閉店後の人狼会", "そのときは\n「人狼はいない」に\n投票できれば\nみんなの勝ち。", "\xEE\x95\x81"},   // local_cafe
};
const size_t kStoryCount = sizeof(kStory) / sizeof(kStory[0]);

const char *const kIconWolf = "\xEE\xA4\x9D";   // pets
const char *const kIconSeer = "\xEE\xA3\xB4";   // visibility
const char *const kIconVillager = "\xEE\xA2\x8A";   // home

}}} // namespace coffee::wolf::content

namespace coffee { namespace wolf { namespace rules {
const TimingByPlayers kTimingByPlayers[] = {
    {3, 120, 60, 5, 20, 2, 5},
    {4, 150, 60, 6, 30, 2, 6},
    {5, 180, 60, 7, 42, 2, 7},
    {6, 210, 60, 8, 56, 2, 8},
    {7, 240, 90, 9, 72, 2, 9},
    {8, 270, 90, 10, 90, 2, 10},
    {9, 300, 90, 11, 110, 2, 11},
    {10, 330, 90, 12, 132, 2, 12},
};
const size_t kTimingByPlayersCount = sizeof(kTimingByPlayers) / sizeof(kTimingByPlayers[0]);

const TimingByPlayers *findTiming(uint8_t players) {
    for (size_t i = 0; i < kTimingByPlayersCount; ++i) {
        if (kTimingByPlayers[i].players == players) return &kTimingByPlayers[i];
    }
    return nullptr;
}

}}} // namespace coffee::wolf::rules
