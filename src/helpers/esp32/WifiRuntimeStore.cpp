#include "WifiRuntimeStore.h"

#if defined(ESP32)

#include <Preferences.h>
#include <WiFi.h>
#include <cstring>

static const char *WIFI_CONFIG_NAMESPACE = "meshcomod";
static const char *WIFI_CONFIG_SSID_KEY = "wifi_ssid";
static const char *WIFI_CONFIG_PWD_KEY = "wifi_pwd";
static const char *WIFI_CONFIG_RADIO_EN_KEY = "wifi_radio_en";
static const char *WIFI_CONFIG_WIFI_CHOSEN_KEY = "wifi_chosen";

static Preferences s_prefs;
static bool s_begun = false;
static volatile bool s_wifi_apply_requested = false;

void wifiConfigBegin() {
  if (s_begun) return;
  s_begun = s_prefs.begin(WIFI_CONFIG_NAMESPACE, true);
  if (!s_begun) {
    if (s_prefs.begin(WIFI_CONFIG_NAMESPACE, false)) {
      s_prefs.end();
      s_begun = s_prefs.begin(WIFI_CONFIG_NAMESPACE, true);
    }
  }
}

bool wifiConfigHasRuntime() {
  if (!s_begun) wifiConfigBegin();
  if (!s_prefs.isKey(WIFI_CONFIG_SSID_KEY)) return false;
  char ssid[WIFI_CONFIG_SSID_MAX];
  s_prefs.getString(WIFI_CONFIG_SSID_KEY, ssid, sizeof(ssid));
  return ssid[0] != '\0';
}

void wifiConfigGetSsid(char *buf, size_t len) {
  if (!buf || len == 0) return;
  buf[0] = '\0';
  if (!s_begun) wifiConfigBegin();
  if (s_prefs.isKey(WIFI_CONFIG_SSID_KEY))
    s_prefs.getString(WIFI_CONFIG_SSID_KEY, buf, len);
  buf[len - 1] = '\0';
}

void wifiConfigGetPwd(char *buf, size_t len) {
  if (!buf || len == 0) return;
  buf[0] = '\0';
  if (!s_begun) wifiConfigBegin();
  if (s_prefs.isKey(WIFI_CONFIG_PWD_KEY))
    s_prefs.getString(WIFI_CONFIG_PWD_KEY, buf, len);
  buf[len - 1] = '\0';
}

bool wifiConfigSetSsid(const char *ssid) {
  if (!ssid) return false;
  size_t n = strlen(ssid);
  if (n >= WIFI_CONFIG_SSID_MAX) return false;
  if (!s_begun) {
    wifiConfigBegin();
  }
  s_prefs.end();
  if (!s_prefs.begin(WIFI_CONFIG_NAMESPACE, false)) return false;
  bool ok = s_prefs.putString(WIFI_CONFIG_SSID_KEY, ssid);
  s_prefs.end();
  s_begun = s_prefs.begin(WIFI_CONFIG_NAMESPACE, true);
  return ok;
}

bool wifiConfigSetPwd(const char *pwd) {
  if (!pwd) pwd = "";
  size_t n = strlen(pwd);
  if (n >= WIFI_CONFIG_PWD_MAX) return false;
  if (!s_begun) {
    wifiConfigBegin();
  }
  s_prefs.end();
  if (!s_prefs.begin(WIFI_CONFIG_NAMESPACE, false)) return false;
  bool ok = s_prefs.putString(WIFI_CONFIG_PWD_KEY, pwd);
  s_prefs.end();
  s_begun = s_prefs.begin(WIFI_CONFIG_NAMESPACE, true);
  return ok;
}

void wifiConfigClear() {
  if (!s_begun) wifiConfigBegin();
  s_prefs.end();
  s_prefs.begin(WIFI_CONFIG_NAMESPACE, false);
  s_prefs.remove(WIFI_CONFIG_SSID_KEY);
  s_prefs.remove(WIFI_CONFIG_PWD_KEY);
  s_prefs.end();
  s_begun = s_prefs.begin(WIFI_CONFIG_NAMESPACE, true);
}

bool wifiConfigGetRadioEnabled() {
  if (!s_begun) wifiConfigBegin();
  return s_prefs.getUChar(WIFI_CONFIG_RADIO_EN_KEY, 1) != 0;
}

void wifiConfigSetRadioEnabled(bool enabled) {
  if (!s_begun) wifiConfigBegin();
  s_prefs.end();
  if (!s_prefs.begin(WIFI_CONFIG_NAMESPACE, false)) return;
  s_prefs.putUChar(WIFI_CONFIG_RADIO_EN_KEY, enabled ? 1 : 0);
  // Enabling the radio is an explicit "use Wi-Fi" choice — remember it (sticky)
  // so the touch build can bring Wi-Fi up to scan/configure even before any
  // creds exist (see wifiConfigWantsWifi). Fresh devices never call this with
  // the default-on pref, so they stay on BLE until the user actually picks
  // Wi-Fi. Non-touch builds ignore the flag.
  if (enabled) s_prefs.putUChar(WIFI_CONFIG_WIFI_CHOSEN_KEY, 1);
  s_prefs.end();
  s_begun = s_prefs.begin(WIFI_CONFIG_NAMESPACE, true);

  /* Defer the actual WiFi.mode / disconnect / begin to the main loop so we
   * never touch the WiFi state machine from within an LVGL event handler.
   * Calling WiFi.disconnect()+begin() from the LV event ctx was racing with
   * the main wifi loop and could leave the radio in WIFI_OFF mode. */
  s_wifi_apply_requested = true;
}

bool wifiConfigGetWifiChosen() {
  if (!s_begun) wifiConfigBegin();
  return s_prefs.getUChar(WIFI_CONFIG_WIFI_CHOSEN_KEY, 0) != 0;
}

void wifiConfigSetWifiChosen(bool chosen) {
  if (!s_begun) wifiConfigBegin();
  s_prefs.end();
  if (!s_prefs.begin(WIFI_CONFIG_NAMESPACE, false)) return;
  s_prefs.putUChar(WIFI_CONFIG_WIFI_CHOSEN_KEY, chosen ? 1 : 0);
  s_prefs.end();
  s_begun = s_prefs.begin(WIFI_CONFIG_NAMESPACE, true);
}

bool wifiConfigWantsWifi() {
  // Radio off -> BLE (or no transport). Radio on + creds -> Wi-Fi (classic).
  if (!wifiConfigGetRadioEnabled()) return false;
  if (wifiConfigHasRuntime()) return true;
#ifdef HAS_TOUCH_UI
  // Touch: the user can pick Wi-Fi with no creds yet and scan on-device, so
  // bring the radio up (STA, scannable) in that case too.
  return wifiConfigGetWifiChosen();
#else
  return false;
#endif
}

void wifiConfigRequestApply() {
  s_wifi_apply_requested = true;
}

bool wifiConfigConsumeApplyRequest() {
  if (!s_wifi_apply_requested) return false;
  s_wifi_apply_requested = false;
  return true;
}

void wifiConfigApply() {
  if (!wifiConfigGetRadioEnabled()) {
    WiFi.disconnect(true);
    delay(50);
    WiFi.mode(WIFI_OFF);
    return;
  }
  if (!wifiConfigHasRuntime()) return;
  char ssid[WIFI_CONFIG_SSID_MAX];
  char pwd[WIFI_CONFIG_PWD_MAX];
  wifiConfigGetSsid(ssid, sizeof(ssid));
  wifiConfigGetPwd(pwd, sizeof(pwd));
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid, pwd[0] ? pwd : nullptr);
}

#endif
