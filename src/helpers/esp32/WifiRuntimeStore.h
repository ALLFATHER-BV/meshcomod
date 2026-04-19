#pragma once

/* NVS Wi-Fi credentials: namespace meshcomod; used by companion_radio and repeater_tcp. */
#if defined(ESP32)

#include <stddef.h>

#define WIFI_CONFIG_SSID_MAX 32
#define WIFI_CONFIG_PWD_MAX 64

void wifiConfigBegin();
bool wifiConfigHasRuntime();
void wifiConfigGetSsid(char *buf, size_t len);
void wifiConfigGetPwd(char *buf, size_t len);
bool wifiConfigSetSsid(const char *ssid);
bool wifiConfigSetPwd(const char *pwd);
void wifiConfigClear();
void wifiConfigApply();

bool wifiConfigGetRadioEnabled();
void wifiConfigSetRadioEnabled(bool enabled);

#endif
