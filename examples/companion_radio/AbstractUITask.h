#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/BaseSerialInterface.h>
#include <Arduino.h>

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif

#include "NodePrefs.h"

enum class UIEventType {
    none,
    contactMessage,
    channelMessage,
    roomMessage,
    newContactMessage,
    ack
};

class AbstractUITask {
protected:
  mesh::MainBoard* _board;
  BaseSerialInterface* _serial;
  bool _connected;

  AbstractUITask(mesh::MainBoard* board, BaseSerialInterface* serial) : _board(board), _serial(serial) {
    _connected = false;
  }

public:
  void setHasConnection(bool connected) { _connected = connected; }
  bool hasConnection() const { return _connected; }
  uint16_t getBattMilliVolts() const { return _board->getBattMilliVolts(); }
  bool isSerialEnabled() const { return _serial->isEnabled(); }
  void enableSerial() { _serial->enable(); }
  void disableSerial() { _serial->disable(); }
  bool isTcpEnabled() const { return _serial->isTcpEnabled(); }
  void enableTcp() { _serial->enableTcp(); }
  void disableTcp() { _serial->disableTcp(); }
  bool isWsStarted() const { return _serial->isWsStarted(); }
  uint16_t getWsPort() const { return _serial->getWsPort(); }
  int getWsConnectedCount() const { return _serial->getWsConnectedCount(); }
  void enableWss() { _serial->enableWss(); }
  void disableWss() { _serial->disableWss(); }
  bool isWssEnabled() const { return _serial->isWssEnabled(); }
  bool hasBleCapability() const { return _serial->hasBleCapability(); }
  bool isBleEnabled() const { return _serial->isBleEnabled(); }
  void enableBle() { _serial->enableBle(); }
  void disableBle() { _serial->disableBle(); }
  bool getBlePeerAddress(char* buf, size_t len) const { return _serial->getBlePeerAddress(buf, len); }
  virtual void msgRead(int msgcount) = 0;
  virtual void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) = 0;
  virtual void notify(UIEventType t = UIEventType::none) = 0;
  virtual void loop() = 0;
};
