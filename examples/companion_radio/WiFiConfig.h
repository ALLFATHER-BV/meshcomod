#pragma once

#include <cstddef>

/* Runtime WiFi credentials (persisted via ESP32 NVS). Used when set via Meshcomod commands;
 * otherwise compile-time WIFI_SSID/WIFI_PWD are used. Only built for ESP32 WiFi-enabled targets. */

#ifdef ESP32
#if defined(WIFI_SSID) || defined(MULTI_TRANSPORT_COMPANION)

#define WIFI_CONFIG_SSID_MAX 32
#define WIFI_CONFIG_PWD_MAX  64

void wifiConfigBegin();
bool wifiConfigHasRuntime();
void wifiConfigGetSsid(char* buf, size_t len);
void wifiConfigGetPwd(char* buf, size_t len);
bool wifiConfigSetSsid(const char* ssid);
bool wifiConfigSetPwd(const char* pwd);
void wifiConfigClear();
/** Apply stored credentials to WiFi (disconnect and reconnect). No-op if no runtime creds. */
void wifiConfigApply();

#endif
#endif
