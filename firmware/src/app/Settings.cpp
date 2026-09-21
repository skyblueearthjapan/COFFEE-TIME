#include "Settings.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_rom_crc.h>

#include <string.h>

namespace settings {

const uint32_t kDimSeconds[kDimChoiceCount] = {60, 300, 600, 1800, 0};

namespace {

constexpr const char *kNamespace = "cfg";
constexpr const char *kKey = "v1";
constexpr uint16_t kVersion = 1;

// NVS に置く並び。末尾の crc は「その手前までのバイト列」に対して計算する。
// 項目を足すときは version を上げ、古い版は既定値で読み直すこと（杯数には影響させない）
struct __attribute__((packed)) Blob {
    uint16_t version;
    uint8_t brightness;      // 10〜100
    uint8_t dim_choice;      // 0〜4
    uint8_t sound;           // 0/1
    uint8_t max_cups;        // 5〜15
    uint8_t morning_full;    // 0/1
    uint8_t reserved;
    uint32_t crc;
};
static_assert(sizeof(Blob) == 12, "cfg blob layout changed");

Blob s_cfg = {kVersion, 100, 1, 0, 10, 0, 0, 0};

uint32_t crcOf(const Blob &b)
{
    return esp_rom_crc32_le(0, (const uint8_t *)&b, sizeof(Blob) - sizeof(uint32_t));
}

uint8_t clampU8(int v, int lo, int hi)
{
    return (uint8_t)(v < lo ? lo : (v > hi ? hi : v));
}

// 読み込んだ値が範囲外でも落とさず、丸めて使う
void sanitize()
{
    s_cfg.brightness = clampU8((s_cfg.brightness + 5) / 10 * 10, 10, 100);
    s_cfg.dim_choice = clampU8(s_cfg.dim_choice, 0, kDimChoiceCount - 1);
    s_cfg.sound = s_cfg.sound ? 1 : 0;
    s_cfg.max_cups = clampU8(s_cfg.max_cups, kMinCups, kMaxCupsLimit);
    s_cfg.morning_full = s_cfg.morning_full ? 1 : 0;
}

}  // namespace

void load()
{
    Blob read = {};
    Preferences prefs;
    bool ok = false;
    if (prefs.begin(kNamespace, true)) {
        if (prefs.getBytesLength(kKey) == sizeof(Blob) &&
            prefs.getBytes(kKey, &read, sizeof(Blob)) == sizeof(Blob)) {
            ok = read.version == kVersion && read.crc == crcOf(read);
        }
        prefs.end();
    }
    if (ok) {
        s_cfg = read;
        sanitize();
    } else {
        // 一度も保存していない場合もここに来る（異常ではない）。既定値のまま始める
        Serial.println("[CFG] default settings");
    }
    Serial.printf("[CFG] bright=%u%% dim=%lus sound=%s max=%u morning=%s\n",
                  (unsigned)s_cfg.brightness, (unsigned long)kDimSeconds[s_cfg.dim_choice],
                  s_cfg.sound ? "on" : "off", (unsigned)s_cfg.max_cups,
                  s_cfg.morning_full ? "full" : "0");
}

void save()
{
    sanitize();
    s_cfg.version = kVersion;
    s_cfg.reserved = 0;
    s_cfg.crc = crcOf(s_cfg);

    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        Serial.println("[CFG] save failed (open)");
        return;
    }
    const size_t written = prefs.putBytes(kKey, &s_cfg, sizeof(Blob));
    Blob back = {};
    const size_t read = prefs.getBytes(kKey, &back, sizeof(Blob));
    prefs.end();
    // 設定が消えると明るさや杯数の上限が戻ってしまうので、書いたら必ず読み返す
    if (written != sizeof(Blob) || read != sizeof(Blob) || memcmp(&back, &s_cfg, sizeof(Blob)) != 0) {
        Serial.println("[CFG] save verify FAILED");
        return;
    }
    Serial.println("[CFG] saved");
}

uint8_t brightness() { return s_cfg.brightness; }
uint8_t dimChoice() { return s_cfg.dim_choice; }
uint32_t dimSeconds() { return kDimSeconds[s_cfg.dim_choice]; }
bool sound() { return s_cfg.sound != 0; }
uint8_t maxCups() { return s_cfg.max_cups; }
// 日付が変わったときの残りは常に 0 杯（2026-09-21 ユーザー確認: 朝は必ず 0 から始める運用）。
// 設定画面から項目を外したので、保存値が何であっても 0 杯として扱う。入れ物の形式は変えない
bool morningFull() { return false; }

void setBrightness(uint8_t percent) { s_cfg.brightness = clampU8(percent, 10, 100); }
void setDimChoice(uint8_t index) { s_cfg.dim_choice = clampU8(index, 0, kDimChoiceCount - 1); }
void setSound(bool on) { s_cfg.sound = on ? 1 : 0; }
void setMaxCups(uint8_t cups) { s_cfg.max_cups = clampU8(cups, kMinCups, kMaxCupsLimit); }
void setMorningFull(bool full) { s_cfg.morning_full = full ? 1 : 0; }

}  // namespace settings
