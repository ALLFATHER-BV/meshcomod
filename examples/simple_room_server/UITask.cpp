#include "UITask.h"
#include <Arduino.h>
#include <helpers/CommonCLI.h>

#if defined(ESP32) && defined(DISPLAY_CLASS) && defined(PIN_USER_BTN) && defined(MESHCOMOD_ROOM_MULTITRANSPORT)
#include <WiFi.h>
#include <cctype>
#include <cstring>
#include "MyMesh.h"
#include <helpers/esp32/MultiTransportCompanionInterface.h>
#include <helpers/esp32/WifiRuntimeStore.h>
#include <helpers/HttpOtaDisplayState.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/ui/UIScreen.h>
#include "target.h"

extern MultiTransportCompanionInterface serial_interface;

namespace {

enum {
  ROOM_PAGE_RADIO = 0,
  ROOM_PAGE_NETWORK = 1,
  ROOM_PAGE_BLE = 2,
  ROOM_PAGE_WS = 3,
  ROOM_PAGE_COUNT = 4
};

int s_room_page;
unsigned long s_room_ui_started_at;
char s_room_alert[80];
unsigned long s_room_alert_expiry;

#ifndef ROOM_MT_SPLASH_MS
#define ROOM_MT_SPLASH_MS 3000
#endif
#ifndef TCP_PORT
#define TCP_PORT 5000
#endif
#ifndef WS_PORT
#define WS_PORT 8765
#endif

static void room_mt_set_alert(const char *text, unsigned ms) {
  StrHelper::strncpy(s_room_alert, text, sizeof(s_room_alert));
  s_room_alert_expiry = millis() + ms;
}

/** Same trimming as repeater TCP splash: strip git hex suffix, drop leading meshcomod-. */
static void room_mt_splash_version_buf(char out[12], const char *fw) {
  char work[40];
  if (!fw || !fw[0]) {
    out[0] = '\0';
    return;
  }
  const char *last = strrchr(fw, '-');
  if (last && last > fw + 4) {
    size_t tail = strlen(last + 1);
    bool hex = tail >= 6 && tail <= 9;
    for (const char *p = last + 1; hex && *p; p++) {
      if (!isxdigit((unsigned char)*p)) hex = false;
    }
    if (hex) {
      size_t n = (size_t)(last - fw);
      if (n >= sizeof(work)) n = sizeof(work) - 1;
      memcpy(work, fw, n);
      work[n] = '\0';
    } else {
      StrHelper::strncpy(work, fw, sizeof(work));
    }
  } else {
    StrHelper::strncpy(work, fw, sizeof(work));
  }

  const char *s = work;
  if (!strncmp(s, "meshcomod-", 10)) {
    s += 10;
  }

  int len = (int)strlen(s);
  if (len >= (int)sizeof(out)) {
    len = (int)sizeof(out) - 1;
  }
  memcpy(out, s, (size_t)len);
  out[len] = '\0';
}

static void render_room_mt_boot_splash(DisplayDriver &display) {
  char ver_info[12];
  room_mt_splash_version_buf(ver_info, FIRMWARE_VERSION);

  display.setColor(DisplayDriver::LIGHT);
  display.setTextSize(2);
  display.drawTextCentered(display.width() / 2, 4, "meshcomod");

  display.setTextSize(2);
  display.drawTextCentered(display.width() / 2, 24, ver_info);

  display.setTextSize(1);
  display.drawTextCentered(display.width() / 2, 44, FIRMWARE_BUILD_DATE);
}

static void render_room_mt_radio(DisplayDriver &display, NodePrefs *prefs) {
  char tmp[80];
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setColor(DisplayDriver::GREEN);
  display.print(prefs->node_name);

  display.setCursor(0, 20);
  display.setColor(DisplayDriver::YELLOW);
  snprintf(tmp, sizeof(tmp), "FREQ: %06.3f SF%d", prefs->freq, prefs->sf);
  display.print(tmp);

  display.setCursor(0, 30);
  snprintf(tmp, sizeof(tmp), "BW: %03.2f CR: %d", prefs->bw, prefs->cr);
  display.print(tmp);
}

static void render_room_mt_network(DisplayDriver &display) {
  char tmp[80];
  const int foot_y = display.height() - 11;

  display.setColor(DisplayDriver::BLUE);
  display.setTextSize(1);
  int y = 10;
  display.drawTextCentered(display.width() / 2, y, "Network (TCP)");
  y += 12;

  if (!wifiConfigGetRadioEnabled()) {
    display.setColor(DisplayDriver::RED);
    display.drawTextCentered(display.width() / 2, y, "WiFi radio off");
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextCentered(display.width() / 2, foot_y, "USB: set wifi.radio 1");
    return;
  }

  if (!serial_interface.isTcpEnabled()) {
    display.setColor(DisplayDriver::RED);
    display.drawTextCentered(display.width() / 2, y, "TCP/WS off");
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextCentered(display.width() / 2, foot_y, "ON: long press");
    return;
  }

#if defined(WIFI_SSID)
  const bool compile_cred = (strlen(WIFI_SSID) > 0);
#else
  const bool compile_cred = false;
#endif
  const bool have_runtime = wifiConfigHasRuntime();

  if (WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    if (ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0) {
      display.setColor(DisplayDriver::LIGHT);
      snprintf(tmp, sizeof(tmp), "IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
      display.drawTextCentered(display.width() / 2, y, tmp);
      y += 10;
      snprintf(tmp, sizeof(tmp), "TCP:%d WS:%d", (int)TCP_PORT, (int)WS_PORT);
      display.drawTextCentered(display.width() / 2, y, tmp);
      y += 10;
      display.setColor(DisplayDriver::GREEN);
      snprintf(tmp, sizeof(tmp), "%.32s", WiFi.SSID().c_str());
      display.drawTextCentered(display.width() / 2, y, tmp);
    } else {
      display.setColor(DisplayDriver::RED);
      display.drawTextCentered(display.width() / 2, y, "WiFi connecting...");
    }
  } else {
    String ssid = WiFi.SSID();
    wl_status_t ws = WiFi.status();
    display.setColor(DisplayDriver::RED);
    if (!have_runtime && !compile_cred) {
      display.drawTextCentered(display.width() / 2, y, "WiFi not configured");
    } else if (ssid.length() == 0 || ws == WL_NO_SSID_AVAIL) {
      display.drawTextCentered(display.width() / 2, y, "WiFi connecting...");
    } else {
      display.drawTextCentered(display.width() / 2, y, "WiFi connecting...");
    }
    y += 11;
    display.drawTextCentered(display.width() / 2, y, "IP: 0.0.0.0");
  }

  display.setColor(DisplayDriver::LIGHT);
  display.drawTextCentered(display.width() / 2, foot_y, "off: long press");
}

static void render_room_mt_ble(DisplayDriver &display) {
  char tmp[80];
  const int foot_y = display.height() - 11;

  display.setColor(DisplayDriver::BLUE);
  display.setTextSize(1);
  int y = 10;
  display.drawTextCentered(display.width() / 2, y, "Bluetooth (BLE)");
  y += 12;

#ifdef BLE_PIN_CODE
  if (serial_interface.hasBleCapability()) {
    if (serial_interface.isBleEnabled()) {
      display.setColor(DisplayDriver::GREEN);
      snprintf(tmp, sizeof(tmp), "Pin: %lu", (unsigned long)(uint32_t)BLE_PIN_CODE);
      display.drawTextCentered(display.width() / 2, y, tmp);
      y += 11;

      char peer[24];
      if (serial_interface.getBlePeerAddress(peer, sizeof(peer)) && peer[0] != '\0') {
        display.drawTextCentered(display.width() / 2, y, "Connected");
        y += 11;
        display.drawTextCentered(display.width() / 2, y, peer);
      } else {
        display.drawTextCentered(display.width() / 2, y, "Waiting for device");
      }
    } else {
      display.setColor(DisplayDriver::RED);
      display.drawTextCentered(display.width() / 2, y, "BLE disabled");
    }
  } else {
    display.setColor(DisplayDriver::RED);
    display.drawTextCentered(display.width() / 2, y, "BLE n/a");
  }
#else
  display.setColor(DisplayDriver::RED);
  display.drawTextCentered(display.width() / 2, y, "BLE n/a");
#endif

  display.setColor(DisplayDriver::LIGHT);
#ifdef BLE_PIN_CODE
  if (serial_interface.hasBleCapability()) {
    display.drawTextCentered(display.width() / 2, foot_y,
                             serial_interface.isBleEnabled() ? "OFF: long press" : "ON: long press");
  }
#endif
}

static void render_room_mt_ws(DisplayDriver &display) {
  char tmp[80];
  const int foot_y = display.height() - 11;

  display.setColor(DisplayDriver::BLUE);
  display.setTextSize(1);
  int y = 10;
  display.drawTextCentered(display.width() / 2, y, "WebSocket (WS)");
  y += 12;

  if (!serial_interface.isTcpEnabled()) {
    display.setColor(DisplayDriver::RED);
    display.drawTextCentered(display.width() / 2, y, "Off with TCP");
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextCentered(display.width() / 2, foot_y, "ON: Network page");
    return;
  }

  display.setColor(DisplayDriver::GREEN);
  snprintf(tmp, sizeof(tmp), "Port %d", (int)WS_PORT);
  display.drawTextCentered(display.width() / 2, y, tmp);
  y += 11;

  if (WiFi.status() != WL_CONNECTED) {
    display.setColor(DisplayDriver::YELLOW);
    display.drawTextCentered(display.width() / 2, y, "Waiting WiFi");
    y += 11;
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextCentered(display.width() / 2, y, "WS off until IP");
    display.drawTextCentered(display.width() / 2, foot_y, "mirrors TCP on/off");
    return;
  }

  IPAddress ip = WiFi.localIP();
  if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) {
    display.setColor(DisplayDriver::YELLOW);
    display.drawTextCentered(display.width() / 2, y, "Waiting IP...");
    return;
  }

  display.setColor(DisplayDriver::GREEN);
  snprintf(tmp, sizeof(tmp), "ws://%d.%d.%d.%d:%d", ip[0], ip[1], ip[2], ip[3], (int)WS_PORT);
  display.drawTextCentered(display.width() / 2, y, tmp);
  display.setColor(DisplayDriver::LIGHT);
  display.drawTextCentered(display.width() / 2, foot_y, "mirrors TCP on/off");
}

static void draw_room_page_dots(DisplayDriver &display) {
  const int y = 14;
  const int gap = 10;
  int x = display.width() / 2 - (gap * (ROOM_PAGE_COUNT - 1)) / 2;
  for (int i = 0; i < ROOM_PAGE_COUNT; i++, x += gap) {
    if (i == s_room_page) {
      display.fillRect(x - 1, y - 1, 3, 3);
    } else {
      display.fillRect(x, y, 1, 1);
    }
  }
}

static void render_room_http_ota_screen(DisplayDriver &display) {
  display.setTextSize(1);
  display.setColor(DisplayDriver::GREEN);
  display.drawTextCentered(display.width() / 2, 6, "WiFi OTA");
  display.setColor(DisplayDriver::YELLOW);
  display.drawTextCentered(display.width() / 2, 20, g_meshcore_http_ota_display_line);
  int bw = display.width() - 8;
  int bx = 4;
  int by = 34;
  display.setColor(DisplayDriver::DARK);
  display.fillRect(bx, by, bw, 10);
  display.setColor(DisplayDriver::LIGHT);
  display.drawRect(bx, by, bw, 10);
  int pct = (int)(unsigned)g_meshcore_http_ota_display_pct;
  if (pct >= 0 && pct <= 100) {
    int fill = (bw - 2) * pct / 100;
    if (fill > 0) {
      display.setColor(DisplayDriver::GREEN);
      display.fillRect(bx + 1, by + 1, fill, 8);
    }
  } else {
    display.setColor(DisplayDriver::GREEN);
    int seg = (bw - 2) / 4;
    if (seg > 2) {
      uint32_t t = millis() / 400;
      int phase = (int)(t % 4);
      display.fillRect(bx + 1 + phase * seg, by + 2, seg - 2, 6);
    }
  }
  display.setColor(DisplayDriver::LIGHT);
  display.drawTextCentered(display.width() / 2, 50, "do not power off");
}

}  // namespace

#endif  // ESP32 DISPLAY PIN_USER_BTN MESHCOMOD_ROOM_MULTITRANSPORT

#ifndef USER_BTN_PRESSED
#define USER_BTN_PRESSED LOW
#endif

#define AUTO_OFF_MILLIS      20000  // 20 seconds
#define BOOT_SCREEN_MILLIS   4000   // 4 seconds

// 'meshcore', 128x13px
static const uint8_t meshcore_logo [] PROGMEM = {
    0x3c, 0x01, 0xe3, 0xff, 0xc7, 0xff, 0x8f, 0x03, 0x87, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 
    0x3c, 0x03, 0xe3, 0xff, 0xc7, 0xff, 0x8e, 0x03, 0x8f, 0xfe, 0x3f, 0xfe, 0x1f, 0xff, 0x1f, 0xfe, 
    0x3e, 0x03, 0xc3, 0xff, 0x8f, 0xff, 0x0e, 0x07, 0x8f, 0xfe, 0x7f, 0xfe, 0x1f, 0xff, 0x1f, 0xfc, 
    0x3e, 0x07, 0xc7, 0x80, 0x0e, 0x00, 0x0e, 0x07, 0x9e, 0x00, 0x78, 0x0e, 0x3c, 0x0f, 0x1c, 0x00, 
    0x3e, 0x0f, 0xc7, 0x80, 0x1e, 0x00, 0x0e, 0x07, 0x1e, 0x00, 0x70, 0x0e, 0x38, 0x0f, 0x3c, 0x00, 
    0x7f, 0x0f, 0xc7, 0xfe, 0x1f, 0xfc, 0x1f, 0xff, 0x1c, 0x00, 0x70, 0x0e, 0x38, 0x0e, 0x3f, 0xf8, 
    0x7f, 0x1f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x0e, 0x38, 0x0e, 0x3f, 0xf8, 
    0x7f, 0x3f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x1e, 0x3f, 0xfe, 0x3f, 0xf0, 
    0x77, 0x3b, 0x87, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xfc, 0x38, 0x00, 
    0x77, 0xfb, 0x8f, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xf8, 0x38, 0x00, 
    0x73, 0xf3, 0x8f, 0xff, 0x0f, 0xff, 0x1c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x78, 0x7f, 0xf8, 
    0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfe, 0x3c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x3c, 0x7f, 0xf8, 
    0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfc, 0x3c, 0x0e, 0x1f, 0xf8, 0xff, 0xf8, 0x70, 0x3c, 0x7f, 0xf8, 
};

void UITask::begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version) {
#if defined(ESP32) && defined(DISPLAY_CLASS) && defined(PIN_USER_BTN) && defined(MESHCOMOD_ROOM_MULTITRANSPORT)
  user_btn.begin();
  s_room_ui_started_at = millis();
  s_room_page = 0;
  s_room_alert[0] = 0;
  s_room_alert_expiry = 0;
  (void)build_date;
  StrHelper::strncpy(_version_info, firmware_version, sizeof(_version_info));
#else
  // strip off dash and commit hash by changing dash to null terminator
  char *version = strdup(firmware_version);
  if (version) {
    char *dash = strchr(version, '-');
    if (dash) {
      *dash = 0;
    }
    snprintf(_version_info, sizeof(_version_info), "%s (%s)", version, build_date);
    free(version);
  } else {
    _version_info[0] = '\0';
  }
#endif
  _prevBtnState = HIGH;
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _node_prefs = node_prefs;
  _display->turnOn();
}

void UITask::renderCurrScreen() {
#if defined(ESP32) && defined(DISPLAY_CLASS) && defined(PIN_USER_BTN) && defined(MESHCOMOD_ROOM_MULTITRANSPORT)
  if ((millis() - s_room_ui_started_at) < ROOM_MT_SPLASH_MS) {
    render_room_mt_boot_splash(*_display);
    return;
  }
  if (g_meshcore_http_ota_display_active) {
    _display->setTextSize(1);
    render_room_http_ota_screen(*_display);
    return;
  }
  _display->setTextSize(1);
  draw_room_page_dots(*_display);
  if (s_room_page == ROOM_PAGE_RADIO) {
    render_room_mt_radio(*_display, _node_prefs);
  } else if (s_room_page == ROOM_PAGE_NETWORK) {
    render_room_mt_network(*_display);
  } else if (s_room_page == ROOM_PAGE_BLE) {
    render_room_mt_ble(*_display);
  } else {
    render_room_mt_ws(*_display);
  }
  if (millis() < s_room_alert_expiry) {
    int y = _display->height() / 3;
    int p = _display->height() / 32;
    if (p < 2) p = 2;
    _display->setTextSize(1);
    _display->setColor(DisplayDriver::DARK);
    _display->fillRect(p, y, _display->width() - p * 2, y);
    _display->setColor(DisplayDriver::LIGHT);
    _display->drawRect(p, y, _display->width() - p * 2, y);
    _display->drawTextCentered(_display->width() / 2, y + p * 3, s_room_alert);
  }
  return;
#endif

  char tmp[80];
  if (millis() < BOOT_SCREEN_MILLIS) { // boot screen
    // meshcore logo
    _display->setColor(DisplayDriver::BLUE);
    int logoWidth = 128;
    _display->drawXbm((_display->width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    // version info
    _display->setColor(DisplayDriver::LIGHT);
    _display->setTextSize(1);
    uint16_t versionWidth = _display->getTextWidth(_version_info);
    _display->setCursor((_display->width() - versionWidth) / 2, 22);
    _display->print(_version_info);

    // node type
    const char* node_type = "< Room Server >";
    uint16_t typeWidth = _display->getTextWidth(node_type);
    _display->setCursor((_display->width() - typeWidth) / 2, 35);
    _display->print(node_type);
  } else {  // home screen
    // node name
    _display->setCursor(0, 0);
    _display->setTextSize(1);
    _display->setColor(DisplayDriver::GREEN);
    _display->print(_node_prefs->node_name);

    // freq / sf
    _display->setCursor(0, 20);
    _display->setColor(DisplayDriver::YELLOW);
    sprintf(tmp, "FREQ: %06.3f SF%d", _node_prefs->freq, _node_prefs->sf);
    _display->print(tmp);

    // bw / cr
    _display->setCursor(0, 30);
    sprintf(tmp, "BW: %03.2f CR: %d", _node_prefs->bw, _node_prefs->cr);
    _display->print(tmp);
  }
}

void UITask::loop() {
#if defined(ESP32) && defined(DISPLAY_CLASS) && defined(PIN_USER_BTN) && defined(MESHCOMOD_ROOM_MULTITRANSPORT)
  if (millis() >= _next_read) {
    int ev = user_btn.check();
    char c = 0;
    const bool boot_done = ((millis() - s_room_ui_started_at) >= ROOM_MT_SPLASH_MS);
    if (ev == BUTTON_EVENT_CLICK) {
      if (!_display->isOn()) {
        _display->turnOn();
      } else if (boot_done) {
        c = KEY_NEXT;
      }
      _auto_off = millis() + AUTO_OFF_MILLIS;
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      if (_display->isOn() && boot_done) {
        if (s_room_page == ROOM_PAGE_NETWORK) {
          if (serial_interface.isTcpEnabled()) {
            serial_interface.disableTcp();
            room_mt_set_alert("Network off", 1500);
          } else {
            serial_interface.enableTcp();
            room_mt_set_alert("Network on", 1500);
          }
        } else if (s_room_page == ROOM_PAGE_BLE) {
#ifdef BLE_PIN_CODE
          if (serial_interface.hasBleCapability()) {
            if (serial_interface.isBleEnabled()) {
              serial_interface.disableBle();
              room_mt_set_alert("BLE off", 1500);
            } else {
              serial_interface.enableBle();
              room_mt_set_alert("BLE on", 1500);
            }
          }
#endif
        }
      }
      if (_display->isOn()) {
        _auto_off = millis() + AUTO_OFF_MILLIS;
      }
    } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
      if (!_display->isOn()) {
        _display->turnOn();
      } else if (boot_done) {
        c = KEY_PREV;
      }
      _auto_off = millis() + AUTO_OFF_MILLIS;
    }

    if (c == KEY_NEXT) {
      s_room_page = (s_room_page + 1) % ROOM_PAGE_COUNT;
    } else if (c == KEY_PREV) {
      s_room_page = (s_room_page + ROOM_PAGE_COUNT - 1) % ROOM_PAGE_COUNT;
    }

    if (ev != BUTTON_EVENT_NONE) {
      _next_refresh = 0;
    }
    _next_read = millis() + 200;
  }
#elif defined(PIN_USER_BTN)
  if (millis() >= _next_read) {
    int btnState = digitalRead(PIN_USER_BTN);
    if (btnState != _prevBtnState) {
      if (btnState == USER_BTN_PRESSED) {  // pressed?
        if (_display->isOn()) {
          // TODO: any action ?
        } else {
          _display->turnOn();
        }
        _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
      }
      _prevBtnState = btnState;
    }
    _next_read = millis() + 200;  // 5 reads per second
  }
#endif

  if (_display->isOn()) {
    if (millis() >= _next_refresh) {
      _display->startFrame();
      renderCurrScreen();
      _display->endFrame();

#if defined(ESP32) && defined(DISPLAY_CLASS) && defined(PIN_USER_BTN) && defined(MESHCOMOD_ROOM_MULTITRANSPORT)
      _next_refresh = millis() + (g_meshcore_http_ota_display_active ? 250UL : 1000UL);
#else
      _next_refresh = millis() + 1000;   // refresh every second
#endif
    }
    if (millis() > _auto_off) {
      _display->turnOff();
    }
  }
}
