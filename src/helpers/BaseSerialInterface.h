#pragma once

#include <Arduino.h>

#define MAX_FRAME_SIZE  176   // +4 for transport codes (region scoping)

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
  /** Broadcast like writeFrameToAll(), but report WHICH client slots received the frame.
   *  Bit N corresponds to slot N as used by getClientIdForSlot(). Returns 0 if nothing was written.
   *  Lets callers advance per-client sync cursors only for clients that actually got the frame,
   *  instead of gating every cursor on all clients succeeding. */
  virtual uint32_t writeFrameToAllMask(const uint8_t src[], size_t len) {
    return writeFrameToAll(src, len) == len ? 0x1u : 0u;
  }
  /** Client id for a slot reported by writeFrameToAllMask(). Default: the current client. */
  virtual void getClientIdForSlot(int slot, char* dest, size_t max_len) const {
    (void)slot; getCurrentClientId(dest, max_len);
  }
  /** Number of slots writeFrameToAllMask() can report. */
  virtual int getClientSlotCount() const { return 1; }
  virtual size_t checkRecvFrame(uint8_t dest[]) = 0;

  // TCP only (multi-transport): enable/disable TCP server; no-op for single transport.
  virtual void enableTcp() { }
  virtual void disableTcp() { }
  virtual bool isTcpEnabled() const { return true; }

  // WebSocket only (multi-transport): report if WebSocket server is running, port, and client count; no-op for single transport.
  virtual bool isWsStarted() const { return false; }
  virtual uint16_t getWsPort() const { return 0; }
  virtual int getWsConnectedCount() const { return 0; }
  /** Multi-transport: poll WebSocket accept/handshake; no-op for single-transport builds. */
  virtual void tickWebSocketHandshake() { }

  // BLE only (multi-transport with BLE): enable/disable BLE; no-op when BLE not available.
  virtual void enableBle() { }
  virtual void disableBle() { }
  virtual bool isBleEnabled() const { return false; }
  virtual bool hasBleCapability() const { return false; }
  /** If BLE connected, write peer address as "XX:XX:XX:XX:XX:XX" into buf and return true; else buf[0]='\0' and return false. */
  virtual bool getBlePeerAddress(char* buf, size_t len) const { if (buf && len > 0) buf[0] = '\0'; return false; }

  // OTA prep/restore hooks for transports that can reclaim memory before HTTPS starts.
  virtual void prepareForHttpOta() { }
  virtual void restoreAfterHttpOta() { }

  /** True if the current control path is Wi-Fi-based (TCP/WS companion or SerialWifi client), for HTTP OTA gating. */
  virtual bool isHttpOtaWifiControlSession() const { return false; }

  // Per-client history: identity of the connection that sent the last frame (set before handleCmdFrame).
  virtual void setCurrentClientId(const char* id) { (void)id; }
  virtual void getCurrentClientId(char* dest, size_t max_len) const {
    if (dest && max_len > 0) dest[0] = '\0';
  }

  // Multi-transport: reply target for current response (USB/BLE/TCP/WS index). Used so contact-list
  // CONTACT/END go to the same client that got START even if checkRecvFrame overwrites target.
  virtual int getReplyTarget() const { return -1; }
  virtual void setReplyTarget(int target) { (void)target; }

  /** If false, writeFrameToAll may only reach the reply-target client; callers must not advance all sync cursors after a push. */
  virtual bool companionUnsolicitedPushesBroadcastToAll() const { return true; }
};
