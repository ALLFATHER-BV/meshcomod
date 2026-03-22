#ifdef ESP_PLATFORM

#include "ESP32Board.h"

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
  if (!meshcoreHttpOtaUrlAllowed(url)) {
    strcpy(reply, "ERR: URL not allowed");
    return true;
  }
  if (WiFi.status() != WL_CONNECTED) {
    strcpy(reply, "ERR: WiFi not connected");
    return true;
  }

  inhibit_sleep = true;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.setTimeout(90000);
#if defined(HTTPC_STRICT_FOLLOW_REDIRECTS)
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
#endif

  if (!https.begin(client, url)) {
    strcpy(reply, "ERR: HTTP begin failed");
    return true;
  }

  int code = https.GET();
  if (code != HTTP_CODE_OK) {
    snprintf(reply, 128, "ERR: HTTP %d", code);
    https.end();
    return true;
  }

  int clen = https.getSize();
  WiFiClient* stream = https.getStreamPtr();
  if (!stream) {
    strcpy(reply, "ERR: no stream");
    https.end();
    return true;
  }

  if (!Update.begin(clen > 0 ? (size_t)clen : UPDATE_SIZE_UNKNOWN)) {
    snprintf(reply, 128, "ERR: %s", Update.errorString());
    https.end();
    return true;
  }

  uint8_t buf[512];
  int remaining = clen;
  unsigned long t0 = millis();

  while (https.connected() && (remaining > 0 || remaining == -1)) {
    if (millis() - t0 > 180000UL) {
      Update.abort();
      https.end();
      strcpy(reply, "ERR: timeout");
      return true;
    }

    size_t av = stream->available();
    if (!av) {
      if (remaining == 0) break;
      if (remaining < 0 && !stream->connected() && !av) break;
      delay(2);
      continue;
    }

    size_t to_read = av > sizeof(buf) ? sizeof(buf) : av;
    int rd = stream->readBytes(buf, to_read);
    if (rd <= 0) break;

    if (Update.write(buf, (size_t)rd) != (size_t)rd) {
      snprintf(reply, 128, "ERR: write %s", Update.errorString());
      Update.abort();
      https.end();
      return true;
    }

    if (remaining > 0) remaining -= rd;
  }

  https.end();

  if (!Update.end(true)) {
    snprintf(reply, 128, "ERR: %s", Update.errorString());
    return true;
  }

  strcpy(reply, "> OK rebooting");
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
