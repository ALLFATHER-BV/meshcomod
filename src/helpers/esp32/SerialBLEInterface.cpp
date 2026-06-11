#include "SerialBLEInterface.h"

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define ADVERT_RESTART_DELAY  1000   // millis
#define BLE_WRITE_MIN_INTERVAL   60
#define PUSH_CODE_LOG_RX_DATA    0x88

void SerialBLEInterface::begin(const char* prefix, char* name, uint32_t pin_code) {
  _pin_code = pin_code;

  if (strcmp(name, "@@MAC") == 0) {
    uint8_t addr[8];
    memset(addr, 0, sizeof(addr));
    esp_efuse_mac_get_default(addr);
    sprintf(name, "%02X%02X%02X%02X%02X%02X",    // modify (IN-OUT param)
          addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
  }
  char dev_name[32+16];
  sprintf(dev_name, "%s%s", prefix, name);

  // Create the BLE Device (NimBLE host). init()/createServer() are internally
  // idempotent and OTA's NimBLEDevice::deinit(true) resets them, so a plain
  // re-begin() after an OTA teardown re-creates everything cleanly.
  NimBLEDevice::init(dev_name);
  NimBLEDevice::setMTU(MAX_FRAME_SIZE);

  // Security: static passkey, Secure Connections + MITM + bonding. The device
  // "displays" the fixed PIN (IO cap DISPLAY_ONLY) and the phone enters it.
  NimBLEDevice::setSecurityAuth(true /*bond*/, true /*mitm*/, true /*sc*/);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityPasskey(pin_code);

  //NimBLEDevice::setPower(ESP_PWR_LVL_N8);

  // Create the BLE Server
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(this);
  pServer->advertiseOnDisconnect(false);   // we manage advertising restart ourselves

  // Create the BLE Service
  pService = pServer->createService(SERVICE_UUID);

  // TX: read+notify, encrypted + authenticated (MITM). NimBLE auto-adds the CCCD (0x2902).
  pTxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID_TX,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY |
      NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN);

  // RX: write, encrypted + authenticated (MITM)
  NimBLECharacteristic* pRxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID_RX,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
  pRxCharacteristic->setCallbacks(this);

  pService->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
}

void SerialBLEInterface::startAdvertising() {
  NimBLEDevice::getAdvertising()->start();
  adv_restart_time = 0;
}

// -------- NimBLEServerCallbacks: security

uint32_t SerialBLEInterface::onPassKeyRequest() {
  BLE_DEBUG_PRINTLN("onPassKeyRequest()");
  return _pin_code;
}

bool SerialBLEInterface::onConfirmPIN(uint32_t pass_key) {
  BLE_DEBUG_PRINTLN("onConfirmPIN(%u)", pass_key);
  return true;
}

void SerialBLEInterface::onAuthenticationComplete(ble_gap_conn_desc* desc) {
  if (desc && desc->sec_state.encrypted) {
    BLE_DEBUG_PRINTLN(" - SecurityCallback - Authentication Success");
    deviceConnected = true;
  } else {
    BLE_DEBUG_PRINTLN(" - SecurityCallback - Authentication Failure*");
    if (pServer) pServer->disconnect(desc ? desc->conn_handle : last_conn_id);
    adv_restart_time = millis() + ADVERT_RESTART_DELAY;
  }
}

// -------- NimBLEServerCallbacks: connection lifecycle

void SerialBLEInterface::onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) {
  BLE_DEBUG_PRINTLN("onConnect(), conn_id=%d", desc ? desc->conn_handle : -1);
  if (desc) {
    last_conn_id = desc->conn_handle;
    memcpy(_peer_bda, desc->peer_ota_addr.val, 6);   // NimBLE addr is little-endian
    _peer_bda_valid = true;
  }
}

void SerialBLEInterface::onMTUChange(uint16_t MTU, ble_gap_conn_desc* desc) {
  BLE_DEBUG_PRINTLN("onMTUChange(), mtu=%d", MTU);
}

void SerialBLEInterface::onDisconnect(NimBLEServer* pServer) {
  BLE_DEBUG_PRINTLN("onDisconnect()");
  _peer_bda_valid = false;
  if (_isEnabled) {
    adv_restart_time = millis() + ADVERT_RESTART_DELAY;
    // loop() will detect this on next loop, and set deviceConnected to false
  }
}

// -------- NimBLECharacteristicCallbacks

void SerialBLEInterface::onWrite(NimBLECharacteristic* pCharacteristic) {
  NimBLEAttValue val = pCharacteristic->getValue();
  int len = val.length();
  const uint8_t* rxValue = val.data();

  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("ERROR: onWrite(), frame too big, len=%d", len);
  } else if (recv_queue_len >= FRAME_QUEUE_SIZE) {
    BLE_DEBUG_PRINTLN("ERROR: onWrite(), recv_queue is full!");
  } else {
    recv_queue[recv_queue_len].len = len;
    memcpy(recv_queue[recv_queue_len].buf, rxValue, len);
    recv_queue_len++;
  }
}

// ---------- public methods

void SerialBLEInterface::enable() {
  if (_isEnabled) return;

  _isEnabled = true;
  clearBuffers();

  // Service is already started in begin(); just (re)start advertising.
  startAdvertising();
}

void SerialBLEInterface::disable() {
  _isEnabled = false;

  BLE_DEBUG_PRINTLN("SerialBLEInterface::disable");

  if (pServer) {
    NimBLEDevice::getAdvertising()->stop();
    if (pServer->getConnectedCount() > 0) pServer->disconnect(last_conn_id);
  }
  oldDeviceConnected = deviceConnected = false;
  adv_restart_time = 0;
}

size_t SerialBLEInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d", len);
    return 0;
  }

  if (deviceConnected && len > 0) {
    auto tryNotifyNow = [this](uint8_t* data, size_t sz) -> bool {
      if (millis() < _last_write + BLE_WRITE_MIN_INTERVAL) return false;
      _last_write = millis();
      pTxCharacteristic->setValue(data, sz);
      pTxCharacteristic->notify();
      BLE_DEBUG_PRINTLN("writeBytes: sz=%d, hdr=%d", (uint32_t)sz, (uint32_t)data[0]);
      return true;
    };

    // Fast path: when idle and interval elapsed, send immediately.
    if (send_queue_len == 0 && tryNotifyNow(const_cast<uint8_t*>(src), len)) return len;

    auto isLowPriority = [](const uint8_t* data, size_t sz) -> bool {
      return data && sz > 0 && data[0] == PUSH_CODE_LOG_RX_DATA;
    };

    // If queue is full, try to flush one pending frame first.
    if (send_queue_len >= FRAME_QUEUE_SIZE && send_queue_len > 0) {
      if (tryNotifyNow(send_queue[0].buf, send_queue[0].len)) {
        send_queue_len--;
        for (int i = 0; i < send_queue_len; i++) {  // pop front
          send_queue[i] = send_queue[i + 1];
        }
      }
    }

    // Under bursty traffic (especially many RX log pushes), protect
    // message/tickle delivery by preferring newer/important frames.
    if (send_queue_len >= FRAME_QUEUE_SIZE && send_queue_len > 0) {
      if (isLowPriority(src, len)) {
        // Drop new low-priority frame when saturated.
        BLE_DEBUG_PRINTLN("writeFrame(), dropping low-priority frame: queue full");
        return 0;
      }

      // Incoming frame is important. First try to evict an older low-priority frame.
      int evict = -1;
      for (int i = 0; i < send_queue_len; i++) {
        if (isLowPriority(send_queue[i].buf, send_queue[i].len)) {
          evict = i;
          break;
        }
      }
      if (evict >= 0) {
        for (int i = evict; i < send_queue_len - 1; i++) {
          send_queue[i] = send_queue[i + 1];
        }
        send_queue_len--;
      } else {
        // No low-priority frame to evict; drop oldest queued frame so newest
        // important frame can still be delivered.
        for (int i = 0; i < send_queue_len - 1; i++) {
          send_queue[i] = send_queue[i + 1];
        }
        send_queue_len--;
      }
    }

    if (send_queue_len >= FRAME_QUEUE_SIZE) {
      BLE_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      return 0;
    }

    send_queue[send_queue_len].len = len;  // add to send queue
    memcpy(send_queue[send_queue_len].buf, src, len);
    send_queue_len++;

    return len;
  }
  return 0;
}

bool SerialBLEInterface::isWriteBusy() const {
  return millis() < _last_write + BLE_WRITE_MIN_INTERVAL;   // still too soon to start another write?
}

void SerialBLEInterface::drainSendQueue() {
  if (send_queue_len > 0 && millis() >= _last_write + BLE_WRITE_MIN_INTERVAL) {
    _last_write = millis();
    pTxCharacteristic->setValue(send_queue[0].buf, send_queue[0].len);
    pTxCharacteristic->notify();

    BLE_DEBUG_PRINTLN("writeBytes: sz=%d, hdr=%d", (uint32_t)send_queue[0].len, (uint32_t) send_queue[0].buf[0]);

    send_queue_len--;
    for (int i = 0; i < send_queue_len; i++) {
      send_queue[i] = send_queue[i + 1];
    }
  }
}

size_t SerialBLEInterface::checkRecvFrame(uint8_t dest[]) {
  drainSendQueue();

  if (recv_queue_len > 0) {   // check recv queue
    size_t len = recv_queue[0].len;   // take from top of queue
    memcpy(dest, recv_queue[0].buf, len);

    BLE_DEBUG_PRINTLN("readBytes: sz=%d, hdr=%d", len, (uint32_t) dest[0]);

    recv_queue_len--;
    for (int i = 0; i < recv_queue_len; i++) {   // delete top item from queue
      recv_queue[i] = recv_queue[i + 1];
    }
    return len;
  }

  if (!pServer || pServer->getConnectedCount() == 0)  deviceConnected = false;

  if (deviceConnected != oldDeviceConnected) {
    if (!deviceConnected) {    // disconnecting
      clearBuffers();

      BLE_DEBUG_PRINTLN("SerialBLEInterface -> disconnecting...");

      adv_restart_time = millis() + ADVERT_RESTART_DELAY;
    } else {
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> stopping advertising");
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> connecting...");
      // connecting
      NimBLEDevice::getAdvertising()->stop();
      adv_restart_time = 0;
    }
    oldDeviceConnected = deviceConnected;
  }

  if (adv_restart_time && millis() >= adv_restart_time) {
    if (pServer && pServer->getConnectedCount() == 0) {
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> re-starting advertising");
      startAdvertising();  // re-Start advertising
    }
    adv_restart_time = 0;
  }
  return 0;
}

bool SerialBLEInterface::isConnected() const {
  return deviceConnected;
}

bool SerialBLEInterface::getConnectedPeerAddress(char* buf, size_t len) const {
  if (!buf || len == 0) return false;
  buf[0] = '\0';
  if (!_peer_bda_valid) return false;
  if (len < 18) return false;  // "XX:XX:XX:XX:XX:XX" + null
  // NimBLE stores the address little-endian; print MSB-first to match the usual display.
  snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X",
           _peer_bda[5], _peer_bda[4], _peer_bda[3], _peer_bda[2], _peer_bda[1], _peer_bda[0]);
  return true;
}
