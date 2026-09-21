#include "SysInfo.h"

#include <Arduino.h>
#include <esp_system.h>

#ifndef CT_GIT_REV
// ビルド前スクリプト（tools/git_rev.py）が git から取って渡す。git が使えない環境でも
// ビルドは通したいので、ここで既定値を用意しておく
#define CT_GIT_REV "unknown"
#endif

namespace sysinfo {

namespace {
const char *s_reason = "other";
}

void begin()
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  s_reason = "poweron"; break;
    case ESP_RST_SW:       s_reason = "software"; break;
    case ESP_RST_PANIC:    s_reason = "panic"; break;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      s_reason = "watchdog"; break;
    case ESP_RST_BROWNOUT: s_reason = "brownout"; break;
    case ESP_RST_USB:      s_reason = "usb"; break;
    default:               s_reason = "other"; break;
    }
}

const char *resetReasonId()
{
    return s_reason;
}

uint32_t uptimeSeconds()
{
    return (uint32_t)(millis() / 1000);
}

const char *revision()
{
    return CT_GIT_REV;
}

}  // namespace sysinfo
