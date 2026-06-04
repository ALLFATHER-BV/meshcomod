#include "TouchDiagTrace.h"

#if defined(ESP32_PLATFORM) && defined(HAS_HELTEC_V4_CAP_TOUCH) && defined(UI_LVGL)

static void (*s_touch_diag_cb)(const char*) = nullptr;
static uint32_t s_boot_count = 0;
static uint8_t s_boot_reason = 0;

void meshcomod_touch_set_boot_stats(uint32_t count, uint8_t reason) {
  s_boot_count = count;
  s_boot_reason = reason;
}

uint32_t meshcomod_touch_boot_count(void) { return s_boot_count; }

uint8_t meshcomod_touch_boot_reason_code(void) { return s_boot_reason; }

void touchDiagTraceRegister(void (*fn)(const char* line)) { s_touch_diag_cb = fn; }

void touchDiagTraceLine(const char* line) {
  if (!line || !line[0] || !s_touch_diag_cb) return;
  s_touch_diag_cb(line);
}

#endif
