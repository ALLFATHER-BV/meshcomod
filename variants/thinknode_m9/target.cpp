#include <Arduino.h>
#include "target.h"

ThinkNodeM9Board board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);
WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

#ifdef ENV_INCLUDE_GPS
#include <helpers/sensors/MicroNMEALocationProvider.h>
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
ThinkNodeM9SensorManager sensors = ThinkNodeM9SensorManager(nmea);
#else
ThinkNodeM9SensorManager sensors = ThinkNodeM9SensorManager();
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
#endif

static void m9GpsWakePulse() {
#if defined(PIN_GPS_STANDBY) && (PIN_GPS_STANDBY >= 0)
  pinMode(PIN_GPS_STANDBY, OUTPUT);
  digitalWrite(PIN_GPS_STANDBY, LOW);
  delay(80);
  digitalWrite(PIN_GPS_STANDBY, HIGH);
  delay(80);
  digitalWrite(PIN_GPS_STANDBY, LOW);
  delay(20);
  // Release GPIO10 to INPUT after wake pulse so it does not fight the physical GPS button
  pinMode(PIN_GPS_STANDBY, INPUT);
#endif
}

#if ENV_INCLUDE_GPS
void ThinkNodeM9SensorManager::start_gps() {
  if (!gps_active) {
    m9GpsWakePulse();
    Serial1.setPins(PIN_GPS_TX, PIN_GPS_RX);
#ifdef GPS_BAUD_RATE
    Serial1.begin(GPS_BAUD_RATE);
#else
    Serial1.begin(115200);
#endif
    EnvironmentSensorManager::start_gps();
  }
}

void ThinkNodeM9SensorManager::stop_gps() {
  EnvironmentSensorManager::stop_gps();
  Serial1.end();
#ifdef PIN_GPS_RX
  pinMode(PIN_GPS_RX, INPUT);
#endif
#ifdef PIN_GPS_TX
  pinMode(PIN_GPS_TX, INPUT);
#endif
#if defined(PIN_GPS_STANDBY) && (PIN_GPS_STANDBY >= 0)
  pinMode(PIN_GPS_STANDBY, INPUT);
#endif
}
#endif

bool ThinkNodeM9SensorManager::begin() {
#if ENV_INCLUDE_GPS
#ifdef PIN_GPS_SWITCH
  pinMode(PIN_GPS_SWITCH, INPUT);
  last_gps_switch_state = (digitalRead(PIN_GPS_SWITCH) == LOW);
#endif
  m9GpsWakePulse();
#endif
  bool ok = EnvironmentSensorManager::begin();
#if ENV_INCLUDE_GPS
  if (!gps_active) {
    stop_gps();
  }
#endif
  return ok;
}

bool ThinkNodeM9SensorManager::setSettingValue(const char* name, const char* value) {
#if ENV_INCLUDE_GPS
  if (gps_detected && strcmp(name, "gps") == 0) {
    if (strcmp(value, "0") == 0) {
      stop_gps();
    } else {
      start_gps();
    }
    return true;
  }
#endif
  return EnvironmentSensorManager::setSettingValue(name, value);
}

void ThinkNodeM9SensorManager::loop() {
#if ENV_INCLUDE_GPS && defined(PIN_GPS_SWITCH)
  static unsigned long last_switch_check = 0;
  if (millis() - last_switch_check > 1000) {
    bool switch_on = (digitalRead(PIN_GPS_SWITCH) == LOW);
    if (switch_on != last_gps_switch_state) {
      last_gps_switch_state = switch_on;
      if (switch_on) {
        start_gps();
      } else {
        stop_gps();
      }
    }
    last_switch_check = millis();
  }
#endif
#if ENV_INCLUDE_GPS || defined(ENV_INCLUDE_BME680_BSEC)
  EnvironmentSensorManager::loop();
#endif
}

#ifdef RF_SWITCH_TABLE
static const uint32_t rfswitch_dios[Module::RFSWITCH_MAX_PINS] = {
  RADIOLIB_LR11X0_DIO5,
  RADIOLIB_LR11X0_DIO6,
  RADIOLIB_NC,
  RADIOLIB_NC, 
  RADIOLIB_NC
};

static const Module::RfSwitchMode_t rfswitch_table[] = {
  // mode                  DIO5  DIO6
  {LR11x0::MODE_STBY, {LOW, LOW}}, {LR11x0::MODE_RX, {HIGH, LOW}},
  {LR11x0::MODE_TX, {HIGH, HIGH}}, {LR11x0::MODE_TX_HP, {LOW, HIGH}},
  {LR11x0::MODE_TX_HF, {LOW, LOW}}, {LR11x0::MODE_GNSS, {LOW, LOW}},
  {LR11x0::MODE_WIFI, {LOW, LOW}}, END_OF_MODE_TABLE,
};
#endif

#ifndef LORA_CR
  #define LORA_CR 5
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  
#ifdef LR11X0_DIO3_TCXO_VOLTAGE
  float tcxo = LR11X0_DIO3_TCXO_VOLTAGE;
#else
  float tcxo = 1.6f;
#endif
  
  int status = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, tcxo);
  if (status != RADIOLIB_ERR_NONE) {
    delay(150);
    status = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, tcxo);
  }
  if (status != RADIOLIB_ERR_NONE) {
    Serial.print("ERROR: radio init failed: ");
    Serial.println(status);
    return false;  // fail
  }
  
  radio.setCRC(2);
  radio.explicitHeader();

#ifdef RF_SWITCH_TABLE
  radio.setRfSwitchTable(rfswitch_dios, rfswitch_table);
#endif
  radio.setRegulatorDCDC();
  radio.calibrate(RADIOLIB_LR11X0_CALIBRATE_ALL);
  radio.calibrateImageRejection(LORA_FREQ - 4.0f, LORA_FREQ + 4.0f);
#ifdef RX_BOOSTED_GAIN
  radio.setRxBoostedGainMode(RX_BOOSTED_GAIN);
#endif

  return true;  // success
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}