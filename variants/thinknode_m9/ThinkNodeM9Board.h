#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>
#include <helpers/RefCountedDigitalPin.h>
#include "variant.h"

class ThinkNodeM9Board : public ESP32Board {
public:
  RefCountedDigitalPin periph_power;
  RefCountedDigitalPin backlight;

  ThinkNodeM9Board()
      : periph_power(PIN_PERIPH_POWER),
        backlight(PIN_TFT_BL_EN) {}

  void begin();
  void enterDeepSleep(uint32_t secs, int pin_wake_btn = -1);
  void powerOff() override;
  uint16_t getBattMilliVolts() override;
  int isCharging() const;
  bool externalPowerPresent() const;
  bool isExternalPowered() const;
  const char* getManufacturerName() const override;
  uint32_t getIRQGpio() override;
};
