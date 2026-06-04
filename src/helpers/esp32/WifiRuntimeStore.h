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

/* "Wi-Fi chosen" sticky flag: set whenever the radio is explicitly enabled
 * (setRadioEnabled(true)). Lets the touch build bring Wi-Fi up to scan/configure
 * before any credentials exist. A fresh device never explicitly enables the
 * (default-on) radio, so it stays on BLE until the user actually picks Wi-Fi. */
bool wifiConfigGetWifiChosen();
void wifiConfigSetWifiChosen(bool chosen);

/* The actual BLE-vs-Wi-Fi transport decision used at boot and in the main loop.
 * True = bring Wi-Fi up (STA). Classic rule = radio enabled AND creds present;
 * on the touch build it's also true when the user has chosen Wi-Fi but has no
 * creds yet (so they can scan + pick a network on-device). */
bool wifiConfigWantsWifi();

/* Request an immediate (re)apply of current Wi-Fi settings from the main loop.
 * Use after changing SSID/PWD or after Set* writes from another task/context —
 * avoids calling WiFi.disconnect/begin from an LVGL event handler. */
void wifiConfigRequestApply();
bool wifiConfigConsumeApplyRequest();

#endif
