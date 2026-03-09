#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include "MyMesh.h"

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>
    DataStore store(InternalFS, QSPIFlash, rtc_clock);
  #else
  #if defined(EXTRAFS)
    #include <CustomLFS.h>
    CustomLFS ExtraFS(0xD4000, 0x19000, 128);
    DataStore store(InternalFS, ExtraFS, rtc_clock);
  #else
    DataStore store(InternalFS, rtc_clock);
  #endif
  #endif
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
  DataStore store(LittleFS, rtc_clock);
#elif defined(ESP32)
  #include <SPIFFS.h>
  DataStore store(SPIFFS, rtc_clock);
  #if defined(WIFI_SSID) || defined(MULTI_TRANSPORT_COMPANION)
    #include "WiFiConfig.h"
  #endif
#endif

#ifdef ESP32
  #ifdef MULTI_TRANSPORT_COMPANION
    #include <helpers/esp32/MultiTransportCompanionInterface.h>
    MultiTransportCompanionInterface serial_interface;
    #ifndef TCP_PORT
      #define TCP_PORT 5000
    #endif
    #ifndef WS_PORT
      #define WS_PORT 8765
    #endif
  #elif defined(WIFI_SSID)
    #include <helpers/esp32/SerialWifiInterface.h>
    SerialWifiInterface serial_interface;
    #ifndef TCP_PORT
      #define TCP_PORT 5000
    #endif
  #elif defined(BLE_PIN_CODE)
    #include <helpers/esp32/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #elif defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(RP2040_PLATFORM)
  //#ifdef WIFI_SSID
  //  #include <helpers/rp2040/SerialWifiInterface.h>
  //  SerialWifiInterface serial_interface;
  //  #ifndef TCP_PORT
  //    #define TCP_PORT 5000
  //  #endif
  // #elif defined(BLE_PIN_CODE)
  //   #include <helpers/rp2040/SerialBLEInterface.h>
  //   SerialBLEInterface serial_interface;
  #if defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(NRF52_PLATFORM)
  #ifdef BLE_PIN_CODE
    #include <helpers/nrf52/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(STM32_PLATFORM)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface serial_interface;
#else
  #error "need to define a serial interface"
#endif

/* GLOBAL OBJECTS */
#ifdef DISPLAY_CLASS
  #include "UITask.h"
  UITask ui_task(&board, &serial_interface);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store
   #ifdef DISPLAY_CLASS
      , &ui_task
   #endif
);

/* END GLOBAL OBJECTS */

void halt() {
  while (1) ;
}

void setup() {
  Serial.begin(115200);

  board.begin();

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;
  if (display.begin()) {
    disp = &display;
    disp->startFrame();
  #ifdef ST7789
    disp->setTextSize(2);
  #endif
    disp->drawTextCentered(disp->width() / 2, 28, "Loading...");
    disp->endFrame();
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_get_rng_seed());

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  #if defined(QSPIFLASH)
    if (!QSPIFlash.begin()) {
      // debug output might not be available at this point, might be too early. maybe should fall back to InternalFS here?
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: failed to initialize");
    } else {
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: initialized successfully");
    }
  #else
  #if defined(EXTRAFS)
      ExtraFS.begin();
  #endif
  #endif
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

#ifdef BLE_PIN_CODE
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
#else
  serial_interface.begin(Serial);
#endif
  the_mesh.startInterface(serial_interface);
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

  //#ifdef WIFI_SSID
  //  WiFi.begin(WIFI_SSID, WIFI_PWD);
  //  serial_interface.begin(TCP_PORT);
  // #elif defined(BLE_PIN_CODE)
  //   char dev_name[32+16];
  //   sprintf(dev_name, "%s%s", BLE_NAME_PREFIX, the_mesh.getNodeName());
  //   serial_interface.begin(dev_name, the_mesh.getBLEPin());
  #if defined(SERIAL_RX)
    companion_serial.setPins(SERIAL_RX, SERIAL_TX);
    companion_serial.begin(115200);
    serial_interface.begin(companion_serial);
  #else
    serial_interface.begin(Serial);
  #endif
    the_mesh.startInterface(serial_interface);
#elif defined(ESP32)
  SPIFFS.begin(true);
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

#if defined(WIFI_SSID) || defined(MULTI_TRANSPORT_COMPANION)
  wifiConfigBegin();
#endif

#ifdef MULTI_TRANSPORT_COMPANION
  board.setInhibitSleep(true);
  // Defer WiFi until first loop() so setup() always completes (display + USB come up even if WiFi would crash/hang)
  serial_interface.begin(Serial, TCP_PORT, WS_PORT);
  serial_interface.setBroadcastResponses(true);  // RX log, channel messages, etc. go to all clients (USB + TCP + WS [+ BLE]), not only last sender
#if defined(BLE_PIN_CODE)
  serial_interface.beginBle(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
#endif
#elif defined(WIFI_SSID)
  board.setInhibitSleep(true);   // prevent sleep when WiFi is active
  if (wifiConfigHasRuntime()) {
    char ssid[WIFI_CONFIG_SSID_MAX];
    char pwd[WIFI_CONFIG_PWD_MAX];
    wifiConfigGetSsid(ssid, sizeof(ssid));
    wifiConfigGetPwd(pwd, sizeof(pwd));
    WiFi.begin(ssid, pwd[0] ? pwd : nullptr);
  } else {
    WiFi.begin(WIFI_SSID, WIFI_PWD);
  }
  serial_interface.begin(TCP_PORT);
#elif defined(BLE_PIN_CODE)
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
#elif defined(SERIAL_RX)
  companion_serial.setPins(SERIAL_RX, SERIAL_TX);
  companion_serial.begin(115200);
  serial_interface.begin(companion_serial);
#else
  serial_interface.begin(Serial);
#endif
  the_mesh.startInterface(serial_interface);
#else
  #error "need to define filesystem"
#endif

  sensors.begin();

#ifdef DISPLAY_CLASS
  ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());  // still want to pass this in as dependency, as prefs might be moved
#endif
}

void loop() {
#ifdef MULTI_TRANSPORT_COMPANION
  static bool wifi_started = false;
  static uint32_t last_wifi_retry_ms = 0;
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
  if (!wifi_started) {
    wifi_started = true;
    if (wifiConfigHasRuntime()) {
      char ssid[WIFI_CONFIG_SSID_MAX];
      char pwd[WIFI_CONFIG_PWD_MAX];
      wifiConfigGetSsid(ssid, sizeof(ssid));
      wifiConfigGetPwd(pwd, sizeof(pwd));
      if (strlen(ssid) > 0) {
        WiFi.begin(ssid, pwd[0] ? pwd : nullptr);
        last_wifi_retry_ms = millis();
      }
    } else if (strlen(WIFI_SSID) > 0) {
      WiFi.begin(WIFI_SSID, WIFI_PWD);
      last_wifi_retry_ms = millis();
    }
  }
  // Automatic WiFi recovery for TCP mode: retry connection periodically if link drops.
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
  // TCP starts immediately; WSS (TLS) starts only after boot delay so UI/mesh run first and version screen can dismiss.
#if WS_USE_TLS
  static const uint32_t WSS_DEFER_MS = 10000;  // 10 s after boot before WSS is allowed to start
  bool allow_wss = (millis() > WSS_DEFER_MS) && (WiFi.status() == WL_CONNECTED);
  serial_interface.startTcpServer(allow_wss);
#else
  serial_interface.startTcpServer(WiFi.status() == WL_CONNECTED);
#endif
#endif
  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();
}
