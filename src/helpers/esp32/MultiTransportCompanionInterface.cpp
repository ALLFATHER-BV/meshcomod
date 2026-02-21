#include "MultiTransportCompanionInterface.h"

MultiTransportCompanionInterface::MultiTransportCompanionInterface()
  : _tcp_port(0), _tcp_started(false), _isEnabled(false), _broadcast(false), _last_reply_target(REPLY_TARGET_USB) {
}

void MultiTransportCompanionInterface::begin(Stream& usb_serial, uint16_t tcp_port) {
  _usb.begin(usb_serial);
  _tcp_port = tcp_port;
  _last_reply_target = REPLY_TARGET_USB;
}

void MultiTransportCompanionInterface::startTcpServer() {
  if (!_tcp_started && _tcp_port != 0) {
    _tcp.begin(_tcp_port);
    _tcp_started = true;
  }
}

void MultiTransportCompanionInterface::enable() {
  _isEnabled = true;
  _usb.enable();
  _last_reply_target = REPLY_TARGET_USB;
}

void MultiTransportCompanionInterface::disable() {
  _isEnabled = false;
  _usb.disable();
}

bool MultiTransportCompanionInterface::isConnected() const {
  if (_usb.isConnected()) return true;
  if (!_tcp_started) return false;
  return _tcp.connectedCount() > 0;
}

bool MultiTransportCompanionInterface::isWriteBusy() const {
  return _usb.isWriteBusy();
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

  return 0;
}

size_t MultiTransportCompanionInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) return 0;

  size_t written = 0;

  if (_last_reply_target == REPLY_TARGET_USB) {
    written = _usb.writeFrame(src, len);
    if (_tcp_started && _broadcast && written == len)
      _tcp.writeToAllClients(src, len);
  } else if (_tcp_started) {
    written = _tcp.writeToClient(_last_reply_target, src, len);
    if (_broadcast && written == len) {
      _usb.writeFrame(src, len);
      for (int i = 0; i < TCP_COMPANION_MAX_CLIENTS; i++) {
        if (i != _last_reply_target && _tcp.isClientConnected(i))
          _tcp.writeToClient(i, src, len);
      }
    }
  }

  return written;
}
