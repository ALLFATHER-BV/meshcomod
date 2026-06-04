#pragma once

#include <stdint.h>

#if defined(ESP32_PLATFORM) && defined(HAS_HELTEC_V4_CAP_TOUCH) && defined(UI_LVGL)

void touchDiagTraceRegister(void (*fn)(const char* line));
void touchDiagTraceLine(const char* line);

uint32_t meshcomod_touch_boot_count(void);
uint8_t meshcomod_touch_boot_reason_code(void);
void meshcomod_touch_set_boot_stats(uint32_t count, uint8_t reason);

#else

static inline void touchDiagTraceRegister(void (*)(const char*)) {}
static inline void touchDiagTraceLine(const char*) {}
static inline uint32_t meshcomod_touch_boot_count(void) { return 0; }
static inline uint8_t meshcomod_touch_boot_reason_code(void) { return 0; }
static inline void meshcomod_touch_set_boot_stats(uint32_t, uint8_t) {}
#endif
