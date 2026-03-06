#pragma once

#include "../BaseSerialInterface.h"
#include <WiFi.h>

#ifndef WS_COMPANION_MAX_CLIENTS
#define WS_COMPANION_MAX_CLIENTS  2
#endif

#ifndef WS_HANDSHAKE_MAX_LEN
#define WS_HANDSHAKE_MAX_LEN  512
#endif

// Per-client: either HTTP handshake or WebSocket mode with companion state machine
struct WSClientState {
  mutable WiFiClient client;  // mutable so isClientConnected() const can call connected()
  bool in_use;

  // Handshake phase (handshake_done set after sending 101)
  bool handshake_done;
  uint16_t handshake_len;
  char handshake_buf[WS_HANDSHAKE_MAX_LEN];

  // WebSocket frame read state
  uint8_t ws_state;       // 0=header_0, 1=header_1, 2=len_ext, 3=mask, 4=payload
  uint8_t ws_opcode;
  uint64_t ws_payload_len;
  uint32_t ws_payload_read;
  uint8_t ws_mask[4];

  // Companion frame state (same as TCP: '<' + len_lo + len_hi + payload)
  uint8_t comp_state;     // 0=idle, 1=got '<', 2=got len_lo, 3=reading payload
  uint16_t comp_frame_len;
  uint16_t comp_rx_len;
  uint8_t comp_rx_buf[MAX_FRAME_SIZE];
};

// WebSocket server for companion protocol. Same logical protocol as TCP; transport is RFC 6455.
// One WebSocket connection = one companion serial session; binary frames only.
class WebSocketCompanionServer {
public:
  WebSocketCompanionServer();

  void begin(uint16_t port);
  void stop();

  size_t pollRecvFrame(uint8_t dest[], int* client_index_out);
  size_t writeToClient(int client_index, const uint8_t src[], size_t len);
  size_t writeToAllClients(const uint8_t src[], size_t len);

  bool isClientConnected(int client_index) const;
  int connectedCount() const;
  void disconnectClient(int client_index);

private:
  WiFiServer _server;
  mutable WSClientState _clients[WS_COMPANION_MAX_CLIENTS];
  uint16_t _port;
  int _poll_start_idx;

  void acceptNewClients();
  void pruneDisconnected();
  bool doHandshake(int idx);
};
