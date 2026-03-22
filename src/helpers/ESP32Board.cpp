#ifdef ESP_PLATFORM

#include "ESP32Board.h"
#include <Arduino.h>
#include <cstring>
#include <helpers/HttpOtaDisplayState.h>
#include <helpers/RepeaterTcpOtaEmit.h>

volatile int g_meshcore_http_ota_display_active = 0;
volatile uint8_t g_meshcore_http_ota_display_pct = 0xFF;
char g_meshcore_http_ota_display_line[28] = {0};

#if defined(ADMIN_PASSWORD) && !defined(DISABLE_WIFI_OTA)   // Repeater or Room Server only
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

#include <SPIFFS.h>

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>

static volatile bool s_http_ota_reboot_pending = false;

static void httpOtaDisplayReset() {
  g_meshcore_http_ota_display_active = 0;
  g_meshcore_http_ota_display_pct = 0xFF;
  g_meshcore_http_ota_display_line[0] = '\0';
}

static void httpOtaDisplaySet(uint8_t pct, const char *line) {
  g_meshcore_http_ota_display_active = 1;
  g_meshcore_http_ota_display_pct = pct;
  if (line) {
    strncpy(g_meshcore_http_ota_display_line, line, sizeof(g_meshcore_http_ota_display_line) - 1);
    g_meshcore_http_ota_display_line[sizeof(g_meshcore_http_ota_display_line) - 1] = '\0';
  }
}

static unsigned long s_http_ota_last_emit_ms;
static uint8_t s_http_ota_last_emit_pct = 0xFF;

static void httpOtaEmitProgressThrottled(int clen, size_t total_written, const char *fallback_line) {
  uint8_t pct = 0xFF;
  if (clen > 0) {
    pct = (uint8_t)((total_written * 100ULL) / (size_t)clen);
    if (pct > 100) pct = 100;
  }
  httpOtaDisplaySet(pct, fallback_line);

  unsigned long now = millis();
  bool pct_jump = (clen > 0 && s_http_ota_last_emit_pct != 0xFF &&
                   (pct >= s_http_ota_last_emit_pct + 5 || pct == 100));
  if (now - s_http_ota_last_emit_ms < 450 && !pct_jump && s_http_ota_last_emit_ms != 0) return;
  s_http_ota_last_emit_ms = now;
  s_http_ota_last_emit_pct = pct;

  char line[96];
  if (clen > 0) {
    snprintf(line, sizeof(line), "OTA: downloading %u%%", (unsigned)pct);
  } else {
    snprintf(line, sizeof(line), "OTA: downloading (%u KB)", (unsigned)(total_written / 1024));
  }
  meshcoreRepeaterTcpOtaEmitLine(line);
}

/** App-only OTA: allow GitHub raw URLs and meshcomod flasher/repeater firmware-download proxy paths. */
static bool meshcoreHttpOtaUrlAllowed(const char* u) {
#if defined(OTA_URL_ALLOW_HTTP)
  if (strncmp(u, "http://127.0.0.1/", 17) == 0) return true;
  if (strncmp(u, "http://localhost/", 17) == 0) return true;
#endif
  if (strncmp(u, "https://raw.githubusercontent.com/", 34) == 0) return true;
  if (strncmp(u, "https://github.com/", 19) == 0 && strstr(u + 8, "/raw/") != nullptr) return true;
  if (strncmp(u, "https://flasher.meshcomod.com/firmware-download/", 48) == 0) return true;
  if (strncmp(u, "http://flasher.meshcomod.com/firmware-download/", 47) == 0) return true;
  if (strncmp(u, "https://repeater.meshcomod.com/firmware-download/", 50) == 0) return true;
  if (strncmp(u, "http://repeater.meshcomod.com/firmware-download/", 49) == 0) return true;
  return false;
}

bool ESP32Board::startOTAUpdate(const char* id, char reply[]) {
  inhibit_sleep = true;   // prevent sleep during OTA
  WiFi.softAP("MeshCore-OTA", NULL);

  sprintf(reply, "Started: http://%s/update", WiFi.softAPIP().toString().c_str());
  MESH_DEBUG_PRINTLN("startOTAUpdate: %s", reply);

  static char id_buf[60];
  sprintf(id_buf, "%s (%s)", id, getManufacturerName());
  static char home_buf[90];
  sprintf(home_buf, "<H2>Hi! I am a MeshCore Repeater. ID: %s</H2>", id);

  AsyncWebServer* server = new AsyncWebServer(80);

  server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", home_buf);
  });
  server->on("/log", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/packet_log", "text/plain");
  });

  AsyncElegantOTA.setID(id_buf);
  AsyncElegantOTA.begin(server);    // Start ElegantOTA
  server->begin();

  return true;
}

bool ESP32Board::startHttpOtaFromUrl(const char* url, char* reply) {
  if (!url || !reply) return false;
  s_http_ota_last_emit_ms = 0;
  s_http_ota_last_emit_pct = 0xFF;
  if (!meshcoreHttpOtaUrlAllowed(url)) {
    strcpy(reply, "ERR: URL not allowed");
    return true;
  }
  if (WiFi.status() != WL_CONNECTED) {
    strcpy(reply, "ERR: WiFi not connected");
    return true;
  }

  inhibit_sleep = true;

  httpOtaDisplaySet(0xFF, "OTA: connecting");
  meshcoreRepeaterTcpOtaEmitLine("OTA: connecting");

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.setTimeout(90000);
#if defined(HTTPC_STRICT_FOLLOW_REDIRECTS)
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
#endif

  if (!https.begin(client, url)) {
    httpOtaDisplayReset();
    strcpy(reply, "ERR: HTTP begin failed");
    return true;
  }

  int code = https.GET();
  if (code != HTTP_CODE_OK) {
    snprintf(reply, 128, "ERR: HTTP %d", code);
    https.end();
    httpOtaDisplayReset();
    return true;
  }

  int clen = https.getSize();
  WiFiClient* stream = https.getStreamPtr();
  if (!stream) {
    strcpy(reply, "ERR: no stream");
    https.end();
    httpOtaDisplayReset();
    return true;
  }

  httpOtaDisplaySet(0, "OTA: install started");
  meshcoreRepeaterTcpOtaEmitLine("OTA: HTTP OK, flashing");

  if (!Update.begin(clen > 0 ? (size_t)clen : UPDATE_SIZE_UNKNOWN)) {
    snprintf(reply, 128, "ERR: %s", Update.errorString());
    https.end();
    httpOtaDisplayReset();
    return true;
  }

  uint8_t buf[512];
  int remaining = clen;
  unsigned long t0 = millis();
  size_t total_written = 0;

  while (https.connected() && (remaining > 0 || remaining == -1)) {
    if (millis() - t0 > 180000UL) {
      Update.abort();
      https.end();
      httpOtaDisplayReset();
      strcpy(reply, "ERR: timeout");
      meshcoreRepeaterTcpOtaEmitLine("OTA: ERR timeout");
      return true;
    }

    size_t av = stream->available();
    if (!av) {
      if (remaining == 0) break;
      if (remaining < 0 && !stream->connected() && !av) break;
      delay(2);
      yield();
      continue;
    }

    size_t to_read = av > sizeof(buf) ? sizeof(buf) : av;
    int rd = stream->readBytes(buf, to_read);
    if (rd <= 0) break;

    if (Update.write(buf, (size_t)rd) != (size_t)rd) {
      snprintf(reply, 128, "ERR: write %s", Update.errorString());
      Update.abort();
      https.end();
      httpOtaDisplayReset();
      meshcoreRepeaterTcpOtaEmitLine("OTA: ERR flash write");
      return true;
    }

    total_written += (size_t)rd;
    if (remaining > 0) remaining -= rd;

    httpOtaEmitProgressThrottled(clen, total_written, "OTA: downloading");
    yield();
  }

  https.end();

  httpOtaDisplaySet(100, "OTA: verifying");
  meshcoreRepeaterTcpOtaEmitLine("OTA: verifying");

  if (!Update.end(true)) {
    snprintf(reply, 128, "ERR: %s", Update.errorString());
    httpOtaDisplayReset();
    meshcoreRepeaterTcpOtaEmitLine("OTA: ERR verify");
    return true;
  }

  strcpy(reply, "> OK rebooting");
  httpOtaDisplaySet(100, "OTA: rebooting");
  meshcoreRepeaterTcpOtaEmitLine("OTA: rebooting");
  s_http_ota_reboot_pending = true;
  return true;
}

void ESP32Board::pollHttpOtaReboot() {
  if (s_http_ota_reboot_pending) {
    s_http_ota_reboot_pending = false;
    delay(300);
    esp_restart();
  }
}

#else
bool ESP32Board::startOTAUpdate(const char* id, char reply[]) {
  return false; // not supported
}

bool ESP32Board::startHttpOtaFromUrl(const char* url, char reply[]) {
  (void)url;
  (void)reply;
  return false;
}

void ESP32Board::pollHttpOtaReboot() {}
#endif

#endif
