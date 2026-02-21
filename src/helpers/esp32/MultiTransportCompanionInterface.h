#pragma once

#include "../BaseSerialInterface.h"
#include "../ArduinoSerialInterface.h"
#include "TCPCompanionServer.h"

#ifndef TCP_COMPANION_DEFAULT_PORT
#define TCP_COMPANION_DEFAULT_PORT  5000
#endif

// Reply target: -1 = USB, 0..N = TCP client index
#define REPLY_TARGET_USB  (-1)

// Implements BaseSerialInterface for simultaneous USB + TCP companion connections.
// One shared protocol handler; responses go to originating client, optionally broadcast to all.
class MultiTransportCompanionInterface : public BaseSerialInterface {
public:
  MultiTransportCompanionInterface();

  // USB uses Serial (or other Stream). TCP server port is stored; call startTcpServer() after WiFi.begin().
  void begin(Stream& usb_serial, uint16_t tcp_port = TCP_COMPANION_DEFAULT_PORT);
  void startTcpServer();  // call once after WiFi.begin() (idempotent)
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

private:
  int _clientIdSlot() const { return _last_reply_target + 1; }
  static const size_t _max_client_id_len = 32;

  ArduinoSerialInterface _usb;
  TCPCompanionServer _tcp;
  uint16_t _tcp_port;
  bool _tcp_started;
  bool _isEnabled;
  bool _broadcast;           // if true, also send responses to all other clients
  int _last_reply_target;    // REPLY_TARGET_USB or TCP client index
  char _client_ids[1 + TCP_COMPANION_MAX_CLIENTS][_max_client_id_len];
};
