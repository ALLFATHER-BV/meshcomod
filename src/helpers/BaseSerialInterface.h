#pragma once

#include <Arduino.h>

#define MAX_FRAME_SIZE  172

class BaseSerialInterface {
protected:
  BaseSerialInterface() { }

public:
  virtual void enable() = 0;
  virtual void disable() = 0;
  virtual bool isEnabled() const = 0;

  virtual bool isConnected() const = 0;

  virtual bool isWriteBusy() const = 0;
  virtual size_t writeFrame(const uint8_t src[], size_t len) = 0;
  // Unsolicited push to all connections (multi-transport: USB + all TCP). Default: same as writeFrame.
  virtual size_t writeFrameToAll(const uint8_t src[], size_t len) { return writeFrame(src, len); }
  virtual size_t checkRecvFrame(uint8_t dest[]) = 0;

  // TCP only (multi-transport): enable/disable TCP server; no-op for single transport.
  virtual void enableTcp() { }
  virtual void disableTcp() { }
  virtual bool isTcpEnabled() const { return true; }

  // BLE only (multi-transport with BLE): enable/disable BLE; no-op when BLE not available.
  virtual void enableBle() { }
  virtual void disableBle() { }
  virtual bool isBleEnabled() const { return false; }
  virtual bool hasBleCapability() const { return false; }
  /** If BLE connected, write peer address as "XX:XX:XX:XX:XX:XX" into buf and return true; else buf[0]='\0' and return false. */
  virtual bool getBlePeerAddress(char* buf, size_t len) const { if (buf && len > 0) buf[0] = '\0'; return false; }

  // Per-client history: identity of the connection that sent the last frame (set before handleCmdFrame).
  virtual void setCurrentClientId(const char* id) { (void)id; }
  virtual void getCurrentClientId(char* dest, size_t max_len) const {
    if (dest && max_len > 0) dest[0] = '\0';
  }
};
