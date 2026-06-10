#pragma once

#include "BaseSerialInterface.h"
#include <Arduino.h>

class ArduinoSerialInterface : public BaseSerialInterface {
  bool _isEnabled;
  uint8_t _state;
  uint16_t _frame_len;
  uint16_t rx_len;
  Stream* _serial;
  uint8_t rx_buf[MAX_FRAME_SIZE];
  bool _clientSeen;   // a companion client has sent >=1 frame on this serial. Until then we
                      // emit nothing, so a passive (USB-CDC) debug console isn't filled with
                      // binary protocol push-frames when no app is actually connected.

public:
  ArduinoSerialInterface() { _isEnabled = false; _state = 0; _clientSeen = false; }

  void begin(Stream& serial) { 
    _serial = &serial; 
  #ifdef RAK_4631
    pinMode(WB_IO2, OUTPUT);
  #endif  
  }

  // BaseSerialInterface methods
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }

  bool isConnected() const override;

  bool isWriteBusy() const override;
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;
};