#pragma once

#include "../BaseSerialInterface.h"
#include "../ArduinoSerialInterface.h"
#include "TCPCompanionServer.h"
#include "WebSocketCompanionServer.h"
#ifdef BLE_PIN_CODE
#include "SerialBLEInterface.h"
#endif

#ifndef TCP_COMPANION_DEFAULT_PORT
#define TCP_COMPANION_DEFAULT_PORT  5000
#endif

// Reply target: -1 = USB, -2 = BLE (when BLE_PIN_CODE), 0..N = TCP client index, 100+ = WebSocket client index
#define REPLY_TARGET_USB   (-1)
#define REPLY_TARGET_BLE   (-2)
#define REPLY_TARGET_WS_0  100

// Implements BaseSerialInterface for simultaneous USB + TCP companion connections.
// One shared protocol handler; responses go to originating client, optionally broadcast to all.
class MultiTransportCompanionInterface : public BaseSerialInterface {
public:
  MultiTransportCompanionInterface();

  // USB uses Serial (or other Stream). TCP and optional WebSocket ports stored; call startTcpServer() after WiFi.begin().
  void begin(Stream& usb_serial, uint16_t tcp_port = TCP_COMPANION_DEFAULT_PORT, uint16_t ws_port = 0);
  // When using WSS (WS_USE_TLS=1), pass wifi_connected so WSS server starts only after WiFi is up (avoids boot crash from mbedTLS init before heap is ready).
  void startTcpServer(bool wifi_connected = true);  // idempotent; no-op if TCP disabled
  void stopTcpServer();   // stop TCP server and disconnect clients; prevents startTcpServer until enableTcp()

#ifdef BLE_PIN_CODE
  // Call after begin() and the_mesh is ready (e.g. after startInterface). Enables BLE by default.
  void beginBle(const char* prefix, char* name, uint32_t pin_code);
  void enableBle() override;
  void disableBle() override;
  bool isBleEnabled() const override { return _ble_enabled; }
  bool hasBleCapability() const override { return true; }
  bool getBlePeerAddress(char* buf, size_t len) const override;
#endif

  void enableTcp() override;
  void disableTcp() override;
  bool isTcpEnabled() const override { return _tcp_enabled; }
  bool isWsStarted() const override { return _ws_started; }
  uint16_t getWsPort() const override { return _ws_port; }
  int getWsConnectedCount() const override { return _ws.connectedCount(); }

  void setBroadcastResponses(bool enable) { _broadcast = enable; }

  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }
  bool isConnected() const override;
  bool isWriteBusy() const override;
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t writeFrameToAll(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;

  void setCurrentClientId(const char* id) override;
  void getCurrentClientId(char* dest, size_t max_len) const override;

  int getReplyTarget() const override { return _last_reply_target; }
  void setReplyTarget(int target) override { _last_reply_target = target; }

private:
  int _clientIdSlot() const;
  static const size_t _max_client_id_len = 32;

  ArduinoSerialInterface _usb;
  TCPCompanionServer _tcp;
  WebSocketCompanionServer _ws;
  uint16_t _tcp_port;
  uint16_t _ws_port;
  bool _tcp_started;
  bool _ws_started;
  bool _tcp_enabled;   // if false, startTcpServer() no-ops until enableTcp()
  bool _isEnabled;
  bool _broadcast;           // if true, also send responses to all other clients
  int _last_reply_target;    // REPLY_TARGET_USB, REPLY_TARGET_BLE, TCP index, or REPLY_TARGET_WS_0 + ws index
#ifdef BLE_PIN_CODE
  SerialBLEInterface _ble;
  bool _ble_begun;    // beginBle() was called
  bool _ble_enabled;  // user has BLE on (toggle via UI)
#endif
  char _client_ids[2 + TCP_COMPANION_MAX_CLIENTS + WS_COMPANION_MAX_CLIENTS][_max_client_id_len];  // usb, [ble], tcp0.., ws0..
};
