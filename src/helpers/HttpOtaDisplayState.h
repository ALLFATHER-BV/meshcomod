#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Heltec OLED: full-screen WiFi OTA progress (set by ESP32Board during URL OTA). */
extern volatile int g_meshcore_http_ota_display_active; /* 0 = off, 1 = on */
extern volatile uint8_t g_meshcore_http_ota_display_pct;  /* 0–100, 0xFF = unknown size */
extern char g_meshcore_http_ota_display_line[28];

#ifdef __cplusplus
}
#endif
