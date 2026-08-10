#pragma once

#include "BaseSerialInterface.h"

// Follows MAX_CLIENT_ID_LEN where the companion has already defined it.
// Two independent limits let one side be raised while the other truncates a client id.
// That splits the client's history in a way that presents as message loss.
#ifndef MULTI_SERIAL_CLIENT_ID_LEN
  #ifdef MAX_CLIENT_ID_LEN
    #define MULTI_SERIAL_CLIENT_ID_LEN MAX_CLIENT_ID_LEN
  #else
    #define MULTI_SERIAL_CLIENT_ID_LEN 31
  #endif
#endif

#ifndef MAX_INTERFACES
  // ble, usb, wifi, ethernet
  #define MAX_INTERFACES 4
#endif

enum class InterfaceType : uint8_t {
  NONE,
  Bluetooth,
  USB,
  WiFi,
  Ethernet,
  HardwareSerial
};

class MultiSerialInterface : public BaseSerialInterface {
private:

  struct RegisteredInterface {
    InterfaceType type = InterfaceType::NONE;
    BaseSerialInterface* instance = nullptr;
  };

  bool _enabled = false;
  RegisteredInterface _interfaces[MAX_INTERFACES] = {};

  // Index of the interface that produced the last received frame, which is where a reply belongs.
  int _reply_idx = -1;
  char _client_ids[MAX_INTERFACES][MULTI_SERIAL_CLIENT_ID_LEN + 1] = {};

  bool validIdx(int i) const {
    return i >= 0 && i < MAX_INTERFACES && _interfaces[i].instance != nullptr;
  }

  // Gives an unconfigured client a stable per-transport identity.
  // Without one every transport shares an empty id, and with it a shared history cursor.
  static const char* defaultClientId(InterfaceType type) {
    switch (type) {
      case InterfaceType::USB:            return "usb";
      case InterfaceType::Bluetooth:      return "ble";
      case InterfaceType::WiFi:           return "wifi";
      case InterfaceType::Ethernet:       return "eth";
      case InterfaceType::HardwareSerial: return "hwser";
      default:                            return "";
    }
  }

public:
  bool addInterface(InterfaceType type, BaseSerialInterface* iface) {
    // make sure an interface was provided
    if(iface == nullptr){
      return false;
    }

    // put it in the first free slot
    for(int i = 0; i < MAX_INTERFACES; i++){
      if(_interfaces[i].instance == nullptr){
        _interfaces[i].instance = iface;
        _interfaces[i].type = type;
        return true;
      }
    }

    // no free slots available
    return false;
  }

  bool removeInterface(BaseSerialInterface* iface) {
    // make sure an interface was provided
    if(iface == nullptr){
      return false;
    }

    // find and remove interface
    for(int i = 0; i < MAX_INTERFACES; i++){
      if(_interfaces[i].instance == iface){
        _interfaces[i] = {};
        // The next addInterface() reuses this slot, so a stale id or pin would be inherited.
        _client_ids[i][0] = 0;
        if(_reply_idx == i) _reply_idx = -1;
        return true;
      }
    }

    // interface not found
    return false;
  }

  void enableBluetooth() {
    for(auto iface : _interfaces){
      if(iface.instance && iface.type == InterfaceType::Bluetooth){
        iface.instance->enable();
      }
    }
  }

  void disableBluetooth() {
    for(auto iface : _interfaces){
      if(iface.instance && iface.type == InterfaceType::Bluetooth){
        iface.instance->disable();
      }
    }
  }

  bool isBluetoothEnabled() {
    for(auto iface : _interfaces){
      if(iface.instance && iface.type == InterfaceType::Bluetooth){
        return iface.instance->isEnabled();
      }
    }
    return false; 
  }

  // enable all interfaces
  void enable() override {
    _enabled = true;
    for(auto iface : _interfaces){
      if(iface.instance){
        iface.instance->enable();
      }
    }
  }

  // disable all interfaces
  void disable() override {
    _enabled = false;
    for(auto iface : _interfaces){
      if(iface.instance){
        iface.instance->disable();
      }
    }
  }

  bool isEnabled() const override { 
    return _enabled; 
  }

  bool isConnected() const override {
    // not connected when disabled
    if(!_enabled){
      return false;
    }
    
    // check if any interface is connected
    for(auto iface : _interfaces){
      if(iface.instance && iface.instance->isConnected()) {
        return true;
      }
    }

    // nothing connected
    return false;
  }

  // loop all interfaces
  void loop() override {
    for(auto iface : _interfaces){
      if(iface.instance){
        iface.instance->loop();
      }
    }
  }

  bool isWriteBusy() const override {
    // not busy when disabled
    if(!_enabled){
      return false;
    }

    // check if any interface is busy
    for(auto iface : _interfaces){
      if(iface.instance && iface.instance->isEnabled() && iface.instance->isWriteBusy()){
        return true;
      }
    }

    // nothing busy
    return false;
  }

  // Replies go to the client that asked, which is what BaseSerialInterface documents this for.
  // Fanning out instead injects one client's response into another's stream.
  size_t writeFrame(const uint8_t src[], size_t len) override {
    if(!_enabled || len == 0){
      return 0;
    }

    if(_reply_idx >= 0){
      // A pin to a slot that no longer holds an interface is still a target, just a dead one.
      // Broadcasting here would put a targeted reply into the other clients' streams.
      if(!validIdx(_reply_idx)) return 0;

      BaseSerialInterface* target = _interfaces[_reply_idx].instance;
      if(target->isEnabled() && target->isConnected()){
        return target->writeFrame(src, len);
      }
      // The target went away, so the frame was not delivered.
      // Callers commit history on a full-length return, so reporting success would drop it.
      return 0;
    }

    // Nothing has been received yet, so there is no reply target and a push reaches everyone.
    return writeFrameToAll(src, len);
  }

  size_t writeFrameToAll(const uint8_t src[], size_t len) override {
    // don't write when disabled or nothing provided
    if(!_enabled || len == 0){
      return 0;
    }

    // Success is judged only on interfaces holding a client.
    // ArduinoSerialInterface::writeFrame() returns 0 until a companion client has spoken, so counting an idle USB would fail every write.
    // Callers treat 0 as failure and retry, which would re-send the same frame to the transports that did succeed.
    // With nothing connected this reports success for a frame that went nowhere, matching MultiTransportCompanionInterface::writeFrameToAll().
    bool allSuccessful = true;
    for(auto iface : _interfaces){
      if(iface.instance && iface.instance->isEnabled() && iface.instance->isConnected()){
        if(iface.instance->writeFrame(src, len) != len){
          allSuccessful = false;
        }
      }
    }

    // report success if all writes completed successfully
    return allSuccessful ? len : 0;
  }


  size_t checkRecvFrame(uint8_t dest[]) override {
    // don't read when disabled
    if(!_enabled){
      return 0;
    }

    // try to read a frame from any enabled interface
    for(int i = 0; i < MAX_INTERFACES; i++){
      auto& iface = _interfaces[i];
      if(iface.instance && iface.instance->isEnabled()){
        size_t frameSize = iface.instance->checkRecvFrame(dest);
        if(frameSize > 0){
          _reply_idx = i;
          return frameSize;
        }
      }
    }

    // no frame received
    return 0;
  }

  // MyMesh pins the target across a contact list sync so CONTACT and END reach whoever got START.
  int getReplyTarget() const override { return _reply_idx; }
  void setReplyTarget(int target) override {
    if(target < 0 || target >= MAX_INTERFACES){
      _reply_idx = -1;
    } else {
      _reply_idx = target;
    }
  }

  void setCurrentClientId(const char* id) override {
    if(!validIdx(_reply_idx)) return;
    char* slot = _client_ids[_reply_idx];
    if(id == nullptr){
      slot[0] = 0;
      return;
    }
    size_t i = 0;
    while(id[i] && i < MULTI_SERIAL_CLIENT_ID_LEN){ slot[i] = id[i]; i++; }
    slot[i] = 0;
  }

  void getCurrentClientId(char* dest, size_t max_len) const override {
    if(dest == nullptr || max_len == 0) return;
    dest[0] = 0;
    if(!validIdx(_reply_idx)) return;

    // An id the app supplied wins, otherwise the transport's own name keeps histories apart.
    const char* src = _client_ids[_reply_idx][0] ? _client_ids[_reply_idx]
                                                 : defaultClientId(_interfaces[_reply_idx].type);
    size_t i = 0;
    while(src[i] && i + 1 < max_len){ dest[i] = src[i]; i++; }
    dest[i] = 0;
  }

};
