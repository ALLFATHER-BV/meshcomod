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

/**
 * `https://github.com/owner/repo/raw/ref/path` -> `https://raw.githubusercontent.com/owner/repo/ref/path`
 * GitHub returns 302 for the former; ESP32 HTTPClient defaults to not following redirects, which breaks OTA.
 */
static bool meshcoreGithubRawToRawUsercontent(const char* url, char* out, size_t cap) {
  static const char gh[] = "https://github.com/";
  const size_t gh_len = sizeof(gh) - 1;
  if (strncmp(url, gh, gh_len) != 0) return false;
  const char* cursor = url + gh_len;
  const char* rawtok = strstr(cursor, "/raw/");
  if (!rawtok) return false;
  size_t owner_repo_len = (size_t)(rawtok - cursor);
  if (owner_repo_len < 3 || owner_repo_len > 240) return false;
  const char* past_raw = rawtok + 5;
  if (!past_raw[0]) return false;
  const char* path_after_ref = strchr(past_raw, '/');
  if (!path_after_ref || path_after_ref == past_raw) return false;
  size_t ref_len = (size_t)(path_after_ref - past_raw);
  if (ref_len == 0 || ref_len > 200) return false;
  const char* file_path = path_after_ref + 1;
  if (!file_path[0]) return false;
  int n = snprintf(out, cap, "https://raw.githubusercontent.com/%.*s/%.*s/%s", (int)owner_repo_len, cursor,
                   (int)ref_len, past_raw, file_path);
  return n > 0 && (size_t)n < cap;
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

  /* MESHCM / serial lines often end with '\n' or '\r\n'; HTTPClient treats that as part of the URL and
     many servers respond with HTTP 400. */
  static char url_trim[512];
  size_t n = 0;
  while (url[n] && n < sizeof(url_trim) - 1) {
    url_trim[n] = url[n];
    n++;
  }
  url_trim[n] = '\0';
  if (url[n] != '\0') {
    strcpy(reply, "ERR: URL too long");
    return true;
  }
  while (n > 0 && (url_trim[n - 1] == '\n' || url_trim[n - 1] == '\r' || url_trim[n - 1] == ' ' ||
                   url_trim[n - 1] == '\t')) {
    url_trim[--n] = '\0';
  }
  if (n == 0) {
    strcpy(reply, "ERR: missing URL");
    return true;
  }

  if (!meshcoreHttpOtaUrlAllowed(url_trim)) {
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
  // Default is HTTPC_DISABLE_FOLLOW_REDIRECTS; `defined(HTTPC_STRICT_...)` was always false (enum, not macro).
  https.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  https.setUserAgent("MeshCore-OTA/1.0");

  static char ota_url_buf[512];
  const char* fetch_url = url_trim;
  if (meshcoreGithubRawToRawUsercontent(url_trim, ota_url_buf, sizeof(ota_url_buf))) {
    fetch_url = ota_url_buf;
  }

  if (!https.begin(client, fetch_url)) {
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
