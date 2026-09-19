#include "CupState.h"

#include <Arduino.h>
#include <Preferences.h>

namespace cup {

static uint32_t s_ymd = 0;          // 記録中の日付 (YYYYMMDD)。時刻未取得なら 0
static uint32_t s_taken = 0;
static uint32_t s_remaining = 0;
static volatile bool s_dirty = false;

void load()
{
    Preferences prefs;
    prefs.begin("cup", true);
    s_ymd = prefs.getUInt("ymd", 0);
    s_taken = prefs.getUInt("taken", 0);
    s_remaining = prefs.getUInt("left", 0);
    prefs.end();
    if (s_remaining > kMaxCups) {
        s_remaining = kMaxCups;
    }
    Serial.printf("[CUP] loaded ymd=%lu taken=%lu left=%lu\n",
                  (unsigned long)s_ymd, (unsigned long)s_taken, (unsigned long)s_remaining);
}

void saveIfDirty()
{
    if (!s_dirty) {
        return;
    }
    s_dirty = false;
    Preferences prefs;
    prefs.begin("cup", false);
    prefs.putUInt("ymd", s_ymd);
    prefs.putUInt("taken", s_taken);
    prefs.putUInt("left", s_remaining);
    prefs.end();
}

void takeOne()
{
    s_taken++;
    if (s_remaining > 0) {
        s_remaining--;
    }
    s_dirty = true;
    Serial.printf("[CUP] +1 taken=%lu left=%lu\n", (unsigned long)s_taken, (unsigned long)s_remaining);
}

void refill()
{
    s_remaining = kMaxCups;
    s_dirty = true;
    Serial.printf("[CUP] refill left=%lu\n", (unsigned long)s_remaining);
}

void checkNewDay(uint32_t ymd)
{
    if (ymd == 0 || ymd == s_ymd) {
        return;
    }
    // 時刻取得前 (s_ymd==0) に数えた分は、その日の分として引き継ぐ
    if (s_ymd != 0) {
        s_taken = 0;
        s_remaining = 0;    // 新しい日はまだ作っていない扱い。作ったら長押しで補充する
        Serial.printf("[CUP] new day %lu: reset\n", (unsigned long)ymd);
    }
    s_ymd = ymd;
    s_dirty = true;
}

uint32_t taken()
{
    return s_taken;
}

uint32_t remaining()
{
    return s_remaining;
}

}  // namespace cup
