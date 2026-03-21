#include "UITask.h"
#include <Arduino.h>
#include <helpers/CommonCLI.h>

#if defined(REPEATER_TCP_COMPANION) && defined(ESP32) && defined(DISPLAY_CLASS) && defined(PIN_USER_BTN)
#include <WiFi.h>
#include "MyMesh.h"
#include <helpers/TxtDataHelpers.h>
#include <helpers/esp32/WifiRuntimeStore.h>
#include <helpers/ui/UIScreen.h>
#include "repeater_transport.h"
#include "target.h"

extern MyMesh the_mesh;

#ifndef TCP_PORT
#define TCP_PORT 5000
#endif
#ifndef WS_PORT
#define WS_PORT 8765
#endif

namespace {

enum { PAGE_RADIO = 0, PAGE_NETWORK = 1, PAGE_WS = 2, PAGE_ADVERT = 3, PAGE_COUNT = 4 };

int s_page;
unsigned long s_ui_started_at;
char s_alert[80];
unsigned long s_alert_expiry;
static void set_alert(const char *text, unsigned ms) {
  StrHelper::strncpy(s_alert, text, sizeof(s_alert));
  s_alert_expiry = millis() + ms;
}

static void render_repeater_tcp_home(DisplayDriver &display, NodePrefs *prefs) {
  char tmp[80];
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setColor(DisplayDriver::GREEN);
  display.print(prefs->node_name);

  display.setCursor(0, 20);
  display.setColor(DisplayDriver::YELLOW);
  sprintf(tmp, "FREQ: %06.3f SF%d", prefs->freq, prefs->sf);
  display.print(tmp);

  display.setCursor(0, 30);
  sprintf(tmp, "BW: %03.2f CR: %d", prefs->bw, prefs->cr);
  display.print(tmp);
}

static void render_repeater_tcp_network(DisplayDriver &display) {
  char tmp[80];
  display.setColor(DisplayDriver::BLUE);
  display.setTextSize(1);
  int y = 10;
  display.drawTextCentered(display.width() / 2, y, "Network");
  y += 12;

  if (!repeater_transport_enabled) {
    display.setColor(DisplayDriver::RED);
    display.drawTextCentered(display.width() / 2, y, "TCP/WS off");
    y = 64 - 11;
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextCentered(display.width() / 2, y, "ON: long press");
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
#if WS_USE_TLS
      snprintf(tmp, sizeof(tmp), "TCP:%d WSS:%d", (int)TCP_PORT, (int)WS_PORT);
#else
      snprintf(tmp, sizeof(tmp), "TCP:%d WS:%d", (int)TCP_PORT, (int)WS_PORT);
#endif
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

  y = 64 - 11;
  display.setColor(DisplayDriver::LIGHT);
  display.drawTextCentered(display.width() / 2, y, "off: long press");
}

static void render_repeater_tcp_ws(DisplayDriver &display) {
  char tmp[80];
  display.setColor(DisplayDriver::BLUE);
  display.setTextSize(1);
  int y = 10;
#if WS_USE_TLS
  display.drawTextCentered(display.width() / 2, y, "WebSocket (WSS)");
#else
  display.drawTextCentered(display.width() / 2, y, "WebSocket (WS)");
#endif
  y += 12;

  if (!repeater_transport_enabled) {
    display.setColor(DisplayDriver::RED);
    display.drawTextCentered(display.width() / 2, y, "Off with TCP");
    y += 11;
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextCentered(display.width() / 2, y, "ON: Network page");
    y = 64 - 11;
    display.drawTextCentered(display.width() / 2, y, "long press there");
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
    return;
  }

  IPAddress ip = WiFi.localIP();
  if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) {
    display.setColor(DisplayDriver::YELLOW);
    display.drawTextCentered(display.width() / 2, y, "Waiting IP...");
    return;
  }

  display.setColor(DisplayDriver::GREEN);
#if WS_USE_TLS
  snprintf(tmp, sizeof(tmp), "wss://%d.%d.%d.%d:%d", ip[0], ip[1], ip[2], ip[3], (int)WS_PORT);
#else
  snprintf(tmp, sizeof(tmp), "ws://%d.%d.%d.%d:%d", ip[0], ip[1], ip[2], ip[3], (int)WS_PORT);
#endif
  display.drawTextCentered(display.width() / 2, y, tmp);
  y = 64 - 11;
  display.setColor(DisplayDriver::LIGHT);
  display.drawTextCentered(display.width() / 2, y, "mirrors TCP on/off");
}

static void render_repeater_tcp_advert(DisplayDriver &display) {
  display.setColor(DisplayDriver::GREEN);
  display.setTextSize(1);
  display.drawTextCentered(display.width() / 2, 20, "Flood advert");
  display.setColor(DisplayDriver::LIGHT);
  display.drawTextCentered(display.width() / 2, 34, "long press: send");
  display.drawTextCentered(display.width() / 2, 48, "click: next page");
}

static void draw_page_dots(DisplayDriver &display) {
  int y = 14;
  int x = display.width() / 2 - 5 * (PAGE_COUNT - 1);
  for (int i = 0; i < PAGE_COUNT; i++, x += 10) {
    if (i == s_page) {
      display.fillRect(x - 1, y - 1, 3, 3);
    } else {
      display.fillRect(x, y, 1, 1);
    }
  }
}

}  // namespace
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
#if defined(REPEATER_TCP_COMPANION) && defined(ESP32) && defined(DISPLAY_CLASS) && defined(PIN_USER_BTN)
  user_btn.begin();
  s_ui_started_at = millis();
  s_page = 0;
  s_alert[0] = 0;
  s_alert_expiry = 0;
#endif
  _prevBtnState = HIGH;
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _node_prefs = node_prefs;
  _display->turnOn();

  char *version = strdup(firmware_version);
  char *dash = strchr(version, '-');
  if (dash) {
    *dash = 0;
  }

  sprintf(_version_info, "%s (%s)", version, build_date);
}

void UITask::renderCurrScreen() {
#if defined(REPEATER_TCP_COMPANION) && defined(ESP32) && defined(DISPLAY_CLASS) && defined(PIN_USER_BTN)
  if (millis() >= BOOT_SCREEN_MILLIS) {
    _display->setTextSize(1);
    draw_page_dots(*_display);
    if (s_page == PAGE_RADIO) {
      render_repeater_tcp_home(*_display, _node_prefs);
    } else if (s_page == PAGE_NETWORK) {
      render_repeater_tcp_network(*_display);
    } else if (s_page == PAGE_WS) {
      render_repeater_tcp_ws(*_display);
    } else {
      render_repeater_tcp_advert(*_display);
    }
    if (millis() < s_alert_expiry) {
      int y = _display->height() / 3;
      int p = _display->height() / 32;
      _display->setTextSize(1);
      _display->setColor(DisplayDriver::DARK);
      _display->fillRect(p, y, _display->width() - p * 2, y);
      _display->setColor(DisplayDriver::LIGHT);
      _display->drawRect(p, y, _display->width() - p * 2, y);
      _display->drawTextCentered(_display->width() / 2, y + p * 3, s_alert);
    }
    return;
  }
#endif
  char tmp[80];
  if (millis() < BOOT_SCREEN_MILLIS) {
    _display->setColor(DisplayDriver::BLUE);
    int logoWidth = 128;
    _display->drawXbm((_display->width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    _display->setColor(DisplayDriver::LIGHT);
    _display->setTextSize(1);
    uint16_t versionWidth = _display->getTextWidth(_version_info);
    _display->setCursor((_display->width() - versionWidth) / 2, 22);
    _display->print(_version_info);

    const char* node_type = "< Repeater >";
    uint16_t typeWidth = _display->getTextWidth(node_type);
    _display->setCursor((_display->width() - typeWidth) / 2, 35);
    _display->print(node_type);
  } else {
    _display->setCursor(0, 0);
    _display->setTextSize(1);
    _display->setColor(DisplayDriver::GREEN);
    _display->print(_node_prefs->node_name);

    _display->setCursor(0, 20);
    _display->setColor(DisplayDriver::YELLOW);
    sprintf(tmp, "FREQ: %06.3f SF%d", _node_prefs->freq, _node_prefs->sf);
    _display->print(tmp);

    _display->setCursor(0, 30);
    sprintf(tmp, "BW: %03.2f CR: %d", _node_prefs->bw, _node_prefs->cr);
    _display->print(tmp);
  }
}

void UITask::loop() {
#if defined(REPEATER_TCP_COMPANION) && defined(ESP32) && defined(DISPLAY_CLASS) && defined(PIN_USER_BTN)
  if (millis() >= _next_read) {
    int ev = user_btn.check();
    char c = 0;
    const bool boot_done = (millis() >= BOOT_SCREEN_MILLIS);
    if (ev == BUTTON_EVENT_CLICK) {
      if (!_display->isOn()) {
        _display->turnOn();
      } else if (boot_done) {
        // Same as companion: short press always advances page (never trap on advert).
        c = KEY_NEXT;
      }
      _auto_off = millis() + AUTO_OFF_MILLIS;
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      if (millis() - s_ui_started_at < 8000UL) {
        the_mesh.enterCLIRescue();
      } else if (_display->isOn() && boot_done) {
        if (s_page == PAGE_NETWORK) {
          if (repeater_transport_enabled) {
            repeater_transport_enabled = false;
            set_alert("Network off", 1500);
          } else {
            repeater_transport_enabled = true;
            set_alert("Network on", 1500);
          }
        } else if (s_page == PAGE_ADVERT) {
          the_mesh.sendSelfAdvertisement(1500, true);
          set_alert("Advert sent!", 1000);
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
      s_page = (s_page + 1) % PAGE_COUNT;
    } else if (c == KEY_PREV) {
      s_page = (s_page + PAGE_COUNT - 1) % PAGE_COUNT;
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
      if (btnState == LOW) {
        if (_display->isOn()) {
        } else {
          _display->turnOn();
        }
        _auto_off = millis() + AUTO_OFF_MILLIS;
      }
      _prevBtnState = btnState;
    }
    _next_read = millis() + 200;
  }
#endif

  if (_display->isOn()) {
    if (millis() >= _next_refresh) {
      _display->startFrame();
      renderCurrScreen();
      _display->endFrame();
      _next_refresh = millis() + 1000;
    }
    if (millis() > _auto_off) {
      _display->turnOff();
    }
  }
}
