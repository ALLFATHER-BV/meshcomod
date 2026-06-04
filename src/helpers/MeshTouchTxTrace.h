#pragma once

#if defined(ESP32_PLATFORM) && defined(HAS_HELTEC_V4_CAP_TOUCH) && defined(UI_LVGL)

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "TouchDiagTrace.h"

/** Append hex for up to `n` bytes from `src` into `out` (NUL-terminated, capped). */
static inline void mesh_touch_hex_prefix(const uint8_t* src, int n, char* out, size_t out_cap) {
  if (!out || out_cap < 3) return;
  out[0] = '\0';
  if (!src || n <= 0) return;
  size_t p = 0;
  for (int i = 0; i < n && p + 3 < out_cap; ++i) {
    snprintf(out + p, out_cap - p, "%02X", (unsigned)src[i]);
    p += 2;
  }
}

static inline void mesh_touch_tx_tracef(const char* fmt, ...) {
  char buf[224];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.println(buf);
  touchDiagTraceLine(buf);
}

#else

#include <stddef.h>
#include <stdint.h>

static inline void mesh_touch_hex_prefix(const uint8_t*, int, char*, size_t) {}

static inline void mesh_touch_tx_tracef(const char*, ...) {}

#endif
