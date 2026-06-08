#pragma once

/* NVS-backed touch-UI prefs (screen timeout etc). Kept separate from
 * NodePrefs (which is file-persisted and shared across firmware variants)
 * so we don't risk breaking on-disk layout. */

#if defined(ESP32)

#include <stdint.h>

void touchPrefsBegin();

/** Screen timeout in seconds; 0 = never sleep. Default 20. */
uint16_t touchPrefsGetScreenTimeoutSecs();
bool touchPrefsSetScreenTimeoutSecs(uint16_t seconds);

/** Display backlight brightness, 5–100 %. Default 100. */
uint8_t touchPrefsGetBrightness();
bool    touchPrefsSetBrightness(uint8_t pct);

/** Keyboard backlight mode: 0 = off, 1 = on, 2 = auto (on while typing). Default auto. */
uint8_t touchPrefsGetKbBacklight();
bool    touchPrefsSetKbBacklight(uint8_t mode);

/** User-configurable quick-reply macros: up to 6 short strings the user can
 *  drop into the composer with a single tap (e.g. "ok", "on the way",
 *  "stuck — wait"). idx is 0..5; max length 31 chars + null. Returns the
 *  text length actually written into `out` (0 if the slot is empty or idx
 *  is out of range). `out` is always null-terminated when out_cap > 0. */
constexpr int TOUCH_QUICK_REPLY_COUNT  = 6;
constexpr int TOUCH_QUICK_REPLY_MAXLEN = 32;
int  touchPrefsGetQuickReply(int idx, char* out, int out_cap);
bool touchPrefsSetQuickReply(int idx, const char* text);

/** Show the live duty-cycle meter on the Home tab? Default true. The meter
 *  surfaces the regulator-imposed TX budget so the user can see when they're
 *  about to be throttled (LoRa duty caps in the 868/915 MHz ISM bands hit
 *  ~10%/50% depending on region). */
bool touchPrefsGetDutyMeterShown();
bool touchPrefsSetDutyMeterShown(bool show);

/** Distance units for the UI (contact distance badge, map, etc.).
 *  false = kilometres (default), true = miles. Pure display preference. */
bool touchPrefsGetUseMiles();
bool touchPrefsSetUseMiles(bool use_miles);

/** Map tile source: false = tile server + on-device cache (default), true = read tiles off the
 *  microSD card (/tiles/<z>/<x>/<y>.jpg). T-Deck only (the V4 TFT has no SD slot). */
bool touchPrefsGetTilesFromSd();
bool touchPrefsSetTilesFromSd(bool from_sd);

/** Global UI orientation, applied at boot before the screens are built so the
 *  whole layout reflows to the rotated resolution. Stored as the raw LVGL
 *  rotation code: 0 = portrait (LV_DISP_ROT_NONE), 1 = 90° (LV_DISP_ROT_90),
 *  2 = 180°, 3 = 270° (LV_DISP_ROT_270). Default 0. Distinct from the
 *  transient keyboard-landscape toggle ("kbrot"); changing it reboots the
 *  device so the UI rebuilds at the new orientation. */
uint8_t touchPrefsGetUiRotation();
bool    touchPrefsSetUiRotation(uint8_t rot);

/** Calibrated battery "full" voltage in mV — the reading captured when the pack
 *  was fully charged, treated as 100%. 0 = not calibrated (use the 4200 mV
 *  default Li-ion full point). Lets custom batteries / builds read 100%. */
uint16_t touchPrefsGetBattFullMv();
bool     touchPrefsSetBattFullMv(uint16_t mv);

/** Saved Wi-Fi profile slots. The "active" credentials still live in the
 *  meshcomod NVS namespace (WifiRuntimeStore.cpp) and are what
 *  wifiConfigApply() uses; these slots are a touch-UI convenience so the
 *  operator can switch between home / office / hotspot without retyping the
 *  passphrase. Activating a slot copies its ssid+pwd into the active store
 *  and requests an apply. idx is 0..2.
 *  Each slot holds a short user-chosen label (e.g. "home"), the ssid, and
 *  the pwd. All strings are zero-terminated; pass empty string to clear. */
constexpr int TOUCH_WIFI_SLOT_COUNT  = 3;
constexpr int TOUCH_WIFI_LABEL_MAX   = 16;
bool touchPrefsGetWifiSlot(int idx, char* label, int label_cap,
                           char* ssid, int ssid_cap,
                           char* pwd, int pwd_cap);
bool touchPrefsSetWifiSlot(int idx, const char* label,
                           const char* ssid, const char* pwd);
/** Apply slot idx as the active Wi-Fi credentials and request a reconnect.
 *  Returns false if idx out of range or the slot is empty. */
bool touchPrefsActivateWifiSlot(int idx);

/** Favorite contacts. Identified by the first 6 bytes of their pubkey — same
 *  prefix the firmware uses for short-key contact lookups — so 16 favorites
 *  fit in a 96-byte NVS blob. Pure UI metadata; the firmware contact table
 *  is untouched.
 *  • touchPrefsIsFavorite: returns true if the 6-byte prefix is stored.
 *  • touchPrefsSetFavorite: add/remove; returns the new state (true = is
 *    favorite after the call). Silently ignores adds past the cap (16). */
constexpr int TOUCH_FAVORITES_MAX = 16;
constexpr int TOUCH_FAVORITE_KEY_BYTES = 6;
bool touchPrefsIsFavorite(const uint8_t* pub_key6);
bool touchPrefsSetFavorite(const uint8_t* pub_key6, bool fav);

/** Copy all stored favorite prefixes into `out_buf` in one NVS read. Returns
 *  the number of records actually copied (0 .. TOUCH_FAVORITES_MAX). Used
 *  by the contact-list refresh which would otherwise hit NVS once per
 *  contact — measurably slow when the contact table grows past a couple
 *  dozen entries.
 *  `out_buf` must be at least TOUCH_FAVORITES_MAX * TOUCH_FAVORITE_KEY_BYTES
 *  bytes (96). Pair with touchPrefsFavoritesSnapshotContains() for the
 *  membership check. */
int  touchPrefsCopyFavorites(uint8_t* out_buf);
bool touchPrefsFavoritesSnapshotContains(const uint8_t* snapshot, int count,
                                          const uint8_t* pub_key6);

/** Map tile-server base URL. The device fetches missing map tiles by
 *  HTTP GET against `<base>/<z>/<x>/<y>.png`. Defaults to the meshcomod
 *  proxy. Plain HTTP only — mbedTLS doesn't fit in the ~5 KB of internal
 *  heap that survives Wi-Fi association, so the proxy does the HTTPS
 *  upstream to OSM. The default base is reasonable; expose it in
 *  Settings so the user can point at their own proxy / self-hosted
 *  tile-server if they prefer. */
constexpr int TOUCH_TILE_SERVER_MAXLEN = 80;
int  touchPrefsGetTileServer(char* out, int out_cap);
bool touchPrefsSetTileServer(const char* url);

/** Remembered repeater admin passwords. Keyed by the first 6 bytes of the
 *  repeater's pubkey, value is the null-terminated password (max 15 chars
 *  to match what sendLogin truncates to). Stored as a single NVS blob of
 *  up to 16 fixed-size records; an empty/cleared entry is removed from the
 *  blob. Pure UI convenience — the firmware doesn't look at this; only
 *  the touch admin login modal reads/writes it. */
constexpr int TOUCH_REPEATER_PW_MAX     = 16;
constexpr int TOUCH_REPEATER_PW_KEY_LEN = 6;
constexpr int TOUCH_REPEATER_PW_LEN     = 16;   // 15 chars + null
int  touchPrefsGetRepeaterPassword(const uint8_t* pub_key6, char* out, int out_cap);
bool touchPrefsSetRepeaterPassword(const uint8_t* pub_key6, const char* password);

/** Lock-screen wallpaper. Either an internal SPIFFS path (e.g.
 *  "/lock/placeholder.jpg") or an SD-card path prefixed with "sd:" (e.g.
 *  "sd:/walls/x.jpg"). The lockscreen falls back to the embedded placeholder
 *  if the file is missing or won't decode. Default = the placeholder path. */
constexpr int TOUCH_LOCK_WALLPAPER_MAXLEN = 128;
int  touchPrefsGetLockWallpaper(char* out, int out_cap);
bool touchPrefsSetLockWallpaper(const char* path);

/** Lock-screen text colour (clock + labels) as 0xRRGGBB. Default soft white. */
uint32_t touchPrefsGetLockTextColor();
bool     touchPrefsSetLockTextColor(uint32_t rgb);

/** Manual clock correction in whole hours, applied on top of the automatic
 *  (NTP / companion / mesh) time when rendering local time. Range -23..+23,
 *  default 0. Display-only — the RTC and mesh timestamps stay UTC. */
int  touchPrefsGetTimeOffsetHours();
bool touchPrefsSetTimeOffsetHours(int hours);

/** Build the POSIX TZ string for local-time display: the CET/CEST base plus the
 *  user's hour offset (see touchPrefsGetTimeOffsetHours). Used by both the
 *  boot-time setenv and the Wi-Fi NTP sync so the offset is honoured everywhere.
 *  `out` is always null-terminated when out_cap > 0. */
void touchPrefsBuildLocalTz(char* out, int out_cap);

/** First-boot setup wizard completion flag. false until the user finishes (or
 *  skips) the on-device setup flow (welcome → name → region → Wi-Fi); true
 *  thereafter so the wizard never reappears on subsequent boots. */
bool touchPrefsGetSetupDone();
bool touchPrefsSetSetupDone(bool done);

/** GPS serial baud override. The T-Deck Plus GPS runs at 38400; the older
 *  T-Deck v1.0 needs 9600. The build hard-codes GPS_BAUD_RATE, so this NVS
 *  override lets the user match their hardware without a rebuild. Read at GPS
 *  init; `fallback` is the compile-time default. Applied on reboot. */
uint32_t touchPrefsGetGpsBaud(uint32_t fallback);
bool     touchPrefsSetGpsBaud(uint32_t baud);

#endif
