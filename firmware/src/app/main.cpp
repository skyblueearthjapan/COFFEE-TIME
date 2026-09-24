/**
 * COFFEE TIME — 段階2「時計・天気・残り杯数」
 * ボード初期化と LVGL 移植は ESP32_Display_Panel v1.0.4 の lvgl_v8/simple_port を踏襲している。
 */

#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <esp_heap_caps.h>
#include <sys/time.h>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "Battery.h"
#include "Buzzer.h"
#include "CupState.h"
#include "Display.h"
#include "HomeScreen.h"
#include "NetService.h"
#include "RemoteConsole.h"
#include "Settings.h"
#include "SysInfo.h"
#include "ui/HistoryScreen.h"
#include "ui/MainMenu.h"
#include "ui/ScreenManager.h"
#include "ui/SettingsScreen.h"
#include "ui/TodayScreen.h"
#include "games/cards/CardsGame.h"
#include "games/detective/DetectiveGame.h"
#include "games/duel/DuelGame.h"
#include "games/esper/EsperGame.h"
#include "games/reversi/ReversiGame.h"
#include "games/werewolf/WerewolfGame.h"
#include "games/werewolf/WerewolfPort.h"
#include "games/werewolf/WerewolfUI.h"
#include "RtcClock.h"
#include "SdLog.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

static bool s_ready = false;

// 開発用：シリアルで 'S' を受け取ったら画面をそのまま送る（tools/snapshot.py で PNG 化）
static void sendSnapshot()
{
    lv_obj_t *scr = lv_scr_act();
    const uint32_t size = lv_snapshot_buf_size_needed(scr, LV_IMG_CF_TRUE_COLOR);
    uint8_t *buf = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (buf == nullptr) {
        Serial.println("[SNAP] no memory");
        return;
    }
    lv_img_dsc_t dsc;
    if (lv_snapshot_take_to_buf(scr, LV_IMG_CF_TRUE_COLOR, &dsc, buf, size) == LV_RES_OK) {
        Serial.printf("\n[SNAP] %d %d %lu\n", dsc.header.w, dsc.header.h, (unsigned long)dsc.data_size);
        Serial.write(dsc.data, dsc.data_size);
        Serial.flush();
        Serial.println("\n[SNAP] END");
    } else {
        Serial.println("[SNAP] failed");
    }
    heap_caps_free(buf);
}

// 人狼の秘密（役職・占い結果・投票先）や POKER TABLE の手札が映っている間は、
// 開発用コマンドで画面を送ったり切り替えたりしない。
// スクリーンショットが秘密ごと PC に渡るのを防ぐ
// 開発用：POKER TABLE の手札が出ていてもスクリーンショットを許す（シリアル O で切り替え。起動時は必ず不許可）。
// 持ち主が手元で画面を確かめるためのもので、人狼の秘密には効かない
static bool s_dev_peek = false;

static bool blockedBySecret()
{
    if (werewolf::secretOnScreen()) {
        Serial.println("[DEV] ignored: a werewolf secret is on screen");
        return true;
    }
    if (cards::privateOnScreen()) {
        Serial.println("[DEV] ignored: a POKER TABLE hand is on screen");
        return true;
    }
    return false;
}

// 開発用："P<x>,<y>改行" でその座標をタップする（画面確認の自動化。秘密の表示はできない）
static void tapFromSerial()
{
    Serial.setTimeout(1000);
    const String arg = Serial.readStringUntil('\n');
    const int comma = arg.indexOf(',');
    if (comma <= 0) {
        return;
    }
    const int x = arg.substring(0, comma).toInt(), y = arg.substring(comma + 1).toInt();
    lvgl_port_debug_tap((int16_t)x, (int16_t)y, 120);
    Serial.printf("[TAP] %d,%d\n", x, y);   // tools/uiwalk.py はこの返事を待ってから次へ進む
}

// PC から "C<UNIX秒>改行" を受け取り、時刻を設定して時計チップにも保存する（Wi-Fi が使えない場所用）
static void setTimeFromSerial()
{
    Serial.setTimeout(1000);
    const String arg = Serial.readStringUntil('\n');
    const long long epoch = atoll(arg.c_str());
    if (epoch < 1700000000LL) {
        Serial.printf("[TIME] invalid value: %s\n", arg.c_str());
        return;
    }
    const struct timeval tv = {(time_t)epoch, 0};
    settimeofday(&tv, nullptr);
    const time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    Serial.printf("[TIME] set to %04d-%02d-%02d %02d:%02d:%02d JST\n",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    rtc::saveSystemTime();
    sdlog::event("timeset", cup::taken(), cup::remaining(), cup::remaining(), "from serial");
}

static bool allDigits(const String &s)
{
    if (s.length() == 0) {
        return false;
    }
    for (size_t i = 0; i < s.length(); ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    return true;
}

/**
 * 開発用：誤って入った記録を消す。杯数はこの製品のいちばん大事なデータなので、
 * **合言葉を同じ行に書かないと動かない**（'Y' を 1 文字打っただけでは何も起きない）。
 *   Y!undo                  … 今日の杯数を 1 減らす（残り杯数 left はそのまま）
 *   Y!hist,YYYYMMDD,杯数    … 履歴の輪にあるその日の杯数を書き換える（その日が輪に無ければ断る）
 * どちらも GAS（シート）へは何も送らない。シートの行は手で消すこと
 */
static void undoFromSerial()
{
    Serial.setTimeout(1000);
    String arg = Serial.readStringUntil('\n');
    arg.trim();     // 改行の種類の違い（末尾の \r）と前後の空白を落とす

    if (arg == "!undo") {
        home::debugUndoCup();       // 結果（taken n->m／減らせなかった理由）は cup:: 側が表示する
        return;
    }

    if (arg.startsWith("!hist,")) {
        const String rest = arg.substring(6);
        const int comma = rest.indexOf(',');
        if (comma > 0) {
            const String ymd_s = rest.substring(0, comma);
            const String cups_s = rest.substring(comma + 1);
            if (allDigits(ymd_s) && ymd_s.length() == 8 && allDigits(cups_s) && cups_s.length() <= 3) {
                const uint32_t ymd = strtoul(ymd_s.c_str(), nullptr, 10);
                const uint32_t cups = strtoul(cups_s.c_str(), nullptr, 10);
                cup::DayRecord rec = {};
                if (!cup::historyFor(ymd, rec)) {
                    Serial.printf("[DEV] histfix: %lu is not in the history ring\n", (unsigned long)ymd);
                    return;
                }
                if (!cup::setHistoryCups(ymd, (uint16_t)cups)) {
                    Serial.println("[DEV] histfix: save failed (history kept as it was)");
                    return;
                }
                Serial.printf("[CUP] histfix: %lu cups %u->%lu (refills/games kept)\n",
                              (unsigned long)ymd, (unsigned)rec.cups, (unsigned long)cups);
                char note[48];
                snprintf(note, sizeof(note), "%lu %u->%lu", (unsigned long)ymd,
                         (unsigned)rec.cups, (unsigned long)cups);
                sdlog::event("histfix", cup::taken(), cup::remaining(), cup::remaining(), note);
                return;
            }
        }
    }

    Serial.printf("[DEV] ignored: bad input \"%s\" (use Y!undo or Y!hist,YYYYMMDD,cups)\n", arg.c_str());
}

void setup()
{
    Serial.begin(115200);
    // 起動の理由は今しか読めないので最初に控える（「システム情報」と SD の操作ログで使う）
    sysinfo::begin();
    // 時刻は日本時間で扱う（Wi-Fi 接続前・RTC から復元した時刻にも適用するため最初に設定）
    setenv("TZ", "JST-9", 1);
    tzset();

    Serial.println("Initializing board");
    Board *board = new Board();
    board->init();
#if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = board->getLCD();
    // When avoid tearing function is enabled, the frame buffer number should be set in the board driver
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto lcd_bus = lcd->getBus();
    if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
    }
#endif
#endif
    if (!board->begin()) {
        Serial.println("ERROR: board begin failed");
        return;
    }

    // 人狼ゲームはバックライトを切って覗き見を防ぐため、Board を渡しておく
    // （実際の消灯・点灯は display:: が行う）
    werewolf::setBoard(board);

    // 設定 →（明るさに使うので）画面 → 操作音 の順に用意する
    settings::load();
    display::begin(board);
    buzzer::begin(board);

    // Wi-Fi が無くても日付が分かるよう、時計チップから時刻を復元（I2C は board->begin() で初期化済み）
    const bool rtc_ok = rtc::restoreSystemTime();

    // 操作ログ用の microSD（LCD と信号線を共用しているので board->begin() の後。無くても動く）
    sdlog::begin(board);

    Serial.println("Initializing LVGL");
    if (!lvgl_port_init(board->getLCD(), board->getTouch())) {
        Serial.println("ERROR: LVGL port init failed");
        return;
    }

    cup::load();
    battery::begin();
    battery::update();

    char boot_note[40];
    snprintf(boot_note, sizeof(boot_note), "reset=%s rtc=%s", sysinfo::resetReasonId(), rtc_ok ? "ok" : "lost");
    sdlog::event("boot", cup::taken(), cup::remaining(), cup::remaining(), boot_note);

    Serial.println("Creating UI");
    if (!lvgl_port_lock(-1)) {
        Serial.println("ERROR: LVGL lock failed");
        return;
    }
    const bool ui_ok = home::create();
    ui::begin(lv_scr_act());
    lvgl_port_unlock();
    if (!ui_ok) {
        Serial.println("ERROR: COFFEE TIME UI creation failed");
        return;
    }

    net::begin();
    remote::begin();    // Wi-Fi 越しのコンソール（TCP 2323）とソフトの更新（TCP 2324）
    s_ready = true;
    Serial.println("COFFEE TIME ready");
}

void loop()
{
    if (!s_ready) {
        delay(1000);
        return;
    }

    static uint32_t s_last_bat_ms = 0;
    static uint32_t s_last_bat_log_ms = 0;
    if (millis() - s_last_bat_ms >= 1000) {
        s_last_bat_ms = millis();
        battery::update();
        if (millis() - s_last_bat_log_ms >= 5000) {
            s_last_bat_log_ms = millis();
            Serial.printf("[BAT] %lu mV (%d%%)\n", (unsigned long)battery::millivolts(), battery::percent());
        }
    }

    net::Weather weather;
    const bool got_weather = net::poll(weather);
    // 遠隔コンソールの接続・合言葉・ソフトの更新（更新中はここで数十秒止まる）。LVGL のロックの外で呼ぶ
    remote::poll();
    bool dump_log = false;

    if (lvgl_port_lock(-1)) {
        if (got_weather) {
            home::setWeather(weather);
        }
        cup::saveIfDirty();
        // 明るさ・自動暗転の面倒を見る（透明な板の出し入れで LVGL を触るのでロックの中）
        display::poll();
        while (Serial.available() > 0) {
            const int cmd = Serial.read();
            switch (cmd) {
            // O で許可したときは POKER TABLE の手札が出ていても撮る（画面の切り替えの禁止は変えない。人狼の秘密は常に不可）
            case 'S': if ((s_dev_peek && !werewolf::secretOnScreen()) || !blockedBySecret()) { sendSnapshot(); } break;
            // 開発用：背景の時間帯を固定 M=朝 N=昼 E=夕方 A=自動
            case 'M': home::debugForceHour(8); break;
            case 'N': home::debugForceHour(13); break;
            case 'E': home::debugForceHour(19); break;
            case 'A': home::debugForceHour(-1); break;
            // 開発用：T=+1 / R=補充（通知やシート記録も実際に行われる）
            case 'T': home::debugTake(); break;
            case 'R': home::debugRefill(); break;
            case 'W': net::debugScan(); break;       // 開発用：Wi-Fi スキャン
            // 開発用：画面遷移の確認（スクリーンショット用）。
            // 秘密が映っている間は切り替えない（覗き見防止の手順を飛ばさないため）。
            // 秘密が出ていなければ、進行中でも '0' で抜けられる（局は無効になる）
            case '1': if (!blockedBySecret()) { ui::push(ui::createMainMenu); } break;
            case '2': if (!blockedBySecret()) { ui::push(werewolf::createEntryScreen); } break;
            case '3': if (!blockedBySecret()) { ui::push(werewolf::createGameScreen); } break;
            case '4': if (!blockedBySecret()) { ui::push(detective::createGameScreen); } break;
            // 開発用：メニューの中身を直接開く（tools/uiwalk.py で撮るため）
            case '5': if (!blockedBySecret()) { ui::push(ui::createTodayScreen); } break;
            case '6': if (!blockedBySecret()) { ui::push(ui::createTodayHourlyScreen); } break;
            case '7': if (!blockedBySecret()) { ui::push(ui::createHistoryScreen); } break;
            case '8': if (!blockedBySecret()) { ui::push(ui::createHistoryListScreen); } break;
            case '9': if (!blockedBySecret()) { ui::push(ui::createSettingsScreen); } break;
            case 'B': if (!blockedBySecret()) { ui::pushSettingsSub(ui::SettingsSub::Brightness); } break;
            case 'F': if (!blockedBySecret()) { ui::pushSettingsSub(ui::SettingsSub::TimeSet); } break;
            case 'I': if (!blockedBySecret()) { ui::pushSettingsSub(ui::SettingsSub::SystemInfo); } break;
            case '0': if (!blockedBySecret()) { ui::goHome(); } break;
            // 開発用：人狼の今の場面を表示（公開情報のみ。役職や投票先は出さない）
            case 'G': werewolf::debugPrintPublicState(); break;
            // 開発用：探偵の今の場面を表示（画面・話・ページ・既読・ヒント段階）
            case 'D': detective::debugPrintPublicState(); break;
            // 開発用：AI DUEL の今の場面を表示（画面・アイコン・回数・点数・相手の種類）。
            // **プレイヤーが手を選ぶ前に AI の手は出さない**（後出しに見えないようにするため）
            case 'U': duel::debugPrintPublicState(); break;
            // 開発用：エスパーの今の場面を表示（画面・モード・問数・残り候補数）
            case 'V': esper::debugPrintPublicState(); break;
            // 開発用：リバーシの今の場面を表示（画面・盤・手番・枚数・直前の手）。
            // **盤面は公開情報**なので、そのまま出してよい（人狼の秘密とは違う）
            case 'J': reversi::debugPrintPublicState(); break;
            // 開発用：POKER TABLE の今の場面を表示（画面・卓・進み具合・点数）。
            // **手札と、公開前の相手の選択は絶対に出さない**（秘密は端末の中だけ）
            case 'K': cards::debugPrintPublicState(); break;
            // 開発用：画面の重なりの数（1 = HOME）。tools/uiwalk.py が「HOME ではない」ことを確かめてからタップするために使う
            case 'Q': Serial.printf("[UI] view=stack depth=%d\n", ui::depth()); break;
            // 開発用：POKER TABLE の手札が出ている画面のスクリーンショットを許す／禁じる（既定は禁止）
            case 'O':
                s_dev_peek = !s_dev_peek;
                Serial.printf("[DEV] snapshot of private card views: %s\n", s_dev_peek ? "ALLOWED (dev)" : "blocked");
                break;
            // 開発用：探偵の記録を消す（試験で「初回の結果」を使い切らないため）
            case 'X': detective::debugResetProgress(); break;
            case 'C': setTimeFromSerial(); break;    // PC の時計から時刻を設定（tools/settime.py）
            case 'P': tapFromSerial(); break;        // 開発用：P<x>,<y> でタップ
            // 開発用：誤って入った記録を消す。合言葉が同じ行に無ければ何もしない（打ち間違い避け）。
            //   Y!undo                … 今日の杯数を 1 減らす（残り杯数 left はそのまま・GAS へは送らない）
            //   Y!hist,YYYYMMDD,杯数  … 履歴の輪にあるその日の杯数を書き換える（無い日は断る）
            case 'Y': undoFromSerial(); break;
            case 'L': dump_log = true; break;        // 開発用：SD の操作ログの末尾を表示
            default: break;
            }
            // 画面を切り替える命令には受領の返事を返す。tools/uiwalk.py はこれを待ってから次のタップを送る
            // （起動直後など loop が止まっている間に「切替 → タップ」が続けて処理され、タップが前の画面に当たるのを防ぐ）
            if (cmd > 0 && strchr("0123456789BFI", cmd) != nullptr) {
                Serial.printf("[KEY] %c\n", (char)cmd);
            }
        }
        lvgl_port_unlock();
    }

    // SD への書き込みは画面を止めないよう、LVGL のロックの外で行う
    sdlog::poll();
    if (dump_log) {
        sdlog::dumpTail(Serial);
    }
    delay(200);
}
