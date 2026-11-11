#include "ThinkNodeM9Board.h"
#include <SPI.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

static uint16_t s_m9_cached_batt_mv = 0;

void ThinkNodeM9Board::begin() {
  // Enable the M9 3.3V peripheral rail (GPIO18) and TFT backlight enable (GPIO17)
  periph_power.begin();
  periph_power.claim();

  backlight.begin();
  backlight.claim();

  // Release any deep-sleep GPIO hold on keyboard backlight LED (GPIO46) and keep it off by default
#ifdef PIN_KEY_LED
  gpio_hold_dis((gpio_num_t)PIN_KEY_LED);
  pinMode(PIN_KEY_LED, OUTPUT);
  digitalWrite(PIN_KEY_LED, LOW);
#endif

  // Ensure buzzer (GPIO9) is silent on boot
#ifdef THINKNODE_M9_BUZZER_PIN
  pinMode(THINKNODE_M9_BUZZER_PIN, OUTPUT);
  digitalWrite(THINKNODE_M9_BUZZER_PIN, LOW);
#endif

  // Keyboard interrupt input (GPIO12)
#ifdef PIN_KB_WAKEUP
  rtc_gpio_deinit((gpio_num_t)PIN_KB_WAKEUP);
  pinMode(PIN_KB_WAKEUP, INPUT);
#endif

  // USB 5V sense (GPIO1) and U6 (LGS4056HDA-4.35) DONE# open-drain output (GPIO8)
#ifdef PIN_USB_5V_SENSE
  pinMode(PIN_USB_5V_SENSE, INPUT);
#endif
#ifdef PIN_CHG_DONE
  pinMode(PIN_CHG_DONE, INPUT_PULLUP);
#endif

  // Pre-park all SPI chip-selects HIGH before initializing shared SPI2 bus
  // (ST7789V CS=16, LR1110 NSS=39, microSD CS=48) to prevent bus contention
#ifdef PIN_TFT_CS
  pinMode(PIN_TFT_CS, OUTPUT);
  digitalWrite(PIN_TFT_CS, HIGH);
#endif
#ifdef P_LORA_NSS
  gpio_hold_dis((gpio_num_t)P_LORA_NSS);
  rtc_gpio_deinit((gpio_num_t)P_LORA_NSS);
  pinMode(P_LORA_NSS, OUTPUT);
  digitalWrite(P_LORA_NSS, HIGH);
#endif
#ifdef PIN_SD_CS
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
#endif

  delay(20);  // Allow peripheral rail to stabilize before SPI/I2C transactions

  ESP32Board::begin();

  // Bound I2C transactions and run at 400 kHz for PCF8563, QMI8658A, and QMC6309
  Wire.setClock(400000);
  Wire.setTimeOut(20);

  // Configure 11dB attenuation for GPIO13 (ADC2_CH2) full-scale 0..3.1V range
#ifdef PIN_VBAT_READ
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_VBAT_READ, ADC_11db);
  (void)getBattMilliVolts();
#endif

  // Initialize shared SPI bus for LR1110 / ST7789 / SD
#if defined(P_LORA_SCLK) && defined(P_LORA_MISO) && defined(P_LORA_MOSI)
  SPI.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, -1);
#endif
}

uint16_t ThinkNodeM9Board::getBattMilliVolts() {
#ifdef PIN_VBAT_READ
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_VBAT_READ, ADC_11db);

  uint32_t sum = 0;
  int valid_samples = 0;
  for (int i = 0; i < 8; i++) {
    uint32_t mv = analogReadMilliVolts(PIN_VBAT_READ);
    // Filter out ADC2 ESP_ERR_TIMEOUT / zero-read glitches when Wi-Fi is active
    // (1250 mV at ADC pin = 2500 mV battery, below Li-Ion cutoff)
    if (mv >= 1250 && mv <= 2500) {
      sum += mv;
      valid_samples++;
    }
  }
  if (valid_samples > 0) {
    uint16_t vbat = (uint16_t)((sum * 2) / valid_samples);
    s_m9_cached_batt_mv = vbat;
    return vbat;
  }
  return s_m9_cached_batt_mv;
#else
  return 0;
#endif
}

bool ThinkNodeM9Board::externalPowerPresent() const {
#ifdef PIN_USB_5V_SENSE
  if (digitalRead(PIN_USB_5V_SENSE) == HIGH) return true;
#endif
  return false;
}

bool ThinkNodeM9Board::isExternalPowered() const {
  return externalPowerPresent();
}

int ThinkNodeM9Board::isCharging() const {
  if (!externalPowerPresent()) {
    return 0;  // discharging
  }
#ifdef PIN_CHG_DONE
  // U6 (LGS4056HDA-4.35) DONE# pin (GPIO8) is open-drain active-LOW with internal pull-up:
  // LOW (0) = charge complete / top-off (2), HIGH (1) = actively charging (1).
  if (digitalRead(PIN_CHG_DONE) == LOW) {
    return 2;  // full (PIN_USB_5V_SENSE HIGH and PIN_CHG_DONE LOW)
  }
#endif
  return 1;    // charging (PIN_USB_5V_SENSE HIGH and PIN_CHG_DONE HIGH)
}

void ThinkNodeM9Board::enterDeepSleep(uint32_t secs, int pin_wake_btn) {
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

  // Hold LoRa NSS HIGH so LR1110 stays deselected during sleep
#ifdef P_LORA_NSS
  digitalWrite(P_LORA_NSS, HIGH);
  gpio_hold_en((gpio_num_t)P_LORA_NSS);
#endif

  // Silence buzzer and hold KEY_LED LOW during deep sleep
#ifdef THINKNODE_M9_BUZZER_PIN
  digitalWrite(THINKNODE_M9_BUZZER_PIN, LOW);
#endif
#ifdef PIN_KEY_LED
  digitalWrite(PIN_KEY_LED, LOW);
  gpio_hold_en((gpio_num_t)PIN_KEY_LED);
#endif

  // Release GPS pins to high-Z INPUT so no current back-feeds the L76K domain
#ifdef PIN_GPS_STANDBY
  pinMode(PIN_GPS_STANDBY, INPUT);
#endif
#ifdef PIN_GPS_RX
  pinMode(PIN_GPS_RX, INPUT);
#endif
#ifdef PIN_GPS_TX
  pinMode(PIN_GPS_TX, INPUT);
#endif

  // Drop backlight and peripheral 3.3V rail
  backlight.release();
  periph_power.release();
  gpio_deep_sleep_hold_en();

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  // Only arm wake sources when a timed sleep or wake button is requested;
  // powerOff() passes secs=0, pin_wake_btn=-1 for true shutdown.
  if (secs > 0 || pin_wake_btn >= 0) {
#ifdef PIN_KB_WAKEUP
    int wake_pin = (pin_wake_btn >= 0) ? pin_wake_btn : PIN_KB_WAKEUP;
#else
    int wake_pin = pin_wake_btn;
#endif
    if (wake_pin >= 0) {
      rtc_gpio_pulldown_en((gpio_num_t)wake_pin);
      rtc_gpio_pullup_dis((gpio_num_t)wake_pin);
      esp_sleep_enable_ext1_wakeup((1ULL << wake_pin), ESP_EXT1_WAKEUP_ANY_HIGH);
    }
  }

  if (secs > 0) {
    esp_sleep_enable_timer_wakeup(secs * 1000000ULL);
  }

  esp_deep_sleep_start();
}

void ThinkNodeM9Board::powerOff() {
  enterDeepSleep(0, -1);
}

uint32_t ThinkNodeM9Board::getIRQGpio() {
  return P_LORA_DIO_1;  // GPIO42 is LR1110 DIO9/IRQ (GPIO41 is BUSY)
}

const char* ThinkNodeM9Board::getManufacturerName() const {
  return "Elecrow ThinkNode M9";
}
