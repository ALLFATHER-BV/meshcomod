#pragma once
#if defined(ESP32)

#include <Arduino.h>
#include <Preferences.h>
#include <vector>

// Drop-in, Preferences-compatible key/value store that survives Launcher.
//
// The companion "app-only" bin, when installed by Launcher, boots on Launcher's
// partition table: there's no usable SPIFFS and the NVS partition is full
// (`nvs_open failed: NOT_ENOUGH_SPACE`). Every Preferences open then fails, so
// settings/Wi-Fi creds silently never persist (and a flood of [E] logs spams the
// console). The mesh DATA already falls back to the SD card (/meshcomod); this
// does the same for PREFS: it mirrors the subset of the Arduino Preferences API
// that TouchPrefsStore + WifiRuntimeStore use, delegating to NVS when NVS works
// and to a flat /meshcomod/<ns>.kv file on the SD card when it doesn't.
//
// The backend (NVS vs SD) is probed ONCE per boot and cached globally, so a
// device with working NVS behaves exactly as before.
class SdNvsPrefs {
public:
  bool begin(const char* ns, bool readOnly = false);
  void end();

  bool     isKey(const char* key);
  bool     remove(const char* key);
  bool     clear();

  uint8_t  getUChar (const char* key, uint8_t  def = 0);
  size_t   putUChar (const char* key, uint8_t  v);
  int8_t   getChar  (const char* key, int8_t   def = 0);
  size_t   putChar  (const char* key, int8_t   v);
  uint16_t getUShort(const char* key, uint16_t def = 0);
  size_t   putUShort(const char* key, uint16_t v);
  uint32_t getUInt  (const char* key, uint32_t def = 0);
  size_t   putUInt  (const char* key, uint32_t v);
  bool     getBool  (const char* key, bool     def = false);
  size_t   putBool  (const char* key, bool     v);

  String   getString(const char* key, const String& def = String());
  size_t   getString(const char* key, char* buf, size_t maxLen);   // char* overload
  size_t   putString(const char* key, const char* v);
  size_t   putString(const char* key, const String& v) { return putString(key, v.c_str()); }

  size_t   getBytes(const char* key, void* buf, size_t maxLen);
  size_t   putBytes(const char* key, const void* buf, size_t len);

private:
  // --- NVS-backed path ---
  Preferences _nvs;
  bool        _nvs_open = false;
  bool        _read_only = false;

  // --- SD-backed path ---
  struct Kv { char key[16]; std::vector<uint8_t> val; };
  std::vector<Kv> _sd;          // in-RAM mirror of /meshcomod/<ns>.kv
  char            _path[40] = {0};
  bool            _sd_loaded = false;

  bool   useNvs();              // probe once, cache globally
  Kv*    sdFind(const char* key);
  void   sdSet(const char* key, const uint8_t* data, size_t len);
  void   sdLoad();
  void   sdSave();
  uint64_t sdGetInt(const char* key, uint64_t def, int width);
  size_t sdPutInt(const char* key, uint64_t v, int width);
};

#endif  // ESP32
