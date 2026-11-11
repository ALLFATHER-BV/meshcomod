#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <ThinkNodeM9Board.h>
#include <helpers/radiolib/CustomLR1110Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/sensors/LocationProvider.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/ST7789Display.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

class ThinkNodeM9SensorManager : public EnvironmentSensorManager {
  bool last_gps_switch_state = false;
public:
#if ENV_INCLUDE_GPS
  ThinkNodeM9SensorManager(LocationProvider &location) : EnvironmentSensorManager(location) {}
  void start_gps();
  void stop_gps();
#else
  ThinkNodeM9SensorManager() : EnvironmentSensorManager() {}
#endif
  bool begin() override;
  void loop() override;
  bool setSettingValue(const char* name, const char* value) override;
};

extern ThinkNodeM9Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern ThinkNodeM9SensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();

