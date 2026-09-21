#include "DetectiveContent.h"

#include <cstring>

namespace coffee { namespace det { namespace content {

static const Page kPg_CD001_intro[] = {
    {Speaker::Narrator, Emotion::Neutral, "閉店前の喫茶「余白」。\n窓際の席に、白いカップと\n青いカップが並んでいた。"},
    {Speaker::Mocha, Emotion::Puzzled, "注文は一杯だけです。\nあれ、ぼくが二つ\n用意したんでしたっけ？"},
    {Speaker::Latte, Emotion::Neutral, "慌てなくていいよ。\n青い方を置いたのが誰か、\n順番に確かめてみよう。"},
    {Speaker::Chai, Emotion::Puzzled, "……椅子も、二つとも\n引いてあるね。"},
};

static const Page kPg_CD001_premises[] = {
    {Speaker::Narrator, Emotion::Neutral, "今日、3人は窓際の席へ\n一度ずつ行った。\n時刻は17:40、17:45、\n17:50。"},
    {Speaker::Narrator, Emotion::Neutral, "同じ時刻に行った人は\nいない。青いカップは、\n誰かがその訪問中に置いた。"},
    {Speaker::Narrator, Emotion::Neutral, "その間、ほかの人は\n席に触れていない。\n置いた後も動かしていない。"},
};

static const Page kPg_CD001_clue0[] = {
    {Speaker::Narrator, Emotion::Neutral, "17:48、窓際には\n白いカップが一つ。"},
    {Speaker::Narrator, Emotion::Neutral, "17:52、同じ席には\n白と青のカップが一つずつ。"},
};

static const Page kPg_CD001_sum0[] = {
    {Speaker::Narrator, Emotion::Neutral, "17:48には白1つ、\n17:52には白と青。\n青いカップは17:48より後、\n17:52までに置かれた。"},
};

static const Page kPg_CD001_clue1[] = {
    {Speaker::Latte, Emotion::Neutral, "窓際に行ったのは、\n私が最後ではないよ。"},
    {Speaker::Latte, Emotion::Neutral, "そのあとカウンターで\n布巾を洗っていたんだ。\n時計は見ていなかったけど。"},
};

static const Page kPg_CD001_sum1[] = {
    {Speaker::Narrator, Emotion::Neutral, "ラテは3人のうち\n最後ではない。"},
};

static const Page kPg_CD001_clue2[] = {
    {Speaker::Mocha, Emotion::Neutral, "ぼくが窓際から戻った後に、\nラテさんがそちらへ\n行ったのは覚えています。"},
    {Speaker::Mocha, Emotion::Neutral, "椅子の向きを直していたら、\nちょっと緊張してしまって。"},
};

static const Page kPg_CD001_sum2[] = {
    {Speaker::Narrator, Emotion::Neutral, "モカはラテより先に窓際へ\n行った。"},
};

static const Page kPg_CD001_hint1[] = {
    {Speaker::Narrator, Emotion::Neutral, "青いカップが増えた時間に、\n訪問できるのは\n3つの時刻のうちどれ？"},
};

static const Page kPg_CD001_hint2[] = {
    {Speaker::Narrator, Emotion::Neutral, "青いカップは17:50。\nラテは最後ではなく、\nモカはラテより先。"},
    {Speaker::Narrator, Emotion::Neutral, "モカ→ラテの順で、\n二人とも最後になれない。\n残る人を考えよう。"},
    {Speaker::Narrator, Emotion::Neutral, "人柄ではなく、時刻と順番か\nら決められる。"},
};

static const Page kPg_CD001_amb0[] = {
    {Speaker::Narrator, Emotion::Neutral, "普段は片方だけ\n引いてある椅子。\n今日は少し、にぎやかだ。"},
};

static const Ambient kAmbient_CD001[] = {
    {"A1", "窓際の椅子", {kPg_CD001_amb0, 1}},
};

static const Page kPg_CD001_wrong_LATTE[] = {
    {Speaker::Narrator, Emotion::Neutral, "ラテは最後ではない。\nカップが増えた\n時間と比べよう。"},
};

static const Page kPg_CD001_wrong_MOCHA[] = {
    {Speaker::Narrator, Emotion::Neutral, "モカはラテより先なので、\n最後の訪問にはならない。"},
};

static const Page kPg_CD001_expl[] = {
    {Speaker::Narrator, Emotion::Neutral, "正解はチャイ。\n青いカップが増えた間の\n訪問時刻は17:50だけ。"},
    {Speaker::Narrator, Emotion::Neutral, "モカはラテより先。\nラテは最後ではないので、\nモカ→ラテ→チャイとなる。"},
    {Speaker::Narrator, Emotion::Neutral, "17:40がモカ、17:45がラテ、\n17:50がチャイ。\nだから、置いたのはチャイ。"},
};

static const Page kPg_CD001_epi[] = {
    {Speaker::Chai, Emotion::Soft, "うん。置いたのは僕。\n会えるかはまだ\n分からないんだけど。"},
    {Speaker::Latte, Emotion::Soft, "それでも、二人分？"},
    {Speaker::Chai, Emotion::Soft, "前にここで本を貸して\nくれた人が、街へ戻るって。\nその人、青い方を使ってた。"},
    {Speaker::Mocha, Emotion::Smile, "じゃあ、青いカップは\nしまわないでおきます。\n今日は、席の予約だけ。"},
    {Speaker::Latte, Emotion::Smile, "そのカップは片づけよう。\n空けておくのは、\n明日の時間にしようか。"},
    {Speaker::Narrator, Emotion::Neutral, "窓際の二脚は、そのまま。\n帰り道の足音が、\n少し軽く聞こえた。"},
};

static const Page kPg_CD001_recap[] = {
    {Speaker::Narrator, Emotion::Neutral, "チャイが二つ目のカップを\n置いた。街へ戻る友人を\n迎えるつもりだった。"},
};

static const Page kPg_CD002_intro[] = {
    {Speaker::Narrator, Emotion::Neutral, "翌日の夕方。\n伝言入れに使う\n空の砂糖缶に、\n青いリボンの封\n筒が入っていた。"},
    {Speaker::Mocha, Emotion::Puzzled, "この缶、今はお砂糖じゃなく\nお手紙入れなんです。\nでも、差出人の名前がない。"},
    {Speaker::Latte, Emotion::Neutral, "今日、みんなで作った\n封筒の一つだね。\n開ける前に持ち主を探そう。"},
    {Speaker::Chai, Emotion::Puzzled, "リボンの色と、小さな印。\n見覚えはあるんだけど。"},
};

static const Page kPg_CD002_premises[] = {
    {Speaker::Narrator, Emotion::Neutral, "3人は一人一つずつ\n封筒を作った。\n色は青・金・赤を一つずつ。"},
    {Speaker::Narrator, Emotion::Neutral, "印も、本・カップ・葉を\n一つずつ使った。\n同じ色や印は重ならない。"},
    {Speaker::Narrator, Emotion::Neutral, "作った後にリボンや印を\n替えた人はいない。\n話と作業メモはすべて正確。"},
};

static const Page kPg_CD002_clue0[] = {
    {Speaker::Mocha, Emotion::Neutral, "ぼくは金色のリボンを\n使いました。\n作業メモにも書\nいてあります。"},
    {Speaker::Mocha, Emotion::Neutral, "名前を書く欄だけ\n作り忘れてしまいました。\n次は、欄を増やします。"},
};

static const Page kPg_CD002_sum0[] = {
    {Speaker::Narrator, Emotion::Neutral, "モカのリボンは金色。"},
};

static const Page kPg_CD002_clue1[] = {
    {Speaker::Narrator, Emotion::Neutral, "完成した3つを並べて\n作った見本メモ。\n「カップの印は金色」"},
    {Speaker::Narrator, Emotion::Neutral, "もう一行ある。\n「本の印は青色」\nどちらも実物と一致する。"},
};

static const Page kPg_CD002_sum1[] = {
    {Speaker::Narrator, Emotion::Neutral, "カップ印の封筒\nは金色のリボン。\n本印の封筒は青色のリボン。"},
};

static const Page kPg_CD002_clue2[] = {
    {Speaker::Chai, Emotion::Neutral, "僕が貼ったのは葉の印。\n本の印と迷ったけど、\n最後は葉にしたよ。"},
    {Speaker::Chai, Emotion::Neutral, "小さな葉っぱなら、\n風に乗って届きそうで。"},
};

static const Page kPg_CD002_sum2[] = {
    {Speaker::Narrator, Emotion::Neutral, "チャイの印は葉。"},
};

static const Page kPg_CD002_hint1[] = {
    {Speaker::Narrator, Emotion::Neutral, "色と印を、別々に\n推理しないでみよう。\n見本メモで結び付けられる。"},
};

static const Page kPg_CD002_hint2[] = {
    {Speaker::Narrator, Emotion::Neutral, "金色のモカはカップ印。\nチャイは葉の印。\n本の印は、残る一人。"},
    {Speaker::Narrator, Emotion::Neutral, "本の印の封筒が青色。\n誰に当たるかな？"},
    {Speaker::Narrator, Emotion::Neutral, "色と印をつなげれば、\n名前がなくても\nたどり着ける。"},
};

static const Page kPg_CD002_amb0[] = {
    {Speaker::Narrator, Emotion::Neutral, "砂糖缶には、小さく\n「食べられないお知らせ」と\nモカのラベルが貼ってある。"},
};

static const Ambient kAmbient_CD002[] = {
    {"A1", "伝言入れ", {kPg_CD002_amb0, 1}},
};

static const Page kPg_CD002_wrong_MOCHA[] = {
    {Speaker::Narrator, Emotion::Neutral, "モカは金色のリボンを使った\nと記録されている。"},
};

static const Page kPg_CD002_wrong_CHAI[] = {
    {Speaker::Narrator, Emotion::Neutral, "チャイは葉の印。\n本の印の青い封筒\nとは一致しない。"},
};

static const Page kPg_CD002_expl[] = {
    {Speaker::Narrator, Emotion::Neutral, "正解はラテ。\n金色のモカは、\n見本メモからカップ印。"},
    {Speaker::Narrator, Emotion::Neutral, "チャイは葉の印。\n本の印が残るのは\nラテだけになる。"},
    {Speaker::Narrator, Emotion::Neutral, "本の印は青色。\nだから、青い封筒を\n作ったのはラテ。"},
};

static const Page kPg_CD002_epi[] = {
    {Speaker::Latte, Emotion::Soft, "私からチャイへの手紙。\n差出人を書かなかったのは、\n少し照れくさくてね。"},
    {Speaker::Chai, Emotion::Soft, "「明日16時、窓際にて。\n一冊と、一人分の\nおかえりを持ち寄ること」"},
    {Speaker::Mocha, Emotion::Smile, "あ、ぼくの金色は\nラテさんへの招待状です。\n同じことを考えてました。"},
    {Speaker::Chai, Emotion::Smile, "僕の赤い封筒は、\n戻ってくる友人へ。\n店の住所だけ、もう一度。"},
    {Speaker::Latte, Emotion::Smile, "誰も主催者のつもりじゃ\nないのに、会ができたね。\n名前は「余白の一ページ」。"},
    {Speaker::Narrator, Emotion::Neutral, "三つの封筒が、\n同じ明日の窓際へ\nつながっていた。"},
};

static const Page kPg_CD002_recap[] = {
    {Speaker::Narrator, Emotion::Neutral, "青い封筒はラテの招待状。\n3人の手紙から、明日の\n「余白の一ペー\nジ」が生まれた。"},
};

static const Page kPg_CD003_intro[] = {
    {Speaker::Narrator, Emotion::Neutral, "「余白の一ページ」の当日。\n雨が上がると、\n青いカップの横に\n銀色のしおりが\n置かれていた。"},
    {Speaker::Chai, Emotion::Puzzled, "このしおり、見覚えがある。\nでも、結んだ糸が\n新しくなってるね。"},
    {Speaker::Mocha, Emotion::Neutral, "誰が置いたか確かめてから、\nちゃんと渡したいですね。"},
    {Speaker::Latte, Emotion::Soft, "三人の記憶を並べてみよう。\n責めるためじゃなく、\nお礼を言う相手を探すんだ。"},
};

static const Page kPg_CD003_premises[] = {
    {Speaker::Narrator, Emotion::Neutral, "しおりを置いたのは\nラテ・モカ・チャイの\nうち一人だけ。"},
    {Speaker::Narrator, Emotion::Neutral, "これから読む三つの発言で、\n事実と違うのは\nちょうど一つ。"},
    {Speaker::Narrator, Emotion::Neutral, "置いた本人の発言が\n間違いとは限らない。\n表情や口調は手\n掛かりにしない。"},
};

static const Page kPg_CD003_clue0[] = {
    {Speaker::Latte, Emotion::Neutral, "置いたのはモカだよ。\nそう覚えている。"},
};

static const Page kPg_CD003_sum0[] = {
    {Speaker::Narrator, Emotion::Neutral, "ラテの発言：置\nいたのはモカ。"},
};

static const Page kPg_CD003_clue1[] = {
    {Speaker::Mocha, Emotion::Neutral, "置いたのは\nチャイさんではありません。"},
};

static const Page kPg_CD003_sum1[] = {
    {Speaker::Narrator, Emotion::Neutral, "モカの発言：置いたのはチャ\nイではない。"},
};

static const Page kPg_CD003_clue2[] = {
    {Speaker::Chai, Emotion::Neutral, "置いたのは僕だ。\n……そうだったはず。"},
};

static const Page kPg_CD003_sum2[] = {
    {Speaker::Narrator, Emotion::Neutral, "チャイの発言：置いたのは\nチャイ。"},
};

static const Page kPg_CD003_hint1[] = {
    {Speaker::Narrator, Emotion::Neutral, "誰か一人が置いたと仮定し、\n発言がそれぞれ\n正しいか確かめよう。"},
};

static const Page kPg_CD003_hint2[] = {
    {Speaker::Narrator, Emotion::Neutral, "モカとチャイの発言は\n反対の内容なので、\nどちらか一方だけが間違い。"},
    {Speaker::Narrator, Emotion::Neutral, "それだけで間違いは一つ。\nラテの発言まで間違いだと、\n二つになってしまう。"},
    {Speaker::Narrator, Emotion::Neutral, "思い込みではなく、\n条件に合う発言の組合せで確\nかめられた。"},
};

static const Page kPg_CD003_amb0[] = {
    {Speaker::Narrator, Emotion::Neutral, "銀色のしおりに、\n新しい青い糸。\n持ち主の名前は\n書かれていない。"},
};

static const Ambient kAmbient_CD003[] = {
    {"A1", "しおりを眺める", {kPg_CD003_amb0, 1}},
};

static const Page kPg_CD003_wrong_LATTE[] = {
    {Speaker::Narrator, Emotion::Neutral, "ラテが置いたとすると、\nラテとチャイの発言が間違い\nになる。"},
};

static const Page kPg_CD003_wrong_CHAI[] = {
    {Speaker::Narrator, Emotion::Neutral, "チャイが置いたとすると、\nラテとモカの発言\nが間違いになる。"},
};

static const Page kPg_CD003_expl[] = {
    {Speaker::Narrator, Emotion::Neutral, "正解はモカ。\nモカが置いたなら、\nラテの発言は正しい。"},
    {Speaker::Narrator, Emotion::Neutral, "モカの「チャイではない」も\n正しい。間違うのは\nチャイの発言一つだけ。"},
    {Speaker::Narrator, Emotion::Neutral, "ラテが置いた場合も、\nチャイが置いた場合も、\n間違いが二つに\nなってしまう。"},
};

static const Page kPg_CD003_epi[] = {
    {Speaker::Mocha, Emotion::Soft, "糸がほどけかけていたので、\n結び直してから置きました。\n中の文字は、そのままです。"},
    {Speaker::Chai, Emotion::Soft, "昨日、僕が取り出したから\nそのまま置いた\n気になってた。\n直してくれたんだね。"},
    {Speaker::Latte, Emotion::Soft, "間違えた記憶にも、\n理由があったんだね。"},
    {Speaker::Narrator, Emotion::Neutral, "入口のベルが鳴る。\n傘を閉じた人が、\n窓際の青いカッ\nプを見つけた。"},
    {Speaker::Cocoa, Emotion::Smile, "ただいま。\n……まだ、このカップ\n残してくれてたんだ。"},
    {Speaker::Chai, Emotion::Smile, "おかえり、ココア。\n借りた本と、しおり。\nずいぶん長く預かった。"},
    {Speaker::Cocoa, Emotion::Soft, "しおりの裏、覚えてる？\n「つづきは、ここで」って\n書いたんだったね。"},
    {Speaker::Mocha, Emotion::Smile, "じゃあ、一ページ目から\nじゃなくて、つづきから。\nコーヒーは何杯にします？"},
    {Speaker::Latte, Emotion::Smile, "今日は、みんなの分。\nゆっくり淹れよう。"},
    {Speaker::Narrator, Emotion::Neutral, "三つの小さな謎が、\n一つの「おかえ\nり」になった。\n喫茶「余白」は、\n今日も営業中。"},
};

static const Page kPg_CD003_recap[] = {
    {Speaker::Narrator, Emotion::Neutral, "モカがしおりの糸を直した。\n戻ったココアを迎えて、\n「余白の一ペー\nジ」が始まった。"},
};

const Character kCharacters[] = {
    {"NARRATOR", "手帳", Speaker::Narrator, 0x445164u},
    {"LATTE", "ラテ", Speaker::Latte, 0x596B57u},
    {"MOCHA", "モカ", Speaker::Mocha, 0xBA784Cu},
    {"CHAI", "チャイ", Speaker::Chai, 0x64798Fu},
    {"COCOA", "ココア", Speaker::Cocoa, 0x96738Du},
};
const size_t kCharacterCount = sizeof(kCharacters) / sizeof(kCharacters[0]);

const Character *findCharacter(Speaker speaker) {
    for (size_t i = 0; i < kCharacterCount; ++i) {
        if (kCharacters[i].speaker == speaker) return &kCharacters[i];
    }
    return nullptr;
}

const Episode kEpisodes[] = {
    {
        "CD001", "第1話",
        "いつもの席に、二つのカップ",
        "二つのカップ", "ひと息",
        "注文は一杯なのに、\n窓際には二つ。\n順番をたどって確かめよう。",
        "青いカップを\n置いたのは？",
        {{"LATTE", "ラテ"}, {"MOCHA", "モカ"}, {"CHAI", "チャイ"}},
        {kPg_CD001_intro, 4},
        {kPg_CD001_premises, 3},
        {
        {"E1", "窓際の観察メモ", {kPg_CD001_clue0, 2}, {kPg_CD001_sum0, 1}},
        {"E2", "店主の片づけ", {kPg_CD001_clue1, 2}, {kPg_CD001_sum1, 1}},
        {"E3", "新人の順番", {kPg_CD001_clue2, 2}, {kPg_CD001_sum2, 1}},
        },
        {{kPg_CD001_hint1, 1}, {kPg_CD001_hint2, 3}},
        kAmbient_CD001, 1,
        2,
        {{kPg_CD001_wrong_LATTE, 1}, {kPg_CD001_wrong_MOCHA, 1}, {nullptr, 0}},
        {kPg_CD001_expl, 3},
        {kPg_CD001_epi, 6},
        {kPg_CD001_recap, 1},
        {"MEM_BLUE_CUP", "青いカップ", "来るか分からなくても、\n空けておきたい席がある。"},
    },
    {
        "CD002", "第2話",
        "砂糖缶に届いた青い封筒",
        "青い封筒", "ひと考え",
        "差出人のない小さな封筒。\n色と印を組み合わせよう。",
        "青い封筒を\n作ったのは？",
        {{"LATTE", "ラテ"}, {"MOCHA", "モカ"}, {"CHAI", "チャイ"}},
        {kPg_CD002_intro, 4},
        {kPg_CD002_premises, 3},
        {
        {"E1", "モカの作業メモ", {kPg_CD002_clue0, 2}, {kPg_CD002_sum0, 1}},
        {"E2", "リボンと印の見本", {kPg_CD002_clue1, 2}, {kPg_CD002_sum1, 1}},
        {"E3", "チャイの印", {kPg_CD002_clue2, 2}, {kPg_CD002_sum2, 1}},
        },
        {{kPg_CD002_hint1, 1}, {kPg_CD002_hint2, 3}},
        kAmbient_CD002, 1,
        0,
        {{nullptr, 0}, {kPg_CD002_wrong_MOCHA, 1}, {kPg_CD002_wrong_CHAI, 1}},
        {kPg_CD002_expl, 3},
        {kPg_CD002_epi, 6},
        {kPg_CD002_recap, 1},
        {"MEM_BLUE_LETTER", "青い招待状", "別々に書いた手紙が、\n同じ場所へ向かう日もある。"},
    },
    {
        "CD003", "第3話",
        "雨上がりのしおり",
        "雨上がりのしおり", "ひと考え",
        "誰かが結び直した、\n小さな糸。\n三人の記憶を並べてみよう。",
        "しおりを置いたのは？",
        {{"LATTE", "ラテ"}, {"MOCHA", "モカ"}, {"CHAI", "チャイ"}},
        {kPg_CD003_intro, 4},
        {kPg_CD003_premises, 3},
        {
        {"E1", "ラテの記憶", {kPg_CD003_clue0, 1}, {kPg_CD003_sum0, 1}},
        {"E2", "モカの記憶", {kPg_CD003_clue1, 1}, {kPg_CD003_sum1, 1}},
        {"E3", "チャイの記憶", {kPg_CD003_clue2, 1}, {kPg_CD003_sum2, 1}},
        },
        {{kPg_CD003_hint1, 1}, {kPg_CD003_hint2, 3}},
        kAmbient_CD003, 1,
        1,
        {{kPg_CD003_wrong_LATTE, 1}, {nullptr, 0}, {kPg_CD003_wrong_CHAI, 1}},
        {kPg_CD003_expl, 3},
        {kPg_CD003_epi, 10},
        {kPg_CD003_recap, 1},
        {"MEM_SILVER_BOOKMARK", "銀色のしおり", "つづきは、ここで。"},
    },
};
const size_t kEpisodeCount = sizeof(kEpisodes) / sizeof(kEpisodes[0]);

const StringEntry kStrings[] = {
    {"game_title", "CAFE DETECTIVE"},
    {"world_title", "喫茶「余白」"},
    {"pack_title", "つづきは、ここで。"},
    {"menu_new", "まだ読んでいない話"},
    {"menu_replay", "もう一度読む"},
    {"start", "お店に入る"},
    {"next", "次へ"},
    {"previous", "戻る"},
    {"investigate", "話と手掛かりを集めよう"},
    {"read", "読んだ"},
    {"unread", "未読"},
    {"notebook", "手帳"},
    {"guess", "推理する"},
    {"choose", "誰だと思う？"},
    {"confirm", "この推理で決定する"},
    {"change", "選び直す"},
    {"prepare", "AIの回答を確定しています"},
    {"ready", "AIの回答は確定済み"},
    {"mode_solo", "ひとり推理"},
    {"mode_jev", "Jevと推理勝負"},
    {"mode_offline", "オフライン・練習"},
    {"mode_practice", "復習"},
    {"hint", "ヒント"},
    {"hint_notice", "ヒントを読むと、AIとの勝敗には数えません。物語は最後まで読めます。"},
    {"hint_read", "ヒントを読む"},
    {"hint_again", "次のヒント"},
    {"give_up", "答えとつづきを読む"},
    {"give_up_confirm", "答えを読みますか？ 今回の推理は未正解として記録します。"},
    {"human_win", "あなたの勝ち！"},
    {"ai_win", "AIの勝ち！"},
    {"both_correct", "どちらも名探偵！"},
    {"both_incorrect", "今回は、どちらも未解決"},
    {"solo_correct", "謎が解けました"},
    {"solo_incorrect", "手掛かりを一緒に見直そう"},
    {"assisted", "ヒント付きで解決"},
    {"explanation", "手帳で確かめる"},
    {"ending", "物語のつづき"},
    {"collectible", "余白の手帳に記録しました"},
    {"chapter_end", "第一章 おわり"},
    {"back_home", "HOMEへ"},
    {"pause", "あとで読む"},
    {"abandon", "この挑戦を終了"},
    {"resuming", "前のページから再開"},
    {"not_ready", "手掛かりを全部読むと推理できます"},
    {"offline_notice", "通信できません。読んだ内容は保持しています。ひとり推理として続けることもできます。"},
    {"ai_unavailable", "今日はAIがお休みです。ひとり推理で、物語を続けましょう。"},
    {"commit_failure", "AIの回答確認に失敗しました。勝敗は記録せず、物語を続けられます。"},
    {"save_pending", "記録を確認しています。もう一度答えを選ばないでください。"},
    {"maintenance", "記録の安全確認中です。新しいオンライン挑戦は開始できません。"},
    {"privacy", "記録するのは、遊んだ話と結果だけ。読んだ順番や迷った時間はAIへ送りません。"},
    {"coffee_add", "コーヒー＋1"},
    {"coffee_added", "コーヒーを1杯記録"},
    {"font_error", "日本語表示の設定を確認してください"},
    {"disclaimer_preview", "PC参考版：ひとり推理のみ。Jev・Google・実機は接続していません。"},
};
const size_t kStringCount = sizeof(kStrings) / sizeof(kStrings[0]);

const char *findString(const char *key) {
    for (size_t i = 0; i < kStringCount; ++i) {
        if (std::strcmp(kStrings[i].key, key) == 0) return kStrings[i].value;
    }
    return nullptr;
}

const StringEntry kWrappedStrings[] = {
    {"game_title", "CAFE DETECTIVE"},
    {"world_title", "喫茶「余白」"},
    {"pack_title", "つづきは、ここで。"},
    {"menu_new", "まだ読んでいない話"},
    {"menu_replay", "もう一度読む"},
    {"start", "お店に入る"},
    {"next", "次へ"},
    {"previous", "戻る"},
    {"investigate", "話と手掛かりを集めよう"},
    {"read", "読んだ"},
    {"unread", "未読"},
    {"notebook", "手帳"},
    {"guess", "推理する"},
    {"choose", "誰だと思う？"},
    {"confirm", "この推理で決定する"},
    {"change", "選び直す"},
    {"prepare", "AIの回答を確定しています"},
    {"ready", "AIの回答は確定済み"},
    {"mode_solo", "ひとり推理"},
    {"mode_jev", "Jevと推理勝負"},
    {"mode_offline", "オフライン・練習"},
    {"mode_practice", "復習"},
    {"hint", "ヒント"},
    {"hint_notice", "ヒントを読むと、\nAIとの勝敗には数えません。\n物語は最後まで読めます。"},
    {"hint_read", "ヒントを読む"},
    {"hint_again", "次のヒント"},
    {"give_up", "答えとつづきを読む"},
    {"give_up_confirm", "答えを読みますか？\n今回の推理は未正解として\n記録します。"},
    {"human_win", "あなたの勝ち！"},
    {"ai_win", "AIの勝ち！"},
    {"both_correct", "どちらも名探偵！"},
    {"both_incorrect", "今回は、どちらも未解決"},
    {"solo_correct", "謎が解けました"},
    {"solo_incorrect", "手掛かりを一緒に見直そう"},
    {"assisted", "ヒント付きで解決"},
    {"explanation", "手帳で確かめる"},
    {"ending", "物語のつづき"},
    {"collectible", "余白の手帳に記録しました"},
    {"chapter_end", "第一章 おわり"},
    {"back_home", "HOMEへ"},
    {"pause", "あとで読む"},
    {"abandon", "この挑戦を終了"},
    {"resuming", "前のページから再開"},
    {"not_ready", "手掛かりを全部読\nむと推理できます"},
    {"offline_notice", "通信できません。\n読んだ内容は保持していま\nす。ひとり推理として続ける\nこともできます。"},
    {"ai_unavailable", "今日はAIがお休みです。\nひとり推理で、物語を続けま\nしょう。"},
    {"commit_failure", "AIの回答確認に失敗しまし\nた。勝敗は記録せず、\n物語を続けられます。"},
    {"save_pending", "記録を確認しています。\nもう一度答えを選ばないでく\nださい。"},
    {"maintenance", "記録の安全確認中です。\n新しいオンライン挑戦は開始\nできません。"},
    {"privacy", "記録するのは、遊んだ話と結\n果だけ。読んだ順番や迷った\n時間はAIへ送りません。"},
    {"coffee_add", "コーヒー＋1"},
    {"coffee_added", "コーヒーを1杯記録"},
    {"font_error", "日本語表示の設定を確認して\nください"},
    {"disclaimer_preview", "PC参考版：ひとり推理のみ。\nJev・Google・実機は接続し\nていません。"},
};
const size_t kWrappedStringCount = sizeof(kWrappedStrings) / sizeof(kWrappedStrings[0]);

const char *findWrappedString(const char *key) {
    for (size_t i = 0; i < kWrappedStringCount; ++i) {
        if (std::strcmp(kWrappedStrings[i].key, key) == 0) return kWrappedStrings[i].value;
    }
    return nullptr;
}

}}} // namespace coffee::det::content
