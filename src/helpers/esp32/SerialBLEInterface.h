#pragma once

#include "../BaseSerialInterface.h"
#include <NimBLEDevice.h>

// NimBLE port of the companion BLE (Nordic UART) transport. Public API is kept
// identical to the previous Bluedroid implementation so MultiTransportCompanionInterface
// and the plain *_ble envs need no changes. NimBLE's host is ~tens of KB lighter on the
// heap than Bluedroid, which is what lets Wi-Fi + BLE coexist on this ESP32-S3.
class SerialBLEInterface : public BaseSerialInterface, public NimBLEServerCallbacks, public NimBLECharacteristicCallbacks {
  NimBLEServer *pServer;
  NimBLEService *pService;
  NimBLECharacteristic *pTxCharacteristic;
  bool deviceConnected;
  bool oldDeviceConnected;
  bool _isEnabled;
  uint16_t last_conn_id;
  uint32_t _pin_code;
  unsigned long _last_write;
  unsigned long adv_restart_time;
  uint8_t _peer_bda[6];
  bool _peer_bda_valid;

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];
  };

  // Larger queue reduces dropped push/tickle frames during short BLE congestion bursts.
  #define FRAME_QUEUE_SIZE  12
  int recv_queue_len;
  Frame recv_queue[FRAME_QUEUE_SIZE];
  int send_queue_len;
  Frame send_queue[FRAME_QUEUE_SIZE];

  void clearBuffers() { recv_queue_len = 0; send_queue_len = 0; }
  void startAdvertising();

protected:
  // NimBLEServerCallbacks — connection lifecycle + security
  void onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) override;
  void onDisconnect(NimBLEServer* pServer) override;
  void onMTUChange(uint16_t MTU, ble_gap_conn_desc* desc) override;
  uint32_t onPassKeyRequest() override;
  bool onConfirmPIN(uint32_t pass_key) override;
  void onAuthenticationComplete(ble_gap_conn_desc* desc) override;

  // NimBLECharacteristicCallbacks
  void onWrite(NimBLECharacteristic* pCharacteristic) override;

public:
  SerialBLEInterface() {
    pServer = NULL;
    pService = NULL;
    pTxCharacteristic = NULL;
    deviceConnected = false;
    oldDeviceConnected = false;
    adv_restart_time = 0;
    _isEnabled = false;
    _last_write = 0;
    last_conn_id = 0;
    send_queue_len = recv_queue_len = 0;
    _peer_bda_valid = false;
  }

  /**
   * init the BLE interface.
   * @param prefix   a prefix for the device name
   * @param name  IN/OUT - a name for the device (combined with prefix). If "@@MAC", is modified and returned
   * @param pin_code   the BLE security pin
   */
  void begin(const char* prefix, char* name, uint32_t pin_code);

  // BaseSerialInterface methods
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }

  bool isConnected() const override;

  /** If a peer is connected, format their BLE address into buf as "XX:XX:XX:XX:XX:XX" and return true; else buf[0]='\0' and return false. */
  bool getConnectedPeerAddress(char* buf, size_t len) const;

  bool isWriteBusy() const override;
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;

  /** Drain one frame from the send queue if interval allows. Call every loop so BLE gets updates even when USB/TCP are polled first. */
  void drainSendQueue();
};

#if BLE_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define BLE_DEBUG_PRINT(F, ...) Serial.printf("BLE: " F, ##__VA_ARGS__)
  #define BLE_DEBUG_PRINTLN(F, ...) Serial.printf("BLE: " F "\n", ##__VA_ARGS__)
#else
  #define BLE_DEBUG_PRINT(...) {}
  #define BLE_DEBUG_PRINTLN(...) {}
#endif
