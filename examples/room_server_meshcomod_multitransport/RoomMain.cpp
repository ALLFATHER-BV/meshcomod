// ESP32 meshcomod room server: stock room mesh + USB/BLE/TCP/WebSocket (MultiTransportCompanionInterface).
// Built only for *_room_server_multitransport envs (see variants/*/platformio.ini).

#include <Arduino.h>
#include <Mesh.h>
#include <SPIFFS.h>
#include <WiFi.h>

#include <helpers/esp32/MultiTransportCompanionInterface.h>
#include <helpers/esp32/WifiRuntimeStore.h>
#include <helpers/IdentityStore.h>

#include "../simple_room_server/MyMesh.h"

#if defined(ESP32)
volatile int g_boot_phase = 0;
extern "C" void set_boot_phase(int phase) {
  g_boot_phase = phase;
}
#endif

#ifndef BLE_NAME_PREFIX
#define BLE_NAME_PREFIX "MeshCore-"
#endif

#ifndef TCP_PORT
#define TCP_PORT 5000
#endif
#ifndef WS_PORT
#define WS_PORT 8765
#endif

#ifndef WIFI_PWD
#define WIFI_PWD ""
#endif

#ifdef DISPLAY_CLASS
#include "../simple_room_server/UITask.h"
static UITask ui_task(display);
#endif

MultiTransportCompanionInterface serial_interface;

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1)
    ;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  board.begin();

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.setCursor(0, 0);
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) {
    halt();
  }

  fast_rng.begin(radio_get_rng_seed());

  SPIFFS.begin(true);
  FILESYSTEM* fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
  if (!store.load("_main", the_mesh.self_id)) {
    the_mesh.self_id = radio_new_identity();
    int count = 0;
    while (count < 10 &&
           (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {
      the_mesh.self_id = radio_new_identity();
      count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Room ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE);
  Serial.println();

  sensors.begin();

  the_mesh.begin(fs);

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#endif

  wifiConfigBegin();

  board.setInhibitSleep(true);
  serial_interface.begin(Serial, TCP_PORT, WS_PORT);
  serial_interface.setBroadcastResponses(true);
#if defined(BLE_PIN_CODE)
  serial_interface.beginBle(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, (uint32_t)BLE_PIN_CODE);
#endif
  the_mesh.startInterface(serial_interface);
}

void loop() {
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif

  static bool wifi_started = false;
  static uint32_t last_wifi_retry_ms = 0;
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
  static bool wifi_radio_prev = true;
  static bool wifi_radio_inited = false;
  bool wifi_radio_en = wifiConfigGetRadioEnabled();
  if (!wifi_radio_inited) {
    wifi_radio_inited = true;
    wifi_radio_prev = wifi_radio_en;
  } else if (wifi_radio_en != wifi_radio_prev) {
    wifi_radio_prev = wifi_radio_en;
    if (!wifi_radio_en) {
      WiFi.disconnect(true);
      delay(50);
      WiFi.mode(WIFI_OFF);
    }
    wifi_started = false;
  }
  if (wifi_radio_en) {
    if (!wifi_started) {
      wifi_started = true;
      if (wifiConfigHasRuntime()) {
        char ssid[WIFI_CONFIG_SSID_MAX];
        char pwd[WIFI_CONFIG_PWD_MAX];
        wifiConfigGetSsid(ssid, sizeof(ssid));
        wifiConfigGetPwd(pwd, sizeof(pwd));
        if (strlen(ssid) > 0) {
          WiFi.mode(WIFI_STA);
          WiFi.begin(ssid, pwd[0] ? pwd : nullptr);
          last_wifi_retry_ms = millis();
        } else {
          WiFi.mode(WIFI_STA);
          WiFi.begin("", "");
        }
      } else if (strlen(WIFI_SSID) > 0) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PWD);
        last_wifi_retry_ms = millis();
      } else {
        WiFi.mode(WIFI_STA);
        WiFi.begin("", "");
      }
    }
    if (WiFi.status() != WL_CONNECTED) {
      uint32_t now = millis();
      if ((uint32_t)(now - last_wifi_retry_ms) >= WIFI_RETRY_INTERVAL_MS) {
        last_wifi_retry_ms = now;
        if (wifiConfigHasRuntime()) {
          char ssid[WIFI_CONFIG_SSID_MAX];
          char pwd[WIFI_CONFIG_PWD_MAX];
          wifiConfigGetSsid(ssid, sizeof(ssid));
          wifiConfigGetPwd(pwd, sizeof(pwd));
          if (strlen(ssid) > 0) {
            WiFi.begin(ssid, pwd[0] ? pwd : nullptr);
          }
        } else if (strlen(WIFI_SSID) > 0) {
          WiFi.begin(WIFI_SSID, WIFI_PWD);
        }
      }
    }
  }

  static const uint32_t TCP_DEFER_MS = 5000;
  if (millis() > TCP_DEFER_MS) {
    serial_interface.startTcpServer(WiFi.status() == WL_CONNECTED);
    serial_interface.tickWebSocketHandshake();
  }

  the_mesh.loop();
  sensors.loop();
  rtc_clock.tick();
#if defined(ESP32_PLATFORM)
  board.pollHttpOtaReboot();
#endif
}
