#include "WebSocketCompanionServer.h"
#include <mbedtls/sha1.h>
#include <string.h>

// Optional: build with -DWS_FRAME_DEBUG=1 to log each WS send (client, code 2=START/3=CONTACT/4=END, len, written).
// Use WiFi-only when enabled (no Web Serial on same device) so Serial is free for debug.
#ifndef WS_FRAME_DEBUG
#define WS_FRAME_DEBUG 0
#endif

#define TCP_WRITE_TIMEOUT_MS   120
#define WS_MAGIC               "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

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

WebSocketCompanionServer::WebSocketCompanionServer() : _server(WiFiServer()), _port(0), _poll_start_idx(0) {
  for (int i = 0; i < WS_COMPANION_MAX_CLIENTS; i++) {
    _clients[i].in_use = false;
    _clients[i].handshake_done = false;
    _clients[i].handshake_len = 0;
    _clients[i].ws_state = WS_STATE_HEADER_0;
    _clients[i].comp_state = COMP_STATE_IDLE;
  }
}

void WebSocketCompanionServer::begin(uint16_t port) {
  _port = port;
  _server.begin(port);
}

void WebSocketCompanionServer::stop() {
  _server.stop();
  for (int i = 0; i < WS_COMPANION_MAX_CLIENTS; i++) {
    if (_clients[i].in_use) {
      _clients[i].client.stop();
      _clients[i].in_use = false;
    }
  }
}

void WebSocketCompanionServer::acceptNewClients() {
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
}

void WebSocketCompanionServer::pruneDisconnected() {
  for (int i = 0; i < WS_COMPANION_MAX_CLIENTS; i++) {
    if (_clients[i].in_use && !_clients[i].client.connected()) {
      _clients[i].client.stop();
      _clients[i].in_use = false;
    }
  }
}

bool WebSocketCompanionServer::doHandshake(int idx) {
  WSClientState* c = &_clients[idx];
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
      // End of headers; find Sec-WebSocket-Key
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

          // key + magic -> SHA-1 -> base64
          char concat[128 + sizeof(WS_MAGIC)];
          memcpy(concat, key_start, key_len);
          memcpy(concat + key_len, WS_MAGIC, sizeof(WS_MAGIC) - 1);
          size_t concat_len = key_len + sizeof(WS_MAGIC) - 1;

          uint8_t hash[20];
          mbedtls_sha1((const unsigned char*)concat, concat_len, hash);

          char b64[32];
          base64Encode20(hash, b64);

          const char* resp =
            "HTTP/1.1 101 Switching Protocols\r\n"
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
      // Key not found; close
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
  return false;  // still reading
}

size_t WebSocketCompanionServer::pollRecvFrame(uint8_t dest[], int* client_index_out) {
  acceptNewClients();
  pruneDisconnected();

  int start = _poll_start_idx;
  if (start < 0 || start >= WS_COMPANION_MAX_CLIENTS) start = 0;

  for (int off = 0; off < WS_COMPANION_MAX_CLIENTS; off++) {
    int idx = (start + off) % WS_COMPANION_MAX_CLIENTS;
    if (!_clients[idx].in_use || !_clients[idx].client.connected()) continue;
    WSClientState* c = &_clients[idx];
    WiFiClient* cl = &c->client;

    // Still in handshake
    if (!c->handshake_done) {
      doHandshake(idx);
      continue;
    }

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
  }
  _poll_start_idx = (start + 1) % WS_COMPANION_MAX_CLIENTS;
  return 0;
}

size_t WebSocketCompanionServer::writeToClient(int client_index, const uint8_t src[], size_t len) {
  if (client_index < 0 || client_index >= WS_COMPANION_MAX_CLIENTS || len > MAX_FRAME_SIZE) return 0;
  if (!_clients[client_index].in_use || !_clients[client_index].client.connected()) return 0;

  WiFiClient* cl = &_clients[client_index].client;
  uint8_t hdr[4];
  size_t hdr_len;
  hdr[0] = 0x82;  // binary, FIN
  if (len < 126) {
    hdr[1] = (uint8_t)len;
    hdr_len = 2;
  } else {
    hdr[1] = 126;
    hdr[2] = len & 0xFF;
    hdr[3] = (len >> 8) & 0xFF;
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
}

size_t WebSocketCompanionServer::writeToAllClients(const uint8_t src[], size_t len) {
  if (len == 0 || len > MAX_FRAME_SIZE) return 0;
  int connected = 0;
  int sent = 0;
  for (int i = 0; i < WS_COMPANION_MAX_CLIENTS; i++) {
    if (_clients[i].in_use && _clients[i].client.connected() && _clients[i].handshake_done) {
      connected++;
      if (writeToClient(i, src, len) == len) sent++;
    }
  }
  return (sent == connected) ? len : 0;
}

bool WebSocketCompanionServer::isClientConnected(int client_index) const {
  if (client_index < 0 || client_index >= WS_COMPANION_MAX_CLIENTS) return false;
  const WSClientState* c = &_clients[client_index];
  return c->in_use && c->client.connected() && c->handshake_done;
}

int WebSocketCompanionServer::connectedCount() const {
  int n = 0;
  for (int i = 0; i < WS_COMPANION_MAX_CLIENTS; i++) {
    if (_clients[i].in_use && _clients[i].client.connected() && _clients[i].handshake_done)
      n++;
  }
  return n;
}

void WebSocketCompanionServer::disconnectClient(int client_index) {
  if (client_index >= 0 && client_index < WS_COMPANION_MAX_CLIENTS && _clients[client_index].in_use) {
    _clients[client_index].client.stop();
    _clients[client_index].in_use = false;
  }
}
