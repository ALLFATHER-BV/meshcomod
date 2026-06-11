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

/* BLE radio on/off, persisted independently of the Wi-Fi radio (default ON) so
 * the two can be toggled separately and coexist. The companion BLE toggle writes
 * this; boot honors it. Fixes "BT turned off reverts to on after reboot". */
bool wifiConfigGetBleEnabled();
void wifiConfigSetBleEnabled(bool enabled);

/* "Wi-Fi chosen" sticky flag: set whenever the radio is explicitly enabled
 * (setRadioEnabled(true)). Historically gated whether the touch build brought
 * Wi-Fi up with no creds; now vestigial — touch defaults to Wi-Fi whenever the
 * radio is on (see wifiConfigWantsWifi). Kept for compatibility. */
bool wifiConfigGetWifiChosen();
void wifiConfigSetWifiChosen(bool chosen);

/* The actual BLE-vs-Wi-Fi transport decision used at boot and in the main loop.
 * True = bring Wi-Fi up (STA). Non-touch rule = radio enabled AND creds present.
 * On the touch build Wi-Fi is the primary transport: radio-enabled (the fresh
 * default) is enough — Wi-Fi comes up scannable even with no creds, so a brand
 * new device lands on Wi-Fi with BLE off. BLE is opt-in (clears radio_en). */
bool wifiConfigWantsWifi();

/* Request an immediate (re)apply of current Wi-Fi settings from the main loop.
 * Use after changing SSID/PWD or after Set* writes from another task/context —
 * avoids calling WiFi.disconnect/begin from an LVGL event handler. */
void wifiConfigRequestApply();
bool wifiConfigConsumeApplyRequest();

#endif
