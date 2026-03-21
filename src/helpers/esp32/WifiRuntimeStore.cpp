#include "WifiRuntimeStore.h"

#if defined(ESP32)

#include <Preferences.h>
#include <WiFi.h>
#include <cstring>

static const char *WIFI_CONFIG_NAMESPACE = "meshcomod";
static const char *WIFI_CONFIG_SSID_KEY = "wifi_ssid";
static const char *WIFI_CONFIG_PWD_KEY = "wifi_pwd";
static const char *WIFI_CONFIG_RADIO_EN_KEY = "wifi_radio_en";

static Preferences s_prefs;
static bool s_begun = false;

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
  s_prefs.end();
  s_begun = s_prefs.begin(WIFI_CONFIG_NAMESPACE, true);

  if (!enabled) {
    WiFi.disconnect(true);
    delay(50);
    WiFi.mode(WIFI_OFF);
    return;
  }
  WiFi.mode(WIFI_STA);
  if (!wifiConfigHasRuntime()) return;
  char ssid[WIFI_CONFIG_SSID_MAX];
  char pwd[WIFI_CONFIG_PWD_MAX];
  wifiConfigGetSsid(ssid, sizeof(ssid));
  wifiConfigGetPwd(pwd, sizeof(pwd));
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid, pwd[0] ? pwd : nullptr);
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
