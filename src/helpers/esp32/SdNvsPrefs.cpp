#include "SdNvsPrefs.h"
#if defined(ESP32)

#include <SD.h>
#include <string.h>

// Backend is probed ONCE per boot and cached for every namespace/instance: a
// device with healthy NVS keeps using NVS; a Launcher install (NVS full) routes
// everything to /meshcomod/<ns>.kv on the SD card.
static int s_backend = -1;   // -1 undecided, 0 = SD, 1 = NVS

bool SdNvsPrefs::useNvs() {
  if (s_backend < 0) {
    Preferences probe;
    bool ok = false;
    if (probe.begin("mc_kvprobe", false)) {   // RW open must succeed...
      ok = (probe.putUChar("v", 1) > 0);      // ...and a real write must land
      probe.end();
    }
    s_backend = ok ? 1 : 0;
    Serial.printf("[PREFS] backend = %s\n", ok ? "NVS" : "SD /meshcomod");
  }
  return s_backend == 1;
}

// ----------------------------- begin / end -----------------------------
bool SdNvsPrefs::begin(const char* ns, bool readOnly) {
  _read_only = readOnly;
  if (useNvs()) { _nvs_open = _nvs.begin(ns, readOnly); return _nvs_open; }
  snprintf(_path, sizeof(_path), "/meshcomod/%s.kv", ns);
  if (!_sd_loaded) { sdLoad(); _sd_loaded = true; }
  return true;   // SD store is always "open"
}

void SdNvsPrefs::end() {
  if (useNvs() && _nvs_open) { _nvs.end(); _nvs_open = false; }
  // SD mode keeps its RAM mirror across begin/end (flushed on every put).
}

// ----------------------------- SD helpers -----------------------------
SdNvsPrefs::Kv* SdNvsPrefs::sdFind(const char* key) {
  for (auto& e : _sd) if (strncmp(e.key, key, sizeof(e.key)) == 0) return &e;
  return nullptr;
}

void SdNvsPrefs::sdSet(const char* key, const uint8_t* data, size_t len) {
  Kv* e = sdFind(key);
  if (!e) {
    _sd.push_back(Kv{});
    e = &_sd.back();
    strncpy(e->key, key, sizeof(e->key) - 1);
    e->key[sizeof(e->key) - 1] = '\0';
  }
  e->val.assign(data, data + len);
  sdSave();
}

void SdNvsPrefs::sdLoad() {
  _sd.clear();
  File f = SD.open(_path, FILE_READ);
  if (!f) return;
  // record = [keylen u8][key bytes][vallen u16 LE][val bytes]
  // Sanity caps stop a corrupt/garbage file from triggering a huge allocation or
  // an unbounded entry list (internal heap is tight on a Launcher boot). Real
  // blobs are <=192 B and there are only a few dozen keys; on any bad record we
  // stop parsing and keep whatever loaded cleanly before it.
  while (f.available() > 0 && _sd.size() < 256) {
    int kl = f.read();
    if (kl <= 0 || kl > 15) break;
    char k[16] = {0};
    if (f.read((uint8_t*)k, kl) != kl) break;
    int lo = f.read(), hi = f.read();
    if (lo < 0 || hi < 0) break;
    size_t vl = (size_t)lo | ((size_t)hi << 8);
    if (vl > 2048) break;                       // implausible value length -> corrupt
    std::vector<uint8_t> v(vl);
    if (vl && f.read(v.data(), vl) != (int)vl) break;
    Kv e; strncpy(e.key, k, sizeof(e.key) - 1); e.key[sizeof(e.key) - 1] = '\0';
    e.val = std::move(v);
    _sd.push_back(std::move(e));
  }
  f.close();
}

void SdNvsPrefs::sdSave() {
  if (_read_only) return;          // mirror Preferences: a write needs a RW begin
  SD.mkdir("/meshcomod");
  File f = SD.open(_path, FILE_WRITE);   // "w" — truncate + rewrite the whole file
  if (!f) return;
  for (auto& e : _sd) {
    size_t kl = strnlen(e.key, sizeof(e.key));
    size_t vl = e.val.size();
    f.write((uint8_t)kl);
    f.write((const uint8_t*)e.key, kl);
    f.write((uint8_t)(vl & 0xFF));
    f.write((uint8_t)((vl >> 8) & 0xFF));
    if (vl) f.write(e.val.data(), vl);
  }
  f.close();
}

uint64_t SdNvsPrefs::sdGetInt(const char* key, uint64_t def, int width) {
  Kv* e = sdFind(key);
  if (!e || e->val.empty()) return def;
  uint64_t v = 0;
  int n = (int)e->val.size(); if (n > width) n = width;
  for (int i = 0; i < n; i++) v |= (uint64_t)e->val[i] << (8 * i);
  return v;
}

size_t SdNvsPrefs::sdPutInt(const char* key, uint64_t v, int width) {
  uint8_t b[8];
  for (int i = 0; i < width; i++) b[i] = (uint8_t)(v >> (8 * i));
  sdSet(key, b, width);
  return width;
}

// ----------------------------- API -----------------------------
bool SdNvsPrefs::isKey(const char* key) {
  if (useNvs()) return _nvs_open && _nvs.isKey(key);
  return sdFind(key) != nullptr;
}
bool SdNvsPrefs::remove(const char* key) {
  if (useNvs()) return _nvs_open && _nvs.remove(key);
  for (size_t i = 0; i < _sd.size(); i++)
    if (strncmp(_sd[i].key, key, sizeof(_sd[i].key)) == 0) { _sd.erase(_sd.begin() + i); sdSave(); return true; }
  return false;
}
bool SdNvsPrefs::clear() {
  if (useNvs()) return _nvs_open && _nvs.clear();
  _sd.clear(); sdSave(); return true;
}

uint8_t  SdNvsPrefs::getUChar (const char* k, uint8_t def)  { return useNvs() ? _nvs.getUChar(k, def)  : (uint8_t)sdGetInt(k, def, 1); }
size_t   SdNvsPrefs::putUChar (const char* k, uint8_t v)    { return useNvs() ? _nvs.putUChar(k, v)    : sdPutInt(k, v, 1); }
int8_t   SdNvsPrefs::getChar  (const char* k, int8_t def)   { return useNvs() ? _nvs.getChar(k, def)   : (int8_t)sdGetInt(k, (uint8_t)def, 1); }
size_t   SdNvsPrefs::putChar  (const char* k, int8_t v)     { return useNvs() ? _nvs.putChar(k, v)     : sdPutInt(k, (uint8_t)v, 1); }
uint16_t SdNvsPrefs::getUShort(const char* k, uint16_t def) { return useNvs() ? _nvs.getUShort(k, def) : (uint16_t)sdGetInt(k, def, 2); }
size_t   SdNvsPrefs::putUShort(const char* k, uint16_t v)   { return useNvs() ? _nvs.putUShort(k, v)   : sdPutInt(k, v, 2); }
uint32_t SdNvsPrefs::getUInt  (const char* k, uint32_t def) { return useNvs() ? _nvs.getUInt(k, def)   : (uint32_t)sdGetInt(k, def, 4); }
size_t   SdNvsPrefs::putUInt  (const char* k, uint32_t v)   { return useNvs() ? _nvs.putUInt(k, v)     : sdPutInt(k, v, 4); }
bool     SdNvsPrefs::getBool  (const char* k, bool def)     { return useNvs() ? _nvs.getBool(k, def)   : (sdGetInt(k, def ? 1 : 0, 1) != 0); }
size_t   SdNvsPrefs::putBool  (const char* k, bool v)       { return useNvs() ? _nvs.putBool(k, v)     : sdPutInt(k, v ? 1 : 0, 1); }

String SdNvsPrefs::getString(const char* k, const String& def) {
  if (useNvs()) return _nvs.getString(k, def);
  Kv* e = sdFind(k);
  if (!e) return def;
  String s; s.reserve(e->val.size());
  for (uint8_t c : e->val) s += (char)c;
  return s;
}
size_t SdNvsPrefs::getString(const char* k, char* buf, size_t maxLen) {
  if (useNvs()) return _nvs.getString(k, buf, maxLen);
  if (!buf || !maxLen) return 0;
  Kv* e = sdFind(k);
  size_t n = e ? e->val.size() : 0;
  if (n > maxLen - 1) n = maxLen - 1;
  if (e && n) memcpy(buf, e->val.data(), n);
  buf[n] = '\0';
  return n;
}
size_t SdNvsPrefs::putString(const char* k, const char* v) {
  if (useNvs()) return _nvs.putString(k, v);
  size_t n = v ? strlen(v) : 0;
  sdSet(k, (const uint8_t*)v, n);
  return n;
}

size_t SdNvsPrefs::getBytes(const char* k, void* buf, size_t maxLen) {
  if (useNvs()) return _nvs.getBytes(k, buf, maxLen);
  Kv* e = sdFind(k);
  if (!e) return 0;
  size_t n = e->val.size();
  if (buf && maxLen) { size_t c = n > maxLen ? maxLen : n; memcpy(buf, e->val.data(), c); }
  return n;
}
size_t SdNvsPrefs::putBytes(const char* k, const void* buf, size_t len) {
  if (useNvs()) return _nvs.putBytes(k, buf, len);
  sdSet(k, (const uint8_t*)buf, len);
  return len;
}

#endif  // ESP32
