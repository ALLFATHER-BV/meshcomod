#include "TCPCompanionServer.h"

#define RECV_STATE_IDLE        0
#define RECV_STATE_HDR_FOUND   1
#define RECV_STATE_LEN1_FOUND  2
#define RECV_STATE_LEN2_FOUND  3
#define TCP_WRITE_TIMEOUT_MS   120

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

TCPCompanionServer::TCPCompanionServer() : _server(WiFiServer()), _port(0), _poll_start_idx(0) {
  for (int i = 0; i < TCP_COMPANION_MAX_CLIENTS; i++) {
    _clients[i].state = RECV_STATE_IDLE;
    _clients[i].in_use = false;
  }
}

void TCPCompanionServer::begin(uint16_t port) {
  _port = port;
  _server.begin(port);
}

void TCPCompanionServer::stop() {
  _server.stop();
  for (int i = 0; i < TCP_COMPANION_MAX_CLIENTS; i++) {
    if (_clients[i].in_use) {
      _clients[i].client.stop();
      _clients[i].in_use = false;
    }
  }
}

void TCPCompanionServer::acceptNewClients() {
  while (_server.hasClient()) {
    WiFiClient incoming = _server.accept();
    if (!incoming) continue;
    int slot = -1;
    for (int i = 0; i < TCP_COMPANION_MAX_CLIENTS; i++) {
      if (!_clients[i].in_use) {
        slot = i;
        break;
      }
    }
    if (slot >= 0) {
      _clients[slot].client = incoming;
      _clients[slot].in_use = true;
      _clients[slot].state = RECV_STATE_IDLE;
      _clients[slot].rx_len = 0;
    } else {
      incoming.stop();
    }
  }
}

void TCPCompanionServer::pruneDisconnected() {
  for (int i = 0; i < TCP_COMPANION_MAX_CLIENTS; i++) {
    if (_clients[i].in_use && !_clients[i].client.connected()) {
      _clients[i].client.stop();
      _clients[i].in_use = false;
      _clients[i].state = RECV_STATE_IDLE;
    }
  }
}

void TCPCompanionServer::disconnectClient(int client_index) {
  if (client_index >= 0 && client_index < TCP_COMPANION_MAX_CLIENTS && _clients[client_index].in_use) {
    _clients[client_index].client.stop();
    _clients[client_index].in_use = false;
    _clients[client_index].state = RECV_STATE_IDLE;
  }
}

bool TCPCompanionServer::isClientConnected(int client_index) const {
  if (client_index < 0 || client_index >= TCP_COMPANION_MAX_CLIENTS) return false;
  return _clients[client_index].in_use && _clients[client_index].client.connected();
}

int TCPCompanionServer::connectedCount() const {
  int n = 0;
  for (int i = 0; i < TCP_COMPANION_MAX_CLIENTS; i++) {
    if (_clients[i].in_use && _clients[i].client.connected()) n++;
  }
  return n;
}

size_t TCPCompanionServer::pollRecvFrame(uint8_t dest[], int* client_index_out) {
  acceptNewClients();
  pruneDisconnected();

  // Fair polling across active TCP clients: rotate the first slot we inspect
  // so one busy client cannot starve other connected clients.
  int start = _poll_start_idx;
  if (start < 0 || start >= TCP_COMPANION_MAX_CLIENTS) start = 0;

  for (int off = 0; off < TCP_COMPANION_MAX_CLIENTS; off++) {
    int idx = (start + off) % TCP_COMPANION_MAX_CLIENTS;
    if (!_clients[idx].in_use || !_clients[idx].client.connected()) continue;
    TCPClientState* c = &_clients[idx];
    WiFiClient* cl = &c->client;

    while (cl->available()) {
      int b = cl->read();
      if (b < 0) break;

      switch (c->state) {
        case RECV_STATE_IDLE:
          if (b == '<') c->state = RECV_STATE_HDR_FOUND;
          break;
        case RECV_STATE_HDR_FOUND:
          c->frame_len = (uint8_t)b;
          c->state = RECV_STATE_LEN1_FOUND;
          break;
        case RECV_STATE_LEN1_FOUND:
          c->frame_len |= ((uint16_t)b) << 8;
          c->rx_len = 0;
          c->state = (c->frame_len > 0) ? RECV_STATE_LEN2_FOUND : RECV_STATE_IDLE;
          break;
        default:
          if (c->rx_len < MAX_FRAME_SIZE) c->rx_buf[c->rx_len] = (uint8_t)b;
          c->rx_len++;
          if (c->rx_len >= c->frame_len) {
            size_t copy_len = c->frame_len;
            if (copy_len > MAX_FRAME_SIZE) copy_len = MAX_FRAME_SIZE;
            memcpy(dest, c->rx_buf, copy_len);
            c->state = RECV_STATE_IDLE;
            if (client_index_out) *client_index_out = idx;
            _poll_start_idx = (idx + 1) % TCP_COMPANION_MAX_CLIENTS;
            return copy_len;
          }
          break;
      }
    }
  }
  _poll_start_idx = (start + 1) % TCP_COMPANION_MAX_CLIENTS;
  return 0;
}

size_t TCPCompanionServer::writeToClient(int client_index, const uint8_t src[], size_t len) {
  if (client_index < 0 || client_index >= TCP_COMPANION_MAX_CLIENTS || len > MAX_FRAME_SIZE) return 0;
  if (!_clients[client_index].in_use || !_clients[client_index].client.connected()) return 0;

  uint8_t hdr[3];
  hdr[0] = '>';
  hdr[1] = (len & 0xFF);
  hdr[2] = (len >> 8);
  WiFiClient* cl = &_clients[client_index].client;
  if (!writeAllBytes(*cl, hdr, 3, TCP_WRITE_TIMEOUT_MS) ||
      !writeAllBytes(*cl, src, len, TCP_WRITE_TIMEOUT_MS)) {
    // Return 0 so caller can retry (e.g. contact list). Do not disconnect on transient
    // buffer full; companion layer retries and will complete the sequence.
    return 0;
  }
  return len;
}

size_t TCPCompanionServer::writeToAllClients(const uint8_t src[], size_t len) {
  if (len == 0 || len > MAX_FRAME_SIZE) return 0;
  int connected = 0;
  int sent = 0;
  for (int i = 0; i < TCP_COMPANION_MAX_CLIENTS; i++) {
    if (_clients[i].in_use && _clients[i].client.connected()) {
      connected++;
      if (writeToClient(i, src, len) == len) sent++;
    }
  }
  return (sent == connected) ? len : 0;
}
