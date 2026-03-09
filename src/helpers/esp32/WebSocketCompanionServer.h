#pragma once

#include "../BaseSerialInterface.h"
#include <WiFi.h>

#ifndef WS_USE_TLS
#define WS_USE_TLS 0
#endif

#if WS_USE_TLS
#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/x509.h>
#include <mbedtls/pk.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#endif

#ifndef WS_COMPANION_MAX_CLIENTS
#define WS_COMPANION_MAX_CLIENTS  2
#endif

#ifndef WS_HANDSHAKE_MAX_LEN
#define WS_HANDSHAKE_MAX_LEN  1536
#endif

// Per-client: either HTTP handshake or WebSocket mode with companion state machine
struct WSClientState {
#if WS_USE_TLS
  mbedtls_net_context client_net;
  mbedtls_ssl_context ssl_ctx;
  bool in_use;
  bool ssl_handshake_done;  // TLS handshake complete; then doHandshake reads HTTP
  uint32_t handshake_start_ms;  // when in_use && !ssl_handshake_done: accept time for timeout
#else
  mutable WiFiClient client;  // mutable so isClientConnected() const can call connected()
  bool in_use;
#endif

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
  /** Advance TLS/WS handshakes only (no frame read). Call once per loop to reduce ERR_CONNECTION_CLOSED when recv_timeout is NULL. */
  void tickHandshake();

  size_t writeToClient(int client_index, const uint8_t src[], size_t len);
  size_t writeToAllClients(const uint8_t src[], size_t len);

  bool isClientConnected(int client_index) const;
  int connectedCount() const;
  void disconnectClient(int client_index);

private:
#if WS_USE_TLS
  mbedtls_net_context _listen_fd;
  mbedtls_ssl_config _ssl_conf;
  mbedtls_x509_crt _srvcert;
  mbedtls_pk_context _pkey;
  mbedtls_entropy_context _entropy;
  mbedtls_ctr_drbg_context _ctr_drbg;
  bool _tls_initialized;
#else
  WiFiServer _server;
#endif
  mutable WSClientState _clients[WS_COMPANION_MAX_CLIENTS];
  uint16_t _port;
  int _poll_start_idx;

  void acceptNewClients();
  void pruneDisconnected();
  bool doHandshake(int idx);
};
