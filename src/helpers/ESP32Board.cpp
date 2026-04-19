#ifdef ESP_PLATFORM

#include "ESP32Board.h"
#include <Arduino.h>
#include <cstring>
#include <helpers/HttpOtaDisplayState.h>
#include <helpers/HttpOtaWifiSession.h>
#include <helpers/RepeaterTcpOtaEmit.h>

static void (*s_http_ota_minimal_prepare)(uint8_t) = nullptr;
static void (*s_http_ota_minimal_restore)() = nullptr;

void meshcoreRegisterHttpOtaMinimalTransport(void (*prepare)(uint8_t), void (*restore)()) {
  s_http_ota_minimal_prepare = prepare;
  s_http_ota_minimal_restore = restore;
}

void ESP32Board::prepareHttpOtaMinimalTransport(uint8_t wifi_path) {
  if (s_http_ota_minimal_prepare) s_http_ota_minimal_prepare(wifi_path);
}

void ESP32Board::restoreHttpOtaMinimalTransport() {
  if (s_http_ota_minimal_restore) s_http_ota_minimal_restore();
}

volatile int g_meshcore_http_ota_display_active = 0;
volatile uint8_t g_meshcore_http_ota_display_pct = 0xFF;
char g_meshcore_http_ota_display_line[28] = {0};

#if (defined(ADMIN_PASSWORD) || defined(MULTI_TRANSPORT_COMPANION)) && !defined(DISABLE_WIFI_OTA)
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

#include <SPIFFS.h>

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <esp_system.h>
#include <memory>

static volatile bool s_http_ota_reboot_pending = false;
static bool s_http_ota_busy = false;

namespace {

struct HttpOtaBusyGuard {
  bool* flag;
  explicit HttpOtaBusyGuard(bool* f) : flag(f) { *flag = true; }
  ~HttpOtaBusyGuard() { *flag = false; }
};

static bool httpOtaExtractHost(const char* url, char* host, size_t cap) {
  const char* u = url;
  if (strncmp(u, "https://", 8) == 0)
    u += 8;
  else if (strncmp(u, "http://", 7) == 0)
    u += 7;
  else
    return false;
  size_t i = 0;
  while (u[i] && u[i] != '/' && u[i] != '?' && u[i] != '#' && u[i] != ':') {
    if (i + 1 >= cap) return false;
    host[i] = u[i];
    i++;
  }
  host[i] = '\0';
  return i > 0;
}

/**
 * Split http(s):// URL into host, port, and request-target (path + query). Used for OTA so we call
 * HTTPClient::begin(client, host, port, uri, https) instead of begin(client, fullUrl), which avoids
 * rare parser/SNI mismatches on long firmware URLs against nginx/GitHub.
 */
static bool meshcoreHttpOtaParseHttpUrl(const char* url, char* host_buf, size_t host_cap, uint16_t* out_port,
                                        char* uri_buf, size_t uri_cap, bool* out_https) {
  if (!url || !host_buf || !uri_buf || !out_port || !out_https) return false;
  *out_https = false;
  *out_port = 80;
  const char* rest;
  if (strncmp(url, "https://", 8) == 0) {
    *out_https = true;
    *out_port = 443;
    rest = url + 8;
  } else if (strncmp(url, "http://", 7) == 0) {
    rest = url + 7;
  } else {
    return false;
  }
  if (!rest[0]) return false;

  const char* path = strchr(rest, '/');
  const char* host_end = path ? path : (rest + strlen(rest));
  size_t host_len = (size_t)(host_end - rest);
  if (host_len == 0 || host_len >= host_cap) return false;
  memcpy(host_buf, rest, host_len);
  host_buf[host_len] = '\0';

  char* colon = strchr(host_buf, ':');
  if (colon) {
    *colon = '\0';
    int p = atoi(colon + 1);
    if (p > 0 && p <= 65535) *out_port = (uint16_t)p;
  }

  if (path) {
    size_t uri_len = strlen(path);
    if (uri_len == 0 || uri_len >= uri_cap) return false;
    memcpy(uri_buf, path, uri_len + 1);
  } else {
    if (uri_cap < 2) return false;
    uri_buf[0] = '/';
    uri_buf[1] = '\0';
  }
  return true;
}

static void httpOtaEmitHostLookup(const char* url_label, const char* url) {
  char host[64];
  if (!httpOtaExtractHost(url, host, sizeof(host))) return;
  IPAddress ip;
  char line[112];
  if (WiFi.hostByName(host, ip)) {
    snprintf(line, sizeof(line), "OTA: diag %s %s -> %s", url_label, host, ip.toString().c_str());
  } else {
    snprintf(line, sizeof(line), "OTA: diag %s %s -> DNS fail", url_label, host);
  }
  meshcoreRepeaterTcpOtaEmitLine(line);
}

}  // namespace

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

/** Merged images exceed the OTA data partition; reject before any HTTP work. */
static bool httpOtaUrlLooksMergedBin(const char* u) {
  if (!u) return false;
  return strstr(u, "-merged.bin") != nullptr || strstr(u, "/merged.bin") != nullptr;
}

static void httpOtaEmitEffectiveUrl(const char* label, const char* url) {
  if (!url || !url[0]) return;
  size_t len = strlen(url);
  const char* tail = url;
  if (len > 72) tail = url + (len - 72);
  char line[120];
  snprintf(line, sizeof(line), "OTA: %s …%s", label, tail);
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

/**
 * `https://raw.githubusercontent.com/owner/repo/main/path` ->
 * `https://flasher.meshcomod.com/firmware-download/path` (HTTPS only in OTA chain).
 * The meshcomod nginx proxy maps /firmware-download/* to ALLFATHER-BV/meshcomod main/* on GitHub raw.
 */
static bool meshcoreRawGithubToMeshcomodProxy(const char* url, bool repeater_host, bool use_https, char* out,
                                              size_t cap) {
  static const char rawgh[] = "https://raw.githubusercontent.com/";
  const size_t rawgh_len = sizeof(rawgh) - 1;
  if (strncmp(url, rawgh, rawgh_len) != 0) return false;
  const char* p = url + rawgh_len;

  static const char prefix[] = "ALLFATHER-BV/meshcomod/main/";
  const size_t prefix_len = sizeof(prefix) - 1;
  if (strncmp(p, prefix, prefix_len) != 0) return false;
  const char* rel = p + prefix_len;
  if (!rel[0]) return false;

  const char* scheme = use_https ? "https" : "http";
  const char* host = repeater_host ? "repeater.meshcomod.com" : "flasher.meshcomod.com";
  int n = snprintf(out, cap, "%s://%s/firmware-download/%s", scheme, host, rel);
  return n > 0 && (size_t)n < cap;
}

/**
 * `https://flasher.meshcomod.com/firmware-download/path` ->
 * `https://raw.githubusercontent.com/ALLFATHER-BV/meshcomod/main/path`
 * so companion OTA can fetch the firmware directly from GitHub even when the UI hands it a flasher URL.
 */
static bool meshcoreMeshcomodProxyToRawGithub(const char* url, char* out, size_t cap) {
  static const char flasher_https[] = "https://flasher.meshcomod.com/firmware-download/";
  static const char flasher_http[] = "http://flasher.meshcomod.com/firmware-download/";
  static const char repeater_https[] = "https://repeater.meshcomod.com/firmware-download/";
  static const char repeater_http[] = "http://repeater.meshcomod.com/firmware-download/";

  const char* rel = nullptr;
  if (strncmp(url, flasher_https, sizeof(flasher_https) - 1) == 0) {
    rel = url + sizeof(flasher_https) - 1;
  } else if (strncmp(url, flasher_http, sizeof(flasher_http) - 1) == 0) {
    rel = url + sizeof(flasher_http) - 1;
  } else if (strncmp(url, repeater_https, sizeof(repeater_https) - 1) == 0) {
    rel = url + sizeof(repeater_https) - 1;
  } else if (strncmp(url, repeater_http, sizeof(repeater_http) - 1) == 0) {
    rel = url + sizeof(repeater_http) - 1;
  }
  if (!rel || !rel[0]) return false;

  int n = snprintf(out, cap, "https://raw.githubusercontent.com/ALLFATHER-BV/meshcomod/main/%s", rel);
  return n > 0 && (size_t)n < cap;
}

void ESP32Board::emitHttpOtaNetDiagnosticLines() {
  if (WiFi.status() != WL_CONNECTED) {
    meshcoreRepeaterTcpOtaEmitLine("OTA: diag wifi=disconnected");
    return;
  }
  char line[120];
  snprintf(line, sizeof(line), "OTA: diag heap=%u ip=%s", (unsigned)esp_get_free_heap_size(),
           WiFi.localIP().toString().c_str());
  meshcoreRepeaterTcpOtaEmitLine(line);
  snprintf(line, sizeof(line), "OTA: diag gw=%s dns=%s rssi=%d", WiFi.gatewayIP().toString().c_str(),
           WiFi.dnsIP().toString().c_str(), (int)WiFi.RSSI());
  meshcoreRepeaterTcpOtaEmitLine(line);
  static const char* const kHosts[] = {"raw.githubusercontent.com", "flasher.meshcomod.com", "github.com"};
  for (size_t i = 0; i < sizeof(kHosts) / sizeof(kHosts[0]); i++) {
    IPAddress ip;
    if (WiFi.hostByName(kHosts[i], ip)) {
      snprintf(line, sizeof(line), "OTA: diag %s -> %s", kHosts[i], ip.toString().c_str());
    } else {
      snprintf(line, sizeof(line), "OTA: diag %s -> DNS fail", kHosts[i]);
    }
    meshcoreRepeaterTcpOtaEmitLine(line);
  }
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
  if (s_http_ota_busy) {
    strcpy(reply, "ERR: OTA already running");
    return true;
  }
  HttpOtaBusyGuard ota_busy_guard(&s_http_ota_busy);

  inhibit_sleep = true;

  httpOtaDisplaySet(0xFF, "OTA: connecting");
  meshcoreRepeaterTcpOtaEmitLine("OTA: connecting");

  static char ota_url_buf[512];
  static char ota_url_proxy_fls_https[512];
  const char* fetch_url = url_trim;
  const char* proxy_fls_https = nullptr;

  /** v1.14.0.x: flasher/repeater `firmware-download/…` URLs were fetched **as-is** (OTA: direct). Only
   *  `github.com/.../raw/...` was normalized to raw.githubusercontent.com. Rewriting flasher→raw (added
   *  later) forces the ESP32 to hit raw GitHub for the same file; that path can 404 or fail on
   *  RAM-tight companions while **flasher.meshcomod.com** (disk proxy) still works. */
  const bool url_is_meshcomod_firmware_download =
      (strncmp(url_trim, "https://flasher.meshcomod.com/firmware-download/", 48) == 0) ||
      (strncmp(url_trim, "http://flasher.meshcomod.com/firmware-download/", 47) == 0) ||
      (strncmp(url_trim, "https://repeater.meshcomod.com/firmware-download/", 50) == 0) ||
      (strncmp(url_trim, "http://repeater.meshcomod.com/firmware-download/", 49) == 0);

  if (meshcoreGithubRawToRawUsercontent(url_trim, ota_url_buf, sizeof(ota_url_buf))) {
    fetch_url = ota_url_buf;
  } else if (!url_is_meshcomod_firmware_download &&
             meshcoreMeshcomodProxyToRawGithub(url_trim, ota_url_buf, sizeof(ota_url_buf))) {
    fetch_url = ota_url_buf;
  }
  if (httpOtaUrlLooksMergedBin(url_trim) || httpOtaUrlLooksMergedBin(fetch_url)) {
    strcpy(reply, "ERR: merged .bin not supported for OTA");
    httpOtaDisplayReset();
    return true;
  }
  if (meshcoreRawGithubToMeshcomodProxy(fetch_url, false, true, ota_url_proxy_fls_https, sizeof(ota_url_proxy_fls_https))) {
    proxy_fls_https = ota_url_proxy_fls_https;
  }

  {
    char dline[160];
    snprintf(dline, sizeof(dline), "OTA: diag heap=%u max=%u psram=%u pmax=%u ip=%s",
             (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getFreePsram(),
             (unsigned)ESP.getMaxAllocPsram(), WiFi.localIP().toString().c_str());
    meshcoreRepeaterTcpOtaEmitLine(dline);
    snprintf(dline, sizeof(dline), "OTA: diag gw=%s dns=%s rssi=%d", WiFi.gatewayIP().toString().c_str(),
             WiFi.dnsIP().toString().c_str(), (int)WiFi.RSSI());
    meshcoreRepeaterTcpOtaEmitLine(dline);
    httpOtaEmitHostLookup("fetch", fetch_url);
  }

  std::unique_ptr<WiFiClientSecure> p_tls;
  std::unique_ptr<WiFiClient> p_plain;
  std::unique_ptr<HTTPClient> p_https;

  auto rebuildOtaHttpSession = [&]() {
    if (p_https) {
      p_https->end();
    }
    if (p_tls) {
      p_tls->stop();
    }
    if (p_plain) {
      p_plain->stop();
    }
    delay(100);
    p_tls.reset(new WiFiClientSecure());
    p_tls->setInsecure();
    p_tls->setTimeout(90000);
    p_tls->setHandshakeTimeout(30);
    p_plain.reset(new WiFiClient());
    p_plain->setTimeout(90000);
    p_https.reset(new HTTPClient());
    p_https->setTimeout(90000);
    p_https->setReuse(false);
    p_https->setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    p_https->setUserAgent("MeshCore-OTA/1.0");
  };

  rebuildOtaHttpSession();

  auto emitTlsClientError = [&](const char* stage, const char* target_url, int http_code) {
    if (strncmp(target_url, "https://", 8) != 0) return;
    char tls_buf[128] = {0};
    int tls_err = p_tls->lastError(tls_buf, sizeof(tls_buf));
    if (tls_err == 0) return;
    char tls_line[224];
    snprintf(tls_line, sizeof(tls_line), "OTA: %s http=%d tls=%d heap=%u max=%u %s", stage, http_code, tls_err,
             (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), tls_buf);
    meshcoreRepeaterTcpOtaEmitLine(tls_line);
  };

  auto beginAndGet = [&](const char* target_url, int attempt) -> int {
    if (attempt > 1) {
      httpOtaDisplaySet(0xFF, "OTA: reconnecting");
      char retry_line[48];
      snprintf(retry_line, sizeof(retry_line), "OTA: connect retry %d/3", attempt);
      meshcoreRepeaterTcpOtaEmitLine(retry_line);

      if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
        unsigned long wait_t0 = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - wait_t0) < 8000UL) {
          delay(100);
          yield();
        }
      }
      delay(200 * (attempt - 1));
      yield();
    }
    bool is_https = (strncmp(target_url, "https://", 8) == 0);
    bool ok = is_https ? p_https->begin(*p_tls, String(target_url)) : p_https->begin(*p_plain, String(target_url));
    if (!ok) {
      char hbuf[128];
      char ubuf[512];
      uint16_t port = 443;
      bool want_tls = false;
      if (!meshcoreHttpOtaParseHttpUrl(target_url, hbuf, sizeof(hbuf), &port, ubuf, sizeof(ubuf), &want_tls)) {
        meshcoreRepeaterTcpOtaEmitLine("OTA: ERR http url parse");
        p_tls->stop();
        p_plain->stop();
        delay(15);
        return HTTPC_ERROR_CONNECTION_REFUSED;
      }
      if (want_tls) {
        ok = p_https->begin(*p_tls, String(hbuf), port, String(ubuf), true);
      } else {
        ok = p_https->begin(*p_plain, String(hbuf), port, String(ubuf), false);
      }
    }
    if (!ok) {
      emitTlsClientError("begin fail", target_url, HTTPC_ERROR_CONNECTION_REFUSED);
      p_tls->stop();
      p_plain->stop();
      delay(15);
      return HTTPC_ERROR_CONNECTION_REFUSED;
    }
    int code = p_https->GET();
    if (code < 0) emitTlsClientError("get fail", target_url, code);
    if (code > 0 && code != HTTP_CODE_OK) {
      char hbuf[128];
      char ubuf[512];
      uint16_t port = 443;
      bool want_tls = false;
      if (meshcoreHttpOtaParseHttpUrl(target_url, hbuf, sizeof(hbuf), &port, ubuf, sizeof(ubuf), &want_tls)) {
        char rsp_line[192];
        snprintf(rsp_line, sizeof(rsp_line), "OTA: HTTP %d host=%s path=%.100s", code, hbuf, ubuf);
        meshcoreRepeaterTcpOtaEmitLine(rsp_line);
      } else {
        char rsp_line[120];
        snprintf(rsp_line, sizeof(rsp_line), "OTA: HTTP %d url=%.80s", code, target_url);
        meshcoreRepeaterTcpOtaEmitLine(rsp_line);
      }
    }
    return code;
  };

  auto getWithRetries = [&](const char* target_url) -> int {
    httpOtaEmitHostLookup("try", target_url);
    int c = beginAndGet(target_url, 1);
    if (c < 0) {
      String err = p_https->errorToString(c);
      char err_line[96];
      snprintf(err_line, sizeof(err_line), "OTA: connect err %d %s", c, err.c_str());
      meshcoreRepeaterTcpOtaEmitLine(err_line);
      emitTlsClientError("connect diag", target_url, c);
      p_https->end();
      p_tls->stop();
      p_plain->stop();
      delay(15);
      c = beginAndGet(target_url, 2);
      if (c < 0) {
        err = p_https->errorToString(c);
        snprintf(err_line, sizeof(err_line), "OTA: connect err %d %s", c, err.c_str());
        meshcoreRepeaterTcpOtaEmitLine(err_line);
        emitTlsClientError("connect diag", target_url, c);
        p_https->end();
        p_tls->stop();
        p_plain->stop();
        delay(15);
        c = beginAndGet(target_url, 3);
      }
    }
    if (c < 0) {
      p_https->end();
      p_tls->stop();
      p_plain->stop();
    }
    return c;
  };

  const char* effective_url = fetch_url;
  int code = -1;
  if (proxy_fls_https) {
    meshcoreRepeaterTcpOtaEmitLine("OTA: fetch raw-github");
    code = getWithRetries(fetch_url);
    if (code == HTTP_CODE_OK) {
      effective_url = fetch_url;
    } else {
      meshcoreRepeaterTcpOtaEmitLine("OTA: fallback flasher-https");
      rebuildOtaHttpSession();
      code = getWithRetries(proxy_fls_https);
      if (code == HTTP_CODE_OK) effective_url = proxy_fls_https;
    }
  } else {
    meshcoreRepeaterTcpOtaEmitLine("OTA: direct");
    code = getWithRetries(fetch_url);
    /* If direct flasher/repeater HTTPS fails (404, TLS, etc.) but URL is meshcomod firmware-download, try
       raw.githubusercontent.com same path. Nginx :443 must include firmware-download snippet (see
       Meshcomod-client deploy docs); missing snippet often yields 404 for ESP32 while curl still works if
       it hits a different path. Raw is a second independent origin. */
    static char ota_raw_fallback[512];
    if (code != HTTP_CODE_OK && url_is_meshcomod_firmware_download &&
        meshcoreMeshcomodProxyToRawGithub(url_trim, ota_raw_fallback, sizeof(ota_raw_fallback))) {
      meshcoreRepeaterTcpOtaEmitLine("OTA: fallback raw-github");
      if (code < 0) {
        rebuildOtaHttpSession();
      } else {
        if (p_https) p_https->end();
        if (p_tls) p_tls->stop();
        if (p_plain) p_plain->stop();
        delay(80);
      }
      code = getWithRetries(ota_raw_fallback);
      if (code == HTTP_CODE_OK) effective_url = ota_raw_fallback;
    }
  }

  if (code != HTTP_CODE_OK) {
    String err = (code < 0) ? p_https->errorToString(code) : String("");
    if (code < 0 && err.length() > 0) {
      snprintf(reply, 128, "ERR: HTTP %d (%s)", code, err.c_str());
    } else {
      snprintf(reply, 128, "ERR: HTTP %d", code);
    }
    meshcoreRepeaterTcpOtaEmitLine(reply);
    p_https->end();
    p_tls->stop();
    p_plain->stop();
    httpOtaDisplayReset();
    return true;
  }

  int clen = p_https->getSize();
  WiFiClient* stream = p_https->getStreamPtr();
  if (!stream) {
    strcpy(reply, "ERR: no stream");
    meshcoreRepeaterTcpOtaEmitLine(reply);
    p_https->end();
    p_tls->stop();
    p_plain->stop();
    httpOtaDisplayReset();
    return true;
  }

  httpOtaDisplaySet(0, "OTA: install started");
  meshcoreRepeaterTcpOtaEmitLine("OTA: HTTP OK, flashing");

  String ct = p_https->header("Content-Type");
  ct.toLowerCase();
  const char* ct_c = ct.c_str();
  size_t max_sketch = ESP.getFreeSketchSpace();
  {
    char meta[144];
    snprintf(meta, sizeof(meta), "OTA: http meta clen=%d max_sketch=%u", clen, (unsigned)max_sketch);
    meshcoreRepeaterTcpOtaEmitLine(meta);
    char ctline[112];
    snprintf(ctline, sizeof(ctline), "OTA: http ct %s", ct_c[0] ? ct_c : "(none)");
    meshcoreRepeaterTcpOtaEmitLine(ctline);
  }
  httpOtaEmitEffectiveUrl("effective", effective_url);

  if (strstr(ct_c, "text/html") != nullptr) {
    strcpy(reply, "ERR: server sent HTML not firmware");
    p_https->end();
    p_tls->stop();
    p_plain->stop();
    httpOtaDisplayReset();
    meshcoreRepeaterTcpOtaEmitLine("OTA: ERR html payload");
    return true;
  }
  if (clen > 0 && (size_t)clen > max_sketch) {
    snprintf(reply, 128, "ERR: image %u > OTA %u", (unsigned)clen, (unsigned)max_sketch);
    p_https->end();
    p_tls->stop();
    p_plain->stop();
    httpOtaDisplayReset();
    meshcoreRepeaterTcpOtaEmitLine("OTA: ERR image too large");
    return true;
  }

  uint8_t sig[4];
  size_t sig_len = 0;
  unsigned long sig_t0 = millis();
  while (sig_len < sizeof(sig) && (millis() - sig_t0) < 30000UL) {
    if (!p_https->connected() && stream->available() == 0) break;
    size_t av = stream->available();
    if (!av) {
      delay(2);
      yield();
      continue;
    }
    int n = stream->readBytes(sig + sig_len, sizeof(sig) - sig_len);
    if (n <= 0) break;
    sig_len += (size_t)n;
  }
  {
    char sigline[56];
    if (sig_len >= 4) {
      snprintf(sigline, sizeof(sigline), "OTA: http sig %02X%02X%02X%02X", sig[0], sig[1], sig[2], sig[3]);
    } else {
      snprintf(sigline, sizeof(sigline), "OTA: http sig short len=%u", (unsigned)sig_len);
    }
    meshcoreRepeaterTcpOtaEmitLine(sigline);
  }
  if (sig_len >= 1 && sig[0] == '<') {
    strcpy(reply, "ERR: body looks HTML not firmware");
    p_https->end();
    p_tls->stop();
    p_plain->stop();
    httpOtaDisplayReset();
    meshcoreRepeaterTcpOtaEmitLine("OTA: ERR html signature");
    return true;
  }
  if (clen > 0 && sig_len > (size_t)clen) {
    strcpy(reply, "ERR: body shorter than header");
    p_https->end();
    p_tls->stop();
    p_plain->stop();
    httpOtaDisplayReset();
    meshcoreRepeaterTcpOtaEmitLine("OTA: ERR clen/signature");
    return true;
  }

  size_t begin_size = (clen > 0) ? (size_t)clen : UPDATE_SIZE_UNKNOWN;
  if (!Update.begin(begin_size)) {
    snprintf(reply, 128, "ERR: %s", Update.errorString());
    p_https->end();
    p_tls->stop();
    p_plain->stop();
    httpOtaDisplayReset();
    return true;
  }

  size_t total_written = 0;
  if (sig_len > 0) {
    if (Update.write(sig, sig_len) != sig_len) {
      char detail[176];
      snprintf(detail, sizeof(detail), "OTA: ERR flash write pos=0 clen=%d err=%d %s free_sketch=%u", clen,
               (int)Update.getError(), Update.errorString(), (unsigned)ESP.getFreeSketchSpace());
      meshcoreRepeaterTcpOtaEmitLine(detail);
      snprintf(reply, 128, "ERR: write %s", Update.errorString());
      Update.abort();
      p_https->end();
      p_tls->stop();
      p_plain->stop();
      httpOtaDisplayReset();
      meshcoreRepeaterTcpOtaEmitLine("OTA: ERR flash write");
      return true;
    }
    total_written = sig_len;
  }

  uint8_t buf[1024];
  int remaining = clen;
  if (remaining > 0) {
    remaining -= (int)sig_len;
    if (remaining < 0) remaining = 0;
  }
  unsigned long t0 = millis();

  while (p_https->connected() && (remaining > 0 || remaining == -1)) {
    if (millis() - t0 > 180000UL) {
      Update.abort();
      p_https->end();
      p_tls->stop();
      p_plain->stop();
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
      char detail[176];
      snprintf(detail, sizeof(detail), "OTA: ERR flash write pos=%u clen=%d err=%d %s free_sketch=%u",
               (unsigned)total_written, clen, (int)Update.getError(), Update.errorString(),
               (unsigned)ESP.getFreeSketchSpace());
      meshcoreRepeaterTcpOtaEmitLine(detail);
      snprintf(reply, 128, "ERR: write %s", Update.errorString());
      Update.abort();
      p_https->end();
      p_tls->stop();
      p_plain->stop();
      httpOtaDisplayReset();
      meshcoreRepeaterTcpOtaEmitLine("OTA: ERR flash write");
      return true;
    }

    total_written += (size_t)rd;
    if (remaining > 0) remaining -= rd;

    httpOtaEmitProgressThrottled(clen, total_written, "OTA: downloading");
    yield();
  }

  if (remaining > 0) {
    snprintf(reply, 128, "ERR: truncated got %u need %d", (unsigned)total_written, clen);
    Update.abort();
    p_https->end();
    p_tls->stop();
    p_plain->stop();
    httpOtaDisplayReset();
    meshcoreRepeaterTcpOtaEmitLine("OTA: ERR truncated body");
    return true;
  }
  if (clen > 0 && total_written != (size_t)clen) {
    snprintf(reply, 128, "ERR: size mismatch %u/%d", (unsigned)total_written, clen);
    Update.abort();
    p_https->end();
    p_tls->stop();
    p_plain->stop();
    httpOtaDisplayReset();
    meshcoreRepeaterTcpOtaEmitLine("OTA: ERR size mismatch");
    return true;
  }

  p_https->end();

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

void ESP32Board::emitHttpOtaNetDiagnosticLines() {}

void ESP32Board::pollHttpOtaReboot() {}
#endif

#endif
