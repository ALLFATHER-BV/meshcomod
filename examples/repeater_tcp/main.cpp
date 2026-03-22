#include <Arduino.h>
#include <Mesh.h>
#include <WiFi.h>

#include <helpers/esp32/TCPCompanionServer.h>
#include <helpers/esp32/WebSocketCompanionServer.h>
#include <helpers/esp32/WifiRuntimeStore.h>

#include "MyMesh.h"
#include "repeater_transport.h"

#if defined(ESP32)
volatile int g_boot_phase = 0;
extern "C" void set_boot_phase(int phase) {
  g_boot_phase = phase;
}
#endif

#ifdef DISPLAY_CLASS
#include "UITask.h"
static UITask ui_task(display);
#endif

#ifndef TCP_PORT
#define TCP_PORT 5000
#endif

#ifndef WS_PORT
#define WS_PORT 8765
#endif

#if defined(WIFI_SSID) && !defined(WIFI_PWD)
#define WIFI_PWD ""
#endif

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

TCPCompanionServer tcp_server;
WebSocketCompanionServer ws_server;

volatile bool repeater_transport_enabled = true;

void halt() {
  while (1)
    ;
}

static char command[160];

unsigned long lastActive = 0;
unsigned long nextSleepinSecs = 120;

enum class RpCliKind { Tcp, Ws };

struct RepeaterEmitCtx {
  TCPCompanionServer *tcp;
  WebSocketCompanionServer *ws;
  RpCliKind kind;
  int client_index;
};

static void repeater_emit_frame(void *ctx, const uint8_t *buf, size_t len) {
  auto *t = (RepeaterEmitCtx *)ctx;
  if (!t || len == 0) return;
  if (t->kind == RpCliKind::Tcp) {
    t->tcp->writeToClient(t->client_index, buf, len);
  } else {
    t->ws->writeToClient(t->client_index, buf, len);
  }
}

static void repeater_wifi_station_begin() {
  wifiConfigBegin();
  if (!wifiConfigGetRadioEnabled()) {
    WiFi.disconnect(true);
    delay(50);
    WiFi.mode(WIFI_OFF);
    return;
  }
  WiFi.mode(WIFI_STA);
  if (wifiConfigHasRuntime()) {
    char ssid[WIFI_CONFIG_SSID_MAX];
    char pwd[WIFI_CONFIG_PWD_MAX];
    wifiConfigGetSsid(ssid, sizeof(ssid));
    wifiConfigGetPwd(pwd, sizeof(pwd));
    WiFi.begin(ssid, pwd[0] ? pwd : nullptr);
  } else {
#if defined(WIFI_SSID)
    if (strlen(WIFI_SSID) > 0) {
      WiFi.begin(WIFI_SSID, WIFI_PWD);
    } else
#endif
    {
      WiFi.begin("", "");
    }
  }
}

void repeater_on_wifi_radio_toggled() {
  repeater_wifi_station_begin();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  board.begin();

  lastActive = millis();

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.setCursor(0, 0);
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) {
    MESH_DEBUG_PRINTLN("Radio init failed!");
    halt();
  }

  fast_rng.begin(radio_get_rng_seed());

  FILESYSTEM *fs;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "");
  store.begin();
#else
#error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    MESH_DEBUG_PRINTLN("Generating new keypair");
    the_mesh.self_id = radio_new_identity();
    int count = 0;
    while (count < 10 &&
           (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {
      the_mesh.self_id = radio_new_identity();
      count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Repeater ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE);
  Serial.println();

  command[0] = 0;

  sensors.begin();

  the_mesh.begin(fs);

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

#if defined(ESP32) && defined(REPEATER_TCP_COMPANION)
  repeater_wifi_station_begin();
  Serial.print("TCP companion port ");
  Serial.println((int)TCP_PORT);
  Serial.print("WebSocket port ");
  Serial.print((int)WS_PORT);
#if WS_USE_TLS
  Serial.println(" (WSS)");
#else
  Serial.println(" (WS)");
#endif
  Serial.println("WiFi: NVS credentials (meshcomod / USB) or optional compile-time WIFI_SSID");
#endif

#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#endif
}

void loop() {
  int len = strlen(command);
  while (Serial.available() && len < sizeof(command) - 1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
      Serial.print(c);
    }
    if (c == '\r') break;
  }
  if (len == sizeof(command) - 1) {
    command[sizeof(command) - 1] = '\r';
  }

  if (len > 0 && command[len - 1] == '\r') {
    Serial.print('\n');
    command[len - 1] = 0;
    char reply[320];
    the_mesh.handleCommand(0, command, reply);
    if (reply[0]) {
      Serial.print("  -> ");
      Serial.println(reply);
    }
    command[0] = 0;
  }

#if defined(ESP32) && defined(REPEATER_TCP_COMPANION)
  {
    static uint32_t last_wifi_retry_ms = 0;
    if (!wifiConfigGetRadioEnabled()) {
      last_wifi_retry_ms = 0;
    } else if (WiFi.status() != WL_CONNECTED) {
      uint32_t now = millis();
      if (last_wifi_retry_ms == 0) {
        last_wifi_retry_ms = now;
      } else if ((uint32_t)(now - last_wifi_retry_ms) >= 10000UL) {
        last_wifi_retry_ms = now;
        repeater_wifi_station_begin();
      }
    } else {
      last_wifi_retry_ms = 0;
    }

    static bool s_tcp_started = false;
    static bool s_ws_started = false;

    // WebSocket follows TCP: never listen on WS unless the TCP companion server is up.
    if (repeater_transport_enabled) {
      if (!s_tcp_started) {
        tcp_server.begin(TCP_PORT);
        s_tcp_started = true;
      }
      if (s_tcp_started && WiFi.status() == WL_CONNECTED) {
        if (!s_ws_started) {
#if WS_USE_TLS
          ws_server.begin(WS_PORT, true);
#else
          ws_server.begin(WS_PORT, false);
#endif
          s_ws_started = true;
        }
      } else {
        if (s_ws_started) {
          ws_server.stop();
          s_ws_started = false;
        }
      }
#if WS_USE_TLS
      if (s_ws_started) {
        ws_server.tickHandshake();
      }
#endif
    } else {
      if (s_ws_started) {
        ws_server.stop();
        s_ws_started = false;
      }
      if (s_tcp_started) {
        tcp_server.stop();
        s_tcp_started = false;
      }
    }

    static uint8_t frame_in[MAX_FRAME_SIZE];
    static uint8_t frame_out[MAX_FRAME_SIZE];
    size_t n = 0;
    int client_index = -1;
    RpCliKind from = RpCliKind::Tcp;

    if (repeater_transport_enabled && s_tcp_started) {
      n = tcp_server.pollRecvFrame(frame_in, &client_index);
      if (n > 0 && client_index >= 0) {
        from = RpCliKind::Tcp;
      }
    }
    if (n == 0 && repeater_transport_enabled && s_ws_started) {
      int ws_idx = -1;
      n = ws_server.pollRecvFrame(frame_in, &ws_idx);
      if (n > 0 && ws_idx >= 0) {
        from = RpCliKind::Ws;
        client_index = ws_idx;
      }
    }

    if (n > 0 && client_index >= 0) {
      RepeaterEmitCtx emit_ctx = {&tcp_server, &ws_server, from, client_index};
      size_t olen = the_mesh.handleRepeaterTcpCompanionCommand(frame_in, n, frame_out, sizeof(frame_out),
                                                               repeater_emit_frame, &emit_ctx);
      if (olen > 0) {
        if (from == RpCliKind::Tcp) {
          tcp_server.writeToClient(client_index, frame_out, olen);
        } else {
          ws_server.writeToClient(client_index, frame_out, olen);
        }
      }
    }
  }
#endif

  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();
  board.pollHttpOtaReboot();

  if (the_mesh.getNodePrefs()->powersaving_enabled && !the_mesh.hasPendingWork()) {
#if defined(NRF52_PLATFORM)
    board.sleep(1800);
#else
    if (the_mesh.millisHasNowPassed(lastActive + nextSleepinSecs * 1000)) {
      board.sleep(1800);
      lastActive = millis();
      nextSleepinSecs = 5;
    } else {
      nextSleepinSecs += 5;
    }
#endif
  }
}
