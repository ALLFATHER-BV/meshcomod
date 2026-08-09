#pragma once

#include <Wire.h>
#include <Arduino.h>
#include "helpers/ESP32Board.h"

#define PIN_VBAT_READ 4
#define BATTERY_SAMPLES 8
#define ADC_MULTIPLIER (2.0f * 3.3f * 1000)

class TDeckBoard : public ESP32Board {
public:
  void begin();

  #ifdef P_LORA_TX_LED
    void onBeforeTransmit() override{
      digitalWrite(P_LORA_TX_LED, LOW); // turn TX LED on - invert pin for SX1276
    }

    void onAfterTransmit() override{
      digitalWrite(P_LORA_TX_LED, HIGH); // turn TX LED off - invert pin for SX1276
    }
  #endif

  uint16_t getBattMilliVolts() {
    #if defined(PIN_VBAT_READ)
      // Use analogReadMilliVolts (eFuse-calibrated) rather than
      // analogRead()*Vref/4096. The ESP32-S3 ADC is markedly non-linear and
      // under-reads near the top of its range, so the old linear formula made a
      // full 4.2 V cell read ~3.78 V (≈53 %). The calibrated path corrects that
      // across the whole range. PIN_VBAT_READ (GPIO4) sits behind a 2:1 divider,
      // so the battery voltage is twice the pin voltage.
      analogReadResolution(12);
      uint32_t sum = 0;
      for (int i = 0; i < BATTERY_SAMPLES; i++) {
        sum += analogReadMilliVolts(PIN_VBAT_READ);
      }
      return (uint16_t)(2 * (sum / BATTERY_SAMPLES));
    #else
      return 0;
    #endif
  }

  const char* getManufacturerName() const{
    return "LilyGo T-Deck";
  }
};