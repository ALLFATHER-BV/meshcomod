#include "WebSocketCompanionServer.h"
#include <mbedtls/sha1.h>
#include <string.h>
#if WS_USE_TLS
#include "wss_cert.h"
#include <Arduino.h>
#include "lwip/sockets.h"
#include <fcntl.h>
#include <errno.h>
#include <mbedtls/error.h>

// Non-blocking recv/send for TLS: return WANT_READ/WANT_WRITE on EAGAIN so handshake never blocks (fixes ERR_CONNECTION_RESET on ESP32).
// WSS debug: set to 1 to log every I/O error (errno) from bio callbacks; 0 = key events only
#ifndef WSS_DEBUG_IO
#define WSS_DEBUG_IO 0
#endif

static int wss_net_recv_nonblock(void* ctx, unsigned char* buf, size_t len) {
  mbedtls_net_context* net = (mbedtls_net_context*)ctx;
  if (net->fd < 0) return MBEDTLS_ERR_NET_INVALID_CONTEXT;
  int n = (int)recv(net->fd, buf, len, 0);
  if (n > 0) return n;
  if (n == 0) return MBEDTLS_ERR_SSL_CONN_EOF;
  if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_READ;
#if WSS_DEBUG_IO
  Serial.printf("[WSS] recv err fd=%d errno=%d\n", net->fd, errno);
#endif
  return MBEDTLS_ERR_NET_RECV_FAILED;
}
static int wss_net_send_nonblock(void* ctx, const unsigned char* buf, size_t len) {
  mbedtls_net_context* net = (mbedtls_net_context*)ctx;
  if (net->fd < 0) return MBEDTLS_ERR_NET_INVALID_CONTEXT;
  int n = (int)send(net->fd, buf, len, 0);
  if (n >= 0) return n;
  if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_WRITE;
#if WSS_DEBUG_IO
  Serial.printf("[WSS] send err fd=%d errno=%d\n", net->fd, errno);
#endif
  return MBEDTLS_ERR_NET_SEND_FAILED;
}

#endif

// Optional: build with -DWS_FRAME_DEBUG=1 to log each WS send (client, code 2=START/3=CONTACT/4=END, len, written).
// Use WiFi-only when enabled (no Web Serial on same device) so Serial is free for debug.
#ifndef WS_FRAME_DEBUG
#define WS_FRAME_DEBUG 0
#endif

#define TCP_WRITE_TIMEOUT_MS   120
#define WS_MAGIC               "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
// Plain GET (e.g. browser opening https://device:8765): send this so user can accept cert and see confirmation.
static const char WSS_HTTP_CERT_PAGE[] =
  "HTTP/1.1 200 OK\r\n"
  "Content-Type: text/html; charset=utf-8\r\n"
  "Connection: close\r\n"
  "\r\n"
  "<!DOCTYPE html><html><head><meta charset=utf-8><title>meshcomod WSS</title></head><body>"
  "<h1>meshcomod WSS</h1><p>Secure WebSocket endpoint. If you see this, the certificate was accepted. "
  "Use the meshcomod client with wss:// this address.</p></body></html>";

// Companion receive states (same as TCPCompanionServer)
#define COMP_STATE_IDLE        0
#define COMP_STATE_HDR_FOUND   1
#define COMP_STATE_LEN1_FOUND  2
#define COMP_STATE_LEN2_FOUND  3

// WebSocket frame read states
#define WS_STATE_HEADER_0      0
#define WS_STATE_HEADER_1      1
#define WS_STATE_LEN_EXT       2
#define WS_STATE_MASK          3
#define WS_STATE_PAYLOAD       4

static const char BASE64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64Encode20(const uint8_t* in, char* out) {
  // 20 bytes -> 28 chars (7 groups of 3 bytes -> 4 chars each), then null
  for (int i = 0; i < 20; i += 3) {
    uint32_t v = in[i] << 16;
    if (i + 1 < 20) v |= in[i + 1] << 8;
    if (i + 2 < 20) v |= in[i + 2];
    out[0] = BASE64[(v >> 18) & 63];
    out[1] = BASE64[(v >> 12) & 63];
    out[2] = (i + 1 < 20) ? BASE64[(v >> 6) & 63] : '=';
    out[3] = (i + 2 < 20) ? BASE64[v & 63] : '=';
    out += 4;
  }
  *out = '\0';
}

#if !WS_USE_TLS
static bool writeAllBytes(WiFiClient& client, const uint8_t* buf, size_t len, uint32_t timeout_ms) {
  size_t sent = 0;
  uint32_t start = millis();
  while (sent < len) {
    if (!client.connected()) return false;
    size_t n = client.write(buf + sent, len - sent);
    if (n > 0) {
      sent += n;
      continue;
    }
    if (millis() - start >= timeout_ms) return false;
    delay(1);
  }
  return true;
}
#endif

WebSocketCompanionServer::WebSocketCompanionServer()
#if WS_USE_TLS
  : _port(0), _poll_start_idx(0), _tls_initialized(false)
#else
  : _server(WiFiServer()), _port(0), _poll_start_idx(0)
#endif
{
  for (int i = 0; i < WS_COMPANION_MAX_CLIENTS; i++) {
    _clients[i].in_use = false;
    _clients[i].handshake_done = false;
    _clients[i].handshake_len = 0;
    _clients[i].ws_state = WS_STATE_HEADER_0;
    _clients[i].comp_state = COMP_STATE_IDLE;
#if WS_USE_TLS
    _clients[i].ssl_handshake_done = false;
    _clients[i].handshake_start_ms = 0;
    /* mbedTLS inits moved to begin() so no TLS runs at global-construct time (avoids "Loading..." hang). */
#endif
  }
#if WS_USE_TLS
  /* mbedTLS inits deferred to begin() so setup()/display can complete before any TLS/heap use. */
#endif
}

void WebSocketCompanionServer::begin(uint16_t port) {
  _port = port;
#if WS_USE_TLS
  // Defer all mbedTLS init to here (not constructor) so device boots past "Loading..." and UI runs first.
  mbedtls_net_init(&_listen_fd);
  mbedtls_ssl_config_init(&_ssl_conf);
  mbedtls_x509_crt_init(&_srvcert);
  mbedtls_pk_init(&_pkey);
  mbedtls_entropy_init(&_entropy);
  mbedtls_ctr_drbg_init(&_ctr_drbg);
  for (int i = 0; i < WS_COMPANION_MAX_CLIENTS; i++) {
    mbedtls_net_init(&_clients[i].client_net);
    mbedtls_ssl_init(&_clients[i].ssl_ctx);
  }
  if (mbedtls_ctr_drbg_seed(&_ctr_drbg, mbedtls_entropy_func, &_entropy, (const unsigned char*)"wss", 3) != 0) {
    Serial.println("[WSS] init failed: ctr_drbg_seed");
    return;
  }
  if (mbedtls_x509_crt_parse(&_srvcert, (const unsigned char*)WSS_SERVER_CERT_PEM, strlen(WSS_SERVER_CERT_PEM) + 1) != 0) {
    Serial.println("[WSS] init failed: x509_crt_parse");
    return;
  }
  if (mbedtls_pk_parse_key(&_pkey, (const unsigned char*)WSS_SERVER_KEY_PEM, strlen(WSS_SERVER_KEY_PEM) + 1, NULL, 0) != 0) {
    Serial.println("[WSS] init failed: pk_parse_key");
    return;
  }
  if (mbedtls_ssl_config_defaults(&_ssl_conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
    Serial.println("[WSS] init failed: ssl_config_defaults");
    return;
  }
#if defined(MBEDTLS_SSL_PROTO_TLS1_2) && defined(MBEDTLS_SSL_VERSION_TLS1_2)
  mbedtls_ssl_conf_min_tls_version(&_ssl_conf, MBEDTLS_SSL_VERSION_TLS1_2);
#endif
  mbedtls_ssl_conf_authmode(&_ssl_conf, MBEDTLS_SSL_VERIFY_NONE);
  mbedtls_ssl_conf_own_cert(&_ssl_conf, &_srvcert, &_pkey);
  mbedtls_ssl_conf_rng(&_ssl_conf, mbedtls_ctr_drbg_random, &_ctr_drbg);
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    Serial.printf("[WSS] init failed: socket errno=%d\n", errno);
    return;
  }
  int opt = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    Serial.printf("[WSS] init failed: bind port %u errno=%d\n", (unsigned)port, errno);
    close(fd);
    return;
  }
  if (listen(fd, 4) != 0) {
    Serial.printf("[WSS] init failed: listen errno=%d\n", errno);
    close(fd);
    return;
  }
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  _listen_fd.fd = fd;
  _tls_initialized = true;
  Serial.printf("[WSS] listening port=%u fd=%d\n", (unsigned)port, fd);
#else
  _server.begin(port);
#endif
}

void WebSocketCompanionServer::stop() {
#if WS_USE_TLS
  if (_listen_fd.fd >= 0) {
    close(_listen_fd.fd);
    _listen_fd.fd = -1;
  }
  for (int i = 0; i < WS_COMPANION_MAX_CLIENTS; i++) {
    if (_clients[i].in_use) {
      mbedtls_ssl_free(&_clients[i].ssl_ctx);
      mbedtls_net_free(&_clients[i].client_net);
      _clients[i].in_use = false;
    }
  }
  mbedtls_ssl_config_free(&_ssl_conf);
  mbedtls_x509_crt_free(&_srvcert);
  mbedtls_pk_free(&_pkey);
  mbedtls_ctr_drbg_free(&_ctr_drbg);
  mbedtls_entropy_free(&_entropy);
  _tls_initialized = false;
#else
  _server.stop();
  for (int i = 0; i < WS_COMPANION_MAX_CLIENTS; i++) {
    if (_clients[i].in_use) {
      _clients[i].client.stop();
      _clients[i].in_use = false;
    }
  }
#endif
}

void WebSocketCompanionServer::acceptNewClients() {
#if WS_USE_TLS
  if (!_tls_initialized) return;
  // Advance in-progress TLS handshakes. No select() block; yield with delay(1) on WANT_* so WiFi stack can deliver data (fixes BEACON_TIMEOUT / RESET). Timeout stuck clients.
  const uint32_t WSS_HANDSHAKE_TIMEOUT_MS = 30000;
  for (int i = 0; i < WS_COMPANION_MAX_CLIENTS; i++) {
    if (!_clients[i].in_use || _clients[i].ssl_handshake_done) continue;
    if ((uint32_t)(millis() - _clients[i].handshake_start_ms) > WSS_HANDSHAKE_TIMEOUT_MS) {
      Serial.printf("[WSS] handshake timeout slot=%d\n", i);
      mbedtls_ssl_free(&_clients[i].ssl_ctx);
      mbedtls_net_free(&_clients[i].client_net);
      _clients[i].in_use = false;
      continue;
    }
    mbedtls_ssl_context* ssl = &_clients[i].ssl_ctx;
    for (int step = 0; step < 25; step++) {
      int ret = mbedtls_ssl_handshake(ssl);
      if (ret == 0) {
        _clients[i].ssl_handshake_done = true;
        Serial.printf("[WSS] TLS handshake OK slot=%d\n", i);
        break;
      }
      if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
        char errbuf[80];
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        Serial.printf("[WSS] handshake err %d: %s\n", ret, errbuf);
        mbedtls_ssl_free(ssl);
        mbedtls_net_free(&_clients[i].client_net);
        _clients[i].in_use = false;
        break;
      }
      delay(1);  // yield so WiFi/LwIP can process and deliver data (avoids BEACON_TIMEOUT / RESET)
    }
  }
  int slot = -1;
  for (int i = 0; i < WS_COMPANION_MAX_CLIENTS; i++) {
    if (!_clients[i].in_use) { slot = i; break; }
  }
  if (slot < 0) return;
  struct sockaddr_in client_addr;
  socklen_t addrlen = sizeof(client_addr);
  int client_fd = accept(_listen_fd.fd, (struct sockaddr*)&client_addr, &addrlen);
  if (client_fd < 0) return;  // non-blocking: no client waiting
  Serial.printf("[WSS] TCP accept slot=%d fd=%d\n", slot, client_fd);
  int flags = fcntl(client_fd, F_GETFL, 0);
  if (flags >= 0) fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
  mbedtls_net_context* client_net = &_clients[slot].client_net;
  mbedtls_net_init(client_net);
  client_net->fd = client_fd;
  mbedtls_ssl_context* ssl = &_clients[slot].ssl_ctx;
  mbedtls_ssl_init(ssl);
  if (mbedtls_ssl_setup(ssl, &_ssl_conf) != 0) {
    Serial.println("[WSS] ssl_setup failed, close");
    mbedtls_ssl_free(ssl);
    mbedtls_net_free(client_net);
    return;
  }
  // recv_timeout = NULL: handshake never blocks (any select() block causes ERR_CONNECTION_RESET). Rely on frequent main-loop polling.
  mbedtls_ssl_set_bio(ssl, client_net, wss_net_send_nonblock, wss_net_recv_nonblock, NULL);
  _clients[slot].in_use = true;
  _clients[slot].ssl_handshake_done = false;
  _clients[slot].handshake_start_ms = millis();
  _clients[slot].handshake_done = false;
  _clients[slot].handshake_len = 0;
  _clients[slot].ws_state = WS_STATE_HEADER_0;
  _clients[slot].comp_state = COMP_STATE_IDLE;
  int ret = mbedtls_ssl_handshake(ssl);
  if (ret == 0) {
    _clients[slot].ssl_handshake_done = true;
    Serial.printf("[WSS] TLS handshake OK slot=%d (first try)\n", slot);
  } else if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
    char errbuf[80];
    mbedtls_strerror(ret, errbuf, sizeof(errbuf));
    Serial.printf("[WSS] new client handshake err %d: %s\n", ret, errbuf);
    mbedtls_ssl_free(ssl);
    mbedtls_net_free(&_clients[slot].client_net);
    _clients[slot].in_use = false;
  }
#else
  while (_server.hasClient()) {
    WiFiClient incoming = _server.accept();
    if (!incoming) continue;
    int slot = -1;
    for (int i = 0; i < WS_COMPANION_MAX_CLIENTS; i++) {
      if (!_clients[i].in_use) {
        slot = i;
        break;
      }
    }
    if (slot >= 0) {
      _clients[slot].client = incoming;
      _clients[slot].in_use = true;
      _clients[slot].handshake_done = false;
      _clients[slot].handshake_len = 0;
      _clients[slot].ws_state = WS_STATE_HEADER_0;
      _clients[slot].comp_state = COMP_STATE_IDLE;
    } else {
      incoming.stop();
    }
  }
#endif
}

void WebSocketCompanionServer::pruneDisconnected() {
#if WS_USE_TLS
  (void)0;  // TLS: disconnect is detected on read in doHandshake/pollRecvFrame
#else
  for (int i = 0; i < WS_COMPANION_MAX_CLIENTS; i++) {
    if (_clients[i].in_use && !_clients[i].client.connected()) {
      _clients[i].client.stop();
      _clients[i].in_use = false;
    }
  }
#endif
}

bool WebSocketCompanionServer::doHandshake(int idx) {
  WSClientState* c = &_clients[idx];
#if WS_USE_TLS
  if (!c->ssl_handshake_done) return false;  // TLS handshake still in progress (done in acceptNewClients)
  mbedtls_ssl_context* ssl = &c->ssl_ctx;
  unsigned char byte;
  int r = mbedtls_ssl_read(ssl, &byte, 1);
  if (r == 1) {
    if (c->handshake_len < WS_HANDSHAKE_MAX_LEN - 1) {
      c->handshake_buf[c->handshake_len++] = (char)byte;
      c->handshake_buf[c->handshake_len] = '\0';
    }
    if (c->handshake_len >= 4 &&
        c->handshake_buf[c->handshake_len - 4] == '\r' &&
        c->handshake_buf[c->handshake_len - 3] == '\n' &&
        c->handshake_buf[c->handshake_len - 2] == '\r' &&
        c->handshake_buf[c->handshake_len - 1] == '\n') {
      char first_line[64];
      size_t nl = 0;
      while (nl < c->handshake_len && c->handshake_buf[nl] != '\r' && nl < sizeof(first_line) - 1)
        first_line[nl] = c->handshake_buf[nl], nl++;
      first_line[nl] = '\0';
      Serial.printf("[WSS] HTTP slot=%d: %s\n", idx, first_line);
      const char* key_hdr = "Sec-WebSocket-Key:";
      char* buf = c->handshake_buf;
      for (size_t i = 0; i + 20 < c->handshake_len; i++) {
        if (strncasecmp(buf + i, key_hdr, 18) == 0) {
          char* key_start = buf + i + 18;
          while (*key_start == ' ' || *key_start == '\t') key_start++;
          char* key_end = key_start;
          while (*key_end && *key_end != '\r' && *key_end != '\n') key_end++;
          size_t key_len = key_end - key_start;
          if (key_len == 0 || key_len > 128) break;

          char concat[128 + sizeof(WS_MAGIC)];
          memcpy(concat, key_start, key_len);
          memcpy(concat + key_len, WS_MAGIC, sizeof(WS_MAGIC) - 1);
          size_t concat_len = key_len + sizeof(WS_MAGIC) - 1;
          uint8_t hash[20];
          mbedtls_sha1((const unsigned char*)concat, concat_len, hash);
          char b64[32];
          base64Encode20(hash, b64);

          const char* resp = "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: ";
          size_t resp_len = strlen(resp);
          size_t written = 0;
          while (written < resp_len) {
            r = mbedtls_ssl_write(ssl, (const unsigned char*)resp + written, resp_len - written);
            if (r <= 0) { mbedtls_ssl_free(ssl); mbedtls_net_free(&c->client_net); c->in_use = false; return false; }
            written += (size_t)r;
          }
          written = 0;
          while (written < 28) {
            r = mbedtls_ssl_write(ssl, (const unsigned char*)b64 + written, 28 - written);
            if (r <= 0) { mbedtls_ssl_free(ssl); mbedtls_net_free(&c->client_net); c->in_use = false; return false; }
            written += (size_t)r;
          }
          r = mbedtls_ssl_write(ssl, (const unsigned char*)"\r\n\r\n", 4);
          if (r != 4) { mbedtls_ssl_free(ssl); mbedtls_net_free(&c->client_net); c->in_use = false; return false; }
          c->handshake_done = true;
          c->ws_state = WS_STATE_HEADER_0;
          c->comp_state = COMP_STATE_IDLE;
          Serial.printf("[WSS] WS upgrade OK slot=%d\n", idx);
          return true;
        }
      }
      // Plain GET (e.g. browser opening https://device:8765): send cert-acceptance page then close.
      Serial.printf("[WSS] plain GET slot=%d, sending cert page then close\n", idx);
      size_t page_len = strlen(WSS_HTTP_CERT_PAGE);
      size_t written = 0;
      while (written < page_len) {
        r = mbedtls_ssl_write(ssl, (const unsigned char*)WSS_HTTP_CERT_PAGE + written, page_len - written);
        if (r <= 0) break;
        written += (size_t)r;
      }
      mbedtls_ssl_free(ssl);
      mbedtls_net_free(&c->client_net);
      c->in_use = false;
      return false;
    }
  } else if (r == 0 || r == MBEDTLS_ERR_SSL_CONN_EOF) {
    mbedtls_ssl_free(ssl);
    mbedtls_net_free(&c->client_net);
    c->in_use = false;
    return false;
  } else if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
    char errbuf[80];
    mbedtls_strerror(r, errbuf, sizeof(errbuf));
    Serial.printf("[WSS] HTTP read err slot=%d r=%d: %s\n", idx, r, errbuf);
    mbedtls_ssl_free(ssl);
    mbedtls_net_free(&c->client_net);
    c->in_use = false;
    return false;
  }
  if (c->handshake_len >= WS_HANDSHAKE_MAX_LEN - 1) {
    Serial.printf("[WSS] handshake too long slot=%d, close\n", idx);
    mbedtls_ssl_free(ssl);
    mbedtls_net_free(&c->client_net);
    c->in_use = false;
    return false;
  }
  return false;
#else
  WiFiClient* cl = &c->client;
  while (cl->available() && c->handshake_len < WS_HANDSHAKE_MAX_LEN - 1) {
    char ch = (char)cl->read();
    c->handshake_buf[c->handshake_len++] = ch;
    c->handshake_buf[c->handshake_len] = '\0';
    if (c->handshake_len >= 4 &&
        c->handshake_buf[c->handshake_len - 4] == '\r' &&
        c->handshake_buf[c->handshake_len - 3] == '\n' &&
        c->handshake_buf[c->handshake_len - 2] == '\r' &&
        c->handshake_buf[c->handshake_len - 1] == '\n') {
      const char* key_hdr = "Sec-WebSocket-Key:";
      char* buf = c->handshake_buf;
      for (size_t i = 0; i + 20 < c->handshake_len; i++) {
        if (strncasecmp(buf + i, key_hdr, 18) == 0) {
          char* key_start = buf + i + 18;
          while (*key_start == ' ' || *key_start == '\t') key_start++;
          char* key_end = key_start;
          while (*key_end && *key_end != '\r' && *key_end != '\n') key_end++;
          size_t key_len = key_end - key_start;
          if (key_len == 0 || key_len > 128) break;

          char concat[128 + sizeof(WS_MAGIC)];
          memcpy(concat, key_start, key_len);
          memcpy(concat + key_len, WS_MAGIC, sizeof(WS_MAGIC) - 1);
          size_t concat_len = key_len + sizeof(WS_MAGIC) - 1;
          uint8_t hash[20];
          mbedtls_sha1((const unsigned char*)concat, concat_len, hash);
          char b64[32];
          base64Encode20(hash, b64);

          const char* resp = "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: ";
          size_t resp_len = strlen(resp);
          if (!writeAllBytes(*cl, (const uint8_t*)resp, resp_len, TCP_WRITE_TIMEOUT_MS))
            return false;
          if (!writeAllBytes(*cl, (const uint8_t*)b64, 28, TCP_WRITE_TIMEOUT_MS))
            return false;
          if (!writeAllBytes(*cl, (const uint8_t*)"\r\n\r\n", 4, TCP_WRITE_TIMEOUT_MS))
            return false;

          c->handshake_done = true;
          c->ws_state = WS_STATE_HEADER_0;
          c->comp_state = COMP_STATE_IDLE;
          return true;
        }
      }
      // Plain GET: send friendly page then close (for wss:// lets user accept cert; for ws:// just confirms endpoint).
      (void)writeAllBytes(*cl, (const uint8_t*)WSS_HTTP_CERT_PAGE, strlen(WSS_HTTP_CERT_PAGE), TCP_WRITE_TIMEOUT_MS);
      c->client.stop();
      c->in_use = false;
      return false;
    }
  }
  if (c->handshake_len >= WS_HANDSHAKE_MAX_LEN - 1) {
    c->client.stop();
    c->in_use = false;
    return false;
  }
  return false;
#endif
}

void WebSocketCompanionServer::tickHandshake() {
  acceptNewClients();
  pruneDisconnected();
}

size_t WebSocketCompanionServer::pollRecvFrame(uint8_t dest[], int* client_index_out) {
  acceptNewClients();
  pruneDisconnected();

  int start = _poll_start_idx;
  if (start < 0 || start >= WS_COMPANION_MAX_CLIENTS) start = 0;

  for (int off = 0; off < WS_COMPANION_MAX_CLIENTS; off++) {
    int idx = (start + off) % WS_COMPANION_MAX_CLIENTS;
#if WS_USE_TLS
    if (!_clients[idx].in_use || _clients[idx].client_net.fd < 0) continue;
#else
    if (!_clients[idx].in_use || !_clients[idx].client.connected()) continue;
#endif
    WSClientState* c = &_clients[idx];

    if (!c->handshake_done) {
      doHandshake(idx);
      continue;
    }

#if WS_USE_TLS
    // TLS: read one byte at a time
    mbedtls_ssl_context* ssl = &c->ssl_ctx;
    unsigned char b;
    int r = mbedtls_ssl_read(ssl, &b, 1);
    if (r == 1) {
      // Feed one byte into the same state machine (single iteration)
      if (c->ws_state == WS_STATE_HEADER_0) {
        c->ws_opcode = b & 0x0F;
        c->ws_state = WS_STATE_HEADER_1;
      } else if (c->ws_state == WS_STATE_HEADER_1) {
        uint8_t len7 = b & 0x7F;
        c->ws_payload_len = len7;
        c->ws_payload_read = 0;
        if (len7 == 126) { c->ws_state = WS_STATE_LEN_EXT; c->handshake_len = 0; }
        else if (len7 == 127) { c->ws_state = WS_STATE_LEN_EXT; c->ws_payload_len = 0; c->handshake_len = 0; }
        else c->ws_state = WS_STATE_MASK;
      } else if (c->ws_state == WS_STATE_LEN_EXT) {
        if (c->ws_payload_len == 126) {
          if (c->handshake_len == 0) { c->ws_payload_len = (uint16_t)b << 8; c->handshake_len = 1; }
          else { c->ws_payload_len |= (uint16_t)b; c->ws_state = WS_STATE_MASK; c->handshake_len = 0; }
        } else {
          // 8-byte length big-endian
          c->ws_payload_len |= (uint64_t)b << ((7 - c->handshake_len) * 8);
          c->handshake_len++;
          if (c->handshake_len >= 8) { c->ws_state = WS_STATE_MASK; c->handshake_len = 0; }
        }
      } else if (c->ws_state == WS_STATE_MASK) {
        c->ws_mask[c->handshake_len & 3] = b;
        c->handshake_len++;
        if ((c->handshake_len & 3) == 0) { c->ws_state = WS_STATE_PAYLOAD; c->handshake_len = 0; }
      } else {
        b = b ^ c->ws_mask[c->ws_payload_read % 4];
        c->ws_payload_read++;
        if (c->ws_opcode == 0x02) {
          switch (c->comp_state) {
            case COMP_STATE_IDLE: if (b == '<') c->comp_state = COMP_STATE_HDR_FOUND; break;
            case COMP_STATE_HDR_FOUND: c->comp_frame_len = (uint16_t)b; c->comp_state = COMP_STATE_LEN1_FOUND; break;
            case COMP_STATE_LEN1_FOUND:
              c->comp_frame_len |= ((uint16_t)b) << 8; c->comp_rx_len = 0;
              c->comp_state = (c->comp_frame_len > 0) ? COMP_STATE_LEN2_FOUND : COMP_STATE_IDLE;
              break;
            default:
              if (c->comp_rx_len < MAX_FRAME_SIZE) c->comp_rx_buf[c->comp_rx_len] = b;
              c->comp_rx_len++;
              if (c->comp_rx_len >= c->comp_frame_len) {
                size_t copy_len = c->comp_frame_len; if (copy_len > MAX_FRAME_SIZE) copy_len = MAX_FRAME_SIZE;
                memcpy(dest, c->comp_rx_buf, copy_len);
                c->comp_state = COMP_STATE_IDLE;
                if (client_index_out) *client_index_out = idx;
                _poll_start_idx = (idx + 1) % WS_COMPANION_MAX_CLIENTS;
                return copy_len;
              }
              break;
          }
        }
        if (c->ws_payload_read >= c->ws_payload_len) c->ws_state = WS_STATE_HEADER_0;
      }
    } else if (r == 0 || r == MBEDTLS_ERR_SSL_CONN_EOF) {
      mbedtls_ssl_free(ssl);
      mbedtls_net_free(&c->client_net);
      c->in_use = false;
    } else if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
      char errbuf[80];
      mbedtls_strerror(r, errbuf, sizeof(errbuf));
      Serial.printf("[WSS] frame read err slot=%d r=%d: %s\n", idx, r, errbuf);
      mbedtls_ssl_free(ssl);
      mbedtls_net_free(&c->client_net);
      c->in_use = false;
    }
    continue;
#else
    WiFiClient* cl = &c->client;
    // WebSocket frame state machine
    while (cl->available()) {
      if (c->ws_state == WS_STATE_HEADER_0) {
        uint8_t b = (uint8_t)cl->read();
        c->ws_opcode = b & 0x0F;
        c->ws_state = WS_STATE_HEADER_1;
        continue;
      }
      if (c->ws_state == WS_STATE_HEADER_1) {
        uint8_t b = (uint8_t)cl->read();
        uint8_t len7 = b & 0x7F;
        c->ws_payload_len = len7;
        c->ws_payload_read = 0;
        if (len7 == 126) {
          c->ws_state = WS_STATE_LEN_EXT;
          continue;
        }
        if (len7 == 127) {
          c->ws_state = WS_STATE_LEN_EXT;
          c->ws_payload_len = 0;  // read 8 bytes
          continue;
        }
        // Mask (client must send masked)
        c->ws_state = WS_STATE_MASK;
        continue;
      }
      if (c->ws_state == WS_STATE_LEN_EXT) {
        if (c->ws_payload_len == 126) {
          if (cl->available() < 2) break;
          uint8_t lo = (uint8_t)cl->read();
          uint8_t hi = (uint8_t)cl->read();
          c->ws_payload_len = (uint16_t)lo | ((uint16_t)hi << 8);
        } else {
          if (cl->available() < 8) break;
          c->ws_payload_len = 0;
          for (int i = 0; i < 8; i++)
            c->ws_payload_len |= (uint64_t)(uint8_t)cl->read() << (i * 8);
        }
        c->ws_state = WS_STATE_MASK;
        continue;
      }
      if (c->ws_state == WS_STATE_MASK) {
        if (cl->available() < 4) break;
        for (int i = 0; i < 4; i++) c->ws_mask[i] = (uint8_t)cl->read();
        c->ws_state = WS_STATE_PAYLOAD;
        continue;
      }

      // WS_STATE_PAYLOAD: read one byte, unmask, feed to companion state machine
      if (c->ws_payload_read >= c->ws_payload_len) {
        c->ws_state = WS_STATE_HEADER_0;
        continue;
      }
      uint8_t b = (uint8_t)cl->read() ^ c->ws_mask[c->ws_payload_read % 4];
      c->ws_payload_read++;

      if (c->ws_opcode != 0x02) continue;  // only binary

      switch (c->comp_state) {
        case COMP_STATE_IDLE:
          if (b == '<') c->comp_state = COMP_STATE_HDR_FOUND;
          break;
        case COMP_STATE_HDR_FOUND:
          c->comp_frame_len = (uint16_t)b;
          c->comp_state = COMP_STATE_LEN1_FOUND;
          break;
        case COMP_STATE_LEN1_FOUND:
          c->comp_frame_len |= ((uint16_t)b) << 8;
          c->comp_rx_len = 0;
          c->comp_state = (c->comp_frame_len > 0) ? COMP_STATE_LEN2_FOUND : COMP_STATE_IDLE;
          break;
        default:
          if (c->comp_rx_len < MAX_FRAME_SIZE) c->comp_rx_buf[c->comp_rx_len] = b;
          c->comp_rx_len++;
          if (c->comp_rx_len >= c->comp_frame_len) {
            size_t copy_len = c->comp_frame_len;
            if (copy_len > MAX_FRAME_SIZE) copy_len = MAX_FRAME_SIZE;
            memcpy(dest, c->comp_rx_buf, copy_len);
            c->comp_state = COMP_STATE_IDLE;
            if (client_index_out) *client_index_out = idx;
            _poll_start_idx = (idx + 1) % WS_COMPANION_MAX_CLIENTS;
            return copy_len;
          }
          break;
      }
    }
#endif
  }  // close for
  _poll_start_idx = (start + 1) % WS_COMPANION_MAX_CLIENTS;
  return 0;
}

size_t WebSocketCompanionServer::writeToClient(int client_index, const uint8_t src[], size_t len) {
  if (client_index < 0 || client_index >= WS_COMPANION_MAX_CLIENTS || len > MAX_FRAME_SIZE) return 0;
#if WS_USE_TLS
  if (!_clients[client_index].in_use || _clients[client_index].client_net.fd < 0) return 0;
  mbedtls_ssl_context* ssl = &_clients[client_index].ssl_ctx;
  uint8_t hdr[4];
  size_t hdr_len;
  hdr[0] = 0x82;
  if (len < 126) {
    hdr[1] = (uint8_t)len;
    hdr_len = 2;
  } else {
    hdr[1] = 126;
    hdr[2] = (len >> 8) & 0xFF;
    hdr[3] = len & 0xFF;
    hdr_len = 4;
  }
  size_t sent = 0;
  uint32_t start_ms = millis();
  while (sent < hdr_len) {
    int r = mbedtls_ssl_write(ssl, hdr + sent, hdr_len - sent);
    if (r <= 0) return 0;
    sent += (size_t)r;
    if (millis() - start_ms >= TCP_WRITE_TIMEOUT_MS) return 0;
    delay(1);
  }
  sent = 0;
  start_ms = millis();
  while (sent < len) {
    int r = mbedtls_ssl_write(ssl, src + sent, len - sent);
    if (r <= 0) return 0;
    sent += (size_t)r;
    if (millis() - start_ms >= TCP_WRITE_TIMEOUT_MS) return 0;
    delay(1);
  }
#if WS_FRAME_DEBUG
  Serial.printf("WS frame client=%d code=%u len=%u written=%u\n", client_index, (unsigned)(len ? src[0] : 0), (unsigned)len, (unsigned)len);
#endif
  return len;
#else
  if (!_clients[client_index].in_use || !_clients[client_index].client.connected()) return 0;

  WiFiClient* cl = &_clients[client_index].client;
  uint8_t hdr[4];
  size_t hdr_len;
  hdr[0] = 0x82;  // binary, FIN
  if (len < 126) {
    hdr[1] = (uint8_t)len;
    hdr_len = 2;
  } else {
    // RFC 6455: extended payload length is 2-byte big-endian (network order)
    hdr[1] = 126;
    hdr[2] = (len >> 8) & 0xFF;
    hdr[3] = len & 0xFF;
    hdr_len = 4;
  }
  if (!writeAllBytes(*cl, hdr, hdr_len, TCP_WRITE_TIMEOUT_MS) ||
      !writeAllBytes(*cl, src, len, TCP_WRITE_TIMEOUT_MS)) {
#if WS_FRAME_DEBUG
    Serial.printf("WS frame client=%d code=%u len=%u written=0\n", client_index, (unsigned)(len ? src[0] : 0), (unsigned)len);
#endif
    // Return 0 so caller can retry (e.g. contact list iterator). Do not disconnect on
    // transient buffer full; companion layer retries and will complete the sequence.
    return 0;
  }
#if WS_FRAME_DEBUG
  Serial.printf("WS frame client=%d code=%u len=%u written=%u\n", client_index, (unsigned)(len ? src[0] : 0), (unsigned)len, (unsigned)len);
#endif
  return len;
#endif
}

size_t WebSocketCompanionServer::writeToAllClients(const uint8_t src[], size_t len) {
  if (len == 0 || len > MAX_FRAME_SIZE) return 0;
  int connected = 0;
  int sent = 0;
  for (int i = 0; i < WS_COMPANION_MAX_CLIENTS; i++) {
#if WS_USE_TLS
    if (_clients[i].in_use && _clients[i].client_net.fd >= 0 && _clients[i].handshake_done) {
#else
    if (_clients[i].in_use && _clients[i].client.connected() && _clients[i].handshake_done) {
#endif
      connected++;
      if (writeToClient(i, src, len) == len) sent++;
    }
  }
  return (sent == connected) ? len : 0;
}

bool WebSocketCompanionServer::isClientConnected(int client_index) const {
  if (client_index < 0 || client_index >= WS_COMPANION_MAX_CLIENTS) return false;
  const WSClientState* c = &_clients[client_index];
#if WS_USE_TLS
  return c->in_use && c->client_net.fd >= 0 && c->handshake_done;
#else
  return c->in_use && c->client.connected() && c->handshake_done;
#endif
}

int WebSocketCompanionServer::connectedCount() const {
  int n = 0;
  for (int i = 0; i < WS_COMPANION_MAX_CLIENTS; i++) {
#if WS_USE_TLS
    if (_clients[i].in_use && _clients[i].client_net.fd >= 0 && _clients[i].handshake_done)
#else
    if (_clients[i].in_use && _clients[i].client.connected() && _clients[i].handshake_done)
#endif
      n++;
  }
  return n;
}

void WebSocketCompanionServer::disconnectClient(int client_index) {
  if (client_index >= 0 && client_index < WS_COMPANION_MAX_CLIENTS && _clients[client_index].in_use) {
#if WS_USE_TLS
    mbedtls_ssl_free(&_clients[client_index].ssl_ctx);
    mbedtls_net_free(&_clients[client_index].client_net);
#else
    _clients[client_index].client.stop();
#endif
    _clients[client_index].in_use = false;
  }
}
