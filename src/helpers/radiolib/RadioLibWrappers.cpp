
#define RADIOLIB_STATIC_ONLY 1
#include "RadioLibWrappers.h"

#define STATE_IDLE       0
#define STATE_RX         1
#define STATE_TX_WAIT    3
#define STATE_TX_DONE    4
#define STATE_INT_READY 16

#define NUM_NOISE_FLOOR_SAMPLES  64
#define SAMPLING_THRESHOLD  14

static volatile uint8_t state = STATE_IDLE;

// DIO1 RX-done events, counted in the ISR while in RX state (TX-done shares the
// same interrupt but arrives in STATE_TX_WAIT, so it is excluded). Comparing this
// against getPacketsRecv()+getPacketsRecvErrors() exposes packets that completed
// in the radio but were never read out before the next one landed.
static volatile uint32_t rx_evt_count = 0;

#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ---- Buffered receive: drain task + packet ring + radio mutex ----------------
// The mutex is recursive (wrapper entry points nest: resetAGC -> isReceivingPacket)
// and is only created on first rxQueueEnable(true) — until then every helper is a
// no-op and the wrapper behaves (and times) exactly like stock.
#define RXQ_SLOTS 4
struct RxQSlot { uint8_t buf[256]; uint16_t len; float snr; float rssi; };
static RxQSlot rxq[RXQ_SLOTS];
static volatile uint8_t rxq_w = 0, rxq_r = 0;        // w==r empty; (w+1)%N==r full
static volatile bool rxq_enabled = false;
static volatile bool rxq_suspend = false;            // spectrum owns the raw radio
static volatile uint32_t rxq_full_drops = 0;
static SemaphoreHandle_t rxq_sem = nullptr;
static SemaphoreHandle_t radio_mtx = nullptr;
static TaskHandle_t rxq_task_h = nullptr;
static RadioLibWrapper* rxq_owner = nullptr;

static inline void rlwLock()   { if (radio_mtx) xSemaphoreTakeRecursive(radio_mtx, portMAX_DELAY); }
static inline void rlwUnlock() { if (radio_mtx) xSemaphoreGiveRecursive(radio_mtx); }

void rlwRxqTask(void*) {
  for (;;) {
    // Semaphore wake on RX-done; the timeout is a lost-wake safety net only.
    xSemaphoreTake(rxq_sem, pdMS_TO_TICKS(250));
    if (!rxq_enabled || rxq_suspend || !rxq_owner) continue;
    rxq_owner->pollRxIfNoIrq();   // polled boards: the 250ms tick IS the interrupt
    rlwLock();
    if (rxq_enabled && !rxq_suspend &&
        (state & STATE_INT_READY) && (state & ~STATE_INT_READY) == STATE_RX) {
      rxq_owner->rxqDrainOne();
    }
    rlwUnlock();
  }
}
#else
static inline void rlwLock() {}
static inline void rlwUnlock() {}
#endif

// this function is called when a complete packet
// is transmitted by the module
static
#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void setFlag(void) {
  // fires for BOTH RxDone and TxDone; only RX state counts as a heard packet
  if ((state & ~STATE_INT_READY) == STATE_RX) {
    rx_evt_count++;
#if defined(ESP32)
    if (rxq_sem && rxq_enabled && !rxq_suspend) {
      BaseType_t hpw = pdFALSE;
      xSemaphoreGiveFromISR(rxq_sem, &hpw);
      if (hpw) portYIELD_FROM_ISR();
    }
#endif
  }
  state |= STATE_INT_READY;
}

void RadioLibWrapper::begin() {
  _radio->setPacketReceivedAction(setFlag);  // this is also SentComplete interrupt
  _preamble_sf = getSpreadingFactor();
  _radio->setPreambleLength(preambleLengthForSF(_preamble_sf)); // longer preamble for lower SF improves reliability
  state = STATE_IDLE;

  if (_board->getStartupReason() == BD_STARTUP_RX_PACKET) {  // received a LoRa packet (while in deep sleep)
    setFlag(); // LoRa packet is already received
  }

  _noise_floor = 0;
  _threshold = 0;
  _cad_enabled = false;

  // start average out some samples
  _num_floor_samples = 0;
  _floor_sample_sum = 0;
}

uint32_t RadioLibWrapper::getRngSeed() {
  rlwLock();
  uint32_t r = _radio->random(0x7FFFFFFF);
  rlwUnlock();
  return r;
}

void RadioLibWrapper::setTxPower(int8_t dbm) {
  rlwLock();
  _radio->setOutputPower(dbm);
  rlwUnlock();
}

void RadioLibWrapper::idle() {
  rlwLock();
  _radio->standby();
  state = STATE_IDLE;   // need another startReceive()
  rlwUnlock();
}

void RadioLibWrapper::triggerNoiseFloorCalibrate(int threshold) {
  _threshold = threshold;
  if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES) {  // ignore trigger if currently sampling
    _num_floor_samples = 0;
    _floor_sample_sum = 0;
  }
}

void RadioLibWrapper::doResetAGC() {
  _radio->sleep();  // warm sleep to reset analog frontend
}

void RadioLibWrapper::resetAGC() {
  rlwLock();
  // make sure we're not mid-receive of packet!
  if ((state & STATE_INT_READY) != 0 || isReceivingPacket()) { rlwUnlock(); return; }

  doResetAGC();
  state = STATE_IDLE;   // trigger a startReceive()
  rlwUnlock();

  // Reset noise floor sampling so it reconverges from scratch.
  // Without this, a stuck _noise_floor of -120 makes the sampling threshold
  // too low (-106) to accept normal samples (~-105), self-reinforcing the
  // stuck value even after the receiver has recovered.
  _noise_floor = 0;
  _num_floor_samples = 0;
  _floor_sample_sum = 0;
}

void RadioLibWrapper::loop() {
  if (state == STATE_RX && _num_floor_samples < NUM_NOISE_FLOOR_SAMPLES) {
    rlwLock();
    if (!isReceivingPacket()) {
      int rssi = getCurrentRSSI();
      if (rssi < _noise_floor + SAMPLING_THRESHOLD) {  // only consider samples below current floor + sampling THRESHOLD
        _num_floor_samples++;
        _floor_sample_sum += rssi;
      }
    }
    rlwUnlock();
  } else if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES && _floor_sample_sum != 0) {
    _noise_floor = _floor_sample_sum / NUM_NOISE_FLOOR_SAMPLES;
    if (_noise_floor < -120) {
      _noise_floor = -120;    // clamp to lower bound of -120dBi
    }
    _floor_sample_sum = 0;

    MESH_DEBUG_PRINTLN("RadioLibWrapper: noise_floor = %d", (int)_noise_floor);
  }
}

void RadioLibWrapper::startRecv() {
  #if defined(USE_LR2021)
  _radio->standby(); // without this LR2021 can throw -706 when calling startReceive after hardware CAD when side detectors are enabled
  #endif
  int err = _radio->startReceive();
  if (err == RADIOLIB_ERR_NONE) {
    state = STATE_RX;
  } else {
    MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startReceive(%d)", err);
  }
}

bool RadioLibWrapper::isInRecvMode() const {
  return (state & ~STATE_INT_READY) == STATE_RX;
}

#if defined(ESP32)
// Lift one completed packet out of the radio into the ring and re-arm RX.
// Caller (the drain task) holds the radio mutex; state is RX + INT_READY.
void RadioLibWrapper::rxqDrainOne() {
  int len = _radio->getPacketLength();
  if (len > 0) {
    uint8_t next_w = (uint8_t)((rxq_w + 1) % RXQ_SLOTS);
    if (next_w == rxq_r) {
      // Ring full (a very long consumer stall): the radio buffer must still be
      // freed or the next packet overwrites it anyway — read and count the loss.
      uint8_t scratch[256];
      if (len > (int)sizeof(scratch)) len = (int)sizeof(scratch);
      _radio->readData(scratch, len);
      rxq_full_drops++;
    } else {
      RxQSlot* s = &rxq[rxq_w];
      if (len > (int)sizeof(s->buf)) len = (int)sizeof(s->buf);
      int err = _radio->readData(s->buf, len);
      if (err != RADIOLIB_ERR_NONE) {
        MESH_DEBUG_PRINTLN("RadioLibWrapper: error: readData(%d)", err);
        n_recv_errors++;
      } else {
        // Capture this packet's link stats NOW — after the next drain the radio
        // would report the newer packet's values (mis-attributed SNR/RSSI).
        s->len  = (uint16_t)len;
        s->snr  = _radio->getSNR();
        s->rssi = _radio->getRSSI();
        n_recv++;
        rxq_w = next_w;
      }
    }
  }
  state = STATE_IDLE;
  startRecv();   // re-arm immediately — the whole point of the drain task
}
#endif

void RadioLibWrapper::pollRxIfNoIrq() {
#if defined(MESH_RADIO_DIO1_POLLED)
  // Stand in for the DIO1 ISR on a board that cannot have one. Same effect as
  // setFlag(): count the event, latch STATE_INT_READY so a reader will lift the
  // packet out, and kick the drain task if the buffered path owns servicing.
  rlwLock();
  if (!(state & STATE_INT_READY) && (state & ~STATE_INT_READY) == STATE_RX && pollRxDone()) {
    rx_evt_count++;
    state |= STATE_INT_READY;
#if defined(ESP32)
    if (rxq_sem && rxq_enabled && !rxq_suspend) xSemaphoreGive(rxq_sem);
#endif
  }
  rlwUnlock();
#endif
}

int RadioLibWrapper::recvRaw(uint8_t* bytes, int sz) {
  int len = 0;
  pollRxIfNoIrq();      // no-op unless this board polls (see the header)
  rlwLock();
#if defined(ESP32)
  // Buffered path: pop a packet the drain task already lifted out of the radio.
  // Also drains leftover slots after the queue is toggled off.
  if (rxq_r != rxq_w) {
    RxQSlot* s = &rxq[rxq_r];
    len = s->len;
    if (len > sz) len = sz;
    memcpy(bytes, s->buf, len);
    _q_last_snr  = s->snr;
    _q_last_rssi = s->rssi;
    _q_has_last  = true;
    rxq_r = (uint8_t)((rxq_r + 1) % RXQ_SLOTS);
    rlwUnlock();
    return len;
  }
  if (rxq_enabled) {
    // The drain task owns radio-side RX servicing. One duty remains here: after
    // a TX finishes the state is IDLE and only a read re-armed RX in the stock
    // flow — re-arm it now or the radio would sit in standby.
    if (state == STATE_IDLE) startRecv();
    rlwUnlock();
    return 0;
  }
#endif
  if (state & STATE_INT_READY) {
    len = _radio->getPacketLength();
    if (len > 0) {
      if (len > sz) { len = sz; }
      int err = _radio->readData(bytes, len);
      if (err != RADIOLIB_ERR_NONE) {
        MESH_DEBUG_PRINTLN("RadioLibWrapper: error: readData(%d)", err);
        len = 0;
        n_recv_errors++;
      } else {
      //  Serial.print("  readData() -> "); Serial.println(len);
        n_recv++;
        _q_has_last = false;   // link stats come live from the radio again
      }
    }
    #if defined(USE_LR2021)
    state = STATE_RX;     // LR2021 stays in Rx after readData, calling startReceive while still in Rx throws -706 errors
    #else
    state = STATE_IDLE;   // need another startReceive()
    #endif
  }

  if (state != STATE_RX) {
    int err = _radio->startReceive();
    if (err == RADIOLIB_ERR_NONE) {
      state = STATE_RX;
    } else {
      MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startReceive(%d)", err);
    }
  }
  rlwUnlock();
  return len;
}

bool RadioLibWrapper::isReceiving() {
  rlwLock();
  bool r = isReceivingPacket() || isChannelActive();
  rlwUnlock();
  return r;
}

void RadioLibWrapper::powerOff() {
#if defined(ESP32)
  rxq_enabled = false;   // park the drain task before taking the radio down
#endif
  rlwLock();
  _radio->sleep();
  rlwUnlock();
}

void RadioLibWrapper::rxQueueEnable(bool en) {
#if defined(ESP32)
  if (en) {
    if (!radio_mtx) radio_mtx = xSemaphoreCreateRecursiveMutex();
    if (!rxq_sem)   rxq_sem   = xSemaphoreCreateBinary();
    rxq_owner = this;
    if (!rxq_task_h) {
      // Core 0 (loop/LVGL run on core 1), above loopTask, below the radio stacks.
      xTaskCreatePinnedToCore(rlwRxqTask, "lora_rx", 4096, nullptr, 10, &rxq_task_h, 0);
    }
    rxq_enabled = true;
    xSemaphoreGive(rxq_sem);   // service a packet that may already be pending
  } else {
    rxq_enabled = false;   // task idles; recvRaw pops leftovers, then legacy path
  }
#else
  (void)en;
#endif
}

bool RadioLibWrapper::rxQueueEnabled() const {
#if defined(ESP32)
  return rxq_enabled;
#else
  return false;
#endif
}

void RadioLibWrapper::rxQueueSuspend(bool s) {
#if defined(ESP32)
  rxq_suspend = s;
  if (!s && rxq_sem) xSemaphoreGive(rxq_sem);   // catch up on anything missed
#else
  (void)s;
#endif
}

void RadioLibWrapper::radioAcquire() { rlwLock(); }
void RadioLibWrapper::radioRelease() { rlwUnlock(); }

uint32_t RadioLibWrapper::getRxEvents() const { return rx_evt_count; }

uint32_t RadioLibWrapper::getRxQueueDrops() const {
#if defined(ESP32)
  return rxq_full_drops;
#else
  return 0;
#endif
}

uint32_t RadioLibWrapper::getEstAirtimeFor(int len_bytes) {
  return _radio->getTimeOnAir(len_bytes) / 1000;
}

bool RadioLibWrapper::startSendRaw(const uint8_t* bytes, int len) {
  rlwLock();
  _board->onBeforeTransmit();
  int err = _radio->startTransmit((uint8_t *) bytes, len);
  if (err == RADIOLIB_ERR_NONE) {
    state = STATE_TX_WAIT;
    rlwUnlock();
    return true;
  }
  MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startTransmit(%d)", err);
  idle();   // trigger another startRecv()
  _board->onAfterTransmit();
  rlwUnlock();
  return false;
}

bool RadioLibWrapper::isSendComplete() {
  if (state & STATE_INT_READY) {
    if ((state & ~STATE_INT_READY) != STATE_TX_WAIT) return false;  // an RX flag is not ours
    state = STATE_IDLE;
    n_sent++;
    return true;
  }
  return false;
}

void RadioLibWrapper::onSendFinished() {
  rlwLock();
  _radio->finishTransmit();
  _board->onAfterTransmit();
  state = STATE_IDLE;
  rlwUnlock();
}

int16_t RadioLibWrapper::performChannelScan() {
  return _radio->scanChannel();
}

bool RadioLibWrapper::isChannelActive() {
  // Every SPI touch below is serialised on the radio mutex, which is RECURSIVE, so
  // this is safe standalone and nested inside isReceiving(), which already holds it.
  rlwLock();
  bool active = false;

  // int.thresh: RSSI-based interference detection (relative to noise floor)
  if (_threshold != 0 && getCurrentRSSI() > _noise_floor + _threshold) {
    active = true;
  } else if (_cad_enabled) {
    // cad: hardware channel activity detection
    int16_t result = performChannelScan();
    // scanChannel() triggers DIO interrupt (CAD done) which sets STATE_INT_READY
    // via setFlag() ISR. Clear it before restarting RX so recvRaw() doesn't
    // try to read a non-existent packet and count a spurious recv error.
    state = STATE_IDLE;
    startRecv();
    if (result != RADIOLIB_CHANNEL_FREE) active = true;
  }

  rlwUnlock();
  return active;
}

float RadioLibWrapper::getLastRSSI() const {
  if (_q_has_last) return _q_last_rssi;   // popped ring slot's stats (buffered RX)
  rlwLock();
  float r = _radio->getRSSI();
  rlwUnlock();
  return r;
}
float RadioLibWrapper::getLastSNR() const {
  if (_q_has_last) return _q_last_snr;
  rlwLock();
  float r = _radio->getSNR();
  rlwUnlock();
  return r;
}

// Approximate SNR threshold per SF for successful reception (based on Semtech datasheets)
static float snr_threshold[] = {
    -7.5,  // SF7 needs at least -7.5 dB SNR
    -10,   // SF8 needs at least -10 dB SNR
    -12.5, // SF9 needs at least -12.5 dB SNR
    -15,  // SF10 needs at least -15 dB SNR
    -17.5,// SF11 needs at least -17.5 dB SNR
    -20   // SF12 needs at least -20 dB SNR
};
  
float RadioLibWrapper::packetScoreInt(float snr, int sf, int packet_len) {
  if (sf < 7) return 0.0f;
  
  if (snr < snr_threshold[sf - 7]) return 0.0f;    // Below threshold, no chance of success

  auto success_rate_based_on_snr = (snr - snr_threshold[sf - 7]) / 10.0;
  auto collision_penalty = 1 - (packet_len / 256.0);   // Assuming max packet of 256 bytes

  return max(0.0, min(1.0, success_rate_based_on_snr * collision_penalty));
}

PacketMillis RadioLibWrapper::calcMaxPacketMillis(uint8_t sf, float bw, uint8_t cr, uint8_t preambleSymbols) {
  // based on RadioLib's calculateTimeOnAir()
  uint32_t tsym_us = ((uint32_t)10000 << sf) / (bw * 10);
  uint32_t sfCoeff1_x4 = (sf == 5 || sf == 6) ? 25 : 17; // 6.25 : 4.25, semtech magic numbers to account for sync word + sfd

  // preamble + syncword + sfd + header
  uint32_t preamble_us = (((preambleSymbols + 8) * 4 + sfCoeff1_x4) * tsym_us) / 4;
  
  // airtime for max packet at current radio settings
  uint32_t total_us   = _radio->getTimeOnAir(MAX_TRANS_UNIT);
  // airtime for payload only (no preamble, header or SOF).
  //
  // The fallback taken when getTimeOnAir() is not usable yet (it can return 0,
  // or a value for the pre-config modem params, depending on when the radio is
  // brought up) must never underflow: these are UNSIGNED, and the intended
  // "4 secs at worst case" was written as 4000 — microseconds, so 4 ms. Any
  // config whose preamble runs longer than that wrapped the subtraction to
  // ~4.29e9 us. SF8 at 62.5 kHz has a ~4.1 ms symbol time and a ~83 ms
  // preamble, so EU/UK Narrow hit it every time.
  //
  // The result was a silently deaf radio: _maxPayloadMillis became ~4.29
  // million seconds, so isReceiving() could never time out a detected header,
  // and startRecv() — which returns early while isReceivingPacket() is true —
  // stopped re-arming RX. No acks, no channel messages, no adverts, while a
  // raw-SPI spectrum sweep still showed the traffic arriving.
  static const uint32_t kFallbackTotalUs = 4000000;   // 4 SECONDS, in microseconds
  const uint32_t base_us = (total_us > preamble_us) ? total_us : kFallbackTotalUs;
  uint32_t payload_us = (base_us > preamble_us) ? (base_us - preamble_us) : (base_us / 2);
  // rescale payload_us for max possible CR
  if (cr >= 5 && cr < 8) { payload_us = (payload_us * 8) / cr; }

  return PacketMillis {(preamble_us + 999) / 1000, (payload_us + 999) / 1000};
}