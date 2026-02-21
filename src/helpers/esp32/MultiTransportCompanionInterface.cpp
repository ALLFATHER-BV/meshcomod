#include "MultiTransportCompanionInterface.h"
#include <string.h>

MultiTransportCompanionInterface::MultiTransportCompanionInterface()
  : _tcp_port(0), _tcp_started(false), _tcp_enabled(true), _isEnabled(false), _broadcast(false), _last_reply_target(REPLY_TARGET_USB)
#ifdef BLE_PIN_CODE
  , _ble_begun(false), _ble_enabled(false)
#endif
{
  for (size_t i = 0; i < sizeof(_client_ids) / sizeof(_client_ids[0]); i++)
    _client_ids[i][0] = '\0';
}

void MultiTransportCompanionInterface::begin(Stream& usb_serial, uint16_t tcp_port) {
  _usb.begin(usb_serial);
  _tcp_port = tcp_port;
  _last_reply_target = REPLY_TARGET_USB;
}

void MultiTransportCompanionInterface::startTcpServer() {
  if (_tcp_enabled && !_tcp_started && _tcp_port != 0) {
    _tcp.begin(_tcp_port);
    _tcp_started = true;
  }
}

void MultiTransportCompanionInterface::stopTcpServer() {
  if (_tcp_started) {
    _tcp.stop();
    _tcp_started = false;
  }
  _tcp_enabled = false;
}

void MultiTransportCompanionInterface::enableTcp() {
  _tcp_enabled = true;
}

void MultiTransportCompanionInterface::disableTcp() {
  stopTcpServer();
}

#ifdef BLE_PIN_CODE
void MultiTransportCompanionInterface::beginBle(const char* prefix, char* name, uint32_t pin_code) {
  _ble.begin(prefix, name, pin_code);
  _ble_begun = true;
  _ble_enabled = true;
  _ble.enable();
}

void MultiTransportCompanionInterface::enableBle() {
  if (!_ble_begun) return;
  _ble_enabled = true;
  _ble.enable();
}

void MultiTransportCompanionInterface::disableBle() {
  _ble_enabled = false;
  _ble.disable();
}
#endif

void MultiTransportCompanionInterface::enable() {
  _isEnabled = true;
  _usb.enable();
  _last_reply_target = REPLY_TARGET_USB;
#ifdef BLE_PIN_CODE
  if (_ble_begun && _ble_enabled)
    _ble.enable();
#endif
}

void MultiTransportCompanionInterface::disable() {
  _isEnabled = false;
  _usb.disable();
#ifdef BLE_PIN_CODE
  _ble.disable();
#endif
}

bool MultiTransportCompanionInterface::isConnected() const {
  if (_usb.isConnected()) return true;
#ifdef BLE_PIN_CODE
  if (_ble_begun && _ble_enabled && _ble.isConnected()) return true;
#endif
  if (!_tcp_started) return false;
  return _tcp.connectedCount() > 0;
}

bool MultiTransportCompanionInterface::isWriteBusy() const {
  if (_usb.isWriteBusy()) return true;
#ifdef BLE_PIN_CODE
  if (_ble_begun && _ble_enabled && _ble.isWriteBusy()) return true;
#endif
  return false;
}

size_t MultiTransportCompanionInterface::checkRecvFrame(uint8_t dest[]) {
  if (!_isEnabled) return 0;

  // Poll USB first (preserve Home Assistant / USB priority)
  size_t len = _usb.checkRecvFrame(dest);
  if (len > 0) {
    _last_reply_target = REPLY_TARGET_USB;
    return len;
  }

  // Then poll TCP clients (only after TCP server was started)
  if (_tcp_started) {
    int tcp_client = -1;
    len = _tcp.pollRecvFrame(dest, &tcp_client);
    if (len > 0) {
      _last_reply_target = tcp_client;
      return len;
    }
  }

#ifdef BLE_PIN_CODE
  if (_ble_begun && _ble_enabled) {
    len = _ble.checkRecvFrame(dest);
    if (len > 0) {
      _last_reply_target = REPLY_TARGET_BLE;
      return len;
    }
  }
#endif

  return 0;
}

size_t MultiTransportCompanionInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) return 0;
  // Single-target only (command responses, sync history). Never broadcast.
  if (_last_reply_target == REPLY_TARGET_USB)
    return _usb.writeFrame(src, len);
#ifdef BLE_PIN_CODE
  if (_last_reply_target == REPLY_TARGET_BLE && _ble_begun && _ble_enabled)
    return _ble.writeFrame(src, len);
#endif
  if (_tcp_started)
    return _tcp.writeToClient(_last_reply_target, src, len);
  return 0;
}

size_t MultiTransportCompanionInterface::writeFrameToAll(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) return 0;
  if (!_broadcast)
    return writeFrame(src, len);
  size_t written = _usb.writeFrame(src, len);
#ifdef BLE_PIN_CODE
  if (_ble_begun && _ble_enabled && written == len)
    _ble.writeFrame(src, len);
#endif
  if (_tcp_started && written == len)
    _tcp.writeToAllClients(src, len);
  return written;
}

int MultiTransportCompanionInterface::_clientIdSlot() const {
#ifdef BLE_PIN_CODE
  if (_last_reply_target == REPLY_TARGET_USB) return 0;
  if (_last_reply_target == REPLY_TARGET_BLE) return 1;
  return _last_reply_target + 2;  // TCP 0..N -> slots 2..
#else
  return _last_reply_target + 1;
#endif
}

void MultiTransportCompanionInterface::setCurrentClientId(const char* id) {
  int slot = _clientIdSlot();
  if (slot >= 0 && slot < (int)(sizeof(_client_ids) / sizeof(_client_ids[0]))) {
    if (id) {
      strncpy(_client_ids[slot], id, _max_client_id_len - 1);
      _client_ids[slot][_max_client_id_len - 1] = '\0';
    } else {
      _client_ids[slot][0] = '\0';
    }
  }
}

void MultiTransportCompanionInterface::getCurrentClientId(char* dest, size_t max_len) const {
  if (!dest || max_len == 0) return;
  dest[0] = '\0';
  int slot = _clientIdSlot();
  if (slot < 0 || slot >= (int)(sizeof(_client_ids) / sizeof(_client_ids[0]))) return;
  // If app sent client_id in CMD_APP_START, use it; otherwise use connection-based id
  // so non-custom clients (HA, MeshCore app) still get per-connection history without sending anything.
  if (_client_ids[slot][0] != '\0') {
    strncpy(dest, _client_ids[slot], max_len - 1);
    dest[max_len - 1] = '\0';
    return;
  }
#ifdef BLE_PIN_CODE
  static const char* const default_ids[] = { "usb", "ble", "tcp0", "tcp1", "tcp2" };
#else
  static const char* const default_ids[] = { "usb", "tcp0", "tcp1", "tcp2" };
#endif
  size_t n = sizeof(default_ids) / sizeof(default_ids[0]);
  if ((size_t)slot < n) {
    strncpy(dest, default_ids[slot], max_len - 1);
    dest[max_len - 1] = '\0';
  }
}
