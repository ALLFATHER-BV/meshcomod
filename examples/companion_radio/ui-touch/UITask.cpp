#include "UITask.h"

#include "../MyMesh.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#if defined(ESP32)
  #include <time.h>
  #include <SPIFFS.h>
  // Dedicated LittleFS instance for the map tile pack (separate
  // partition — see variants/heltec_v4/partitions_tft_touch.csv). Keeps
  // tiles out of the SPIFFS partition where /new_prefs + /contacts3 +
  // chat history live, so refreshing the tile pack no longer wipes the
  // operator's Profile / Radio settings.
  #include <LittleFS.h>
  #include <esp_heap_caps.h>
  #include <esp_system.h>
  #include <esp_sleep.h>   // esp_deep_sleep_start / ext0 wakeup for the power-off menu
  #include <driver/rtc_io.h>   // rtc_gpio_pullup_en — hold the wake pin's level in deep sleep
  #include "assets/lockscreen_placeholder_jpg.h"   // seeded to SPIFFS /lock/placeholder.jpg on first boot (PNG decode is broken on this board)
  #if defined(HAS_TDECK_GT911)
    #include "assets/lockscreen_wallpaper_rgb565.h"   // crisp pre-dithered default lock-screen wallpaper (T-Deck only; no JPEG banding)
  #endif
  #include <esp_timer.h>
  #include <esp_chip_info.h>
  #include <Esp.h>
  #include <esp_ota_ops.h>     // A/B slot info + reboot-to-recovery (esp_ota_get_running_partition)
  #include <esp_partition.h>   // find/erase otadata to fall back to the factory(recovery) slot
#endif
#if defined(HAS_TDECK_GT911)
  #include <SD.h>             // microSD (CS=39) on the shared LoRa SPI bus
  #include "sd_diskio.h"      // internal Arduino-SD drive helpers (sdcard_init / sd_*_raw)
  extern SPIClass* tdeckSharedSPI();
  // FatFs mkfs (the prebuilt ESP-IDF compiles f_setlabel OUT — FF_USE_LABEL=0 —
  // so the "MESHCOMOD" volume label is written by hand via sd_*_raw, below).
  extern "C" int f_mkfs(const char* path, uint8_t opt, uint32_t au, void* work, unsigned len);
  #ifndef MC_FM_FAT32
    #define MC_FM_FAT32 0x02  // FatFs f_mkfs option: force FAT32
  #endif
  #ifndef PIN_SD_CS
    #define PIN_SD_CS 39      // T-Deck microSD chip-select
  #endif
  #include <driver/i2s.h>     // T-Deck MAX98357A speaker amp (notification tones)
  // T-Deck I2S audio amp pins (MAX98357A, no MCLK). Overridable via build flags.
  #ifndef PIN_I2S_BCK
    #define PIN_I2S_BCK  7
  #endif
  #ifndef PIN_I2S_WS
    #define PIN_I2S_WS   5
  #endif
  #ifndef PIN_I2S_DOUT
    #define PIN_I2S_DOUT 6
  #endif
#endif
#include <Utils.h>
#include <LvglPsramAlloc.h>   // PSRAM-preferred alloc helpers for the map tile cache

#if defined(HAS_TOUCH_UI)
  #include <lvgl.h>
  #include <helpers/input/HeltecV4CapTouch.h>
  #if defined(HAS_TDECK_TRACKBALL)
    #include <helpers/input/TDeckTrackball.h>
  #endif
  #if defined(HAS_TDECK_KEYBOARD)
    #include <helpers/input/TDeckKeyboard.h>
  #endif
  #include "KeyboardLayouts.h"
  #include <helpers/ui/ST7789LCDDisplay.h>
  #include <helpers/AdvertDataHelpers.h>
  #include <helpers/sensors/LPPDataHelpers.h>
  #include <helpers/TouchDiagTrace.h>
  #if defined(ESP32)
    #include <Preferences.h>
    #include <helpers/esp32/TouchPrefsStore.h>
    #if defined(MULTI_TRANSPORT_COMPANION)
      #include <WiFi.h>
      #include <HTTPClient.h>
      #include <helpers/esp32/WifiRuntimeStore.h>
    #endif
  #endif
  extern ST7789LCDDisplay display;
#endif

constexpr unsigned long UI_REFRESH_MS = 250;
constexpr int UI_SORT_SCRATCH = UITask::MAX_UI_THREADS;
UIEventType g_last_event = UIEventType::none;

#if defined(ESP32)
namespace {
constexpr const char* k_ui_history_path = "/ui_chat_history_v1.bin";
constexpr uint32_t k_ui_history_magic = 0x55494348; // "UICH"
// Bump when the on-disk record layout changes. From v5 on, the header
// self-describes its record sizes (see loadHistoryFromStorage), so you can
// APPEND fields to the END of UiHistoryThread/UiHistoryMsg and OLD blobs still
// load — the appended tail reads back zero instead of the whole history being
// discarded. v4 added meta_flags/path_len/snr_q4/rssi per message.
constexpr uint16_t k_ui_history_version = 5;
// Oldest on-disk version we still load. v4's record layout matches the current
// build, so it reads fine via the sizeof() fallback below; anything older
// predates that layout and is discarded.
constexpr uint16_t k_ui_history_min_version = 4;

struct __attribute__((packed)) UiHistoryHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint16_t ui_msg_count;
  uint16_t ui_msg_head;
  uint32_t msgcount;
  int16_t active_thread_idx;
  uint8_t active_thread_is_channel;
  // v5+: on-disk record sizes at write time, so the loader can read a blob
  // whose records were SHORTER (fields appended since) and zero-fill the new
  // tail. Zero in a v4 blob -> loader falls back to sizeof(). Carved out of the
  // old _pad, so the header size is unchanged from v4.
  uint16_t thread_rec_size;
  uint16_t msg_rec_size;
  uint8_t _pad[1];
};

struct __attribute__((packed)) UiHistoryThread {
  uint8_t used;
  uint8_t channel;
  uint16_t unread;
  uint32_t last_ts;
  int16_t mesh_contact_idx;
  uint8_t mesh_contact_pub[32];
  uint8_t mesh_contact_key6[6];
  int16_t mesh_channel_slot;
  char name[UITask::MAX_THREAD_NAME + 1];
};

struct __attribute__((packed)) UiHistoryMsg {
  uint32_t ts;
  uint8_t channel;
  uint8_t outgoing;
  // v4 additions — RX metadata for the per-bubble Info popup. All zero when
  // the message was either outgoing or loaded from a pre-v4 blob (which
  // gets discarded outright on the version-mismatch check, so the latter
  // shouldn't happen in practice, but the zero defaults keep the Info
  // popup honest if it ever does).
  uint8_t meta_flags;
  uint8_t path_len;
  int8_t  snr_q4;
  int8_t  rssi;
  char thread[UITask::MAX_THREAD_NAME + 1];
  char sender[UITask::MAX_SENDER_NAME + 1];
  char text[UITask::MAX_MSG_TEXT + 1];
  // Append any FUTURE fields HERE (after text) and bump k_ui_history_version.
  // The v5+ loader zero-fills appended fields for older blobs, so appending
  // never wipes chat history. Inserting in the middle (as v4 did) breaks that
  // and must instead raise k_ui_history_min_version.
};
} // namespace
#endif

// ---- Shared by all companion_radio + ui-new builds (thread/mesh sync, prefs bits, loop counters) ----
constexpr uint8_t AUTO_ADD_OVERWRITE_OLDEST = (1u << 0);
constexpr uint8_t AUTO_ADD_CHAT             = (1u << 1);
constexpr uint8_t AUTO_ADD_REPEATER         = (1u << 2);
constexpr uint8_t AUTO_ADD_ROOM_SERVER      = (1u << 3);
constexpr uint8_t AUTO_ADD_SENSOR           = (1u << 4);

static bool hasContactPub(const uint8_t pub[32]) {
  static const uint8_t z[32] = {0};
  return memcmp(pub, z, sizeof(z)) != 0;
}
static bool hasContactKey6(const uint8_t key6[6]) {
  static const uint8_t z[6] = {0, 0, 0, 0, 0, 0};
  return memcmp(key6, z, sizeof(z)) != 0;
}

static uint32_t s_live_diag_loops = 0;

#if !defined(HAS_TOUCH_UI)
static void pushDiagLine(const char* message) { (void)message; }
#else
static void pushDiagLine(const char* message);   // real def near the diag log
#endif

// ============================================================
// LVGL TOUCH UI — Heltec V4 TFT Cap-Touch only
// ============================================================
#if defined(HAS_TOUCH_UI)

// ---- colour palette: TACTICAL (sober) ----
// Aimed at how *actual* fielded military comms equipment looks — Harris
// PRC handhelds, Codan terminals, Nett Warrior / MFOCS dashboards —
// rather than the Hollywood "amber + OD-green camo" look. Real military
// UIs are deliberately boring: dark neutral gray, clean white text, and
// colour reserved strictly for FUNCTIONAL status (amber = caution, green
// = OK, red = danger). The base palette here is all desaturated grays
// and one cool slate accent; the warm/saturated colours (the status
// amber + OD green + alert red) live in the status-color constants
// below and are used only where state really matters.
constexpr uint32_t COLOR_BG           = 0x000000;  // pure black, OLED-style
constexpr uint32_t COLOR_PANEL        = 0x040506;  // essentially black panel
// Earlier accent was 0x4E5C66 — RGB (78,92,102), cool blue-leaning. Even
// at low opacity that tinted every chip and settings row blue. Switched
// to a true neutral medium gray (very slight warm) so chip fills read as
// "darker gray" rather than "blue".
constexpr uint32_t COLOR_ACCENT       = 0x57585A;  // neutral gray
constexpr uint32_t COLOR_ACCENT_PRESS = 0x3A3B3D;  // darker neutral gray
constexpr uint32_t COLOR_TEXT         = 0xE0E3E6;  // clean off-white
constexpr uint32_t COLOR_SUB          = 0x828891;  // medium neutral gray
// Chat-bubble palette: kept near-monochrome (military comms terminals
// don't colour-code direction). Slight luminance + hue lean keeps L/R
// readable but neither side gets a "fun" tint.
constexpr uint32_t COLOR_SENT_BG      = 0x1D2226;  // very subtle steel
constexpr uint32_t COLOR_RECV_BG      = 0x1B1D1F;  // warmer neutral gray
constexpr uint32_t COLOR_MENTION      = 0x4FA3FF;  // blue — @mentions of me
constexpr uint32_t COLOR_MENTION_BG   = 0x16324F;  // blue-tinted bubble for a message that @mentions me
// Functional status colours — use sparingly. These are the ONLY warm/
// saturated colours in the palette, so when they appear the operator
// instantly reads them as state, not decoration.
constexpr uint32_t COLOR_STATUS_OK    = 0x4A8E4A;  // muted go-green
constexpr uint32_t COLOR_STATUS_WARN  = 0xC8A030;  // dim amber
constexpr uint32_t COLOR_STATUS_DANGER= 0xA04040;  // muted red

// LVGL 8.3 / Montserrat doesn't ship a STAR glyph. We carry a small custom
// font subset (one glyph: U+2605 BLACK STAR at size 28) in
// star_font_28.c — see &star_font_28 below. Pair the BIG macro with that
// font; the small ASCII macro stays as a plain '*' for places that still
// render in Montserrat (e.g. the action-sheet menu rows at font 12).
#define TOUCH_SYM_STAR     "*"             /* render in Montserrat at any size */
#define TOUCH_SYM_STAR_BIG "\xE2\x98\x85"  /* render ONLY with star_font_28    */
extern "C" const lv_font_t star_font_28;
extern "C" const lv_font_t star_font_14;

// Extras fallback fonts — em-dash (U+2014), ellipsis (U+2026), middle dot
// (U+00B7). LVGL's stock Montserrat subset doesn't include these, so any
// string carrying them renders missing-glyph rectangles (visible in the
// settings page, toasts, etc.). We carry tiny custom subsets at the three
// label sizes we use (12, 14, 16) and chain them as fallback fonts via
// initTouchFontFallbacks() — runs once at the top of UITask::begin after
// lv_init. Anywhere the touch UI references a Montserrat font, swap to
// &g_font_NN to pick up the fallback chain.
extern "C" const lv_font_t extras_12;
extern "C" const lv_font_t extras_14;
extern "C" const lv_font_t extras_16;
static lv_font_t g_font_12;
static lv_font_t g_font_14;
static lv_font_t g_font_16;
static void initTouchFontFallbacks() {
  g_font_12 = lv_font_montserrat_12;
  g_font_12.fallback = &extras_12;
  g_font_14 = lv_font_montserrat_14;
  g_font_14.fallback = &extras_14;
  g_font_16 = lv_font_montserrat_16;
  g_font_16.fallback = &extras_16;
}

// ---- T-Deck notification tones (I2S → MAX98357A speaker amp) ----
// Simple synthesized beeps for UI feedback (message arrived, etc). NOT file
// playback — the T-Deck's amp is driven over I2S; we generate a short sine
// burst on the fly. The legacy genericBuzzer (RTTTL on a digital pin) doesn't
// apply here — that's for boards with a piezo on a GPIO, which the touch boards
// don't have. Gated to the T-Deck; the V4 has no speaker at all.
#if defined(HAS_TDECK_GT911)
static constexpr int kI2sSampleRate = 16000;
static constexpr i2s_port_t kI2sPort = I2S_NUM_0;
// The tile fetcher's in-flight counter (defined later in the file). We skip
// beeping while tiles are downloading — the I2S DMA buffers + Wi-Fi RX DMA + a
// tile decode all contend for the scarce internal DMA RAM, and this build is
// already tight enough that tile downloads can OOM-reboot on their own.
extern volatile uint16_t s_tile_fetch_pending;

// I2S is installed ON DEMAND for the duration of a tone and uninstalled after.
// Holding the driver resident permanently kept ~2 KB of internal DMA RAM, which
// shrank the margin the tile-fetch worker relies on and made tile downloads
// OOM-reboot. Transient install keeps steady-state internal RAM untouched.
static bool tdeckAudioInstall() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = kI2sSampleRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;   // MAX98357A is mono
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  if (i2s_driver_install(kI2sPort, &cfg, 0, nullptr) != ESP_OK) return false;
  i2s_pin_config_t pins = {};
  pins.mck_io_num   = I2S_PIN_NO_CHANGE;
  pins.bck_io_num   = PIN_I2S_BCK;
  pins.ws_io_num    = PIN_I2S_WS;
  pins.data_out_num = PIN_I2S_DOUT;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(kI2sPort, &pins) != ESP_OK) { i2s_driver_uninstall(kI2sPort); return false; }
  return true;
}

// Render `freq` Hz for `ms` ms into the already-installed I2S as a 16-bit sine
// with a short fade-in/out so it doesn't click.
static void tdeckPlayToneRaw(int freq, int ms, int vol = 9000) {
  const int total = (kI2sSampleRate * ms) / 1000;
  const int fade = total / 8 > 0 ? total / 8 : 1;
  int16_t buf[128];
  int written_total = 0;
  double phase = 0.0;
  const double step = 2.0 * M_PI * (double)freq / (double)kI2sSampleRate;
  while (written_total < total) {
    int n = 0;
    for (; n < 128 && written_total < total; ++n, ++written_total) {
      double amp = (double)vol;
      if (written_total < fade)            amp *= (double)written_total / fade;
      else if (written_total > total-fade) amp *= (double)(total - written_total)/fade;
      buf[n] = (int16_t)(sin(phase) * amp);
      phase += step;
      if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
    }
    size_t bw = 0;
    i2s_write(kI2sPort, buf, n * sizeof(int16_t), &bw, pdMS_TO_TICKS(200));
  }
  i2s_zero_dma_buffer(kI2sPort);
}

static volatile bool s_notify_playing = false;
// The chime body: install I2S, play the two notes, uninstall. ~300 ms of blocking
// i2s_write + driver setup/teardown — run on its own throwaway task (below).
static void tdeckNotifyTaskFn(void* arg) {
  (void)arg;
  if (tdeckAudioInstall()) {
    tdeckPlayToneRaw(880, 90);    // A5
    tdeckPlayToneRaw(1318, 110);  // E6
    i2s_driver_uninstall(kI2sPort);
  }
  s_notify_playing = false;
  vTaskDelete(nullptr);
}

// Notification chime. Spawned on a short-lived task so the ~300 ms of I2S work
// does NOT freeze the UI — newMsgImpl (the caller) runs on the UI/mesh thread, so
// playing synchronously locked the screen for the whole chime. Skips while tiles
// download (DMA-RAM contention → reboot) and won't stack itself. Caller checks the
// sound pref.
static void tdeckPlayNotify() {
  if (s_tile_fetch_pending > 0) return;   // don't fight the Wi-Fi/tile DMA buffers
  if (s_notify_playing) return;           // already chiming — don't stack tasks/I2S
  s_notify_playing = true;
  if (xTaskCreate(tdeckNotifyTaskFn, "notify", 4096, nullptr, 3, nullptr) != pdPASS) {
    s_notify_playing = false;             // couldn't spawn (low DRAM) — skip the chime
  }
}
#endif  // HAS_TDECK_GT911

// ---- misc UI constants ----
constexpr int SWIPE_SCROLL_STEP  = 90;

// ---- Global status bar (lv_layer_sys → drawn above everything) ----
// Always-visible 240×22 strip across the top of the display. Left zone is
// dynamic (MESHCOMOD on Home tab, mail-icon + total-unread count on the
// other tabs). Right zone is a fixed stack: connection icon (WiFi or
// BLE, whichever is up), clock, battery percent, battery icon. The bar
// sits on lv_layer_sys so modals on lv_layer_top can't cover it, and the
// tabview / chat overlay are explicitly sized to leave the top 22 px
// clear.
constexpr lv_coord_t STATUSBAR_H = 22;
struct GlobalStatusBar {
  lv_obj_t* root;
  lv_obj_t* left_label;
  lv_obj_t* conn_icon;
  lv_obj_t* clock;
  lv_obj_t* batt_pct;
  lv_obj_t* batt_icon;
  lv_obj_t* layout_label;   // EN / BG indicator, right side
};
static GlobalStatusBar g_statusbar = {};
static void updateGlobalStatusBar();   // fwd decl, called from refresh tick

// Active chat thread name, surfaced in the status bar's left zone. The in-chat
// header bar (back button + name) was removed to give the conversation the full
// height — like the Terminal/Files views, the chat name lives in the status bar
// and a small floating HOME button handles exit. Empty = no chat open.
static char s_chat_title[40] = {0};
static void setChatStatusTitle(const char* sanitized) {   // already glyph-sanitized name, or nullptr to clear
  if (sanitized && sanitized[0]) {
    strncpy(s_chat_title, sanitized, sizeof(s_chat_title) - 1);
    s_chat_title[sizeof(s_chat_title) - 1] = '\0';
  } else {
    s_chat_title[0] = '\0';
  }
  updateGlobalStatusBar();
}
/** Bottom tab indices. Tabs: Home=0, Chats=1, Contacts=2, Map=3, Set=4. */
constexpr int CHAT_INBOX_TAB_INDEX   = 1;
constexpr int CONTACTS_TAB_INDEX     = 2;
constexpr int MAP_TAB_INDEX          = 3;
constexpr int SETTINGS_TAB_INDEX     = 4;
constexpr int TAB_LAST               = 4;

// ---- Chat overlay layout ----
constexpr int CHAT_HDR_H       = 0;    // in-chat header bar removed; thread name shows in the status bar
constexpr int CHAT_COMP_H      = 34;   // composer row (slimmed 50 → 40 → 34; hugs the 30px textbox)
constexpr int CHAT_KB_H        = 130;  // on-screen keyboard (portrait)
// Bottom tab-bar height (matches lv_tabview_create in buildUiTree). A tab
// page's usable content area is the screen minus the status bar and tab bar —
// queried live so it tracks the current rotation (240×260 portrait /
// 320×180 landscape).
constexpr int TABBAR_H = 30;   // bottom nav bar (trimmed from 38; icons stay g_font_16)
static inline lv_coord_t tabContentW() { return lv_disp_get_hor_res(nullptr); }
static inline lv_coord_t tabContentH() { return lv_disp_get_ver_res(nullptr) - STATUSBAR_H - TABBAR_H; }
// Usable area for a centered modal below the global status bar (small margin).
// Popups were sized for the 320-tall portrait screen; these let them shrink to
// fit the 240-tall landscape one. Backdrops should use the full screen so the
// dark overlay covers everything and the card centers on-screen.
static inline lv_coord_t modalAvailW() { return lv_disp_get_hor_res(nullptr) - 12; }
static inline lv_coord_t modalAvailH() { return lv_disp_get_ver_res(nullptr) - STATUSBAR_H - 12; }
// Chat-detail geometry — runtime so the conversation view fills the screen and
// tracks rotation. The keyboard is half the screen tall in landscape (matches
// kbApplyLayoutForRotation), full CHAT_KB_H in portrait.
static inline bool       chatLandscape() { return lv_disp_get_hor_res(nullptr) > lv_disp_get_ver_res(nullptr); }
static inline lv_coord_t chatScreenW()   { return lv_disp_get_hor_res(nullptr); }
static inline lv_coord_t chatScreenH()   { return lv_disp_get_ver_res(nullptr) - STATUSBAR_H; }
static inline lv_coord_t chatKbH()       { return chatLandscape() ? (lv_disp_get_ver_res(nullptr) / 2) : CHAT_KB_H; }
// without keyboard:
static inline lv_coord_t chatMsgHOpen()  { return chatScreenH() - CHAT_HDR_H - CHAT_COMP_H; }
static inline lv_coord_t chatCompYOpen() { return chatScreenH() - CHAT_COMP_H; }
// with keyboard shown:
static inline lv_coord_t chatMsgHKb()    { return chatScreenH() - CHAT_HDR_H - CHAT_COMP_H - chatKbH(); }
static inline lv_coord_t chatCompYKb()   { return chatScreenH() - CHAT_COMP_H - chatKbH(); }

// ---- Thread list button context ----
struct LvThreadButtonCtx { int idx; bool channel; };

// ---- Per-panel state (DM chats and channels share this shape) ----
struct LvChatPanel {
  lv_obj_t* list_cont;     // full-page lv_list in the tab (thread chooser)
  lv_obj_t* overlay;       // full-screen overlay on lv_scr_act (chat detail)
  lv_obj_t* header_name;   // label: thread name in the overlay header
  lv_obj_t* msgs;          // read-only textarea: message history
  lv_obj_t* composer_row;  // container: input + send button
  lv_obj_t* composer_ta;   // textarea: user types here
  LvThreadButtonCtx ctx_store[UITask::MAX_UI_THREADS];
  bool channel_mode;
  /** When true, `list_cont` shows channels + DMs with history (Chats tab only). */
  bool inbox_combined;
  bool detail_open;
};

struct LvContactButtonCtx {
  uint32_t mesh_idx;
  bool     is_repeater;
};

static LvContactButtonCtx s_contacts_ctx[128];

// ---- Contact list sort modes (cycled via short-press on the active filter) ----
enum ContactsSortMode : uint8_t {
  CONTACTS_SORT_AZ          = 0,  // alphabetical
  CONTACTS_SORT_LAST_HEARD  = 1,  // most recent advert first (default)
  CONTACTS_SORT_LAST_MSG    = 2,  // most recent message first (falls back to last-heard)
  CONTACTS_SORT_COUNT       = 3,
};
// Default to most-recently-heard at the top — operators care who's on the
// air now far more than alphabetical order. Favorites still pin above all
// (see the qsort comparator in refreshContactsList).
static uint8_t g_contacts_sort = CONTACTS_SORT_LAST_HEARD;

static const char* contactsSortLabel(uint8_t m) {
  switch (m) {
    case CONTACTS_SORT_AZ:         return "A-Z";
    case CONTACTS_SORT_LAST_HEARD: return "RECENT";
    case CONTACTS_SORT_LAST_MSG:   return "MSG";
  }
  return "?";
}
/** Last tab index before `tabChangedCb`; used to close chat overlays only when leaving Chats. */
static int s_lv_tab_prev = 0;

// ---- Top-level LVGL state ----
struct LvUiState {
  bool ready;
  bool touch_inited;
  bool dirty_threads;
  bool dirty_timeline;
  bool defer_heavy_refresh;
  unsigned long heavy_refresh_at_ms;
  uint32_t lvgl_tick_prev_us;
  lv_disp_draw_buf_t draw_buf;
  lv_disp_drv_t      disp_drv;
  lv_indev_drv_t     indev_drv;
  lv_obj_t* tabview;
  lv_obj_t* home_state;
  lv_obj_t* home_stats;
  // Duty-cycle meter (label + bar) on the Home tab. Surfaces the live TX
  // budget remaining so the operator notices regulatory throttling before
  // a ten-message-burst stalls. Created in makeHome iff the user pref is on.
  lv_obj_t* home_dc_label;
  lv_obj_t* home_dc_bar;
  lv_obj_t* settings_status;
  lv_obj_t* diag_id_label;   // pinned ID/freq/sf line above the diag ring
  lv_obj_t* diag_label;
  lv_obj_t* keyboard;     // shared LVGL on-screen keyboard
  LvChatPanel dm;          // Chats tab list + DM overlay
  LvChatPanel ch;          // channel thread overlay only (no tab list)
  lv_obj_t* contacts_list;
  uint8_t   contacts_filter;  // 0=all 1=repeaters 2=peers (non-repeater) 3=favorites
  // Per-session text-search filter applied on top of the category filter.
  // Empty = no text filter. Set/cleared by the magnifier sheet.
  char      contacts_search[24];
  lv_obj_t* contacts_search_indicator;  // tiny chip shown when search is active
  UITask* task;
};

// ---- Diagnostics ring buffer ----
constexpr int DIAG_LINES = 16;   // bumped from 7 so dispatcher TX traces survive the UI flow
constexpr int DIAG_COLS  = 44;
static char     s_diag_ring[DIAG_LINES][DIAG_COLS];
static int      s_diag_line  = 0;
// Sticky copy of the latest "ID …" identity/radio diag line. MyMesh emits
// this during begin() — before the Diag tab's labels exist — so we cache
// the most recent value and back-fill the pinned label when the tab is
// constructed.
static char     s_diag_id_pinned[DIAG_COLS] = { 0 };
constexpr int RXLOG_LINES = 28;
constexpr int RXLOG_COLS  = 80;
static char s_rxlog_ring[RXLOG_LINES][RXLOG_COLS];
static int s_rxlog_line = 0;
static char s_rawlog_ring[RXLOG_LINES][RXLOG_COLS];
static int s_rawlog_line = 0;
static lv_obj_t* s_live_diag_label = nullptr;
static uint32_t  s_live_diag_reads = 0;
static uint32_t  s_live_diag_pressed = 0;
static uint32_t  s_live_diag_tap_edges = 0;
static unsigned long s_live_diag_next_ms = 0;
/** Keep live diag overlay available but hidden by default; flip true for field debugging. */
constexpr bool k_show_live_diag_overlay = false;

#if defined(HAS_TDECK_TRACKBALL)
// ---- T-Deck trackball cursor ----
// A soft pointer the trackball drives; auto-hides after inactivity, and the
// centre click injects a touch press at the cursor (see lvglTouchRead / loop).
static lv_obj_t*     s_tb_cursor = nullptr;
static int           s_tb_cursor_x = 160;   // rendered position (landscape 320x240)
static int           s_tb_cursor_y = 120;
static float         s_tb_target_x = 160.0f, s_tb_target_y = 120.0f;  // full-sensitivity target
static float         s_tb_render_x = 160.0f, s_tb_render_y = 120.0f;  // eased render position
static unsigned long s_tb_prev_ms = 0;
static unsigned long s_tb_last_active_ms = 0;
static bool          s_tb_click_press = false;     // centre button held this frame
constexpr int           kTbCursorDiameter = 16;    // px
constexpr int           kTbCursorStepPx   = 12;    // px per encoder step
constexpr float         kTbCursorSmoothMs = 60.0f; // ease time-constant (smaller = snappier)
constexpr unsigned long kTbCursorHideMs   = 800;   // auto-hide after idle

// True while the trackball cursor is visible AND screen-Y is within the bottom
// tab bar. Used to swallow stray FINGER touches on the tab bar while the user is
// driving the cursor — a trackball CLICK on a tab still works (it arrives via
// the cursor path, not as a raw touch).
static bool tbFingerTouchOnTabBarBlocked(uint16_t y) {
  if (!s_tb_cursor) return false;
  if ((millis() - s_tb_last_active_ms) >= kTbCursorHideMs) return false;  // cursor hidden
  return (int)y >= (lv_disp_get_ver_res(nullptr) - TABBAR_H);
}
#endif

// ---- Panel currently linked to the keyboard (or nullptr) ----
static LvChatPanel* s_kb_panel = nullptr;
/** Full-width strip above the on-screen keyboard: mirrors the focused textarea so edits stay visible. */
static lv_obj_t* s_kb_mirror_root = nullptr;
static lv_obj_t* s_kb_mirror_ta = nullptr;
/** Real textarea whose text is synced with `s_kb_mirror_ta` while the keyboard is open. */
static lv_obj_t* s_kb_bind_ta = nullptr;

// ---- Chats "+" add-channel modal pointers (see lower in file for impl) ----
static lv_obj_t* s_addch_sheet      = nullptr;
static lv_obj_t* s_addch_name_ta    = nullptr;
static lv_obj_t* s_addch_secret_ta  = nullptr;
static lv_obj_t* s_addch_hashtag_ta = nullptr;
static lv_obj_t* s_addch_error_l    = nullptr;
// ---- Contacts → "Add" manual contact modal pointers ----
static lv_obj_t* s_addct_pub_ta  = nullptr;
static lv_obj_t* s_addct_name_ta = nullptr;
static lv_obj_t* s_addct_error_l = nullptr;

// ---- Keyboard rotation (landscape typing) ----
// Stored as the lv_disp_rot_t value: 0 = portrait, 1 = ROT_90, 3 = ROT_270.
// Applied to the whole display while the keyboard is shown; reverts to
// portrait on hideKb(). Persisted in NVS under "kbrot" so the user's choice
// survives reboots.
static uint8_t   s_kb_rotation       = 0;
static lv_obj_t* s_kb_rot_left_btn   = nullptr;
static lv_obj_t* s_kb_rot_right_btn  = nullptr;

// ---- Global UI orientation (persistent) ----
// The base orientation of the whole UI, loaded from NVS ("uirot") and applied
// at boot BEFORE the screens are built so the layout reflows to the rotated
// resolution. The keyboard-landscape toggle above is transient and layers on
// top of this; hideKb() reverts to s_ui_rotation (not hard portrait).
// Stored as the lv_disp_rot_t value: 0 = portrait, 1 = ROT_90, 3 = ROT_270.
static uint8_t   s_ui_rotation       = 0;

// The rotation the keyboard should use when shown: if the UI is already in a
// landscape base orientation, keep it; otherwise honour the transient
// keyboard-landscape toggle. Lets the rotate arrows still work in portrait.
static inline uint8_t effectiveKbRotation() {
  const bool ui_landscape =
      (s_ui_rotation == LV_DISP_ROT_90 || s_ui_rotation == LV_DISP_ROT_270);
  return ui_landscape ? s_ui_rotation : s_kb_rotation;
}

// ---- Discovered adverts ring buffer ----
// Filled by UITask::discoveredContact() each time an advert comes in for a
// node that's not in contacts[]. Browsable + addable via the Discovered
// modal opened from the Contacts tab "Found" button.
constexpr int DISCOVERED_MAX = 24;
struct LvDiscoveredEntry {
  bool used;
  bool in_contacts;
  uint8_t path_len;
  uint32_t last_advert_ts;
  uint32_t recv_ms;
  ContactInfo ci;
};
// PSRAM-first allocator (falls back to internal RAM) for moving large static UI buffers off
// the scarce internal SRAM. Safe as a static initializer: PSRAM is up before C++ ctors run,
// and the fallback keeps the original internal behaviour if PSRAM is ever unavailable.
static void* psAlloc(size_t n) {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (!p) p = heap_caps_malloc(n, MALLOC_CAP_8BIT);
    return p;
}
static LvDiscoveredEntry* s_discovered = (LvDiscoveredEntry*)psAlloc(sizeof(LvDiscoveredEntry) * DISCOVERED_MAX);
static uint32_t s_discovered_seq = 0;

// ---- Touch-init deferred flag ----
static bool g_cap_touch_hw_started = false;

// ---- LVGL draw buffer ----
// 240x24 RGB565 = 11,520 bytes. Allocated in PSRAM at UITask::begin() so the
// internal DRAM stays free for WiFi DMA buffers (esp_wifi_init needs ~50 KB
// of DRAM; with the buffer + LVGL widgets in DRAM the device was OOM'ing as
// soon as WiFi came up). PSRAM is slower than DRAM but the Adafruit ST7789
// SPI driver reads the buffer linearly which the cache handles well.
//
// 1.5x the original 240x16 size — modest bump to reduce setAddrWindow
// round-trips without giving LVGL more headroom to over-invalidate.
// Double-buffering and 240x40 were tried and starved the CHSC6X poll cadence
// (touch I2C is read from inside lv_timer_handler at LV_INDEV_DEF_READ_PERIOD
// = 30 ms, and bigger render windows pushed effective poll rate to ~11 Hz).
// 48 lines × 240 × 2 B = 23 KB. Lives in internal DMA-capable DRAM (see
// allocator below) which reads ~3× faster than PSRAM during SPI push.
// Doubled from 24 to amortize the per-flush setAddrWindow + startWrite/
// endWrite overhead. Tried double-buffering (two 23 KB internal blocks)
// — bootlooped after the top bar showed, because the second 23 KB block
// starved Bluedroid's DMA-DRAM pool at init time. Stayed single-buffer.
// Was 48 — dropped to 24 to claw back ~12 KB of internal DMA-DRAM for
// the Wi-Fi tile-fetch task stack (which has to live in internal RAM
// because mbedTLS/lwIP crash on PSRAM stacks under this Arduino-ESP32
// build). 24 × 240 × 2 B = 11.5 KB. Flush time is bound by SPI clock
// (80 MHz), not memcpy speed, so the throughput delta is small —
// LVGL just calls flush_cb more often.
static constexpr int LV_DRAW_BUF_LINES = 24;
static lv_color_t* g_draw_buffer = nullptr;

// ---- Global UI state instance ----
LvUiState g_lv = {};

/** If `cp` has no glyph in `font`, show '*' instead of LVGL's missing-glyph box. Preserves \\n \\r \\t. */
static bool uiFontHasGlyph(const lv_font_t* font, uint32_t cp) {
  if (!font) return true;
  if (cp == '\n' || cp == '\r' || cp == '\t') return true;
  if (cp < 0x20u) return true;
  lv_font_glyph_dsc_t dsc{};
  return lv_font_get_glyph_dsc(font, &dsc, cp, 0);
}

/**
 * Copy UTF-8 `in` → `out` (NUL-terminated, capped). Codepoints not in `font` become ASCII '*'.
 * Invalid UTF-8 bytes become '*'.
 */
static void copyUtf8ReplacingMissingGlyphs(const lv_font_t* font, char* out, size_t out_cap, const char* in) {
  if (!out || out_cap == 0) return;
  out[0] = '\0';
  if (!in) return;
  size_t      w   = 0;
  const char* p   = in;
  const char* end = in + strlen(in);

  while (*p && w + 1 < out_cap) {
    const char*     seq_beg = p;
    uint32_t        cp      = 0;
    bool             ok      = false;

    const unsigned char c0 = static_cast<unsigned char>(*p);
    if (c0 < 0x80u) {
      cp = c0;
      p += 1;
      ok = true;
    } else if ((c0 & 0xE0u) == 0xC0u && end - p >= 2) {
      const unsigned char c1 = static_cast<unsigned char>(p[1]);
      if ((c1 & 0xC0u) == 0x80u) {
        cp = (static_cast<uint32_t>(c0 & 0x1Fu) << 6) | (c1 & 0x3Fu);
        if (cp >= 0x80u) {
          p += 2;
          ok = true;
        }
      }
    } else if ((c0 & 0xF0u) == 0xE0u && end - p >= 3) {
      const unsigned char c1 = static_cast<unsigned char>(p[1]);
      const unsigned char c2 = static_cast<unsigned char>(p[2]);
      if ((c1 & 0xC0u) == 0x80u && (c2 & 0xC0u) == 0x80u) {
        cp = (static_cast<uint32_t>(c0 & 0x0Fu) << 12) |
             (static_cast<uint32_t>(c1 & 0x3Fu) << 6) | (c2 & 0x3Fu);
        if (cp >= 0x800u && !(cp >= 0xD800u && cp <= 0xDFFFu)) {
          p += 3;
          ok = true;
        }
      }
    } else if ((c0 & 0xF8u) == 0xF0u && end - p >= 4) {
      const unsigned char c1 = static_cast<unsigned char>(p[1]);
      const unsigned char c2 = static_cast<unsigned char>(p[2]);
      const unsigned char c3 = static_cast<unsigned char>(p[3]);
      if ((c1 & 0xC0u) == 0x80u && (c2 & 0xC0u) == 0x80u && (c3 & 0xC0u) == 0x80u) {
        cp = (static_cast<uint32_t>(c0 & 0x07u) << 18) |
             (static_cast<uint32_t>(c1 & 0x3Fu) << 12) |
             (static_cast<uint32_t>(c2 & 0x3Fu) << 6) | (c3 & 0x3Fu);
        if (cp >= 0x10000u && cp <= 0x10FFFFu) {
          p += 4;
          ok = true;
        }
      }
    }

    if (!ok) {
      out[w++] = '*';
      p = seq_beg + 1;
      continue;
    }

    if (uiFontHasGlyph(font, cp)) {
      for (const char* q = seq_beg; q < p && w + 1 < out_cap; ++q) out[w++] = *q;
    } else {
      out[w++] = '*';
    }
  }
  out[w] = '\0';
}

enum class SettingsModalKind : uint8_t {
  None = 0,
  Profile,
  Radio,
  AutoAdd,
  Experimental,
  Bluetooth,    // was Transport — replaced with a dedicated BLE page
  Device,
  Wifi,
  Log,
  Discovered,   // discovered adverts list (manual-add view)
  Advert,       // "send advert" picker (flood vs zero-hop)
  ChCreatePrv,  // Chats "+": create a private channel (name + secret)
  ChJoinPrv,    // Chats "+": join a private channel by secret
  ChJoinTag,    // Chats "+": join a hashtag channel by name
  SystemInfo,   // Read-only system / firmware diagnostic page
  AddContact,   // Manual add-contact (pubkey + name) modal
  QuickReply,   // Edit the 6 quick-reply preset macros
};

struct SettingsModalState {
  lv_obj_t* root;
  SettingsModalKind kind;
  lv_obj_t* name_ta;
  lv_obj_t* lat_ta;
  lv_obj_t* lon_ta;
  lv_obj_t* freq_ta;
  lv_obj_t* bw_ta;
  lv_obj_t* sf_ta;
  lv_obj_t* cr_ta;
  lv_obj_t* tx_ta;
  lv_obj_t* airtime_ta;
  lv_obj_t* max_hops_ta;
  lv_obj_t* auto_chat_sw;
  lv_obj_t* auto_rep_sw;
  lv_obj_t* auto_room_sw;
  lv_obj_t* auto_sensor_sw;
  lv_obj_t* auto_overwrite_sw;
  lv_obj_t* manual_add_sw;
  lv_obj_t* share_loc_sw;
  lv_obj_t* path_mode_dd;
  lv_obj_t* exp_multi_sw;
  lv_obj_t* exp_repeat_sw;
  lv_obj_t* exp_boost_sw;
  lv_obj_t* exp_dc_sw;
  lv_obj_t* wifi_sw;
  lv_obj_t* wifi_ssid_ta;
  lv_obj_t* wifi_pwd_ta;
  /** Transports modal: live STA line (IP when connected, else Arduino WiFi status). */
  lv_obj_t* wifi_sta_status_l;
  lv_obj_t* radio_preset_dd;
  lv_obj_t* log_view_ta;
  lv_obj_t* log_rx_btn;
  lv_obj_t* log_raw_btn;
  uint8_t log_mode; // 0 = rx summary, 1 = raw
  /** Device modal: screen-timeout (seconds, 0=never) textarea. */
  lv_obj_t* screen_to_ta;
  /** Device modal: live GPS fix-status line under the GPS toggle. */
  lv_obj_t* gps_status;
  /** Quick replies modal: 6 textareas, one per slot. Empty save clears
   *  the slot (so the picker shows "(empty)" and a save with text repopulates).
   *  Allocated only while the modal is open. */
  lv_obj_t* qr_tas[TOUCH_QUICK_REPLY_COUNT];
};
static SettingsModalState g_set_modal = {};

/** Mirrors `Meshcomod-client/shared/radio_presets.ts` (community presets); keep in sync manually. */
struct MeshRadioPreset {
  const char* label;
  float       freq_mhz;
  float       bw_khz;
  uint8_t     sf;
  uint8_t     cr;
  int8_t       tx_dbm;
  uint8_t      airtime_limit_pct;  // web % → NodePrefs.airtime_factor = pct / 100
};

static constexpr size_t k_mesh_radio_preset_count = 18;
static const MeshRadioPreset k_mesh_radio_presets[k_mesh_radio_preset_count] = {
    {"Australia", 915.8f, 250.f, 10, 5, 22, 10},
    {"Australia (Narrow)", 916.575f, 62.5f, 7, 8, 22, 10},
    {"Australia: SA, WA", 923.125f, 62.5f, 8, 8, 22, 10},
    {"Australia: QLD", 923.125f, 62.5f, 8, 5, 22, 10},
    {"Australia: NSW (SF11)", 915.8f, 250.f, 11, 5, 22, 10},
    {"New Zealand", 917.375f, 250.f, 11, 5, 22, 10},
    {"New Zealand (Narrow)", 917.375f, 62.5f, 7, 5, 22, 10},
    {"EU/UK (Narrow)", 869.618f, 62.5f, 8, 8, 22, 10},
    {"EU/UK (Deprecated)", 869.525f, 250.f, 11, 5, 22, 10},
    {"Switzerland (Narrow)", 869.618f, 62.5f, 8, 8, 22, 10},
    {"Czech Republic (Narrow)", 869.432f, 62.5f, 7, 5, 22, 10},
    {"Slovakia (Narrow)", 869.618f, 62.5f, 8, 8, 22, 10},
    {"EU 433MHz (Long Range)", 433.65f, 250.f, 11, 5, 22, 10},
    {"US915", 915.f, 250.f, 11, 5, 22, 10},
    {"USA: SoCal (Community)", 927.875f, 62.5f, 7, 5, 22, 10},
    {"USA: San Francisco (Community)", 910.525f, 62.5f, 7, 5, 22, 10},
    {"USA: Sacramento (Community)", 909.875f, 62.5f, 9, 5, 22, 10},
    {"USA: Pacific NW (Community)", 910.525f, 62.5f, 7, 5, 22, 10},
};

static bool g_radio_preset_cb_silent = false;

static void kbMirrorSyncToReal();  // keyboard mirror strip; defined below

static int findMatchingMeshRadioPreset(const NodePrefs* prefs) {
  if (!prefs) return -1;
  for (size_t i = 0; i < k_mesh_radio_preset_count; ++i) {
    const MeshRadioPreset& p = k_mesh_radio_presets[i];
    if (std::fabs(prefs->freq - p.freq_mhz) > 0.002) continue;
    if (std::fabs(prefs->bw - p.bw_khz) > 0.02) continue;
    if (prefs->sf != p.sf) continue;
    if (prefs->cr != p.cr) continue;
    if (prefs->tx_power_dbm != p.tx_dbm) continue;
    const double exp_af = static_cast<double>(p.airtime_limit_pct) / 100.0;
    if (std::fabs(static_cast<double>(prefs->airtime_factor) - exp_af) > 0.05) continue;
    return static_cast<int>(i);
  }
  return -1;
}

static void applyMeshRadioPresetFields(unsigned preset_idx) {
  if (preset_idx >= k_mesh_radio_preset_count || !g_set_modal.freq_ta) return;
  kbMirrorSyncToReal();
  const MeshRadioPreset& p = k_mesh_radio_presets[preset_idx];
  char buf[28];
  snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(p.freq_mhz));
  lv_textarea_set_text(g_set_modal.freq_ta, buf);
  snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(p.bw_khz));
  lv_textarea_set_text(g_set_modal.bw_ta, buf);
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(p.sf));
  lv_textarea_set_text(g_set_modal.sf_ta, buf);
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(p.cr));
  lv_textarea_set_text(g_set_modal.cr_ta, buf);
  snprintf(buf, sizeof(buf), "%d", static_cast<int>(p.tx_dbm));
  lv_textarea_set_text(g_set_modal.tx_ta, buf);
  const float af = static_cast<float>(p.airtime_limit_pct) / 100.0f;
  snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(af));
  lv_textarea_set_text(g_set_modal.airtime_ta, buf);
}

static void radioPresetChangedCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || g_radio_preset_cb_silent) return;
  if (!g_set_modal.radio_preset_dd || !g_set_modal.freq_ta) return;
  const uint16_t sel = lv_dropdown_get_selected(g_set_modal.radio_preset_dd);
  if (sel == 0) return;
  const unsigned idx = static_cast<unsigned>(sel - 1u);
  if (idx >= k_mesh_radio_preset_count) return;
  applyMeshRadioPresetFields(idx);
}

/** Subtitle labels on Set tab section rows (titles are static; see `refreshSettingsSectionSubtitles`). */
enum : int {
  SEC_PROFILE = 0,
  SEC_RADIO,
  SEC_AUTOADD,
  SEC_BLUETOOTH,   // was SEC_TRANSPORT — replaced with a dedicated BLE page
  SEC_DEVICE,
  SEC_EXPERIMENTAL,
  SEC_WIFI,
  SEC_LOG,
  SEC_SYSTEM,
  SEC_QUICKREPLY,  // user-editable composer macro presets
  SEC_COUNT
};
static lv_obj_t* g_set_sec_sub[SEC_COUNT];

/** Settings list rows: full width inside tab padding (240 − 16). */
static constexpr lv_coord_t k_settings_row_w   = 224;
static constexpr lv_coord_t k_settings_row_h   = 54;
static constexpr lv_coord_t k_settings_row_gap = 6;

static bool settingsModalIsOpen() { return g_set_modal.root != nullptr; }

// Compact one-line GPS status for the Device settings panel + control center.
// "GPS: off" / "GPS: searching · N sats" / "GPS: fix · N sats  <lat>, <lon>".
static const char* gpsStatusStr() {
  static char s[72];
  if (!g_lv.task || !g_lv.task->getGPSState()) { snprintf(s, sizeof s, "GPS: off"); return s; }
  // satellitesCount() is satellites USED IN THE FIX (0 until a lock), so during
  // cold acquisition it stays 0 — show "acquiring…" rather than a misleading
  // "0 sats". On lock, show the sat count + the (auto-populated) coordinates.
  if (!g_lv.task->getGpsFix()) { snprintf(s, sizeof s, "GPS: acquiring..."); return s; }
  const int sats = g_lv.task->getGpsSats();
  int n = snprintf(s, sizeof s, "GPS: fix");
  if (sats >= 0 && n < (int)sizeof s) n += snprintf(s + n, sizeof s - n, " · %d sats", sats);
  if (n < (int)sizeof s)
    snprintf(s + n, sizeof s - n, "  %.5f, %.5f",
             g_lv.task->getNodeLat(), g_lv.task->getNodeLon());
  return s;
}

// Control-center popup state — declared up here so the periodic settings refresh
// (which is defined above the control-center code) can update the live GPS line.
static lv_obj_t* s_cc_root      = nullptr;
static lv_obj_t* s_cc_gps_label = nullptr;
static void closeControlCenter();   // defined in the control-center section below
static lv_obj_t* s_power_menu   = nullptr;   // power off / reboot menu (control center)
static void closePowerMenu();               // defined in the control-center section below
#if defined(HAS_TDECK_KEYBOARD)
// Keyboard backlight: mode 0=off, 1=on, 2=auto (on while typing, off after idle).
static uint8_t       s_kb_bl_mode    = 2;
static unsigned long s_kb_last_key_ms = 0;
constexpr unsigned long kKbBacklightIdleMs = 3000;   // auto: off 3 s after last key
// Register input activity (keypress / field focus / tap) for the auto backlight.
static inline void noteKbActivity() { s_kb_last_key_ms = millis(); }
#else
static inline void noteKbActivity() {}
#endif

/** List-style settings launcher (not theme-default `lv_btn` chrome). */
static void styleSettingsRow(lv_obj_t* row) {
  // Matches the same low-opacity neutral-slate look as styleButton so the
  // settings rows don't read as a different visual language than the
  // chips / sheet buttons elsewhere. Earlier hardcoded blue-slate hexes
  // (0x141C27 / 0x2A3848) predated the tactical palette overhaul.
  lv_obj_remove_style_all(row);
  lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(row, LV_OPA_0, LV_PART_MAIN);     // fully transparent at rest
  lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_ACCENT_PRESS), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, LV_OPA_30, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_radius(row, 6, LV_PART_MAIN);     // sharper than 14
  lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(row, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_set_style_border_opa(row, LV_OPA_30, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_ofs_y(row, 0, LV_PART_MAIN);
}

// ============================================================
// Style helpers
// ============================================================
static void styleSurface(lv_obj_t* obj, uint32_t bg, lv_coord_t radius = 10) {
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(obj, lv_color_hex(bg), LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
}

static void styleCard(lv_obj_t* obj) {
  styleSurface(obj, COLOR_PANEL, 10);
  lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(obj, lv_color_hex(0x18191A), LV_PART_MAIN);
}

static void styleButton(lv_obj_t* obj) {
  // Subdued slate chip. Background sits at the panel colour with a low
  // overall opacity so the pure-black BG bleeds through — chips read as
  // "etched out of the bezel" rather than glowing. Border is a faint
  // 1-px line of COLOR_ACCENT at low opacity, text is clean white-ish.
  // Press state flashes a brighter slate fill so taps still register.
  // Primary action buttons (Send / Save / Login / Apply / Add) override
  // the bg to COLOR_STATUS_OK so they remain visually distinct.
  lv_obj_set_style_bg_color(obj, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(obj, lv_color_hex(COLOR_ACCENT_PRESS), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(obj, LV_OPA_50, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_border_color(obj, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
  lv_obj_set_style_border_opa(obj, LV_OPA_40, LV_PART_MAIN);
  lv_obj_set_style_text_color(obj, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_radius(obj, 4, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
}

// "X" close affordance for popup cards. A bare 16-px glyph with an
// invisible 32×32 tap target so it reads as just a symbol but is forgiving
// to hit on a 240-px touch screen. Sits flush inside the card's top-right
// corner — no border / bg, so it doesn't compete visually with the card's
// own buttons.
// IMPORTANT: each caller is responsible for keeping the top-right ~32×32
// of the card content-free (or for placing only short titles there) so
// the X doesn't sit on top of a real button.
static void addCloseXBadge(lv_obj_t* card, lv_event_cb_t cb, void* user_data = nullptr) {
  lv_obj_t* x = lv_obj_create(card);
  lv_obj_remove_style_all(x);
  lv_obj_set_size(x, 32, 32);
  // Flush to top-right of the card. No outward offset — keeps the badge
  // entirely on top of the card so it can't clip behind the modal backdrop.
  lv_obj_align(x, LV_ALIGN_TOP_RIGHT, 0, 0);
  // No fill / border — bare glyph. Pressed state nudges a faint dim so
  // there's *some* visual feedback on tap.
  lv_obj_set_style_bg_opa(x, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(x, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(x, LV_OPA_20, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_radius(x, 16, LV_PART_MAIN);
  lv_obj_set_style_border_width(x, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(x, 0, LV_PART_MAIN);
  lv_obj_add_flag(x, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(x, LV_OBJ_FLAG_FLOATING);
  lv_obj_add_flag(x, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_clear_flag(x, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_foreground(x);
  lv_obj_add_event_cb(x, cb, LV_EVENT_CLICKED, user_data);
  lv_obj_t* lbl = lv_label_create(x);
  lv_label_set_text(lbl, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_font(lbl, &g_font_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  // Anchor the glyph itself to the TOP-RIGHT of the (32×32) tap area —
  // centering it made the X look like it was floating in the middle of
  // nothing. The 32×32 hit zone still extends down/left for an easy tap,
  // but visually the glyph occupies the corner of the popup card.
  lv_obj_align(lbl, LV_ALIGN_TOP_RIGHT, -6, 4);
}

// ============================================================
// Display / input drivers
// ============================================================
static void lvglFlush(lv_disp_drv_t* disp_drv, const lv_area_t* area, lv_color_t* color_p) {
  (void)disp_drv;
  int32_t w = area->x2 - area->x1 + 1;
  int32_t h = area->y2 - area->y1 + 1;
  display.writePixelsRGB565(area->x1, area->y1, w, h, reinterpret_cast<uint16_t*>(color_p));
  lv_disp_flush_ready(disp_drv);
}

// Rotate the ST7789 panel in hardware for the landscape orientations. Uses the
// global `display` (ST7789LCDDisplay) — a free function so it isn't shadowed by
// UITask::begin's DisplayDriver* parameter, also called `display`. ROT_90 maps
// to panel rotation 1, ROT_270 to 3. Portrait leaves the panel as inited.
static void applyHardwarePanelRotation(uint8_t lvgl_rot) {
  if (lvgl_rot == LV_DISP_ROT_90)       display.setDisplayRotation(1);
  else if (lvgl_rot == LV_DISP_ROT_270) display.setDisplayRotation(3);
}

static void lvglTouchRead(lv_indev_drv_t* indev, lv_indev_data_t* data) {
  (void)indev;
  static lv_point_t p = {0, 0};
  ++s_live_diag_reads;
  // When the background polling task is running it is the sole owner of the
  // chsc6x driver's state machine — calling heltecV4CapTouchCheck() from here
  // too would race the state machine and double-dispatch tap/swipe events.
  if (!heltecV4CapTouchIsAsyncPolling()) {
    (void)heltecV4CapTouchCheck();
  }
  // If the gesture has turned into a swipe, tell LVGL to abandon the press
  // it started on the originally-touched widget. Without this, swiping
  // sideways across a settings row both switches tabs (our applySwipeGesture)
  // and clicks the row (LVGL's default press-release-to-click).
  //
  // EXCEPTION: on the Map tab, pan IS the primary gesture. The swipe abort
  // was cutting off horizontal drags after the 36-px swipe threshold, so
  // the user could only move the map a tiny amount before LVGL released
  // the finger. Skip the abort there and keep delivering live touch.
  if (heltecV4CapTouchIsSwiping()) {
    const bool on_map = g_lv.tabview &&
                        (int)lv_tabview_get_tab_act(g_lv.tabview) == MAP_TAB_INDEX;
    // Chat detail overlay needs full touch too — its composer / send /
    // back / bubble interactions are all taps, not swipes. Bypass the
    // abort there as well, otherwise opening a chat from a map marker
    // (which leaves the swipe-detector latched briefly) freezes the
    // detail until the next manual tab change.
    const bool detail_open = g_lv.dm.detail_open || g_lv.ch.detail_open;
    // Control center open: its brightness slider needs the full horizontal drag,
    // so don't abort the press there either.
    if (!on_map && !detail_open && !s_cc_root) {
      lv_indev_t* act = lv_indev_get_act();
      if (act) lv_indev_wait_release(act);
      data->state = LV_INDEV_STATE_RELEASED;
      data->point = p;
      return;
    }
  }
  uint16_t x = 0, y = 0;
  bool raw_press = false;
  if (heltecV4CapTouchGetLive(&x, &y)) {
    ++s_live_diag_pressed;
    raw_press = true;
  } else if (heltecV4CapTouchPopTap(&x, &y)) {
    // Fallback: if live state is missed, emit one press edge from finalized tap.
    ++s_live_diag_tap_edges;
    raw_press = true;
  }
  if (raw_press
#if defined(HAS_TDECK_TRACKBALL)
      // Ignore a stray finger on the tab bar while the cursor is up.
      && !tbFingerTouchOnTabBarBlocked(y)
#endif
     ) {
    p.x = static_cast<lv_coord_t>(x);
    p.y = static_cast<lv_coord_t>(y);
    data->point = p;
#if defined(HAS_TDECK_GT911)
    // A finger press must NOT reach the hidden/locked UI. When merely
    // idle-dimmed, noteUserInput() wakes it; on a manual lock the touch is
    // inert (only a trackball *hold* unlocks), whether the lock screen is dark
    // or lit. Either way the press is absorbed, not delivered.
    if (g_lv.task && (g_lv.task->isScreenOff() || g_lv.task->isManualLock())) {
      if (!g_lv.task->isManualLock()) g_lv.task->noteUserInput();
      data->state = LV_INDEV_STATE_RELEASED;
      return;
    }
#endif
    data->state = LV_INDEV_STATE_PRESSED;
    if (g_lv.task) g_lv.task->noteUserInput();
    return;
  }
#if defined(HAS_TDECK_TRACKBALL)
  // Trackball centre click acts as a touch at the cursor (lower priority than a
  // real finger). Holding it = a held press, so taps, long-press and drag all
  // work; releasing fires the click on whatever is under the cursor.
  if (s_tb_click_press) {
    p.x = static_cast<lv_coord_t>(s_tb_cursor_x);
    p.y = static_cast<lv_coord_t>(s_tb_cursor_y);
    data->point = p;
    data->state = LV_INDEV_STATE_PRESSED;
    if (g_lv.task) g_lv.task->noteUserInput();
    return;
  }
#endif
  data->point = p;
  data->state = LV_INDEV_STATE_RELEASED;
}

// ============================================================
// Forward declarations
// ============================================================
static void refreshChatDetail(LvChatPanel& p);
static void refreshChatList(LvChatPanel& p);
static void refreshContactsList();
static void refreshThreadLists();
static void refreshStatusLabels();
static void refreshLiveDiag(unsigned long now);
static void refreshSettingsSectionSubtitles();
static void refreshLogModalView();
static void hideKb();
static void showKb(LvChatPanel* p);
static void closeSettingsModal();
static void contactSelectCb(lv_event_t* e);
static double contactDistanceKm(double lat1, double lon1, double lat2, double lon2);
static void openLogModalCb(lv_event_t* e);
static void logModeRxCb(lv_event_t* e);
static void logModeRawCb(lv_event_t* e);
static void showConfirm(const char* msg, const char* ok_label, void (*on_confirm)());
static void clipboardSet(const char* text, const char* tag);
static void copyLabelLongPressCb(lv_event_t* e);
static lv_obj_t* createSettingsModal(const char* title, SettingsModalKind kind);
static void chatDeleteApply();
static void shareMyContactBtnCb(lv_event_t* e);
static void openShareMyContactPopup();
static void refreshMapInfoLabel();
static void renderMapTiles();
static void freeMapTiles();
// Single entry point used by tabChangedCb. Recenters on self GPS and
// rebuilds the tile grid. Defined alongside the map state below.
static void onMapTabActivated();
static void applyMapChrome(bool on);   // map-tab immersive chrome (transparent bars); defined near makeMapTab
static void formatAgeBadge(char* buf, size_t cap, uint32_t age_secs);        // defined with the contacts list
static void formatDistanceBadge(char* out, size_t out_cap, double self_lat, double self_lon,
                                int32_t c_lat_e6, int32_t c_lon_e6);          // defined with the contacts list
static void openChannelLongPressActionSheet(int thread_idx, const char* name);
// ============================================================
// Helpers
// ============================================================
static const char* onOff(bool v) { return v ? "On" : "Off"; }

static void scheduleHeavyRefresh(unsigned long delay_ms) {
  g_lv.defer_heavy_refresh = true;
  g_lv.heavy_refresh_at_ms = millis() + delay_ms;
}

static bool hasChatDetailOpen() {
  return (g_lv.dm.detail_open && g_lv.dm.overlay) ||
         (g_lv.ch.detail_open && g_lv.ch.overlay);
}

static void resetSettingsModalState() {
  g_set_modal = {};
  g_set_modal.kind = SettingsModalKind::None;
}

static bool parseFloatField(lv_obj_t* ta, float& out) {
  if (!ta) return false;
  const char* txt = lv_textarea_get_text(ta);
  if (!txt || !txt[0]) return false;
  char* endptr = nullptr;
  float v = strtof(txt, &endptr);
  if (!endptr || endptr == txt || *endptr != '\0') return false;
  out = v;
  return true;
}

static bool parseIntField(lv_obj_t* ta, int& out) {
  if (!ta) return false;
  const char* txt = lv_textarea_get_text(ta);
  if (!txt || !txt[0]) return false;
  char* endptr = nullptr;
  long v = strtol(txt, &endptr, 10);
  if (!endptr || endptr == txt || *endptr != '\0') return false;
  out = static_cast<int>(v);
  return true;
}

// ============================================================
// Tab / swipe navigation
// ============================================================
static void goToTab(int idx);   // fwd-declared; defined after tabChangedCb

static int getActiveTab() {
  return g_lv.tabview ? static_cast<int>(lv_tabview_get_tab_act(g_lv.tabview)) : 0;
}

static lv_obj_t* getActiveTabPage() {
  if (!g_lv.tabview) return nullptr;
  lv_obj_t* content = lv_tabview_get_content(g_lv.tabview);
  if (!content) return nullptr;
  return lv_obj_get_child(content, getActiveTab());
}

// The settings sub-tabview (assigned in makeSettings). Horizontal swipes on the
// Settings tab move between its sub-tabs instead of switching the main tab.
static lv_obj_t* s_settings_subtab = nullptr;
static constexpr int kSettingsSubtabCount = 5;   // Profile / Radio / Network / Device / About
static lv_obj_t* s_settings_pages[kSettingsSubtabCount] = { nullptr };  // sub-tab page containers
static void settingsBuildPage(int idx);          // fwd: lazy page builder (swipe handler calls it)
static int       s_settings_built_page = -1;     // which sub-tab currently has its controls built
                                                 // (only one is built at a time to keep DRAM low)

// ---- Firmware update check (red badge on the Settings gear + About-tab line) ----
// Compares our embedded release tag against the latest pre-alpha_N published to
// ALLFATHER-BV/meshcomod (the same listing the web flasher reads), fetched over
// plain HTTP via the meshcomod api-github proxy. FIRMWARE_RELEASE_TAG is set at
// release time; an empty/non-release tag disables the check (dev build).
#ifndef FIRMWARE_RELEASE_TAG
#define FIRMWARE_RELEASE_TAG ""
#endif
static lv_obj_t* s_update_badge     = nullptr;   // red "!" over the bottom-bar gear
static lv_obj_t* s_update_subtab_badge = nullptr;// red dot over the "About" sub-tab button
static lv_obj_t* s_update_about_lbl = nullptr;   // status line on the About sub-tab
static lv_obj_t* s_sysinfo_lbl      = nullptr;   // System-info text (refreshed live on the About tab)
static bool      s_update_available = false;
static bool      s_verchk_ran       = false;     // a check has completed (ok or failed)
static volatile bool s_verchk_request  = false;  // UI -> core-0 worker: please check
static volatile bool s_verchk_done     = false;  // worker -> UI: result ready
static volatile int  s_verchk_latest_n = -1;     // highest published pre-alpha_N (-1 = failed)

// ---- Wi-Fi scan (serviced by the core-0 fetch worker; results drawn in the
// Network tab + the setup wizard) ----
static constexpr int kWifiScanMax = 14;
static char          s_wifiscan_ssids[kWifiScanMax][WIFI_CONFIG_SSID_MAX];
static volatile int  s_wifiscan_count   = 0;
static volatile bool s_wifiscan_request = false;   // UI -> worker: scan now
static volatile bool s_wifiscan_done    = false;   // worker -> UI: results ready
static lv_obj_t*     s_wifi_scan_list   = nullptr; // results list (inside the scan popup)
static lv_obj_t*     s_wifi_scan_popup  = nullptr; // full-screen scan overlay (covers bar + sub-tabs)
static bool ensureTileFetchTaskRunning();          // fwd: respawn the core-0 worker if it idled out

// ---- First-boot setup wizard (welcome -> name -> region -> Wi-Fi). Shown once
// on a fresh flash; gated by touchPrefsGetSetupDone(). s_setup_root is declared
// here (not with the wizard functions further down) so the hardware-key handler
// can swallow stray keystrokes while the wizard owns the screen. ----
static lv_obj_t* s_setup_root      = nullptr;  // full-screen overlay; non-null = wizard active
static int       s_setup_step      = 0;        // 0 welcome / 1 name / 2 region / 3 Wi-Fi
static lv_obj_t* s_setup_name_ta   = nullptr;
static lv_obj_t* s_setup_region_list = nullptr;
static int       s_setup_region_sel  = -1;     // selected preset index, -1 = keep default
static lv_obj_t* s_setup_ssid_ta   = nullptr;
static lv_obj_t* s_setup_pwd_ta    = nullptr;
static bool      s_setup_wifi_autoscan_done = false;  // auto-open the picker once per wizard run
static void setupWizardOpen();            // fwd: re-trigger the flow (Device settings button)
static void setupRerunCb(lv_event_t* e);  // fwd: "Run setup again" button callback

// Our release number (the N in "beta_N"); -1 if this isn't a tagged build.
static int firmwareReleaseN() {
  const char* t = FIRMWARE_RELEASE_TAG;
  const char* pfx = "beta_";
  const size_t pl = strlen(pfx);
  if (strncmp(t, pfx, pl) != 0) return -1;
  if (t[pl] < '0' || t[pl] > '9') return -1;
  return atoi(t + pl);
}

// Reflect the latest check result in the gear badge + the About status line.
static void otaButtonRefreshState();   // defined below; greys the Install button when current

static void versionCheckUpdateUi() {
  // Two badges: the bottom-bar gear (get the user into Settings) and the About
  // sub-tab (the actual update info lives there).
  lv_obj_t* badges[2] = { s_update_badge, s_update_subtab_badge };
  for (lv_obj_t* bd : badges) {
    if (!bd) continue;
    if (s_update_available) lv_obj_clear_flag(bd, LV_OBJ_FLAG_HIDDEN);
    else                    lv_obj_add_flag(bd, LV_OBJ_FLAG_HIDDEN);
  }
  otaButtonRefreshState();   // enable/grey the OTA button to match the result
  if (!s_update_about_lbl) return;
  const int my_n = firmwareReleaseN();
  char b[110];
  if (my_n < 0) {
    snprintf(b, sizeof b, "Firmware %s\nDevelopment build — update check off", FIRMWARE_VERSION);
    lv_obj_set_style_text_color(s_update_about_lbl, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  } else if (s_update_available && s_verchk_latest_n >= 0) {
    snprintf(b, sizeof b, LV_SYMBOL_DOWNLOAD "  Update available: beta_%d\nYou have beta_%d — tap Install update below",
             s_verchk_latest_n, my_n);
    lv_obj_set_style_text_color(s_update_about_lbl, lv_color_hex(0xE2A23A), LV_PART_MAIN);
  } else if (s_verchk_latest_n >= 0) {
    snprintf(b, sizeof b, LV_SYMBOL_OK "  Up to date (beta_%d)", my_n);
    lv_obj_set_style_text_color(s_update_about_lbl, lv_color_hex(0x6FCF6F), LV_PART_MAIN);
  } else if (s_verchk_ran) {
    snprintf(b, sizeof b, "Firmware beta_%d\nCouldn't reach the update server", my_n);
    lv_obj_set_style_text_color(s_update_about_lbl, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  } else {
    snprintf(b, sizeof b, "Firmware beta_%d\nChecking for updates over Wi-Fi…", my_n);
    lv_obj_set_style_text_color(s_update_about_lbl, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  }
  lv_label_set_text(s_update_about_lbl, b);
}

// ---- OTA: install the latest published release over the air ----
// The download goes through the meshcomod flasher proxy over PLAIN HTTP (the
// device can't do HTTPS after Wi-Fi associates — see the tile/version-check
// notes), which startHttpOtaFromUrl + its allow-list already support. The
// proxy maps /firmware-download/* → ALLFATHER-BV/meshcomod main/* on GitHub,
// so the app-only .bin under prebuilt/releases/TOUCH/<tag>/<env>.bin is what we
// fetch. Requires the dual-slot partition table (app1 ≥ image size).
#ifndef FIRMWARE_OTA_ENV
#define FIRMWARE_OTA_ENV ""
#endif
static lv_obj_t* s_ota_status_lbl = nullptr;   // live OTA status line (About page)
static lv_obj_t* s_ota_btn        = nullptr;   // "Install update" button (greyed when up to date)
static lv_obj_t* s_ota_btn_lbl    = nullptr;   // its caption (recoloured when disabled)

// Enable the OTA button only when an update is actually available — or when the
// check hasn't completed yet (offline / still checking), so the user isn't
// blocked by a stale "up to date". Grey + non-clickable when current.
static void otaButtonRefreshState() {
  if (!s_ota_btn) return;
  // Known-current = a successful check (latest_n >= 0) that isn't newer than us.
  const bool known_current = (s_verchk_latest_n >= 0) && !s_update_available;
  const bool enabled = !known_current;
  if (enabled) {
    lv_obj_add_flag(s_ota_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_state(s_ota_btn, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(s_ota_btn, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ota_btn, LV_OPA_COVER, LV_PART_MAIN);
    if (s_ota_btn_lbl) lv_obj_set_style_text_color(s_ota_btn_lbl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  } else {
    lv_obj_clear_flag(s_ota_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_state(s_ota_btn, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(s_ota_btn, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ota_btn, LV_OPA_20, LV_PART_MAIN);
    if (s_ota_btn_lbl) lv_obj_set_style_text_color(s_ota_btn_lbl, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  }
}

static void otaInstallLatestCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  const char* env = FIRMWARE_OTA_ENV;
  if (!env || !env[0]) {
    if (g_lv.task) g_lv.task->showAlert("OTA not available on this build", 1800);
    return;
  }
  if (s_verchk_latest_n < 0) {
    if (g_lv.task) g_lv.task->showAlert("No update info yet — check Wi-Fi", 1800);
    return;
  }
  if (!s_update_available) {   // already current (button is greyed, but guard anyway)
    if (g_lv.task) g_lv.task->showAlert("Already on the latest version", 1600);
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (g_lv.task) g_lv.task->showAlert("Connect Wi-Fi to update", 1800);
    return;
  }
  // App-only image over PLAIN HTTP via app.meshcomod.com — it serves
  // /firmware-download/ directly (200), whereas flasher./repeater. 301-redirect
  // port 80 → HTTPS, which the RAM-tight touch build can't follow (the OTA then
  // failed "HTTP -1 connection refused").
  char url[200];
  snprintf(url, sizeof(url),
           "http://app.meshcomod.com/firmware-download/prebuilt/releases/TOUCH/beta_%d/%s.bin",
           s_verchk_latest_n, env);
  if (g_lv.task) {
    g_lv.task->showAlert("Starting update\xE2\x80\xA6 keep Wi-Fi on", 2500);
    g_lv.task->persistHistoryNow();   // flush chat before the device reboots into the new image
  }
  // Reclaim internal heap before the download: HTTPClient's socket + the OTA
  // write need a few KB of internal DMA RAM, which is scarce on this build. Drop
  // the decoded map tiles (PSRAM + their widgets) so the working set is minimal.
  freeMapTiles();
  if (s_ota_status_lbl) {
    char b[80];
    snprintf(b, sizeof b, "Downloading update\xE2\x80\xA6 do not power off (heap %uK)",
             (unsigned)(ESP.getFreeHeap() / 1024));
    lv_label_set_text(s_ota_status_lbl, b);
    lv_obj_set_style_text_color(s_ota_status_lbl, lv_color_hex(0xE2A23A), LV_PART_MAIN);
    lv_refr_now(NULL);
  }
  // startHttpOtaFromUrl blocks until done (streams to flash, then reboots on
  // success). `board` is visible via target.h (included through MyMesh.h).
  char reply[140] = {0};
  board.startHttpOtaFromUrl(url, reply);
  // We only reach here on FAILURE (success reboots into the new image).
  if (s_ota_status_lbl) {
    char b[160];
    snprintf(b, sizeof b, "Update failed: %s", reply[0] ? reply : "unknown error");
    lv_label_set_text(s_ota_status_lbl, b);
    lv_obj_set_style_text_color(s_ota_status_lbl, lv_color_hex(0xE08080), LV_PART_MAIN);
  }
  if (g_lv.task) g_lv.task->showAlert(reply[0] ? reply : "Update failed", 2500);
#endif
}

// Trigger a check once Wi-Fi is up (then every 6 h); apply the result when ready.
static void versionCheckService(unsigned long now) {
#if defined(MULTI_TRANSPORT_COMPANION)
  if (firmwareReleaseN() < 0) return;   // dev build: nothing to compare against
  static bool started = false;
  static unsigned long next_ms = 0;
  if (WiFi.status() == WL_CONNECTED && !s_verchk_request && !s_verchk_done &&
      (!started || (long)(now - next_ms) >= 0)) {
    started   = true;
    next_ms   = now + 6UL * 60UL * 60UL * 1000UL;   // re-check every 6 h
    ensureTileFetchTaskRunning();                   // worker idles out — make sure it's up
    s_verchk_request = true;
  }
  if (s_verchk_done) {
    s_verchk_done = false;
    s_verchk_ran  = true;
    s_update_available = (s_verchk_latest_n > firmwareReleaseN());
    versionCheckUpdateUi();
  }
#else
  (void)now;
#endif
}
static void applySwipeGesture(int8_t swipe_x, int8_t swipe_y) {
  // Block tab swipes while a chat detail is open; back button is the exit.
  if (hasChatDetailOpen()) return;
  if (s_wifi_scan_popup) return;   // scan popup is modal; don't switch tabs underneath
  // The control center drops down from the top bar, so an up-swipe dismisses
  // it (natural "push it back up" gesture). Any other swipe is swallowed so
  // that e.g. dragging its brightness slider can't also switch the tab
  // underneath the card. swipe_y < 0 == upward (see TDeckTouch swipe sign).
  if (s_cc_root) {
    if (swipe_y < 0) closeControlCenter();
    return;
  }
  if (!g_lv.tabview) return;
  if (swipe_x != 0) {
    if (settingsModalIsOpen()) return;
    // Block horizontal tab-swipes on the Map tab — that gesture is "pan"
    // there, not "switch tab". The +/− and recenter buttons + the tabbar
    // icons remain the only way off the tab.
    if (getActiveTab() == MAP_TAB_INDEX) return;
    // On the Settings tab, swipe moves between the settings sub-tabs (clamped at
    // the ends — use the bottom bar to change the main tab).
    if (getActiveTab() == SETTINGS_TAB_INDEX && s_settings_subtab) {
      const int n   = kSettingsSubtabCount;
      const int cur = (int)lv_tabview_get_tab_act(s_settings_subtab);
      int nx = cur + (swipe_x < 0 ? 1 : -1);
      if (nx < 0) nx = 0;
      if (nx > n - 1) nx = n - 1;
      if (nx != cur) {
        lv_tabview_set_act(s_settings_subtab, (uint16_t)nx, LV_ANIM_ON);
        // set_act() only scrolls + re-checks the button; it does NOT emit
        // LV_EVENT_VALUE_CHANGED, so the lazy builder that runs on a tab *click*
        // never fires for a swipe. Build the target page explicitly (it no-ops
        // if already built, so a later stray VALUE_CHANGED can't duplicate it).
        settingsBuildPage(nx);
      }
      return;
    }
    int idx = getActiveTab();
    int next = idx + (swipe_x < 0 ? 1 : -1);
    if (next < 0) next = 0;
    if (next > TAB_LAST) next = TAB_LAST;
    if (next != idx) goToTab(next);
    return;
  }
  // Vertical swipes intentionally fall through: LVGL's native drag-scroll on
  // the underlying list (chats / contacts / settings tab) handles them
  // continuously during the gesture, which feels naturally finger-locked.
  // The old fixed-step jump on release was the source of "jittery / jumpy"
  // vertical scroll feel.
  (void)swipe_y;
}

/** After elastic/rubber-band overscroll, snap back to the furthest valid top or bottom. */
static void scrollClampOnEndCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_SCROLL_END) return;
  lv_obj_t* obj = lv_event_get_target(e);
  if (!obj || !lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE)) return;
  lv_dir_t d = lv_obj_get_scroll_dir(obj);
  if (!(d & LV_DIR_VER)) return;

  lv_obj_update_layout(obj);
  lv_coord_t sy = lv_obj_get_scroll_y(obj);
  lv_coord_t sm = lv_obj_get_scroll_top(obj) + lv_obj_get_scroll_bottom(obj);
  if (sm < 0) sm = 0;

  if (sy < 0) lv_obj_scroll_to_y(obj, 0, LV_ANIM_ON);
  else if (sy > sm) lv_obj_scroll_to_y(obj, sm, LV_ANIM_ON);
}

// ============================================================
// Keyboard show / hide
// Chat: keyboard binds to composer (layout already lifts composer above keys).
// Settings modals: optional top mirror strip so low fields stay readable.
// ============================================================
constexpr int KB_MIRROR_STRIP_H = 52;

// Keyboard rotation helpers (defined here so showKb/hideKb/kbMirrorBind can use them).
// Layout adjusts keyboard + mirror + rotate arrows for the current rotation.
static void kbApplyLayoutForRotation(uint8_t rot) {
  lv_disp_t* disp = lv_disp_get_default();
  if (!disp) return;
  const lv_coord_t scr_w  = lv_disp_get_hor_res(disp);
  const lv_coord_t scr_h  = lv_disp_get_ver_res(disp);
  const bool landscape    = (rot == LV_DISP_ROT_90 || rot == LV_DISP_ROT_270);
  // Cap keyboard to half the screen so the textarea above stays visible.
  const lv_coord_t kb_h   = landscape ? (scr_h / 2) : CHAT_KB_H;

  if (g_lv.keyboard) {
    lv_obj_set_size(g_lv.keyboard, scr_w, kb_h);
    lv_obj_align(g_lv.keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  }
  if (s_kb_mirror_root) {
    lv_obj_set_width(s_kb_mirror_root, scr_w);
    if (s_kb_mirror_ta) lv_obj_set_width(s_kb_mirror_ta, scr_w - 16);
  }
  // Rotation arrows. Default: just above the keyboard's top edge. In the
  // chat panel that area is occupied by the composer row (QR + textarea +
  // Send), so the arrows would land on top of the buttons. When the chat
  // composer is active, bump the arrows up to sit just above the COMPOSER
  // instead of just above the keyboard.
  lv_coord_t arrow_bottom_y_from_screen_top = scr_h - (kb_h + 2);
  if (s_kb_panel && s_kb_panel->composer_row) {
    // Force a layout pass so the composer's freshly-set y (CHAT_COMP_Y_KB)
    // is reflected in get_coords before we sample it.
    lv_obj_update_layout(s_kb_panel->composer_row);
    lv_area_t comp_coords;
    lv_obj_get_coords(s_kb_panel->composer_row, &comp_coords);
    // 2-px gap between arrow bottom and composer top.
    if (comp_coords.y1 > 0) arrow_bottom_y_from_screen_top = comp_coords.y1 - 2;
  }
  // Convert "absolute y of arrow bottom" → offset usable by LV_ALIGN_BOTTOM_*.
  const lv_coord_t y_off = -(scr_h - arrow_bottom_y_from_screen_top);
  if (s_kb_rot_left_btn)  lv_obj_align(s_kb_rot_left_btn,  LV_ALIGN_BOTTOM_LEFT,  4, y_off);
  if (s_kb_rot_right_btn) lv_obj_align(s_kb_rot_right_btn, LV_ALIGN_BOTTOM_RIGHT, -4, y_off);
}

static void kbApplyRotation(uint8_t rot) {
  lv_disp_t* disp = lv_disp_get_default();
  if (!disp) return;
  // In hardware-landscape the panel is already rotated; applying LVGL software
  // rotation on top would double-rotate. Software rotation is used ONLY for the
  // transient keyboard-landscape trick when the base orientation is portrait.
  const bool hw_landscape = (s_ui_rotation == LV_DISP_ROT_90 ||
                             s_ui_rotation == LV_DISP_ROT_270);
  if (!hw_landscape) {
    lv_disp_set_rotation(disp, static_cast<lv_disp_rot_t>(rot));
  }
  // Keep the touch driver's swipe-axis transform in sync with the visible
  // orientation so horizontal swipes are detected correctly in landscape.
  heltecV4CapTouchSetRotation(rot);
  kbApplyLayoutForRotation(rot);
  // Force a full redraw — without this the previous frame's contents linger
  // briefly at the new orientation's coordinates.
  lv_obj_invalidate(lv_scr_act());
}

static void kbSaveRotationPref() {
#if defined(ESP32)
  Preferences pr;
  if (pr.begin("meshTouch", false)) {
    pr.putUChar("kbrot", s_kb_rotation);
    pr.end();
  }
#endif
}

static void kbSetRotateArrowsOpa(lv_opa_t opa) {
  if (s_kb_rot_left_btn)  lv_obj_set_style_opa(s_kb_rot_left_btn,  opa, LV_PART_MAIN);
  if (s_kb_rot_right_btn) lv_obj_set_style_opa(s_kb_rot_right_btn, opa, LV_PART_MAIN);
}

static void kbShowRotateArrows(bool show) {
  if (s_kb_rot_left_btn)  show ? lv_obj_clear_flag(s_kb_rot_left_btn,  LV_OBJ_FLAG_HIDDEN)
                               : lv_obj_add_flag  (s_kb_rot_left_btn,  LV_OBJ_FLAG_HIDDEN);
  if (s_kb_rot_right_btn) show ? lv_obj_clear_flag(s_kb_rot_right_btn, LV_OBJ_FLAG_HIDDEN)
                               : lv_obj_add_flag  (s_kb_rot_right_btn, LV_OBJ_FLAG_HIDDEN);
  if (show) {
    // Each new show starts at full opacity; the first keystroke fades arrows
    // to ~20% so they're out of the way while typing but still tappable.
    kbSetRotateArrowsOpa(LV_OPA_COVER);
    if (s_kb_rot_left_btn)  lv_obj_move_foreground(s_kb_rot_left_btn);
    if (s_kb_rot_right_btn) lv_obj_move_foreground(s_kb_rot_right_btn);
  }
}

static void kbRotLeftCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  // Tap the left arrow: rotate landscape that way; tap again returns to portrait.
  s_kb_rotation = (s_kb_rotation == LV_DISP_ROT_270) ? LV_DISP_ROT_NONE : LV_DISP_ROT_270;
  kbApplyRotation(s_kb_rotation);
  kbSaveRotationPref();
}

static void kbRotRightCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  s_kb_rotation = (s_kb_rotation == LV_DISP_ROT_90) ? LV_DISP_ROT_NONE : LV_DISP_ROT_90;
  kbApplyRotation(s_kb_rotation);
  kbSaveRotationPref();
}

static void kbMirrorEnsureCreated() {
  if (s_kb_mirror_root) return;
  s_kb_mirror_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_kb_mirror_root);
  lv_obj_set_size(s_kb_mirror_root, 240, KB_MIRROR_STRIP_H);
  // Sit just below the global status bar so the time/battery are still
  // visible while typing.
  lv_obj_align(s_kb_mirror_root, LV_ALIGN_TOP_MID, 0, STATUSBAR_H + 2);
  styleSurface(s_kb_mirror_root, COLOR_PANEL, 8);
  lv_obj_set_style_pad_hor(s_kb_mirror_root, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(s_kb_mirror_root, 6, LV_PART_MAIN);
  lv_obj_clear_flag(s_kb_mirror_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_kb_mirror_root, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* hint = lv_label_create(s_kb_mirror_root);
  lv_label_set_text(hint, "Editing");
  lv_obj_set_style_text_font(hint, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 0, 0);

  s_kb_mirror_ta = lv_textarea_create(s_kb_mirror_root);
  lv_obj_set_size(s_kb_mirror_ta, 224, 30);
  lv_obj_align(s_kb_mirror_ta, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_textarea_set_one_line(s_kb_mirror_ta, true);
  lv_obj_set_style_bg_color(s_kb_mirror_ta, lv_color_hex(0x0A0B0C), LV_PART_MAIN);
  lv_obj_set_style_text_color(s_kb_mirror_ta, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_border_color(s_kb_mirror_ta, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_kb_mirror_ta, 1, LV_PART_MAIN);
  lv_obj_set_style_text_font(s_kb_mirror_ta, &g_font_14, LV_PART_MAIN);
  // Live-sync the mirror's text into the bound real textarea on every
  // keystroke instead of waiting for hideKb. Lets the underlying field
  // reflect what the user is typing in real time (useful when the field is
  // partially visible behind the keyboard, e.g. a chat composer).
  lv_obj_add_event_cb(s_kb_mirror_ta, [](lv_event_t* e) {
    (void)e;
    kbMirrorSyncToReal();
  }, LV_EVENT_VALUE_CHANGED, nullptr);
}

static void kbMirrorSyncToReal() {
  if (!s_kb_bind_ta || !s_kb_mirror_ta) return;
  lv_textarea_set_text(s_kb_bind_ta, lv_textarea_get_text(s_kb_mirror_ta));
}

// Set a textarea's text from code (not via keystrokes) while keeping the on-screen
// keyboard mirror consistent. If the mirror is currently bound to this field, a
// later kbMirrorSyncToReal() (on Save, or when focus moves to another field) would
// otherwise copy the STALE mirror contents back over what we just set — which is
// exactly what made the Wi-Fi scan picker "not stick" and save the OLD SSID.
// Updating the mirror too keeps both in agreement.
static void setTextareaSynced(lv_obj_t* ta, const char* text) {
  if (!ta) return;
  lv_textarea_set_text(ta, text ? text : "");
  if (s_kb_bind_ta == ta && s_kb_mirror_ta) {
    lv_textarea_set_text(s_kb_mirror_ta, text ? text : "");
  }
}

// Cleared automatically when the bound textarea is destroyed. kbMirrorSyncToReal
// (and the click/keystroke paths) write into s_kb_bind_ta; if its modal is torn
// down by a path that skips hideKb() — e.g. a hardware-key action or screen lock
// firing mid-flow — the pointer would dangle and the next sync writes into freed
// memory (the 'loopTask' LoadProhibited use-after-free seen in the coredump). An
// LV_EVENT_DELETE hook nulls it the moment LVGL frees the field, so it can never
// point at freed memory regardless of which close path runs.
static void kbBoundTaDeletedCb(lv_event_t* e) {
  if (lv_event_get_target(e) == s_kb_bind_ta) s_kb_bind_ta = nullptr;
}

/** Show keyboard bound to a top mirror of `real_ta` so typed text stays visible above the keyboard. */
static void kbMirrorBind(lv_obj_t* real_ta) {
  if (!real_ta || !g_lv.keyboard) return;
  kbMirrorEnsureCreated();
  if (s_kb_bind_ta && s_kb_bind_ta != real_ta) kbMirrorSyncToReal();
  // Detach the bind while we mutate the mirror's properties below.
  // set_one_line / set_text emit VALUE_CHANGED on the mirror synchronously,
  // and the live-sync callback would otherwise copy the mirror's stale/empty
  // state into the freshly-bound real textarea before we've loaded it.
  s_kb_bind_ta = nullptr;

  const uint32_t max_len = lv_textarea_get_max_length(real_ta);
  lv_textarea_set_max_length(s_kb_mirror_ta, max_len);
  lv_textarea_set_password_mode(s_kb_mirror_ta, lv_textarea_get_password_mode(real_ta));
  lv_textarea_set_one_line(s_kb_mirror_ta, true);
  lv_textarea_set_text(s_kb_mirror_ta, lv_textarea_get_text(real_ta));

  // Mirror now holds the real_ta's current text; arm the bind so subsequent
  // user keystrokes get mirrored live.
  s_kb_bind_ta = real_ta;
  // Auto-clear the bind if this field is ever deleted (use-after-free guard).
  // Dedup so re-binding a long-lived field (chat composer) doesn't stack hooks.
  lv_obj_remove_event_cb(real_ta, kbBoundTaDeletedCb);
  lv_obj_add_event_cb(real_ta, kbBoundTaDeletedCb, LV_EVENT_DELETE, nullptr);

  lv_keyboard_set_textarea(g_lv.keyboard, s_kb_mirror_ta);
#if !defined(HAS_TDECK_KEYBOARD)
  // T-Deck has a physical keyboard — the mirror/keys never show; the typed text
  // syncs straight into the (visible) real field. Other boards show both.
  lv_obj_clear_flag(s_kb_mirror_root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_kb_mirror_root);
  // Slide the mirror BELOW the settings modal header (when one is open)
  // so it doesn't sit on top of the modal's Close button. The mirror is on
  // lv_layer_top and gets move_foreground'd above the modal — so any tap
  // in the mirror's area was being consumed by the mirror's textarea
  // instead of reaching the Close button just behind it.
  const lv_coord_t mirror_y = settingsModalIsOpen()
      ? (STATUSBAR_H + 46 + 2)   // below the 46-px modal header
      : (STATUSBAR_H + 2);       // top of screen for chat / non-modal contexts
  lv_obj_align(s_kb_mirror_root, LV_ALIGN_TOP_MID, 0, mirror_y);
  lv_obj_clear_flag(g_lv.keyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_lv.keyboard);
  kbApplyRotation(effectiveKbRotation());
  kbShowRotateArrows(true);
#endif
}

static void hideKb() {
  kbMirrorSyncToReal();
  s_kb_bind_ta = nullptr;
  if (s_kb_mirror_root) lv_obj_add_flag(s_kb_mirror_root, LV_OBJ_FLAG_HIDDEN);
  if (g_lv.keyboard) {
    lv_keyboard_set_textarea(g_lv.keyboard, nullptr);
    lv_obj_add_flag(g_lv.keyboard, LV_OBJ_FLAG_HIDDEN);
  }
  kbShowRotateArrows(false);
  // Revert the display to the global UI orientation once the keyboard is
  // dismissed (portrait unless the user set a landscape base in Settings).
  // s_kb_rotation is preserved so the next showKb restores the transient
  // keyboard-landscape choice when the base is portrait.
  const bool kb_was_landscape =
      (effectiveKbRotation() == LV_DISP_ROT_90 || effectiveKbRotation() == LV_DISP_ROT_270);
  const bool base_is_landscape =
      (s_ui_rotation == LV_DISP_ROT_90 || s_ui_rotation == LV_DISP_ROT_270);
  const bool was_landscape = kb_was_landscape && !base_is_landscape;
  kbApplyRotation(s_ui_rotation);
  // Landscape forced the modal/chat scroll containers down to keep the field
  // visible above the rotated keyboard; reset them to the top so the user
  // doesn't land mid-scroll when the screen comes back to portrait.
  if (was_landscape) {
    if (g_set_modal.root) lv_obj_scroll_to(g_set_modal.root, 0, 0, LV_ANIM_OFF);
    lv_obj_scroll_to(lv_scr_act(), 0, 0, LV_ANIM_OFF);
  }
  if (s_kb_panel) {
    if (s_kb_panel->msgs)         lv_obj_set_height(s_kb_panel->msgs,         chatMsgHOpen());
    if (s_kb_panel->composer_row) lv_obj_set_y(s_kb_panel->composer_row,      chatCompYOpen());
    s_kb_panel = nullptr;
  }
}

static void showKb(LvChatPanel* p) {
  if (!g_lv.keyboard || !p || !p->composer_ta || !p->msgs || !p->composer_row) return;
  kbMirrorSyncToReal();
  s_kb_bind_ta = nullptr;
  if (s_kb_mirror_root) lv_obj_add_flag(s_kb_mirror_root, LV_OBJ_FLAG_HIDDEN);
  s_kb_panel = p;
  lv_keyboard_set_textarea(g_lv.keyboard, p->composer_ta);
#if !defined(HAS_TDECK_KEYBOARD)
  // No on-screen keyboard on the T-Deck — the physical keyboard types straight
  // into the composer (already visible), so skip showing the keys + the lift.
  lv_obj_clear_flag(g_lv.keyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_lv.keyboard);
  // Shrink message area to keep composer visible above keyboard.
  lv_obj_set_height(p->msgs,         chatMsgHKb());
  lv_obj_set_y(p->composer_row,      chatCompYKb());
  kbApplyRotation(effectiveKbRotation());
  kbShowRotateArrows(true);
#endif
}

// ============================================================
// Callbacks
// ============================================================
static void keyboardCb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) hideKb();
  // VALUE_CHANGED fires for any keypress (incl. backspace). Fade the rotate
  // arrows down to ~20% so they don't compete visually with the text the
  // user is typing. Reset to full opacity on the next showKb / kbMirrorBind.
  else if (code == LV_EVENT_VALUE_CHANGED) {
    kbSetRotateArrowsOpa(LV_OPA_20);
  }
}

static void composerFocusCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_FOCUSED) return;
  auto* p = static_cast<LvChatPanel*>(lv_event_get_user_data(e));
  if (p && p->detail_open) { showKb(p); noteKbActivity(); }
}

static void backBtnCb(lv_event_t* e) {
  // Fire on PRESSED rather than CLICKED so the back action triggers on
  // touch-down instead of touch-up. CLICKED was sometimes dropped because
  // the cap-touch driver's horizontal-swipe detector calls
  // lv_indev_wait_release mid-tap, which aborts the LVGL click on the
  // freshly-pressed button. Triggering on PRESSED side-steps that race.
  const lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_PRESSED && code != LV_EVENT_CLICKED) return;
  auto* p = static_cast<LvChatPanel*>(lv_event_get_user_data(e));
  if (!p || !p->detail_open) return;  // ignore repeat fires from CLICKED after PRESSED already closed it
  hideKb();
  if (p->overlay) lv_obj_add_flag(p->overlay, LV_OBJ_FLAG_HIDDEN);
  p->detail_open = false;
  setChatStatusTitle(nullptr);   // drop the thread name from the status bar
}

// Long-press → delete-this-chat confirmation popup. The popup uses the
// existing showConfirm helper, so we stash the thread context in module-
// static state and consume it inside the OK callback.
static int  s_chat_del_thread_idx = -1;
static bool s_chat_del_is_channel = false;

// Modal that shows the channel's 32-hex PSK with a Copy action, so the user
// can share it out-of-band (QR / chat / paper). Read-only.
static char s_channel_share_secret_hex[33] = {0};
static char s_channel_share_name[33] = {0};

static void channelShareCopyCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  clipboardSet(s_channel_share_secret_hex, "secret");
}

static void openChannelShareModal(const char* channel_name, const uint8_t secret[16]) {
  // Render hex once and stash it for the Copy button.
  for (int i = 0; i < 16; ++i) {
    snprintf(s_channel_share_secret_hex + i*2, 3, "%02x", secret[i]);
  }
  strncpy(s_channel_share_name, channel_name ? channel_name : "Channel",
          sizeof(s_channel_share_name) - 1);

  lv_obj_t* body = createSettingsModal("Share channel", SettingsModalKind::AddContact);  // reuse a kind
  int y = 0;

  lv_obj_t* name_l = lv_label_create(body);
  lv_label_set_text_fmt(name_l, LV_SYMBOL_LOOP "  %s", s_channel_share_name);
  lv_obj_set_style_text_color(name_l, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(name_l, &g_font_14, LV_PART_MAIN);
  lv_obj_set_pos(name_l, 2, y);
  y += 26;

  lv_obj_t* hint = lv_label_create(body);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(hint, lv_pct(100));
  lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(hint, &g_font_12, LV_PART_MAIN);
  lv_label_set_text(hint, "32-hex secret. Anyone with this can read the channel.");
  lv_obj_set_pos(hint, 2, y);
  y += 32;

  lv_obj_t* sec_lbl = lv_label_create(body);
  lv_label_set_long_mode(sec_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(sec_lbl, lv_pct(100));
  lv_label_set_text(sec_lbl, s_channel_share_secret_hex);
  lv_obj_set_style_text_color(sec_lbl, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_set_style_text_font(sec_lbl, &g_font_14, LV_PART_MAIN);
  lv_obj_set_style_bg_color(sec_lbl, lv_color_hex(0x0A0B0C), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(sec_lbl, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_all(sec_lbl, 6, LV_PART_MAIN);
  lv_obj_set_style_radius(sec_lbl, 4, LV_PART_MAIN);
  lv_obj_set_pos(sec_lbl, 2, y);
  // Long-press copies too — handy if the Copy button is hidden by the keyboard.
  lv_obj_add_flag(sec_lbl, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(sec_lbl, copyLabelLongPressCb, LV_EVENT_LONG_PRESSED,
                      const_cast<char*>("secret"));
  y += 52;

  lv_obj_t* b = lv_btn_create(body);
  lv_obj_set_size(b, lv_pct(100),36);
  lv_obj_set_pos(b, 2, y);
  styleButton(b);
  lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x3B7039), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_add_event_cb(b, channelShareCopyCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* bl = lv_label_create(b);
  lv_label_set_text(bl, "Copy secret");
  lv_obj_center(bl);
}

// Per-channel long-press sheet: [Share secret] [Remove channel] [Cancel].
static lv_obj_t* s_channel_long_sheet = nullptr;
static int       s_channel_long_idx = -1;

static void closeChannelLongSheet() {
  if (s_channel_long_sheet) {
    lv_obj_del(s_channel_long_sheet);
    s_channel_long_sheet = nullptr;
  }
}

static void channelLongSheetDismissCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeChannelLongSheet();
}

static void channelLongSheetShareCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const int t = s_channel_long_idx;
  closeChannelLongSheet();
  if (t < 0 || !g_lv.task) return;
  bool ch; uint16_t unread; uint32_t ts;
  char tname[UITask::MAX_THREAD_NAME + 1];
  if (!g_lv.task->getThreadInfo(t, ch, unread, ts, tname, sizeof(tname)) || !ch) return;
#ifdef MAX_GROUP_CHANNELS
  ChannelDetails cd{};
  for (int i = 0; i < MAX_GROUP_CHANNELS; ++i) {
    if (the_mesh.getChannel(i, cd) && cd.name[0] &&
        strncmp(cd.name, tname, sizeof(cd.name)) == 0) {
      openChannelShareModal(cd.name, cd.channel.secret);
      return;
    }
  }
#endif
  if (g_lv.task) g_lv.task->showAlert("Channel not found", 1200);
}

static void channelLongSheetDeleteCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeChannelLongSheet();
  bool ch; uint16_t unread; uint32_t ts;
  char tname[UITask::MAX_THREAD_NAME + 1];
  if (!g_lv.task->getThreadInfo(s_chat_del_thread_idx, ch, unread, ts, tname, sizeof(tname))) return;
  char msg[80];
  snprintf(msg, sizeof(msg),
           "Remove channel \"%s\"?\nLeaves it on this device only.", tname);
  showConfirm(msg, "Remove", chatDeleteApply);
}

static void threadSheetMarkReadCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const int t = s_channel_long_idx;
  closeChannelLongSheet();
  if (t < 0 || !g_lv.task) return;
  g_lv.task->markThreadRead(t);
  g_lv.dirty_threads = true;
  g_lv.task->showAlert("Marked read", 900);
}
static void threadSheetDeleteDmCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const int t = s_channel_long_idx;
  closeChannelLongSheet();
  if (t < 0 || !g_lv.task) return;
  bool ch; uint16_t unread; uint32_t ts; char name[UITask::MAX_THREAD_NAME + 1] = "";
  if (!g_lv.task->getThreadInfo(t, ch, unread, ts, name, sizeof(name))) return;
  s_chat_del_thread_idx = t;
  s_chat_del_is_channel = false;
  char msg[96];
  snprintf(msg, sizeof(msg), "Delete chat with \"%s\"?\nMessage history will be cleared.", name);
  showConfirm(msg, "Delete", chatDeleteApply);
}

// Long-press action sheet for a thread (DM or channel). Channels also get
// Share secret / Remove; DMs get Delete. Both get Mark as read.
static void openThreadActionSheet(int thread_idx, const char* name, bool is_channel) {
  closeChannelLongSheet();
  s_channel_long_idx = thread_idx;

  s_channel_long_sheet = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_channel_long_sheet);
  // Backdrop sits below the global status bar — keeps the centered card
  // from being shoved partway behind the time/battery row and lets the
  // bar stay readable while the sheet is open.
  lv_obj_set_size(s_channel_long_sheet, lv_disp_get_hor_res(nullptr),
                  lv_disp_get_ver_res(nullptr) - STATUSBAR_H);
  lv_obj_set_pos(s_channel_long_sheet, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_channel_long_sheet, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_channel_long_sheet, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_channel_long_sheet, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_foreground(s_channel_long_sheet);
  lv_obj_add_event_cb(s_channel_long_sheet, channelLongSheetDismissCb, LV_EVENT_CLICKED, nullptr);

  const int card_w = 220;
  const int btn_h = 42;
  const int pad = 10;
  const int rows = is_channel ? 3 : 2;   // mark-read + (share/remove | delete)
  const int card_h = 36 + rows * (btn_h + 6) + pad;
  lv_obj_t* card = lv_obj_create(s_channel_long_sheet);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, pad, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  addCloseXBadge(card, channelLongSheetDismissCb);

  lv_obj_t* title = lv_label_create(card);
  char nm[40];
  copyUtf8ReplacingMissingGlyphs(&g_font_14, nm, sizeof(nm), name ? name : "");
  lv_label_set_text_fmt(title, "%s  %s", is_channel ? LV_SYMBOL_LOOP : LV_SYMBOL_ENVELOPE,
                        nm[0] ? nm : (is_channel ? "(channel)" : "(chat)"));
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &g_font_14, LV_PART_MAIN);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  // Trim 32 px on the right so long channel names don't slide under the X.
  lv_obj_set_width(title, card_w - 2 * pad - 32);
  lv_obj_set_pos(title, 0, 0);

  int y = 28;
  auto mk = [&](const char* lbl, lv_event_cb_t cb, uint32_t bg) {
    lv_obj_t* b = lv_btn_create(card);
    lv_obj_set_size(b, card_w - 2 * pad, btn_h);
    lv_obj_set_pos(b, 0, y);
    styleButton(b);
    if (bg) lv_obj_set_style_bg_color(b, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, lbl);
    lv_obj_center(l);
    y += btn_h + 6;
  };
  mk(LV_SYMBOL_OK "  Mark as read", threadSheetMarkReadCb, 0);
  if (is_channel) {
    mk(LV_SYMBOL_SHUFFLE "  Share secret",   channelLongSheetShareCb,  0);
    mk(LV_SYMBOL_TRASH   "  Remove channel", channelLongSheetDeleteCb, 0xB23A48);
  } else {
    mk(LV_SYMBOL_TRASH   "  Delete chat",    threadSheetDeleteDmCb,    0xB23A48);
  }
}

static void chatDeleteApply() {
  if (!g_lv.task || s_chat_del_thread_idx < 0) return;
  const int idx = s_chat_del_thread_idx;
  const bool is_channel = s_chat_del_is_channel;
  s_chat_del_thread_idx = -1;

  // For a channel thread we also drop the slot from the_mesh's channel
  // table so the same secret doesn't keep matching new floods. For a DM
  // thread we just drop the UI-side thread; the contact stays in the
  // Contacts tab and can be removed there separately if the user wants.
  int channel_slot = -1;
  if (is_channel) {
    bool ch; uint16_t unread; uint32_t ts;
    char name[UITask::MAX_THREAD_NAME + 1];
    if (g_lv.task->getThreadInfo(idx, ch, unread, ts, name, sizeof(name)) && ch) {
#ifdef MAX_GROUP_CHANNELS
      ChannelDetails cd{};
      for (int i = 0; i < MAX_GROUP_CHANNELS; ++i) {
        if (the_mesh.getChannel(i, cd) && cd.name[0] &&
            strncmp(cd.name, name, sizeof(cd.name)) == 0) {
          channel_slot = i;
          break;
        }
      }
#endif
    }
  }

  if (!g_lv.task->removeThread(idx)) {
    g_lv.task->showAlert("Couldn't delete", 1200);
    return;
  }
  if (is_channel && channel_slot >= 0) {
    the_mesh.uiDeleteChannel(channel_slot);
  }
  g_lv.dirty_threads = true;
  g_lv.task->showAlert(is_channel ? "Channel removed" : "Chat removed", 1000);
}

static void threadLongPressCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_LONG_PRESSED || !g_lv.task) return;
  auto* ctx = static_cast<LvThreadButtonCtx*>(lv_event_get_user_data(e));
  if (!ctx) return;
  bool ch; uint16_t unread; uint32_t ts;
  char name[UITask::MAX_THREAD_NAME + 1] = "";
  if (!g_lv.task->getThreadInfo(ctx->idx, ch, unread, ts, name, sizeof(name))) return;
  // Swallow the rest of this gesture so the matching CLICKED that LVGL
  // would otherwise fire on release doesn't also open the chat behind the
  // confirm popup.
  lv_indev_t* act = lv_indev_get_act();
  if (act) lv_indev_wait_release(act);

  s_chat_del_thread_idx = ctx->idx;
  s_chat_del_is_channel = ch;

  // Both DMs and channels get an action sheet: Mark as read, plus manage
  // (channels: Share secret / Remove; DMs: Delete chat).
  openThreadActionSheet(ctx->idx, name, ch);
}

static void threadSelectCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  auto* ctx = static_cast<LvThreadButtonCtx*>(lv_event_get_user_data(e));
  if (!ctx) return;
  LvChatPanel& p = ctx->channel ? g_lv.ch : g_lv.dm;
  g_lv.task->enterThread(ctx->channel, ctx->idx);
  // Update header name
  bool ch; uint16_t unread; uint32_t ts; char name[UITask::MAX_THREAD_NAME + 1];
  if (g_lv.task->getThreadInfo(ctx->idx, ch, unread, ts, name, sizeof(name))) {
    char san[UITask::MAX_THREAD_NAME + 8];
    copyUtf8ReplacingMissingGlyphs(&g_font_12, san, sizeof(san), name);
    setChatStatusTitle(san);   // thread name → status bar (no in-chat header bar)
  }
  refreshChatDetail(p);
  p.detail_open = true;
  hideKb();
  if (p.overlay) {
    lv_obj_clear_flag(p.overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(p.overlay);
  }
#if defined(HAS_TDECK_KEYBOARD)
  // Physical keyboard: focus the composer on open so typing goes straight in.
  showKb(&p);
#endif
}

static void sendFromPanelCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  auto* p = static_cast<LvChatPanel*>(lv_event_get_user_data(e));
  if (!p || !p->composer_ta) return;
  const char* text = lv_textarea_get_text(p->composer_ta);
  if (!text || !text[0]) return;
  hideKb();
  g_lv.task->setComposerMode(true);
  g_lv.task->composerReset();
  for (const char* cp = text; *cp; ++cp) g_lv.task->composerAppendChar(*cp);
  if (g_lv.task->composerSend()) {
    lv_textarea_set_text(p->composer_ta, "");
    refreshChatDetail(*p);
    g_lv.dirty_threads = true;
  }
}

// ---- Quick-reply macro picker (composer bar → list icon) ----
// One sheet at a time; we cache the active panel pointer so the tap handler
// can stuff the chosen text into the right composer textarea (DM vs channel
// thread both reuse this picker).
static lv_obj_t*    s_qr_sheet  = nullptr;
static LvChatPanel* s_qr_panel  = nullptr;

// ===== Emoji / special-character picker =====================================
// Neither the on-screen keyboard nor the T-Deck's physical keyboard can type
// emoji or many special characters, so this popup is the way to insert them.
// The glyphs themselves render thanks to the extras_* fallback fonts (see
// initTouchFontFallbacks); this picker just inserts the UTF-8 bytes. Grid of
// tappable glyphs; tap inserts into the active composer + closes.
static lv_obj_t*    s_emoji_sheet = nullptr;
static lv_obj_t*    s_emoji_target_ta = nullptr;   // textarea to insert into
static lv_obj_t*    s_emoji_grid  = nullptr;       // the scrollable button grid
static int          s_emoji_sel   = -1;            // trackball-highlighted index (-1 = none)
static int          s_emoji_cols  = 1;             // grid columns (computed at open)

// Curated insert set, grouped. Kept in sync with what the extras fonts bake
// (see scripts/build/regen-extras-fonts.sh) — a glyph here that isn't in the
// font would just render as the font's notdef box, so only ship baked ones.
static const char* const k_emoji_items[] = {
  // faces
  "\xF0\x9F\x98\x80","\xF0\x9F\x98\x83","\xF0\x9F\x98\x84","\xF0\x9F\x98\x81",
  "\xF0\x9F\x98\x86","\xF0\x9F\x98\x85","\xF0\x9F\x98\x82","\xF0\x9F\x98\x89",
  "\xF0\x9F\x98\x8A","\xF0\x9F\x98\x8D","\xF0\x9F\x98\x98","\xF0\x9F\x98\x8B",
  "\xF0\x9F\x98\x9C","\xF0\x9F\x98\x8E","\xF0\x9F\x98\x8F","\xF0\x9F\x98\x92",
  "\xF0\x9F\x98\x9E","\xF0\x9F\x98\x94","\xF0\x9F\x98\xA2","\xF0\x9F\x98\xAD",
  "\xF0\x9F\x98\xA1","\xF0\x9F\x98\xA0","\xF0\x9F\x98\xB1","\xF0\x9F\x98\xB4",
  "\xF0\x9F\x98\xAC","\xF0\x9F\x98\x90",
  // gestures + people
  "\xF0\x9F\x91\x8D","\xF0\x9F\x91\x8E","\xF0\x9F\x91\x8C","\xE2\x9C\x8C",
  "\xF0\x9F\x91\x8A","\xE2\x9C\x8A","\xF0\x9F\x91\x8F","\xF0\x9F\x99\x8C",
  "\xF0\x9F\x99\x8F","\xF0\x9F\x92\xAA","\xF0\x9F\x91\x8B",
  // hearts
  "\xE2\x9D\xA4","\xF0\x9F\x92\x94","\xF0\x9F\x92\x95","\xF0\x9F\x92\x99",
  "\xF0\x9F\x92\x9A","\xF0\x9F\x92\x9B","\xF0\x9F\x92\x9C",
  // symbols / celebration
  "\xF0\x9F\x8E\x89","\xF0\x9F\x8E\x8A","\xE2\x9C\xA8","\xF0\x9F\x94\xA5",
  "\xF0\x9F\x92\xAF","\xE2\x9C\x85","\xE2\x9D\x8C","\xE2\x9D\x97",
  "\xE2\x9D\x93","\xE2\x9A\xA0","\xF0\x9F\x92\xA9","\xE2\xAD\x90",
  "\xF0\x9F\x8C\x9F","\xE2\x9A\xA1","\xE2\x98\x80","\xE2\x98\x81",
  "\xE2\x9D\x84","\xE2\x98\x94","\xE2\x98\x95","\xF0\x9F\x8D\xBB",
  "\xF0\x9F\x8D\x95","\xF0\x9F\x8E\x82","\xF0\x9F\x8E\x81","\xF0\x9F\x92\xAC",
  "\xF0\x9F\x92\xA4","\xF0\x9F\x92\xA5",
  // objects
  "\xF0\x9F\x9A\x80","\xF0\x9F\x93\xB7","\xF0\x9F\x93\xB1","\xF0\x9F\x92\xBB",
  "\xF0\x9F\x93\x8D","\xF0\x9F\x93\x8C","\xF0\x9F\x93\x85","\xF0\x9F\x94\x8B",
  "\xF0\x9F\x93\xA1","\xF0\x9F\x94\x91","\xF0\x9F\x94\x92","\xF0\x9F\x93\xA7",
  "\xF0\x9F\x8F\xA0","\xF0\x9F\x9A\x97","\xE2\x8C\x9A","\xF0\x9F\x92\xA1",
  // animals
  "\xF0\x9F\x90\xB6","\xF0\x9F\x90\xB1","\xF0\x9F\x90\xB8","\xF0\x9F\x90\xBB",
  "\xF0\x9F\x90\xA7","\xF0\x9F\x90\x9D",
  // special characters / punctuation / currency / math
  "\xE2\x80\x94","\xE2\x80\xA6","\xE2\x80\x9C","\xE2\x80\x9D","\xE2\x80\x98",
  "\xE2\x80\x99","\xE2\x86\x92","\xE2\x86\x90","\xC2\xB0","\xC2\xB1",
  "\xC3\x97","\xC3\xB7","\xE2\x82\xAC","\xC2\xA3","\xC2\xA5","\xC2\xA9",
  "\xC2\xAE","\xE2\x84\xA2","\xE2\x89\xA0","\xE2\x89\xA4","\xE2\x89\xA5",
  "\xC2\xBD","\xC2\xBC","\xC2\xBE",
};
static constexpr int k_emoji_count = (int)(sizeof(k_emoji_items) / sizeof(k_emoji_items[0]));

static void closeEmojiSheet() {
  if (s_emoji_sheet) { lv_obj_del(s_emoji_sheet); s_emoji_sheet = nullptr; }
  s_emoji_target_ta = nullptr;
  s_emoji_grid = nullptr;
  s_emoji_sel = -1;
}
static void emojiSheetCloseCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;  // backdrop only
  closeEmojiSheet();
}

// Insert item `idx` into the active composer and close. Shared by finger-tap
// (emojiPickCb) and the trackball selector (emojiSelectorClick).
static void emojiInsertIndex(int idx) {
  lv_obj_t* ta = s_emoji_target_ta;
  if (idx < 0 || idx >= k_emoji_count || !ta) { closeEmojiSheet(); return; }
  const char* g = k_emoji_items[idx];
  // Insert into the keyboard MIRROR when it's bound to this field — otherwise a
  // later kbMirrorSyncToReal() would overwrite our insert with the stale mirror
  // contents (same trap the Wi-Fi scan picker hit). When not bound, insert
  // straight into the real field.
  lv_obj_t* dest = (s_kb_bind_ta == ta && s_kb_mirror_ta) ? s_kb_mirror_ta : ta;
  lv_textarea_add_text(dest, g);
  closeEmojiSheet();
}
static void emojiPickCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  emojiInsertIndex((int)(intptr_t)lv_event_get_user_data(e));
}

// Paint the trackball-selected cell highlighted and the rest normal, and keep
// the selection scrolled into view. No-op when nothing is selected (finger-only).
static void emojiPaintSelection() {
  if (!s_emoji_grid) return;
  const uint32_t n = lv_obj_get_child_cnt(s_emoji_grid);
  for (uint32_t i = 0; i < n; ++i) {
    lv_obj_t* b = lv_obj_get_child(s_emoji_grid, i);
    if (!b) continue;
    const bool sel = ((int)i == s_emoji_sel);
    lv_obj_set_style_bg_color(b, lv_color_hex(sel ? COLOR_MENTION : 0x1A1B1C), LV_PART_MAIN);
    lv_obj_set_style_border_width(b, sel ? 2 : 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(b, lv_color_hex(0xCFE6FF), LV_PART_MAIN);
  }
  if (s_emoji_sel >= 0 && s_emoji_sel < (int)n) {
    lv_obj_t* b = lv_obj_get_child(s_emoji_grid, (uint32_t)s_emoji_sel);
    if (b) lv_obj_scroll_to_view(b, LV_ANIM_ON);
  }
}

// Trackball is high-resolution: one physical roll emits several motion counts,
// so stepping one cell per raw count made the selector fly across the grid.
// Accumulate raw motion and only advance one cell once the accumulator crosses
// kEmojiSelStep — so it takes a deliberate roll to move each cell.
static int s_emoji_acc_x = 0, s_emoji_acc_y = 0;
static constexpr int kEmojiSelStep = 3;   // raw counts per one-cell move (higher = less sensitive)

// Feed raw trackball motion; advances the highlighted cell at most one step per
// axis per call. Called from the trackball poll while the emoji sheet is open.
static void emojiSelectorMove(int rawdx, int rawdy) {
  if (!s_emoji_grid || k_emoji_count == 0) return;
  if (s_emoji_sel < 0) {             // first motion just lands on cell 0
    s_emoji_sel = 0; s_emoji_acc_x = s_emoji_acc_y = 0;
    emojiPaintSelection();
    return;
  }
  s_emoji_acc_x += rawdx;
  s_emoji_acc_y += rawdy;
  int dc = 0, dr = 0;
  if (s_emoji_acc_x >=  kEmojiSelStep) { dc =  1; s_emoji_acc_x = 0; }
  else if (s_emoji_acc_x <= -kEmojiSelStep) { dc = -1; s_emoji_acc_x = 0; }
  if (s_emoji_acc_y >=  kEmojiSelStep) { dr =  1; s_emoji_acc_y = 0; }
  else if (s_emoji_acc_y <= -kEmojiSelStep) { dr = -1; s_emoji_acc_y = 0; }
  if (dc == 0 && dr == 0) return;    // not enough travel yet

  const int cols = s_emoji_cols > 0 ? s_emoji_cols : 1;
  int idx = s_emoji_sel;
  if (dc) idx += dc;                          // horizontal: free move across the flat list
  if (dr) {
    const int ni = idx + dr * cols;           // vertical: jump a full row
    if (ni >= 0 && ni < k_emoji_count) idx = ni;
  }
  if (idx < 0) idx = 0;
  if (idx >= k_emoji_count) idx = k_emoji_count - 1;
  if (idx != s_emoji_sel) { s_emoji_sel = idx; emojiPaintSelection(); }
}

// Trackball centre-click while the sheet is open: insert the highlighted glyph.
// Returns true if it consumed the click (so it isn't also injected as a tap).
static bool emojiSelectorClick() {
  if (!s_emoji_sheet) return false;
  if (s_emoji_sel >= 0) emojiInsertIndex(s_emoji_sel);
  return true;   // swallow the click even if nothing selected yet
}

// Opened from the composer's emoji button. `ta` is the composer textarea.
static void openEmojiPicker(lv_obj_t* ta) {
  closeEmojiSheet();
  s_emoji_target_ta = ta;
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_emoji_sheet = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_emoji_sheet);
  lv_obj_set_size(s_emoji_sheet, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_emoji_sheet, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_emoji_sheet, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_emoji_sheet, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_emoji_sheet, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_emoji_sheet, emojiSheetCloseCb, LV_EVENT_CLICKED, nullptr);

  const lv_coord_t cardw = sw - 16;
  const lv_coord_t cardh = sh - STATUSBAR_H - 16;
  lv_obj_t* card = lv_obj_create(s_emoji_sheet);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, cardw, cardh);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  styleSurface(card, COLOR_PANEL, 8);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 8, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "Insert emoji / symbol");
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &g_font_14, LV_PART_MAIN);
  lv_obj_set_pos(title, 2, 0);
  addCloseXBadge(card, emojiSheetCloseCb);

  // Hint line: how to use the trackball (T-Deck) — finger tap also works.
  lv_obj_t* hint = lv_label_create(card);
  lv_label_set_text(hint, "Roll to highlight \xE2\x80\xA2 click to insert");
  lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(hint, &g_font_12, LV_PART_MAIN);
  lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -24, 4);

  // Scrollable grid of glyph buttons.
  const lv_coord_t grid_w = cardw - 16;
  lv_obj_t* grid = lv_obj_create(card);
  lv_obj_remove_style_all(grid);
  lv_obj_set_size(grid, grid_w, cardh - 16 - 26);
  lv_obj_set_pos(grid, 0, 26);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(grid, 4, LV_PART_MAIN);
  lv_obj_set_style_pad_column(grid, 4, LV_PART_MAIN);
  lv_obj_set_scroll_dir(grid, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);
  s_emoji_grid = grid;
  // Column count for the trackball selector's row jumps: floor((w + gap) /
  // (btn + gap)) with btn=38, gap=4. Matches the flex-wrap that LVGL computes.
  s_emoji_cols = (int)((grid_w + 4) / (38 + 4));
  if (s_emoji_cols < 1) s_emoji_cols = 1;
  s_emoji_sel = -1;   // start un-highlighted; first roll selects index 0
  s_emoji_acc_x = s_emoji_acc_y = 0;

  for (int i = 0; i < k_emoji_count; ++i) {
    lv_obj_t* b = lv_btn_create(grid);
    lv_obj_set_size(b, 38, 38);
    styleButton(b);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1A1B1C), LV_PART_MAIN);
    lv_obj_set_style_pad_all(b, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(b, emojiPickCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, k_emoji_items[i]);
    lv_obj_set_style_text_font(l, &g_font_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_center(l);
  }
}

static void openEmojiPickerCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  auto* p = static_cast<LvChatPanel*>(lv_event_get_user_data(e));
  if (!p || !p->composer_ta) return;
  openEmojiPicker(p->composer_ta);
}

static void closeQuickReplySheet() {
  if (s_qr_sheet) {
    lv_obj_del(s_qr_sheet);
    s_qr_sheet = nullptr;
  }
  s_qr_panel = nullptr;
}

static void qrPickCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  intptr_t idx = (intptr_t)lv_event_get_user_data(e);
  LvChatPanel* p = s_qr_panel;
  closeQuickReplySheet();
  if (!p || !p->composer_ta) return;
#if defined(ESP32)
  char buf[TOUCH_QUICK_REPLY_MAXLEN];
  int n = touchPrefsGetQuickReply((int)idx, buf, sizeof(buf));
  if (n <= 0) return;
  // Stuff into the LVGL textarea (no auto-send: lets the operator tweak
  // before tapping the send button — and keeps a stray tap from blasting
  // out a macro). To send-on-tap, route directly through composerSend
  // instead, but accidental sends are a worse UX than an extra tap.
  lv_textarea_set_text(p->composer_ta, buf);
  // Move keyboard focus to the textarea so a quick send tap works next.
  lv_obj_add_state(p->composer_ta, LV_STATE_FOCUSED);
#else
  (void)idx;
#endif
}

static void qrSheetCloseCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeQuickReplySheet();
}

static void openQuickReplyPickerCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  auto* p = static_cast<LvChatPanel*>(lv_event_get_user_data(e));
  if (!p) return;
  closeQuickReplySheet();
  s_qr_panel = p;

  lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_qr_sheet = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_qr_sheet);
  // Sit below the status bar — the QR card (~290 px tall with 6 macro
  // rows) was getting its top row clipped behind the time/battery row.
  lv_obj_set_size(s_qr_sheet, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_qr_sheet, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_qr_sheet, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_qr_sheet, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_qr_sheet, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_qr_sheet, qrSheetCloseCb, LV_EVENT_CLICKED, nullptr);

  const int card_w = 220;
  const int btn_h  = 32;          // 34→32: 6 macro rows have to fit in the
  const int pad    = 8;           // visible area (298 px) below the status
  const int title_h = 26;         // bar — the old sizing produced a 302 px
  const int hint_h  = 22;         // card that clipped behind the bar.
  const int card_h = title_h + TOUCH_QUICK_REPLY_COUNT * (btn_h + 4) + hint_h + pad;
  lv_obj_t* card = lv_obj_create(s_qr_sheet);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, pad, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  addCloseXBadge(card, qrSheetCloseCb);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "Quick reply");
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &g_font_14, LV_PART_MAIN);
  lv_obj_set_pos(title, 0, 0);

  int y = title_h;
#if defined(ESP32)
  for (int i = 0; i < TOUCH_QUICK_REPLY_COUNT; ++i) {
    char buf[TOUCH_QUICK_REPLY_MAXLEN];
    int n = touchPrefsGetQuickReply(i, buf, sizeof(buf));
    if (n <= 0) { strncpy(buf, "(empty)", sizeof(buf) - 1); buf[sizeof(buf)-1] = '\0'; }
    lv_obj_t* b = lv_btn_create(card);
    lv_obj_set_size(b, card_w - 2 * pad, btn_h);
    lv_obj_set_pos(b, 0, y);
    styleButton(b);
    lv_obj_set_style_bg_color(b, lv_color_hex(n > 0 ? 0x1A1B1C : 0x0C0D0E), LV_PART_MAIN);
    lv_obj_add_event_cb(b, qrPickCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_t* lbl = lv_label_create(b);
    lv_label_set_text(lbl, buf);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, card_w - 2 * pad - 16);
    lv_obj_set_style_text_font(lbl, &g_font_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(n > 0 ? COLOR_TEXT : COLOR_SUB), LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
    y += btn_h + 4;   // match the 4-px gap used in card_h calc above
  }
#endif
  lv_obj_t* hint = lv_label_create(card);
  lv_label_set_text(hint, "Edit in Settings \xe2\x86\x92 Quick replies");
  lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(hint, &g_font_12, LV_PART_MAIN);
  lv_obj_set_pos(hint, 0, y + 2);
}

static void tabChangedCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  scheduleHeavyRefresh(170);
  closeSettingsModal();

  const int new_t         = getActiveTab();
  const bool leaving_inbox =
      (s_lv_tab_prev == CHAT_INBOX_TAB_INDEX && new_t != CHAT_INBOX_TAB_INDEX);
  if (leaving_inbox) {
    hideKb();
    if (g_lv.dm.detail_open && g_lv.dm.overlay) {
      lv_obj_add_flag(g_lv.dm.overlay, LV_OBJ_FLAG_HIDDEN);
      g_lv.dm.detail_open = false;
    }
    if (g_lv.ch.detail_open && g_lv.ch.overlay) {
      lv_obj_add_flag(g_lv.ch.overlay, LV_OBJ_FLAG_HIDDEN);
      g_lv.ch.detail_open = false;
    }
  } else {
    hideKb();
  }

  // Chats / Contacts tab pages are non-scrolling shells; reset any stale scroll offset.
  if (new_t == CHAT_INBOX_TAB_INDEX || new_t == CONTACTS_TAB_INDEX) {
    lv_obj_t* t = getActiveTabPage();
    if (t) lv_obj_scroll_to(t, 0, 0, LV_ANIM_OFF);
  }

  const int prev_t = s_lv_tab_prev;
  s_lv_tab_prev = new_t;
  if (new_t == CONTACTS_TAB_INDEX) refreshContactsList();
  if (new_t == MAP_TAB_INDEX) {
    applyMapChrome(true);    // transparent status bar + tab bar so the map shows through
    onMapTabActivated();
  } else {
    if (prev_t == MAP_TAB_INDEX) applyMapChrome(false);   // restore opaque chrome
    // Drop tile JPEGs from PSRAM the moment we leave the tab — keeps the
    // working set small and lets the map cold-load with fresh data on
    // the next visit. (CPU stays at 160 MHz — the whole UI runs there.)
    freeMapTiles();
  }

  // Settings tab: reclaim the top status-bar strip — the dense settings UI is
  // cramped otherwise. Hide the global bar and raise/grow the tabview to the full
  // height; restore on every other tab. Runs after the map-chrome block so a
  // map → settings switch still ends up with the bar hidden.
  {
    const bool on_settings = (new_t == SETTINGS_TAB_INDEX);
    const lv_coord_t hor = lv_disp_get_hor_res(nullptr);
    const lv_coord_t ver = lv_disp_get_ver_res(nullptr);
    if (g_lv.tabview) {
      lv_obj_set_pos(g_lv.tabview, 0, on_settings ? 0 : STATUSBAR_H);
      lv_obj_set_size(g_lv.tabview, hor, on_settings ? ver : (lv_coord_t)(ver - STATUSBAR_H));
    }
    if (g_statusbar.root) {
      if (on_settings) lv_obj_add_flag(g_statusbar.root, LV_OBJ_FLAG_HIDDEN);
      else              lv_obj_clear_flag(g_statusbar.root, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (g_lv.task) g_lv.task->onLvTabChanged(new_t);
}

static void homeTileCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  uintptr_t idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  // goToTab is defined below; can call it here because this runs at runtime
  if (g_lv.tabview) lv_tabview_set_act(g_lv.tabview, static_cast<uint32_t>(idx), LV_ANIM_ON);
  scheduleHeavyRefresh(170);
  s_lv_tab_prev = static_cast<int>(idx);
  if (g_lv.task) g_lv.task->onLvTabChanged(static_cast<int>(idx));
}

static void resetPathCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  g_lv.task->resetActiveDmPath();
  refreshStatusLabels();
}

static void toggleTcpCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  g_lv.task->isTcpEnabled() ? g_lv.task->disableTcp() : g_lv.task->enableTcp();
  g_lv.task->showAlert(g_lv.task->isTcpEnabled() ? "TCP on" : "TCP off", 900);
  refreshStatusLabels();
}

static void toggleBleCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task || !g_lv.task->hasBleCapability()) return;
  g_lv.task->isBleEnabled() ? g_lv.task->disableBle() : g_lv.task->enableBle();
  g_lv.task->showAlert(g_lv.task->isBleEnabled() ? "BLE on" : "BLE off", 900);
  refreshStatusLabels();
}

static void toggleGpsCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  g_lv.task->toggleGPS();
  g_lv.task->showAlert(g_lv.task->getGPSState() ? "GPS on" : "GPS off", 900);
  refreshStatusLabels();
}

static void toggleBuzzerCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  g_lv.task->toggleBuzzer();
  const bool quiet = g_lv.task->isBuzzerQuiet();
  // Refresh the button's own label in place — the Device page isn't rebuilt on
  // tap, so a static "Sound: on" would otherwise never change.
  lv_obj_t* btn = lv_event_get_target(e);
  if (btn) {
    lv_obj_t* lbl = lv_obj_get_child(btn, 0);
    if (lbl) lv_label_set_text_fmt(lbl, "Sound: %s", quiet ? "off" : "on");
  }
  g_lv.task->showAlert(quiet ? "Sound off" : "Sound on", 900);
#if defined(HAS_TDECK_GT911)
  if (!quiet) tdeckPlayNotify();   // confirmation chime when enabling
#endif
  refreshStatusLabels();
}

static void settingsFieldFocusCb(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  // Drop LV_EVENT_PRESSED: it fires on touch-down before LVGL can tell
  // whether the gesture is a tap or the start of a vertical scroll inside
  // the settings body. Reacting to PRESSED meant any scroll attempt that
  // started on a textarea spawned the keyboard. CLICKED only fires after
  // release IF the press qualified as a click (movement under the scroll
  // threshold), so it's the right hook for "open keyboard on real tap".
  if (code != LV_EVENT_FOCUSED && code != LV_EVENT_CLICKED) return;
  if (!g_lv.keyboard) return;
  // Belt-and-suspenders: even CLICKED can fire at the tail end of a flick
  // if the touch happened to lift while still on the textarea. Same guard
  // used for contactSelectCb — if anything is currently being scrolled,
  // treat this CLICKED as scroll spillover, not a deliberate tap.
  lv_indev_t* act = lv_indev_get_act();
  if (act) {
    if (lv_indev_get_scroll_obj(act)) return;
    if (lv_indev_get_scroll_dir(act) != LV_DIR_NONE) return;
  }
  lv_obj_t* ta = lv_event_get_target(e);
  if (!ta) return;
  s_kb_panel = nullptr;
  kbMirrorBind(ta);
  noteKbActivity();   // focusing/tapping a field lights the auto backlight
}

// ===== Clipboard ============================================================
// Mirrors MCterm's in-RAM clipboard. Anything the user long-presses to copy
// lands here; long-press on a textarea pastes the contents at the cursor.
// One global buffer keeps the code simple and avoids heap churn.
constexpr size_t CLIPBOARD_MAX = 192;
static char s_clipboard[CLIPBOARD_MAX] = {0};

static void clipboardSet(const char* text, const char* tag) {
  if (!text) return;
  size_t n = strlen(text);
  if (n >= CLIPBOARD_MAX) n = CLIPBOARD_MAX - 1;
  memcpy(s_clipboard, text, n);
  s_clipboard[n] = '\0';
  if (g_lv.task) {
    char toast[60];
    snprintf(toast, sizeof(toast), "Copied %s",
             tag && tag[0] ? tag : "");
    g_lv.task->showAlert(toast, 900);
  }
}

// LV_EVENT_LONG_PRESSED handler: copies the target object's label text.
// User-data carries an optional tag shown in the "Copied X" toast.
static void copyLabelLongPressCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_LONG_PRESSED) return;
  lv_obj_t* obj = lv_event_get_target(e);
  if (!obj) return;
  const char* text = lv_label_get_text(obj);
  if (!text || !text[0]) return;
  lv_indev_t* act = lv_indev_get_act();
  if (act) lv_indev_wait_release(act);   // swallow the trailing CLICKED
  clipboardSet(text, static_cast<const char*>(lv_event_get_user_data(e)));
}

// LV_EVENT_LONG_PRESSED handler on a textarea: pastes clipboard at cursor.
// When the keyboard is bound to a mirror, the paste goes into the mirror so
// the keyboard editor sees it and live-sync propagates it to the real ta.
static void pasteTextareaLongPressCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_LONG_PRESSED) return;
  if (!s_clipboard[0]) return;
  lv_obj_t* ta = lv_event_get_target(e);
  if (!ta) return;
  lv_obj_t* dest = (s_kb_bind_ta == ta && s_kb_mirror_ta) ? s_kb_mirror_ta : ta;
  lv_textarea_add_text(dest, s_clipboard);
  lv_indev_t* act = lv_indev_get_act();
  if (act) lv_indev_wait_release(act);
}

// Touch-down on a text field = backlight activity. Hooked on PRESSED (which
// fires reliably on every tap, including on an already-focused field — unlike
// CLICKED/FOCUSED, which the swipe detector can drop via lv_indev_wait_release).
// It only bumps the auto backlight; it does NOT spawn the keyboard, so the
// scroll-vs-tap reason PRESSED is kept out of settingsFieldFocusCb doesn't apply.
static void kbActivityPressCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;
  noteKbActivity();
}

static void attachSettingsTaEvents(lv_obj_t* ta) {
  lv_obj_add_event_cb(ta, settingsFieldFocusCb, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(ta, settingsFieldFocusCb, LV_EVENT_CLICKED, nullptr);
  // PRESSED is used only for the keyboard backlight (see kbActivityPressCb),
  // never for focus — settingsFieldFocusCb still avoids it for the scroll case.
  lv_obj_add_event_cb(ta, kbActivityPressCb, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(ta, pasteTextareaLongPressCb, LV_EVENT_LONG_PRESSED, nullptr);
}

static void closeSettingsModal() {
  hideKb();
  if (g_set_modal.root) {
    lv_obj_del(g_set_modal.root);
  }
  // Pointers into the add-channel modal body get freed with the root above;
  // null them so a later submit (queued event) doesn't dereference garbage.
  s_addch_name_ta    = nullptr;
  s_addch_secret_ta  = nullptr;
  s_addch_hashtag_ta = nullptr;
  s_addch_error_l    = nullptr;
  s_addct_pub_ta     = nullptr;
  s_addct_name_ta    = nullptr;
  s_addct_error_l    = nullptr;
  resetSettingsModalState();
}

static void settingsCloseCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  (void)e;
  closeSettingsModal();
  refreshStatusLabels();
}

// When non-null, createSettingsModal() skips the modal chrome and returns an
// inline content container (a flex item, with a coloured section header) on
// this page — so the Set tab's sub-tab pages can host the exact same builders
// inline. Set per page in makeSettings; g_set_modal is reset ONCE before the
// pages are populated, and each builder writes its own distinct widget ptrs.
static lv_obj_t* s_settings_inline_parent = nullptr;
// Width (px) of the current settings content area — the inline card's inner
// width, or the modal body's inner width. Builders read it to right-align
// switches and spread multi-column rows responsively.
static lv_coord_t s_settings_content_w = 280;

static lv_obj_t* createSettingsModal(const char* title, SettingsModalKind kind) {
  if (s_settings_inline_parent) {
    // Grouped-card section: a muted group header above a rounded panel card with
    // internal padding. The card fills the page content width (symmetric 8 px
    // page margins); the returned `content` is the card's inner area, with an
    // explicit pixel width (lv_pct does NOT resolve through a flex column of
    // LV_SIZE_CONTENT items). The card padding keeps every control clear of the
    // screen edge and gives consistent visual separation between groups.
    const lv_coord_t card_w    = lv_disp_get_hor_res(nullptr) - 16;
    const lv_coord_t card_pad  = 8;
    const lv_coord_t content_w = card_w - card_pad * 2;

    lv_obj_t* wrap = lv_obj_create(s_settings_inline_parent);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_width(wrap, card_w);
    lv_obj_set_height(wrap, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(wrap, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(wrap, 14, LV_PART_MAIN);   // gap between cards
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
    if (title && title[0]) {
      lv_obj_t* h = lv_label_create(wrap);
      lv_label_set_text(h, title);
      lv_obj_set_style_text_font(h, &g_font_12, LV_PART_MAIN);
      lv_obj_set_style_text_color(h, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
      lv_obj_set_style_pad_left(h, 4, LV_PART_MAIN);
      lv_obj_set_style_pad_bottom(h, 4, LV_PART_MAIN);
    }
    lv_obj_t* card = lv_obj_create(wrap);
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, card_w);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x121417), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2A2E34), LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, card_pad, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* content = lv_obj_create(card);
    lv_obj_remove_style_all(content);
    lv_obj_set_width(content, content_w);
    lv_obj_set_height(content, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    (void)kind;
    s_settings_content_w = content_w;
    return content;
  }
  closeSettingsModal();

  lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  lv_obj_t* root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(root);
  // Root sits BELOW the global status bar (always on top, drawn on
  // lv_layer_sys). Without this offset the modal's own header (title +
  // Close button at y=0) was clipped behind the time/battery row.
  lv_obj_set_size(root, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(root, 0, STATUSBAR_H);
  styleSurface(root, COLOR_BG, 0);
  lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_foreground(root);

  lv_obj_t* header = lv_obj_create(root);
  lv_obj_remove_style_all(header);
  lv_obj_set_size(header, sw, 46);
  lv_obj_set_pos(header, 0, 0);
  styleSurface(header, COLOR_PANEL, 0);
  lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_style_border_width(header, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(header, lv_color_hex(0x18191A), LV_PART_MAIN);

  lv_obj_t* lbl = lv_label_create(header);
  lv_label_set_text(lbl, title);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl, &g_font_14, LV_PART_MAIN);
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);

  lv_obj_t* close_btn = lv_btn_create(header);
  lv_obj_set_size(close_btn, 58, 32);
  lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -6, 0);
  styleButton(close_btn);
  lv_obj_add_event_cb(close_btn, settingsCloseCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* close_lbl = lv_label_create(close_btn);
  lv_label_set_text(close_lbl, "Close");
  lv_obj_center(close_lbl);

  lv_obj_t* body = lv_obj_create(root);
  lv_obj_remove_style_all(body);
  lv_obj_set_size(body, sw - 8, sh - 52);
  lv_obj_set_pos(body, 4, 48);
  styleSurface(body, COLOR_BG, 0);
  lv_obj_set_style_pad_all(body, 6, LV_PART_MAIN);
  lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_event_cb(body, scrollClampOnEndCb, LV_EVENT_SCROLL_END, nullptr);

  // Single content child holding ALL the controls, sized to its contents. The
  // scrollable `body` therefore only ever has ONE child to scroll — tall modals
  // (e.g. Device, with 20+ controls) used to pile every control directly into
  // the scroll view, which wedged LVGL's scroll-bounds machinery so taps showed
  // their press style but never fired CLICKED ("buttons unresponsive"). Modals
  // add their widgets to this container; it grows and `body` scrolls it.
  lv_obj_t* content = lv_obj_create(body);
  lv_obj_remove_style_all(content);
  lv_obj_set_width(content, lv_pct(100));
  lv_obj_set_height(content, LV_SIZE_CONTENT);
  lv_obj_set_pos(content, 0, 0);
  lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
  lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

  resetSettingsModalState();
  g_set_modal.root = root;
  g_set_modal.kind = kind;
  s_settings_content_w = (sw - 8) - 12;   // modal body width minus its 6px padding each side
  return content;
}

static void saveProfileNameCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task || !g_set_modal.name_ta) return;
  kbMirrorSyncToReal();
  const char* name = lv_textarea_get_text(g_set_modal.name_ta);
  if (g_lv.task->setNodeName(name)) {
    g_lv.task->showAlert("Name saved", 1000);
    refreshStatusLabels();
  }
}

static void saveProfilePosCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  kbMirrorSyncToReal();
  float lat = 0.0f, lon = 0.0f;
  if (!parseFloatField(g_set_modal.lat_ta, lat) || !parseFloatField(g_set_modal.lon_ta, lon)) {
    g_lv.task->showAlert("Invalid lat/lon", 1200);
    return;
  }
  if (g_lv.task->setPosition(static_cast<double>(lat), static_cast<double>(lon))) {
    g_lv.task->showAlert("Position saved", 1000);
    refreshStatusLabels();
  }
}

static void saveRadioParamsCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  kbMirrorSyncToReal();
  float freq = 0.0f, bw = 0.0f, af = 0.0f;
  int sf = 0, cr = 0, tx = 0;
  if (!parseFloatField(g_set_modal.freq_ta, freq) || !parseFloatField(g_set_modal.bw_ta, bw) ||
      !parseIntField(g_set_modal.sf_ta, sf) || !parseIntField(g_set_modal.cr_ta, cr) ||
      !parseIntField(g_set_modal.tx_ta, tx) || !parseFloatField(g_set_modal.airtime_ta, af)) {
    g_lv.task->showAlert("Invalid radio values", 1200);
    return;
  }
  if (g_lv.task->setRadioParams(freq, bw, static_cast<uint8_t>(sf), static_cast<uint8_t>(cr),
                                static_cast<int8_t>(tx), af)) {
    g_lv.task->showAlert("Radio applied", 1000);
    refreshStatusLabels();
  }
}

static void saveAutoAddCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  kbMirrorSyncToReal();
  int max_hops = 0;
  if (!parseIntField(g_set_modal.max_hops_ta, max_hops)) {
    g_lv.task->showAlert("Invalid max hops", 1200);
    return;
  }
  if (max_hops < 0) max_hops = 0;
  if (max_hops > 64) max_hops = 64;

  uint8_t mask = 0;
  if (g_set_modal.auto_overwrite_sw && lv_obj_has_state(g_set_modal.auto_overwrite_sw, LV_STATE_CHECKED)) mask |= AUTO_ADD_OVERWRITE_OLDEST;
  if (g_set_modal.auto_chat_sw && lv_obj_has_state(g_set_modal.auto_chat_sw, LV_STATE_CHECKED)) mask |= AUTO_ADD_CHAT;
  if (g_set_modal.auto_rep_sw && lv_obj_has_state(g_set_modal.auto_rep_sw, LV_STATE_CHECKED)) mask |= AUTO_ADD_REPEATER;
  if (g_set_modal.auto_room_sw && lv_obj_has_state(g_set_modal.auto_room_sw, LV_STATE_CHECKED)) mask |= AUTO_ADD_ROOM_SERVER;
  if (g_set_modal.auto_sensor_sw && lv_obj_has_state(g_set_modal.auto_sensor_sw, LV_STATE_CHECKED)) mask |= AUTO_ADD_SENSOR;
  uint8_t manual = (g_set_modal.manual_add_sw && lv_obj_has_state(g_set_modal.manual_add_sw, LV_STATE_CHECKED)) ? 1u : 0u;

  g_lv.task->setAutoAddConfig(mask, static_cast<uint8_t>(max_hops), manual);
  g_lv.task->showAlert("Auto-add saved", 1000);
  refreshStatusLabels();
}

// Auto-save on toggle: the auto-add switches persist immediately, so the user
// doesn't have to press "Save auto-add" for them to stick across reboots.
// Recomputes the whole mask from the current switch states + the hops field.
static void autoAddSwitchCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || !g_lv.task) return;
  uint8_t mask = 0;
  if (g_set_modal.auto_overwrite_sw && lv_obj_has_state(g_set_modal.auto_overwrite_sw, LV_STATE_CHECKED)) mask |= AUTO_ADD_OVERWRITE_OLDEST;
  if (g_set_modal.auto_chat_sw && lv_obj_has_state(g_set_modal.auto_chat_sw, LV_STATE_CHECKED)) mask |= AUTO_ADD_CHAT;
  if (g_set_modal.auto_rep_sw && lv_obj_has_state(g_set_modal.auto_rep_sw, LV_STATE_CHECKED)) mask |= AUTO_ADD_REPEATER;
  if (g_set_modal.auto_room_sw && lv_obj_has_state(g_set_modal.auto_room_sw, LV_STATE_CHECKED)) mask |= AUTO_ADD_ROOM_SERVER;
  if (g_set_modal.auto_sensor_sw && lv_obj_has_state(g_set_modal.auto_sensor_sw, LV_STATE_CHECKED)) mask |= AUTO_ADD_SENSOR;
  uint8_t manual = (g_set_modal.manual_add_sw && lv_obj_has_state(g_set_modal.manual_add_sw, LV_STATE_CHECKED)) ? 1u : 0u;
  int max_hops = 3;
  int mh;
  if (g_set_modal.max_hops_ta && parseIntField(g_set_modal.max_hops_ta, mh)) max_hops = mh;
  if (max_hops < 0) max_hops = 0;
  if (max_hops > 64) max_hops = 64;
  g_lv.task->setAutoAddConfig(mask, static_cast<uint8_t>(max_hops), manual);
}

static void savePolicyCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  uint8_t share = (g_set_modal.share_loc_sw && lv_obj_has_state(g_set_modal.share_loc_sw, LV_STATE_CHECKED)) ? 1u : 0u;
  g_lv.task->setAdvertLocationPolicy(share);   // path-hash size lives in Radio settings now
  g_lv.task->showAlert("Advert policy saved", 1000);
  refreshStatusLabels();
}

static void saveExperimentalCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  uint8_t multi = (g_set_modal.exp_multi_sw && lv_obj_has_state(g_set_modal.exp_multi_sw, LV_STATE_CHECKED)) ? 1u : 0u;
  uint8_t repeat = (g_set_modal.exp_repeat_sw && lv_obj_has_state(g_set_modal.exp_repeat_sw, LV_STATE_CHECKED)) ? 1u : 0u;
  uint8_t boost = (g_set_modal.exp_boost_sw && lv_obj_has_state(g_set_modal.exp_boost_sw, LV_STATE_CHECKED)) ? 1u : 0u;
  g_lv.task->setExperimentalFlags(multi, repeat, boost);
#if defined(ESP32)
  bool dc_show = (g_set_modal.exp_dc_sw && lv_obj_has_state(g_set_modal.exp_dc_sw, LV_STATE_CHECKED));
  touchPrefsSetDutyMeterShown(dc_show);
#endif
  g_lv.task->showAlert("Experimental saved", 1000);
  refreshStatusLabels();
}

static void syncClockCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  g_lv.task->setDeviceTimeFromSystemClock();
  g_lv.task->showAlert("Clock synced", 900);
}

static void advertNowCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  g_lv.task->showAlert(g_lv.task->sendAdvertNow() ? "Advert sent" : "Advert failed", 900);
}

static void rebootCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  g_lv.task->showAlert("Rebooting...", 600);
  g_lv.task->rebootDevice();
}

#if defined(ESP32)
// True when the running image has a spare OTA app slot to write an update into.
// Standalone meshcomod ships a dual-OTA (A/B) table -> true -> in-firmware +
// phone-app update works. Installed under Launcher (single app slot) there's no
// spare partition -> false -> the update affordances hide, so the same app-only
// bin behaves correctly in both worlds. (Replaces the retired reboot-to-recovery
// helpers — the recovery firmware is gone.)
static bool touchHasOtaUpdateSlot() {
  return esp_ota_get_next_update_partition(NULL) != NULL;
}
#endif

// ---- Advert modal: pick flood (multi-hop) or zero-hop ----
static void advertFloodCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  bool ok = g_lv.task->sendAdvertFlood();
  g_lv.task->showAlert(ok ? "Flood advert sent" : "Advert failed", 1000);
  closeSettingsModal();
}

static void advertZeroHopCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  bool ok = g_lv.task->sendAdvertZeroHop();
  g_lv.task->showAlert(ok ? "Zero-hop advert sent" : "Advert failed", 1000);
  closeSettingsModal();
}

static void openAdvertModalCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t* body = createSettingsModal("Send advert", SettingsModalKind::Advert);
  int y = 4;

  lv_obj_t* hint = lv_label_create(body);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(hint, lv_pct(100));
  lv_obj_set_pos(hint, 2, y);
  lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(hint, &g_font_12, LV_PART_MAIN);
  lv_label_set_text(hint,
    "Flood: relays will rebroadcast your advert across the mesh (multi-hop).\n"
    "Zero-hop: only neighbours hear you (no relaying).");
  y += 60;

  lv_obj_t* b_flood = lv_btn_create(body);
  lv_obj_set_size(b_flood, lv_pct(100),40);
  lv_obj_set_pos(b_flood, 2, y);
  styleButton(b_flood);
  lv_obj_add_event_cb(b_flood, advertFloodCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lf = lv_label_create(b_flood);
  lv_label_set_text(lf, LV_SYMBOL_UPLOAD "  Flood advert");
  lv_obj_center(lf);
  y += 46;

  lv_obj_t* b_zh = lv_btn_create(body);
  lv_obj_set_size(b_zh, lv_pct(100),40);
  lv_obj_set_pos(b_zh, 2, y);
  styleButton(b_zh);
  lv_obj_add_event_cb(b_zh, advertZeroHopCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lz = lv_label_create(b_zh);
  lv_label_set_text(lz, LV_SYMBOL_WIFI "  Zero-hop advert");
  lv_obj_center(lz);
}

// ---- Discovered list modal: shows adverts NOT yet in contacts[], with "Add" buttons ----
struct DiscoveredAddCtx { int slot_idx; };
static DiscoveredAddCtx s_disc_add_ctx[DISCOVERED_MAX];

// Forward declared so discoveredAddCb can re-open the list after a successful add.
static void openDiscoveredModalCb(lv_event_t* e);

static void discoveredAddCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  auto* ctx = static_cast<DiscoveredAddCtx*>(lv_event_get_user_data(e));
  if (!ctx) return;
  int idx = ctx->slot_idx;
  if (idx < 0 || idx >= DISCOVERED_MAX) return;
  LvDiscoveredEntry& e_disc = s_discovered[idx];
  if (!e_disc.used) return;

  // Add the captured ContactInfo straight to the_mesh.
  if (the_mesh.addContact(e_disc.ci)) {
    the_mesh.uiPersistContacts();   // write /contacts3 so the add survives reboot
    e_disc.in_contacts = true;
    g_lv.task->showAlert("Added to contacts", 1000);
    // Re-open the Discovered modal so the list updates immediately.
    closeSettingsModal();
    lv_event_t synth{};
    synth.code = LV_EVENT_CLICKED;
    openDiscoveredModalCb(&synth);
  } else {
    g_lv.task->showAlert("Contacts full", 1200);
  }
}

static void openDiscoveredModalCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t* body = createSettingsModal("Discovered", SettingsModalKind::Discovered);
  int y = 0;

  // Auto-add hint: any of the four "auto-add" type bits set means new contacts
  // of those kinds bypass this list and land straight in the Contacts tab.
  // Surfacing it here avoids the "where did my discoveries go?" confusion.
  {
    NodePrefs* prefs = the_mesh.getNodePrefs();
    const uint8_t mask = prefs ? (prefs->autoadd_config & static_cast<uint8_t>(
        AUTO_ADD_CHAT | AUTO_ADD_REPEATER | AUTO_ADD_ROOM_SERVER | AUTO_ADD_SENSOR)) : 0u;
    if (mask) {
      char hint[120];
      char types[64]; types[0] = '\0';
      size_t off = 0;
      auto add = [&](const char* s) {
        if (off > 0 && off + 3 < sizeof(types)) { types[off++] = ','; types[off++] = ' '; types[off] = '\0'; }
        size_t n = strlen(s);
        if (off + n < sizeof(types)) { memcpy(types + off, s, n); off += n; types[off] = '\0'; }
      };
      if (mask & AUTO_ADD_CHAT)        add("chats");
      if (mask & AUTO_ADD_REPEATER)    add("repeaters");
      if (mask & AUTO_ADD_ROOM_SERVER) add("rooms");
      if (mask & AUTO_ADD_SENSOR)      add("sensors");
      snprintf(hint, sizeof(hint), "Auto-add on for %s — new %s land in Contacts automatically.",
               types, types);
      lv_obj_t* l = lv_label_create(body);
      lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(l, lv_pct(100));
      lv_label_set_text(l, hint);
      lv_obj_set_style_text_color(l, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
      lv_obj_set_style_text_font(l, &g_font_12, LV_PART_MAIN);
      lv_obj_set_pos(l, 2, y);
      y += 38;
    }
  }

  // Sort indices by recv_ms desc so newest is first.
  int order[DISCOVERED_MAX];
  int n = 0;
  for (int i = 0; i < DISCOVERED_MAX; ++i) {
    if (s_discovered[i].used && !s_discovered[i].in_contacts) order[n++] = i;
  }
  // simple insertion sort by recv_ms desc
  for (int i = 1; i < n; ++i) {
    int v = order[i];
    int j = i - 1;
    while (j >= 0 && s_discovered[order[j]].recv_ms < s_discovered[v].recv_ms) {
      order[j + 1] = order[j];
      --j;
    }
    order[j + 1] = v;
  }

  if (n == 0) {
    lv_obj_t* empty = lv_label_create(body);
    lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(empty, lv_pct(100));
    lv_obj_set_pos(empty, 2, y + 8);
    lv_obj_set_style_text_color(empty, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(empty, &g_font_12, LV_PART_MAIN);
    lv_label_set_text(empty,
      "No discovered nodes yet.\n\n"
      "Anything heard over the air that isn't in your contacts will show up "
      "here. When auto-add is off, you can manually add nodes to your contacts.");
    return;
  }

  for (int k = 0; k < n; ++k) {
    int idx = order[k];
    LvDiscoveredEntry& e_disc = s_discovered[idx];

    // Card background
    lv_obj_t* card = lv_obj_create(body);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 222, 58);
    lv_obj_set_pos(card, 0, y);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_radius(card, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 4, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Name label
    lv_obj_t* nm = lv_label_create(card);
    char nm_buf[40];
    copyUtf8ReplacingMissingGlyphs(&g_font_14, nm_buf, sizeof(nm_buf), e_disc.ci.name);
    lv_label_set_text(nm, nm_buf[0] ? nm_buf : "(unnamed)");
    lv_obj_set_style_text_color(nm, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
    lv_obj_set_width(nm, 152);
    lv_obj_set_pos(nm, 2, 2);

    // Type + hops + key prefix
    lv_obj_t* meta = lv_label_create(card);
    const char* type_label = "node";
    switch (e_disc.ci.type) {
      case ADV_TYPE_NONE:     type_label = "none";     break;
      case ADV_TYPE_CHAT:     type_label = "chat";     break;
      case ADV_TYPE_REPEATER: type_label = "repeater"; break;
      case ADV_TYPE_ROOM:     type_label = "room";     break;
      case ADV_TYPE_SENSOR:   type_label = "sensor";   break;
      default: break;
    }
    char keyhex[18];
    mesh::Utils::toHex(keyhex, e_disc.ci.id.pub_key, 4);
    keyhex[8] = '\0';
    char meta_buf[64];
    snprintf(meta_buf, sizeof(meta_buf), "%s · %u hop · %s…",
             type_label, (unsigned)e_disc.path_len, keyhex);
    lv_label_set_text(meta, meta_buf);
    lv_obj_set_style_text_color(meta, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(meta, &g_font_12, LV_PART_MAIN);
    lv_obj_set_pos(meta, 2, 22);

    // "Add" button on the right
    lv_obj_t* add_btn = lv_btn_create(card);
    lv_obj_set_size(add_btn, 56, 44);
    lv_obj_set_pos(add_btn, 158, 4);
    styleButton(add_btn);
    s_disc_add_ctx[idx].slot_idx = idx;
    lv_obj_add_event_cb(add_btn, discoveredAddCb, LV_EVENT_CLICKED, &s_disc_add_ctx[idx]);
    lv_obj_t* add_lbl = lv_label_create(add_btn);
    lv_label_set_text(add_lbl, "Add");
    lv_obj_center(add_lbl);

    y += 64;
  }
}

// Buffered Print over a File. The JSON export writes byte-at-a-time; on a raw
// File that's one flash op per byte (a 60 KB backup ≈ 60k syscalls → a
// multi-second UI freeze). Batching into 1 KB writes makes it ~60 ops.
class FileBufWriter : public Print {
  File& _f;
  uint8_t _buf[1024];
  size_t _n = 0;
public:
  explicit FileBufWriter(File& f) : _f(f) {}
  void flushBuf() { if (_n) { _f.write(_buf, _n); _n = 0; } }
  size_t write(uint8_t b) override { _buf[_n++] = b; if (_n >= sizeof(_buf)) flushBuf(); return 1; }
  size_t write(const uint8_t* d, size_t n) override {
    for (size_t i = 0; i < n; ++i) { _buf[_n++] = d[i]; if (_n >= sizeof(_buf)) flushBuf(); }
    return n;
  }
};

// Import settings opens a full-screen .json picker (internal flash + SD), the
// same UX as the lock-wallpaper picker. Defined later in this file (where the
// SD mount state is in scope); forward-declared here for the button below.
static void openBackupPicker();

// Heavy-flash-write watchdog guard (ref-counted, nesting-safe). A large or
// fragmenting SPIFFS write can trigger garbage collection — a multi-second
// flash burst that disables the CPU cache and starves the idle task, long
// enough to trip the TASK watchdog (confirmed by coredump: an 'ipc0' abort from
// task_wdt_isr while another thread was in spiffs_gc_clean). Bracket such writes
// so a bounded-but-slow GC pass can't reset the device. The same pattern the
// file-manager format/paste paths already use. Ref-counted so a nested guard
// (import -> persistHistoryNow -> saveHistoryToStorage) doesn't re-enable early.
static int s_wdt_heavy_depth = 0;
static inline void wdtHeavyBegin() {
  if (s_wdt_heavy_depth++ == 0) { disableCore0WDT(); disableCore1WDT(); }
}
static inline void wdtHeavyEnd() {
  if (s_wdt_heavy_depth > 0 && --s_wdt_heavy_depth == 0) { enableCore0WDT(); enableCore1WDT(); }
}
struct WdtHeavyGuard { WdtHeavyGuard() { wdtHeavyBegin(); } ~WdtHeavyGuard() { wdtHeavyEnd(); } };

static void buildProfileSettings() {
  // No "Profile" group header — it just duplicates the sub-tab button name.
  lv_obj_t* body = createSettingsModal("", SettingsModalKind::Profile);
  int y = 0;
  const lv_coord_t cw = s_settings_content_w;
  auto mk_label = [&](const char* text) {
    lv_obj_t* l = lv_label_create(body);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_pos(l, 2, y);
    y += 16;
  };
  auto mk_ta = [&](int w, int x, const char* ph, int max_len) -> lv_obj_t* {
    lv_obj_t* ta = lv_textarea_create(body);
    lv_obj_set_size(ta, w, 30);
    lv_obj_set_pos(ta, x, y);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, ph);
    lv_textarea_set_max_length(ta, max_len);
    attachSettingsTaEvents(ta);
    return ta;
  };

  mk_label("Node name");
  g_set_modal.name_ta = mk_ta(cw, 0, "Node name", 31);
  if (g_lv.task) {
    char nm_vis[40];
    const char* raw = g_lv.task->getNodeNameCstr();
    copyUtf8ReplacingMissingGlyphs(&g_font_14, nm_vis, sizeof(nm_vis), raw ? raw : "");
    lv_textarea_set_text(g_set_modal.name_ta, nm_vis);
  }
  y += 36;
  mk_label("Advert location");
  const int loc_g = 8, loc_fw = (cw - loc_g) / 2;   // lat / lon fill the row evenly
  g_set_modal.lat_ta = mk_ta(loc_fw, 0, "Latitude", 20);
  g_set_modal.lon_ta = mk_ta(loc_fw, loc_fw + loc_g, "Longitude", 20);
  if (g_lv.task) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.6f", g_lv.task->getNodeLat());
    lv_textarea_set_text(g_set_modal.lat_ta, buf);
    snprintf(buf, sizeof(buf), "%.6f", g_lv.task->getNodeLon());
    lv_textarea_set_text(g_set_modal.lon_ta, buf);
  }
  y += 40;

  const int btn_g = 8, btn_fw = (cw - btn_g) / 2;   // Save name / Save position fill the row
  lv_obj_t* b1 = lv_btn_create(body);
  lv_obj_set_size(b1, btn_fw, 34);
  lv_obj_set_pos(b1, 0, y);
  styleButton(b1);
  lv_obj_add_event_cb(b1, saveProfileNameCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l1 = lv_label_create(b1);
  lv_label_set_text(l1, "Save name");
  lv_obj_center(l1);

  lv_obj_t* b2 = lv_btn_create(body);
  lv_obj_set_size(b2, btn_fw, 34);
  lv_obj_set_pos(b2, btn_fw + btn_g, y);
  styleButton(b2);
  lv_obj_add_event_cb(b2, saveProfilePosCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l2 = lv_label_create(b2);
  lv_label_set_text(l2, "Save position");
  lv_obj_center(l2);
  y += 40;

  {
    auto mk_label = [&](const char* text) {
      lv_obj_t* l = lv_label_create(body);
      lv_label_set_text(l, text);
      lv_obj_set_style_text_color(l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
      lv_obj_set_pos(l, 2, y);
      y += 16;
    };
    auto mk_switch = [&](const char* text, lv_obj_t** out) {
      lv_obj_t* l = lv_label_create(body);
      lv_label_set_text(l, text);
      lv_obj_set_style_text_color(l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
      lv_obj_set_pos(l, 2, y + 6);
      lv_obj_t* sw = lv_switch_create(body);
      lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, y);   // flush to the card's right edge
      if (out) *out = sw;
      y += 34;
    };

    // (Path-hash size moved to Radio settings as "Multi-byte routing" — it's the
    // same NodePrefs.path_hash_mode, so no duplicate control here.)
    mk_label("Advert");
    mk_switch("Share location in advert", &g_set_modal.share_loc_sw);
    NodePrefs* pol_prefs = the_mesh.getNodePrefs();
    if (pol_prefs && pol_prefs->advert_loc_policy)
      lv_obj_add_state(g_set_modal.share_loc_sw, LV_STATE_CHECKED);

    lv_obj_t* bpol = lv_btn_create(body);
    lv_obj_set_size(bpol, lv_pct(100),34);
    lv_obj_set_pos(bpol, 2, y);
    styleButton(bpol);
    lv_obj_add_event_cb(bpol, savePolicyCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lpol = lv_label_create(bpol);
    lv_label_set_text(lpol, "Save policy");
    lv_obj_center(lpol);
    y += 40;   // advance past the Save-policy button so the moved Identity block clears it
  }

  // ---- Identity + Share QR (moved to the BOTTOM of the Profile page) ----
  // Public key — long-press copies. We don't surface the *private* key here
  // (LocalIdentity::prv_key is library-private, and private material on a touch
  // screen is a footgun).
  mk_label("Identity (public key)");
  {
    const uint8_t* pk = the_mesh.getSelfPubKey();
    char pk_hex[2 * PUB_KEY_SIZE + 1];
    for (int i = 0; i < (int)PUB_KEY_SIZE; ++i) {
      snprintf(pk_hex + i*2, 3, "%02x", pk[i]);
    }
    lv_obj_t* pk_lbl = lv_label_create(body);
    lv_label_set_long_mode(pk_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(pk_lbl, lv_pct(100));
    lv_label_set_text(pk_lbl, pk_hex);
    lv_obj_set_style_text_color(pk_lbl, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_text_font(pk_lbl, &g_font_12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(pk_lbl, lv_color_hex(0x0A0B0C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pk_lbl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pk_lbl, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(pk_lbl, 4, LV_PART_MAIN);
    lv_obj_set_pos(pk_lbl, 2, y);
    lv_obj_add_flag(pk_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pk_lbl, copyLabelLongPressCb, LV_EVENT_LONG_PRESSED,
                        const_cast<char*>("pubkey"));
    y += 50;
  }

  // "Share QR" button — second entry point to the same popup as the Chats header.
  {
    lv_obj_t* sb = lv_btn_create(body);
    lv_obj_set_size(sb, lv_pct(100),34);
    lv_obj_set_pos(sb, 2, y);
    styleButton(sb);
    lv_obj_add_event_cb(sb, +[](lv_event_t* e) {
      if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
      openShareMyContactPopup();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* sl = lv_label_create(sb);
    lv_label_set_text(sl, LV_SYMBOL_IMAGE "   Share QR");
    lv_obj_set_style_text_color(sl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(sl, &g_font_14, LV_PART_MAIN);
    lv_obj_center(sl);
    y += 42;
  }

  // "Export settings" — write a MeshCore-app-compatible JSON backup (identity,
  // radio/position, channels, contacts) to SD if a card is in, else internal
  // flash. The file opens in the stock app / web client.
  {
    lv_obj_t* eb = lv_btn_create(body);
    lv_obj_set_size(eb, lv_pct(100), 34);
    lv_obj_set_pos(eb, 2, y);
    styleButton(eb);
    lv_obj_add_event_cb(eb, +[](lv_event_t* e) {
      if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
      // "Exporting…" overlay, painted now so the (now brief) write isn't a
      // mystery freeze.
      lv_obj_t* ov = lv_obj_create(lv_layer_top());
      lv_obj_remove_style_all(ov);
      lv_obj_set_size(ov, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
      lv_obj_set_style_bg_color(ov, lv_color_black(), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(ov, LV_OPA_70, LV_PART_MAIN);
      lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
      { lv_obj_t* ol = lv_label_create(ov);
        lv_label_set_text(ol, "Exporting settings\xe2\x80\xa6");
        lv_obj_set_style_text_color(ol, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
        lv_obj_set_style_text_font(ol, &g_font_16, LV_PART_MAIN);
        lv_obj_center(ol); }
      lv_refr_now(nullptr);

      File f; const char* where = "internal";
#if defined(HAS_TDECK_GT911)
      if (SD.cardType() != CARD_NONE) { f = SD.open("/meshcore-backup.json", FILE_WRITE); if (f) where = "SD"; }
#endif
      if (!f) { SPIFFS.begin(false); f = SPIFFS.open("/meshcore-backup.json", FILE_WRITE); }
      if (!f) { lv_obj_del(ov); g_lv.task->showAlert("Export failed (can't open file)", 1800); return; }
      { WdtHeavyGuard _wg;   // a 60 KB backup write to internal flash can trigger a SPIFFS GC
        { FileBufWriter bw(f);
          the_mesh.uiExportBackup(bw, g_lv.task->getNodeLat(), g_lv.task->getNodeLon());
          bw.flushBuf(); }
        f.close(); }
      lv_obj_del(ov);
      char msg[88];
      snprintf(msg, sizeof msg, "Saved %s:/meshcore-backup.json", where);
      g_lv.task->showAlert(msg, 2600);
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* el = lv_label_create(eb);
    lv_label_set_text(el, "Export settings");
    lv_obj_set_style_text_color(el, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(el, &g_font_14, LV_PART_MAIN);
    lv_obj_center(el);
    y += 42;
  }

  // "Import settings" — restore a MeshCore-app-compatible JSON backup from
  // /meshcore-backup.json (SD if present, else internal). Replaces identity,
  // channels and contacts, then reboots so radio settings take effect.
  {
    lv_obj_t* ib = lv_btn_create(body);
    lv_obj_set_size(ib, lv_pct(100), 34);
    lv_obj_set_pos(ib, 2, y);
    styleButton(ib);
    lv_obj_add_event_cb(ib, +[](lv_event_t* e) {
      if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
      openBackupPicker();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* il = lv_label_create(ib);
    lv_label_set_text(il, "Import settings");
    lv_obj_set_style_text_color(il, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(il, &g_font_14, LV_PART_MAIN);
    lv_obj_center(il);
    y += 42;
  }
}

static void pathHashModeChangedCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || !g_lv.task) return;
  uint8_t sel = (uint8_t)lv_dropdown_get_selected(lv_event_get_target(e));
  g_lv.task->setPathHashMode(sel);   // existing setter: clamps to 0..2 + savePrefs
  char m[40];
  snprintf(m, sizeof m, "Path hash: %u byte%s", (unsigned)(sel + 1), sel ? "s" : "");
  g_lv.task->showAlert(m, 1400);
}

// GPS serial baud picker (Device settings). Persisted in TouchPrefsStore and
// read back at GPS init (EnvironmentSensorManager) — the T-Deck Plus GPS runs
// at 38400, the older T-Deck v1.0 at 9600. Takes effect on reboot.
static const uint32_t kGpsBaudOpts[] = {9600, 19200, 38400, 57600, 115200};
static const int      kGpsBaudCount  = (int)(sizeof(kGpsBaudOpts) / sizeof(kGpsBaudOpts[0]));
static void gpsBaudChangedCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  int sel = (int)lv_dropdown_get_selected(lv_event_get_target(e));
  if (sel < 0 || sel >= kGpsBaudCount) return;
  touchPrefsSetGpsBaud(kGpsBaudOpts[sel]);
  if (g_lv.task) {
    char m[48];
    snprintf(m, sizeof m, "GPS %lu baud — reboot to apply", (unsigned long)kGpsBaudOpts[sel]);
    g_lv.task->showAlert(m, 1800);
  }
}

// LVGL parents an open dropdown list to the screen layer and, when the button
// sits in the lower half of a page, flips the list to drop *upward* — sliding
// its top edge behind our global status bar (which lives on lv_layer_sys, above
// everything) so the first option(s) get clipped. Run after the list is open &
// positioned (post-CLICKED) to cap its height to the area below the status bar
// and nudge it back on-screen. Wire onto every settings dropdown.
static void clampDropdownListCb(lv_event_t* e) {
  lv_obj_t* dd = lv_event_get_target(e);
  lv_obj_t* list = lv_dropdown_get_list(dd);
  if (!list || lv_obj_has_flag(list, LV_OBJ_FLAG_HIDDEN)) return;   // closed/just-closed
  const lv_coord_t ver   = lv_disp_get_ver_res(nullptr);
  const lv_coord_t avail = ver - STATUSBAR_H;                       // usable height below the status bar
  lv_obj_update_layout(list);
  if (lv_obj_get_height(list) > avail) lv_obj_set_height(list, avail);  // too tall -> cap + let it scroll
  lv_obj_update_layout(list);
  const lv_coord_t h  = lv_obj_get_height(list);
  const lv_coord_t y1 = lv_obj_get_y(list);
  if (y1 < STATUSBAR_H)         lv_obj_set_y(list, STATUSBAR_H);    // opened up into the status bar
  else if (y1 + h > ver)        lv_obj_set_y(list, ver - h);        // hangs off the bottom edge
}

static void buildRadioSettings() {
  // No "Radio" group header — it just duplicates the sub-tab button name.
  lv_obj_t* body = createSettingsModal("", SettingsModalKind::Radio);
  NodePrefs* prefs = the_mesh.getNodePrefs();
  int y = 0;
  auto mk_label = [&](const char* text) {
    lv_obj_t* l = lv_label_create(body);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_pos(l, 2, y);
    y += 16;
  };
  auto mk_ta = [&](int w, int x, const char* ph, int max_len) -> lv_obj_t* {
    lv_obj_t* ta = lv_textarea_create(body);
    lv_obj_set_size(ta, w, 30);
    lv_obj_set_pos(ta, x, y);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, ph);
    lv_textarea_set_max_length(ta, max_len);
    attachSettingsTaEvents(ta);
    return ta;
  };

  mk_label("Community preset (meshcomod web)");
  {
    static char preset_opt_buf[1200];
    size_t o = 0;
    int nw = snprintf(preset_opt_buf + o, sizeof(preset_opt_buf) - o, "Custom (manual)");
    if (nw > 0) o += static_cast<size_t>(nw);
    for (size_t i = 0; i < k_mesh_radio_preset_count && o + 2 < sizeof(preset_opt_buf); ++i) {
      preset_opt_buf[o++] = '\n';
      nw = snprintf(preset_opt_buf + o, sizeof(preset_opt_buf) - o, "%s", k_mesh_radio_presets[i].label);
      if (nw < 0) break;
      o += static_cast<size_t>(nw);
    }
    preset_opt_buf[sizeof(preset_opt_buf) - 1] = '\0';

    g_set_modal.radio_preset_dd = lv_dropdown_create(body);
    lv_obj_set_size(g_set_modal.radio_preset_dd, lv_pct(100),34);
    lv_obj_set_pos(g_set_modal.radio_preset_dd, 2, y);
    lv_dropdown_set_options(g_set_modal.radio_preset_dd, preset_opt_buf);
    lv_obj_set_style_text_font(g_set_modal.radio_preset_dd, &g_font_12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_set_modal.radio_preset_dd, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_text_color(g_set_modal.radio_preset_dd, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_border_color(g_set_modal.radio_preset_dd, lv_color_hex(0x18191A), LV_PART_MAIN);
    /* Dropdown list (the popup once tapped) */
    lv_obj_t* preset_list = lv_dropdown_get_list(g_set_modal.radio_preset_dd);
    lv_obj_set_style_bg_color(preset_list, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_text_color(preset_list, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(preset_list, &g_font_12, LV_PART_MAIN);
    lv_obj_add_event_cb(g_set_modal.radio_preset_dd, radioPresetChangedCb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(g_set_modal.radio_preset_dd, clampDropdownListCb, LV_EVENT_CLICKED, nullptr);
    y += 40;
  }

  const lv_coord_t cw = s_settings_content_w;
  mk_label("Frequency / bandwidth");
  {
    const int g = 8, fw = (cw - g) / 2;           // two equal fields filling the row
    g_set_modal.freq_ta = mk_ta(fw, 0,        "MHz", 15);
    g_set_modal.bw_ta   = mk_ta(fw, fw + g,   "kHz", 10);
  }
  y += 36;
  mk_label("SF / CR / TX / AF");
  {
    const int g = 6, fw = (cw - 3 * g) / 4;       // four equal fields filling the row
    g_set_modal.sf_ta      = mk_ta(fw, 0,              "SF", 2);
    g_set_modal.cr_ta      = mk_ta(fw, fw + g,         "CR", 2);
    g_set_modal.tx_ta      = mk_ta(fw, 2 * (fw + g),   "TX", 4);
    g_set_modal.airtime_ta = mk_ta(fw, 3 * (fw + g),   "AF", 6);
  }
  y += 38;

  if (prefs) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.3f", prefs->freq); lv_textarea_set_text(g_set_modal.freq_ta, buf);
    snprintf(buf, sizeof(buf), "%.1f", prefs->bw); lv_textarea_set_text(g_set_modal.bw_ta, buf);
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(prefs->sf)); lv_textarea_set_text(g_set_modal.sf_ta, buf);
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(prefs->cr)); lv_textarea_set_text(g_set_modal.cr_ta, buf);
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(prefs->tx_power_dbm)); lv_textarea_set_text(g_set_modal.tx_ta, buf);
    snprintf(buf, sizeof(buf), "%.2f", prefs->airtime_factor); lv_textarea_set_text(g_set_modal.airtime_ta, buf);
  }

  if (g_set_modal.radio_preset_dd) {
    const int match = findMatchingMeshRadioPreset(prefs);
    g_radio_preset_cb_silent = true;
    lv_dropdown_set_selected(g_set_modal.radio_preset_dd, match < 0 ? 0 : static_cast<uint16_t>(match + 1));
    g_radio_preset_cb_silent = false;
  }

  // Multi-byte routing: how many bytes of each repeater's hash this node stamps
  // into the path when it adverts / sends. 1 byte (legacy) collides in large
  // regions; 2-3 bytes disambiguate. Applied immediately + persisted (no radio
  // reconfig). All repeaters on a path must support it (MeshCore >= v1.14);
  // older ones silently drop 2/3-byte packets.
  mk_label("Multi-byte routing (path hash)");
  {
    lv_obj_t* dd = lv_dropdown_create(body);
    lv_obj_set_size(dd, lv_pct(100), 34);
    lv_obj_set_pos(dd, 2, y);
    lv_dropdown_set_options(dd, "1 byte (legacy)\n2 bytes\n3 bytes");
    lv_obj_set_style_text_font(dd, &g_font_12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dd, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_text_color(dd, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_border_color(dd, lv_color_hex(0x18191A), LV_PART_MAIN);
    lv_obj_t* phlist = lv_dropdown_get_list(dd);
    lv_obj_set_style_bg_color(phlist, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_text_color(phlist, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(phlist, &g_font_12, LV_PART_MAIN);
    uint8_t phm = prefs ? prefs->path_hash_mode : 0;
    if (phm > 2) phm = 0;
    lv_dropdown_set_selected(dd, phm);
    lv_obj_add_event_cb(dd, pathHashModeChangedCb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(dd, clampDropdownListCb, LV_EVENT_CLICKED, nullptr);
    y += 40;
  }

  lv_obj_t* b = lv_btn_create(body);
  lv_obj_set_size(b, lv_pct(100),34);
  lv_obj_set_pos(b, 2, y);
  styleButton(b);
  lv_obj_add_event_cb(b, saveRadioParamsCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, "Apply radio params");
  lv_obj_center(l);
}

static void buildAutoAddSettings() {
  lv_obj_t* body = createSettingsModal("Auto-add contacts", SettingsModalKind::AutoAdd);
  NodePrefs* prefs = the_mesh.getNodePrefs();
  int y = 0;
  auto mk_switch = [&](const char* text, lv_obj_t** out) {
    lv_obj_t* l = lv_label_create(body);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_pos(l, 2, y + 6);
    lv_obj_t* sw = lv_switch_create(body);
    lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, y);   // flush to the card's right edge
    lv_obj_add_event_cb(sw, autoAddSwitchCb, LV_EVENT_VALUE_CHANGED, nullptr);  // auto-save on toggle
    if (out) *out = sw;
    y += 34;
  };
  mk_switch("Auto chat", &g_set_modal.auto_chat_sw);
  mk_switch("Auto repeater", &g_set_modal.auto_rep_sw);
  mk_switch("Auto room", &g_set_modal.auto_room_sw);
  mk_switch("Auto sensor", &g_set_modal.auto_sensor_sw);
  mk_switch("Overwrite oldest", &g_set_modal.auto_overwrite_sw);
  mk_switch("Manual add", &g_set_modal.manual_add_sw);

  lv_obj_t* hops_l = lv_label_create(body);
  lv_label_set_text(hops_l, "Max hops (0..64)");
  lv_obj_set_style_text_color(hops_l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_pos(hops_l, 2, y + 6);
  g_set_modal.max_hops_ta = lv_textarea_create(body);
  lv_obj_set_size(g_set_modal.max_hops_ta, 80, 30);
  lv_obj_set_pos(g_set_modal.max_hops_ta, 142, y);
  lv_textarea_set_one_line(g_set_modal.max_hops_ta, true);
  lv_textarea_set_max_length(g_set_modal.max_hops_ta, 3);
  attachSettingsTaEvents(g_set_modal.max_hops_ta);
  y += 38;

  if (prefs) {
    if (prefs->autoadd_config & AUTO_ADD_CHAT) lv_obj_add_state(g_set_modal.auto_chat_sw, LV_STATE_CHECKED);
    if (prefs->autoadd_config & AUTO_ADD_REPEATER) lv_obj_add_state(g_set_modal.auto_rep_sw, LV_STATE_CHECKED);
    if (prefs->autoadd_config & AUTO_ADD_ROOM_SERVER) lv_obj_add_state(g_set_modal.auto_room_sw, LV_STATE_CHECKED);
    if (prefs->autoadd_config & AUTO_ADD_SENSOR) lv_obj_add_state(g_set_modal.auto_sensor_sw, LV_STATE_CHECKED);
    if (prefs->autoadd_config & AUTO_ADD_OVERWRITE_OLDEST) lv_obj_add_state(g_set_modal.auto_overwrite_sw, LV_STATE_CHECKED);
    if (prefs->manual_add_contacts) lv_obj_add_state(g_set_modal.manual_add_sw, LV_STATE_CHECKED);
    char hops_buf[8];
    snprintf(hops_buf, sizeof(hops_buf), "%u", static_cast<unsigned>(prefs->autoadd_max_hops));
    lv_textarea_set_text(g_set_modal.max_hops_ta, hops_buf);
  }

  lv_obj_t* b = lv_btn_create(body);
  lv_obj_set_size(b, lv_pct(100),34);
  lv_obj_set_pos(b, 2, y);
  styleButton(b);
  lv_obj_add_event_cb(b, saveAutoAddCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, "Save auto-add");
  lv_obj_center(l);
}

static void buildExperimentalSettings() {
  lv_obj_t* body = createSettingsModal("Experimental", SettingsModalKind::Experimental);
  NodePrefs* prefs = the_mesh.getNodePrefs();
  int y = 0;
  auto mk_switch = [&](const char* text, lv_obj_t** out) {
    lv_obj_t* l = lv_label_create(body);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_pos(l, 2, y + 6);
    lv_obj_t* sw = lv_switch_create(body);
    lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, y);   // flush to the card's right edge
    if (out) *out = sw;
    y += 34;
  };

  mk_switch("Multi ACKs", &g_set_modal.exp_multi_sw);
  mk_switch("Client repeat", &g_set_modal.exp_repeat_sw);
  mk_switch("RX boosted gain", &g_set_modal.exp_boost_sw);
  mk_switch("Duty meter", &g_set_modal.exp_dc_sw);

  if (prefs) {
    if (prefs->multi_acks) lv_obj_add_state(g_set_modal.exp_multi_sw, LV_STATE_CHECKED);
    if (prefs->client_repeat) lv_obj_add_state(g_set_modal.exp_repeat_sw, LV_STATE_CHECKED);
    if (prefs->rx_boosted_gain) lv_obj_add_state(g_set_modal.exp_boost_sw, LV_STATE_CHECKED);
  }
#if defined(ESP32)
  if (touchPrefsGetDutyMeterShown()) lv_obj_add_state(g_set_modal.exp_dc_sw, LV_STATE_CHECKED);
#endif

  lv_obj_t* b2 = lv_btn_create(body);
  lv_obj_set_size(b2, lv_pct(100),34);
  lv_obj_set_pos(b2, 2, y);
  styleButton(b2);
  lv_obj_add_event_cb(b2, saveExperimentalCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l2 = lv_label_create(b2);
  lv_label_set_text(l2, "Save experimental");
  lv_obj_center(l2);
}

// ---- Quick replies editor (Settings → Quick replies) -----------------------

static void saveQuickRepliesCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  kbMirrorSyncToReal();
  int n_saved = 0;
#if defined(ESP32)
  for (int i = 0; i < TOUCH_QUICK_REPLY_COUNT; ++i) {
    lv_obj_t* ta = g_set_modal.qr_tas[i];
    if (!ta) continue;
    const char* text = lv_textarea_get_text(ta);
    if (touchPrefsSetQuickReply(i, text ? text : "")) ++n_saved;
  }
#endif
  char msg[48];
  snprintf(msg, sizeof(msg), "Saved %d quick %s", n_saved,
           n_saved == 1 ? "reply" : "replies");
  g_lv.task->showAlert(msg, 1100);
}

static void buildQuickReplySettings() {
  lv_obj_t* body = createSettingsModal("Quick replies", SettingsModalKind::QuickReply);
  for (int i = 0; i < TOUCH_QUICK_REPLY_COUNT; ++i) g_set_modal.qr_tas[i] = nullptr;

  int y = 0;
  // Compact form: one row per slot, with the slot index as a tiny prefix
  // label so the user knows which macro they're editing. Single-line
  // textareas to keep the modal short; macro text caps at 31 chars on the
  // store side anyway, which fits one line.
#if defined(ESP32)
  for (int i = 0; i < TOUCH_QUICK_REPLY_COUNT; ++i) {
    char buf[TOUCH_QUICK_REPLY_MAXLEN];
    touchPrefsGetQuickReply(i, buf, sizeof(buf));

    lv_obj_t* idxlbl = lv_label_create(body);
    char idxs[4];
    snprintf(idxs, sizeof(idxs), "%d", i + 1);
    lv_label_set_text(idxlbl, idxs);
    lv_obj_set_style_text_color(idxlbl, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(idxlbl, &g_font_12, LV_PART_MAIN);
    lv_obj_set_pos(idxlbl, 2, y + 9);

    lv_obj_t* ta = lv_textarea_create(body);
    lv_obj_set_size(ta, 200, 32);
    lv_obj_set_pos(ta, 20, y);
    styleCard(ta);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, TOUCH_QUICK_REPLY_MAXLEN - 1);
    lv_textarea_set_text(ta, buf);
    lv_obj_set_style_text_color(ta, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(ta, &g_font_12, LV_PART_MAIN);
    lv_obj_add_event_cb(ta, composerFocusCb, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(ta, kbActivityPressCb, LV_EVENT_PRESSED, nullptr);
    g_set_modal.qr_tas[i] = ta;
    y += 36;
  }
#endif

  lv_obj_t* b = lv_btn_create(body);
  lv_obj_set_size(b, lv_pct(100),34);
  lv_obj_set_pos(b, 2, y);
  styleButton(b);
  lv_obj_add_event_cb(b, saveQuickRepliesCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, "Save quick replies");
  lv_obj_center(l);
}

static void saveScreenTimeoutCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  if (!g_set_modal.screen_to_ta) return;
  kbMirrorSyncToReal();
  int secs = atoi(lv_textarea_get_text(g_set_modal.screen_to_ta));
  if (secs < 0) secs = 0;
  if (secs > 3600) secs = 3600;
  if (g_lv.task->setScreenTimeoutSecs(static_cast<uint16_t>(secs))) {
    char msg[48];
    if (secs == 0) snprintf(msg, sizeof(msg), "Screen timeout: never");
    else           snprintf(msg, sizeof(msg), "Screen timeout: %ds", secs);
    g_lv.task->showAlert(msg, 1200);
  } else {
    g_lv.task->showAlert("Save failed", 1200);
  }
}

// ===== System info modal =====================================================
// Read-only diagnostic page: uptime, heap / PSRAM / flash usage, chip model
// + revision, last reset reason, MeshCore + Meshcomod versions. One-shot
// snapshot; no live updates while the modal is open.
#if defined(ESP32)
static const char* resetReasonString(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:    return "Power on";
    case ESP_RST_EXT:        return "External pin";
    case ESP_RST_SW:         return "Software";
    case ESP_RST_PANIC:      return "Panic / exception";
    case ESP_RST_INT_WDT:    return "Int watchdog";
    case ESP_RST_TASK_WDT:   return "Task watchdog";
    case ESP_RST_WDT:        return "Other watchdog";
    case ESP_RST_DEEPSLEEP:  return "Deep sleep wake";
    case ESP_RST_BROWNOUT:   return "Brownout";
    case ESP_RST_SDIO:       return "SDIO";
    case ESP_RST_UNKNOWN:    default: return "Unknown";
  }
}
#endif

// "Memory detail" popup — heap usage by region (the ESP32 shares one heap
// across all tasks, so there's no per-process split like a PC task manager;
// this shows internal DRAM vs PSRAM used/free + largest free block, which is
// what actually tells you where RAM is going and how fragmented it is).
static lv_obj_t* s_meminfo_root = nullptr;
static void closeMemInfo() {
  if (s_meminfo_root) { lv_obj_del_async(s_meminfo_root); s_meminfo_root = nullptr; }
}
static void memInfoCloseCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
  closeMemInfo();
}
static void openMemoryDetailCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeMemInfo();
#if defined(ESP32)
  auto kb = [](size_t v) -> unsigned { return (unsigned)((v + 512) / 1024); };
  const size_t i_tot  = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t i_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t i_min  = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t i_lrg  = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t s_tot  = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  const size_t s_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const size_t s_min  = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
  const size_t s_lrg  = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  const size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA);
  char b[700]; int q = 0;
  q += snprintf(b + q, sizeof(b) - q,
    "Internal DRAM\n  used  %u / %u KB\n  free  %u KB (low %u)\n  largest free  %u KB\n\n",
    kb(i_tot - i_free), kb(i_tot), kb(i_free), kb(i_min), kb(i_lrg));
  q += snprintf(b + q, sizeof(b) - q,
    "PSRAM\n  used  %u / %u KB\n  free  %u KB (low %u)\n  largest free  %u KB\n\n",
    kb(s_tot - s_free), kb(s_tot), kb(s_free), kb(s_min), kb(s_lrg));
  q += snprintf(b + q, sizeof(b) - q, "DMA-capable free: %u KB\n", kb(dma_free));
  q += snprintf(b + q, sizeof(b) - q, "UI loop stack free: %u B\n\n",
                (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
  q += snprintf(b + q, sizeof(b) - q,
    "Note: the ESP32 shares one heap\nacross all tasks - no per-task\nsplit like a PC. 'largest free'\nvs 'free' shows fragmentation.");

  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_meminfo_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_meminfo_root);
  lv_obj_set_size(s_meminfo_root, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_meminfo_root, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_meminfo_root, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_meminfo_root, LV_OPA_70, LV_PART_MAIN);
  lv_obj_clear_flag(s_meminfo_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_meminfo_root, memInfoCloseCb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* card = lv_obj_create(s_meminfo_root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, sw - 24, (sh - STATUSBAR_H) - 24);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  styleSurface(card, COLOR_PANEL, 8);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
  lv_obj_set_scroll_dir(card, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);
  addCloseXBadge(card, memInfoCloseCb);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "Memory detail");
  lv_obj_set_style_text_font(title, &g_font_14, LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_pos(title, 0, 2);

  lv_obj_t* lbl = lv_label_create(card);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, sw - 24 - 20);
  lv_obj_set_pos(lbl, 0, 28);
  lv_obj_set_style_text_font(lbl, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_label_set_text(lbl, b);
#endif
}

// Render the System-info text into buf. Re-callable so live values (uptime,
// free heap) update; the inline About page calls it once at build then
// refreshSysInfo() re-runs it ~1 Hz while that tab is visible.
static void sysInfoText(char* buf, size_t cap) {
  int p = 0;
#if defined(ESP32)
  const uint32_t up_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
  const uint32_t up_d = up_s / 86400u;
  const uint32_t up_h = (up_s % 86400u) / 3600u;
  const uint32_t up_m = (up_s % 3600u) / 60u;
  const uint32_t up_ss = up_s % 60u;
  p += snprintf(buf + p, cap - p,
                "Uptime\n  %ud %02uh %02um %02us\n\n",
                (unsigned)up_d, (unsigned)up_h, (unsigned)up_m, (unsigned)up_ss);

  esp_chip_info_t chip;
  esp_chip_info(&chip);
  const char* model = (chip.model == CHIP_ESP32S3) ? "ESP32-S3"
                    : (chip.model == CHIP_ESP32S2) ? "ESP32-S2"
                    : (chip.model == CHIP_ESP32)   ? "ESP32"
                    : "ESP32-?";
  p += snprintf(buf + p, cap - p,
                "Chip\n  %s rev %d, %d core(s)\n  features:%s%s%s\n\n",
                model, (int)chip.revision, (int)chip.cores,
                (chip.features & CHIP_FEATURE_BLE)      ? " BLE"  : "",
                (chip.features & CHIP_FEATURE_WIFI_BGN) ? " WiFi" : "",
                (chip.features & CHIP_FEATURE_BT)       ? " BT"   : "");

  const size_t dram_free  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t dram_tot   = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t dram_min   = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const size_t psram_tot  = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  p += snprintf(buf + p, cap - p,
                "Memory\n  DRAM free: %u KB / %u KB (min %u)\n  PSRAM free: %u KB / %u KB\n\n",
                (unsigned)(dram_free / 1024),
                (unsigned)(dram_tot  / 1024),
                (unsigned)(dram_min  / 1024),
                (unsigned)(psram_free / 1024),
                (unsigned)(psram_tot  / 1024));

  const uint32_t flash_chip_size = ESP.getFlashChipSize();
  const uint32_t sketch_size     = ESP.getSketchSize();
  const uint32_t sketch_free     = ESP.getFreeSketchSpace();
  p += snprintf(buf + p, cap - p,
                "Flash\n  chip: %u MB\n  sketch: %u KB used\n  app slot free: %u KB\n\n",
                (unsigned)(flash_chip_size / (1024u * 1024u)),
                (unsigned)(sketch_size / 1024u),
                (unsigned)(sketch_free / 1024u));

  p += snprintf(buf + p, cap - p,
                "Last reset\n  %s\n\n", resetReasonString(esp_reset_reason()));
#endif
  p += snprintf(buf + p, cap - p,
                "Build\n  %s\n  %s\n  Meshcomod TOUCH\n",
                FIRMWARE_VERSION, FIRMWARE_BUILD_DATE);
  (void)p;
}

// Re-render the System-info text in place while the About sub-tab is visible.
static void refreshSysInfo(unsigned long now) {
  static unsigned long next = 0;
  if ((long)(now - next) < 0) return;
  next = now + 1000;
  if (!s_sysinfo_lbl) return;
  if (getActiveTab() != SETTINGS_TAB_INDEX) return;
  if (s_settings_subtab && (int)lv_tabview_get_tab_act(s_settings_subtab) != 4) return;  // About == index 4
  char buf[800];
  sysInfoText(buf, sizeof buf);
  lv_label_set_text(s_sysinfo_lbl, buf);
}

static void buildSystemInfoSettings() {
  lv_obj_t* body = createSettingsModal("System info", SettingsModalKind::SystemInfo);
  char buf[800];
  sysInfoText(buf, sizeof buf);

  lv_obj_t* lbl = lv_label_create(body);
  s_sysinfo_lbl = lbl;
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, lv_pct(100));
  lv_obj_set_pos(lbl, 2, 0);
  lv_obj_set_style_text_font(lbl, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_label_set_text(lbl, buf);

  // "Memory detail" button below the info text (per-region heap breakdown).
  lv_obj_update_layout(lbl);
  lv_obj_t* membtn = lv_btn_create(body);
  lv_obj_set_size(membtn, lv_pct(96), 34);
  lv_obj_set_pos(membtn, 2, lv_obj_get_y(lbl) + lv_obj_get_height(lbl) + 8);
  styleButton(membtn);
  lv_obj_add_event_cb(membtn, openMemoryDetailCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* mbl = lv_label_create(membtn);
  lv_label_set_text(mbl, "Memory detail");
  lv_obj_set_style_text_font(mbl, &g_font_14, LV_PART_MAIN);
  lv_obj_center(mbl);
}

// Map tile source: false = tile server + on-device cache, true = read tiles off the microSD.
// The toggle + its callback (mapOptTilesSdCb) live in the map options popup, further down.
static bool s_tiles_from_sd = false;

// Distance units toggle (km <-> miles). Applies immediately — saves the
// pref and forces the contacts list to re-render its distance badges.
#if defined(HAS_TDECK_GT911)
// Store all data (identity/prefs/contacts/channels) on SD under /meshcomod vs
// internal SPIFFS. Read at boot before data loads, so it only takes effect on
// the next reboot.
static void useSdStorageToggleCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  const bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
#if defined(ESP32)
  touchPrefsSetUseSdStorage(on);
#endif
  if (g_lv.task) g_lv.task->showAlert(on ? "Data -> SD card on reboot\n(card must be inserted)"
                                         : "Data -> internal on reboot", 1800);
}
#endif

static void useMilesToggleCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  const bool miles = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
#if defined(ESP32)
  touchPrefsSetUseMiles(miles);
#endif
  if (g_lv.task) g_lv.task->showAlert(miles ? "Distance: miles" : "Distance: km", 900);
  // The contacts cache now keys on the units flag, so this rebuilds the
  // badges on the next refresh (and immediately if the list is visible).
  refreshContactsList();
}

// One handler for every per-language switch in the multi-select list. The
// layout id is stashed in the switch's user_data. Flipping a switch updates the
// enabled-mask (persisted + live) and refreshes the on-screen keyboard, which
// snaps back to English if the layout that was just switched off was active.
static void kbLayoutSwitchCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t* sw = lv_event_get_target(e);
  int id = (int)(intptr_t)lv_obj_get_user_data(sw);
  if (id <= 0 || id >= KEYBOARD_LAYOUT_COUNT) return;
  bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
#if defined(ESP32)
  uint16_t mask = touchPrefsGetEnabledLayouts();
  if (on) mask |= (uint16_t)(1u << id);
  else    mask &= (uint16_t)~(1u << id);
  touchPrefsSetEnabledLayouts(mask);
  keyboardLayoutsSetEnabledMask(mask);
  // SetEnabledMask snaps the cached layout to EN if it was just disabled;
  // re-apply it so the keyboard widget reflects the (possibly changed) layout.
  keyboardLayoutsApply(g_lv.keyboard, keyboardLayoutsGetCurrent());
  touchPrefsSetKeyboardLayout(static_cast<uint8_t>(keyboardLayoutsGetCurrent()));
#endif
  if (g_lv.task) {
    char buf[40];
    snprintf(buf, sizeof(buf), "%s: %s",
             keyboardLayoutName(static_cast<KeyboardLayoutId>(id)), on ? "on" : "off");
    g_lv.task->showAlert(buf, 800);
  }
}

// Screen orientation: the persistent base rotation, applied at boot. Tapping
// cycles Portrait -> Landscape -> Landscape (flipped) and reboots so the whole
// UI rebuilds at the chosen resolution (rebootDevice() saves chat history
// synchronously first, so nothing is lost).
static const char* uiRotationLabel() {
#if defined(ESP32)
  // Portrait (0) vs Landscape (anything else). The button toggles between the
  // two so a single tap always returns to portrait from any landscape.
  return (touchPrefsGetUiRotation() == LV_DISP_ROT_NONE) ? "Screen: Portrait"
                                                         : "Screen: Landscape";
#else
  return "Screen: Portrait";
#endif
}

static void rotateScreenCycleCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
#if defined(ESP32)
  // Simple toggle: portrait <-> landscape (90 degrees). One tap each way.
  const uint8_t cur  = touchPrefsGetUiRotation();
  const uint8_t next = (cur == LV_DISP_ROT_NONE) ? (uint8_t)LV_DISP_ROT_90
                                                 : (uint8_t)LV_DISP_ROT_NONE;
  touchPrefsSetUiRotation(next);
#endif
  if (g_lv.task) {
    g_lv.task->showAlert("Rotating\xe2\x80\xa6 rebooting", 600);
    g_lv.task->rebootDevice();
  }
}

// ---- Manual clock offset (Device modal): nudge local time +/- whole hours ----
// Applied on top of the automatic (NTP / companion / mesh) time, purely at the
// display layer via the POSIX TZ; the RTC and mesh timestamps stay UTC.
static lv_obj_t* s_time_offset_lbl = nullptr;   // current offset readout (modal is a singleton)

static void applyTimeOffsetNow() {
  char tz[48];
  touchPrefsBuildLocalTz(tz, sizeof tz);   // rebuilds from the saved offset
  setenv("TZ", tz, 1);
  tzset();                                  // every later localtime_r() reflects it
}
static void timeOffsetLabelRefresh() {
  if (!s_time_offset_lbl) return;
  int off = touchPrefsGetTimeOffsetHours();
  char b[16];
  if (off == 0) snprintf(b, sizeof b, "0 h");
  else          snprintf(b, sizeof b, "%+d h", off);
  lv_label_set_text(s_time_offset_lbl, b);
}
static void timeOffsetStep(int delta) {
  int off = touchPrefsGetTimeOffsetHours() + delta;
  if (off < -23) off = -23;
  if (off >  23) off =  23;
  touchPrefsSetTimeOffsetHours(off);
  applyTimeOffsetNow();
  timeOffsetLabelRefresh();
  if (g_lv.task) {
    char m[28]; snprintf(m, sizeof m, "Time offset: %+d h", off);
    g_lv.task->showAlert(m, 900);
  }
}
static void timeOffsetMinusCb(lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) timeOffsetStep(-1); }
static void timeOffsetPlusCb(lv_event_t* e)  { if (lv_event_get_code(e) == LV_EVENT_CLICKED) timeOffsetStep(+1); }

#if defined(HAS_TDECK_GT911)
// Lock-screen settings live in the Device modal but the picker implementation
// needs the SD-mount state (declared further down), so split: the small bits
// the modal body uses directly are here; the picker is defined below.
static lv_obj_t* s_lockwall_btn_lbl = nullptr;   // Device-modal button caption (current wallpaper)
static const uint32_t kLockColors[] = {
  0xE6F2FF, 0xFFFFFF, 0x5BC0FF, 0x5BE5A0, 0xFFC857, 0xFF6B6B, 0xB39DFF, 0x9AA7B4
};
// Human-readable name from a stored pref path ("sd:/lock/x.jpg" -> "SD: x.jpg").
static void lockwallDisplayName(const char* path, char* out, int cap) {
  if (!out || cap <= 0) return;
  if (!path || !path[0]) { snprintf(out, cap, "(default)"); return; }
  const bool sd = !strncmp(path, "sd:", 3);
  const char* p = sd ? path + 3 : path;
  const char* base = strrchr(p, '/'); base = base ? base + 1 : p;
  snprintf(out, cap, "%s%s", sd ? "SD: " : "", base);
}
static void openLockWallPickerCb(lv_event_t* e);   // defined with the picker, below
static void lockColorChosenCb(lv_event_t* e);
#endif

static void calibrateBatteryCb(lv_event_t* e);   // defined with the battery helpers below
static void buildDeviceSettings() {
  // No "Device" group header — it just duplicates the sub-tab button name.
  lv_obj_t* body = createSettingsModal("", SettingsModalKind::Device);
  int y = 0;

  lv_obj_t* b_gps = lv_btn_create(body);
  lv_obj_set_size(b_gps, lv_pct(100),34);
  lv_obj_set_pos(b_gps, 2, y);
  styleButton(b_gps);
  lv_obj_add_event_cb(b_gps, toggleGpsCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lg = lv_label_create(b_gps);
  lv_label_set_text(lg, "Toggle GPS");
  lv_obj_center(lg);
  y += 38;

  // Live GPS fix status — refreshed each tick while this modal is open.
  g_set_modal.gps_status = lv_label_create(body);
  lv_label_set_long_mode(g_set_modal.gps_status, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(g_set_modal.gps_status, lv_pct(100));
  lv_obj_set_style_text_color(g_set_modal.gps_status, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(g_set_modal.gps_status, &g_font_12, LV_PART_MAIN);
  lv_obj_set_pos(g_set_modal.gps_status, 4, y);
  lv_label_set_text(g_set_modal.gps_status, gpsStatusStr());
  y += 22;

  // GPS serial baud. T-Deck Plus = 38400, T-Deck v1.0 = 9600. Persisted to NVS
  // and read at GPS init; reboot to apply. Also settable via `set gps.baud`.
  {
    lv_obj_t* bl = lv_label_create(body);
    lv_obj_set_style_text_color(bl, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(bl, &g_font_12, LV_PART_MAIN);
    lv_obj_set_pos(bl, 4, y);
    lv_label_set_text(bl, "GPS serial baud");
    y += 18;

    lv_obj_t* dd = lv_dropdown_create(body);
    lv_obj_set_size(dd, lv_pct(100), 34);
    lv_obj_set_pos(dd, 2, y);
    lv_dropdown_set_options(dd, "9600\n19200\n38400\n57600\n115200");
    lv_obj_set_style_text_font(dd, &g_font_12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dd, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_text_color(dd, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_border_color(dd, lv_color_hex(0x18191A), LV_PART_MAIN);
    lv_obj_t* gblist = lv_dropdown_get_list(dd);
    lv_obj_set_style_bg_color(gblist, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_text_color(gblist, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(gblist, &g_font_12, LV_PART_MAIN);
#ifdef GPS_BAUD_RATE
    uint32_t cur = touchPrefsGetGpsBaud(GPS_BAUD_RATE);
#else
    uint32_t cur = touchPrefsGetGpsBaud(9600);
#endif
    uint16_t match = 0;
    for (int i = 0; i < kGpsBaudCount; i++) if (kGpsBaudOpts[i] == cur) { match = (uint16_t)i; break; }
    lv_dropdown_set_selected(dd, match);
    lv_obj_add_event_cb(dd, gpsBaudChangedCb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(dd, clampDropdownListCb, LV_EVENT_CLICKED, nullptr);
    y += 40;
  }

  // Sound toggle — T-Deck only (it has an I2S speaker; the V4 has no audio HW).
#if defined(HAS_TDECK_GT911)
  lv_obj_t* b_bz = lv_btn_create(body);
  lv_obj_set_size(b_bz, lv_pct(100),34);
  lv_obj_set_pos(b_bz, 2, y);
  styleButton(b_bz);
  lv_obj_add_event_cb(b_bz, toggleBuzzerCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lb = lv_label_create(b_bz);
  lv_label_set_text_fmt(lb, "Sound: %s", g_lv.task && !g_lv.task->isBuzzerQuiet() ? "on" : "off");
  lv_obj_center(lb);
  y += 40;
#endif

  lv_obj_t* b_time = lv_btn_create(body);
  lv_obj_set_size(b_time, lv_pct(100),34);
  lv_obj_set_pos(b_time, 2, y);
  styleButton(b_time);
  lv_obj_add_event_cb(b_time, syncClockCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l_time = lv_label_create(b_time);
  lv_label_set_text(l_time, "Sync clock from system");
  lv_obj_center(l_time);
  y += 40;

  lv_obj_t* b_adv = lv_btn_create(body);
  lv_obj_set_size(b_adv, lv_pct(100),34);
  lv_obj_set_pos(b_adv, 2, y);
  styleButton(b_adv);
  lv_obj_add_event_cb(b_adv, advertNowCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l_adv = lv_label_create(b_adv);
  lv_label_set_text(l_adv, "Send advert now");
  lv_obj_center(l_adv);
  y += 40;

  /* Screen timeout (seconds, 0 = never). Persists in NVS via TouchPrefsStore. */
  {
    lv_obj_t* l = lv_label_create(body);
    lv_label_set_text(l, "Screen timeout (s, 0 = never)");
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, &g_font_12, LV_PART_MAIN);
    lv_obj_set_pos(l, 2, y);
    y += 16;
    g_set_modal.screen_to_ta = lv_textarea_create(body);
    lv_obj_set_size(g_set_modal.screen_to_ta, 100, 30);
    lv_obj_set_pos(g_set_modal.screen_to_ta, 2, y);
    lv_textarea_set_one_line(g_set_modal.screen_to_ta, true);
    lv_textarea_set_max_length(g_set_modal.screen_to_ta, 4);
    attachSettingsTaEvents(g_set_modal.screen_to_ta);
    if (g_lv.task) {
      char buf[8];
      snprintf(buf, sizeof(buf), "%u", (unsigned)g_lv.task->getScreenTimeoutSecs());
      lv_textarea_set_text(g_set_modal.screen_to_ta, buf);
    }
    lv_obj_t* b_save_to = lv_btn_create(body);
    lv_obj_set_size(b_save_to, 110, 30);
    lv_obj_set_pos(b_save_to, 110, y);
    styleButton(b_save_to);
    lv_obj_add_event_cb(b_save_to, saveScreenTimeoutCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* ls = lv_label_create(b_save_to);
    lv_label_set_text(ls, "Save");
    lv_obj_center(ls);
    y += 38;
  }

  /* Distance units: OFF = km (default), ON = miles. Applies immediately. */
  {
    lv_obj_t* l = lv_label_create(body);
    lv_label_set_text(l, "Distance in miles");
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_pos(l, 2, y + 6);
    lv_obj_t* sw = lv_switch_create(body);
    lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, y);   // flush to the card's right edge
#if defined(ESP32)
    if (touchPrefsGetUseMiles()) lv_obj_add_state(sw, LV_STATE_CHECKED);
#endif
    lv_obj_add_event_cb(sw, useMilesToggleCb, LV_EVENT_VALUE_CHANGED, nullptr);
    y += 40;
  }

#if defined(HAS_TDECK_GT911)
  /* Store all data (identity/prefs/contacts/channels) on the SD card under
     /meshcomod instead of internal flash — for running under Launcher, or just
     to keep everything on a card. Read at boot, so it applies after a reboot. */
  {
    lv_obj_t* l = lv_label_create(body);
    lv_label_set_text(l, "Store data on SD (reboot)");
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_pos(l, 2, y + 6);
    lv_obj_t* sw = lv_switch_create(body);
    lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, y);
#if defined(ESP32)
    if (touchPrefsGetUseSdStorage()) lv_obj_add_state(sw, LV_STATE_CHECKED);
#endif
    lv_obj_add_event_cb(sw, useSdStorageToggleCb, LV_EVENT_VALUE_CHANGED, nullptr);
    y += 40;
  }
#endif

  /* Secondary keyboards (multi-select). Switch on any layouts you want in the
     rotation; a double-tap of SPACE on the physical keyboard cycles
     English -> each enabled layout -> back. The active layout is remembered
     across reboots. */
  {
    lv_obj_t* l = lv_label_create(body);
    lv_label_set_text(l, "Secondary keyboards");
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, &g_font_12, LV_PART_MAIN);
    lv_obj_set_pos(l, 2, y);
    y += 16;

    lv_obj_t* hint = lv_label_create(body);
    lv_label_set_text(hint, "double-tap SPACE cycles through the ones you enable");
    lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &g_font_12, LV_PART_MAIN);
    lv_obj_set_pos(hint, 2, y);
    y += 18;

#if defined(ESP32)
    uint16_t en_mask = touchPrefsGetEnabledLayouts();
#else
    uint16_t en_mask = 0;
#endif
    /* One row per non-English layout; index id-1 into the display names.
       Keep in KeyboardLayoutId order (BG=1 .. AR=6). */
    static const char* k_kb_disp[] = {
      "Bulgarian", "Russian", "Ukrainian", "Serbian", "Greek", "Arabic (experimental)"
    };
    for (int id = 1; id < KEYBOARD_LAYOUT_COUNT; ++id) {
      lv_obj_t* rl = lv_label_create(body);
      lv_label_set_text(rl, k_kb_disp[id - 1]);
      lv_obj_set_style_text_color(rl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
      lv_obj_set_style_text_font(rl, &g_font_12, LV_PART_MAIN);
      lv_obj_set_pos(rl, 2, y + 4);

      lv_obj_t* sw = lv_switch_create(body);
      lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, y);
      if (en_mask & (1u << id)) lv_obj_add_state(sw, LV_STATE_CHECKED);
      lv_obj_set_user_data(sw, (void*)(intptr_t)id);
      lv_obj_add_event_cb(sw, kbLayoutSwitchCb, LV_EVENT_VALUE_CHANGED, nullptr);
      y += 34;
    }
  }

  /* Screen orientation. Cycles Portrait -> Landscape -> Landscape (flipped);
     applied at boot, so tapping reboots the device. */
  {
    lv_obj_t* l = lv_label_create(body);
    lv_label_set_text(l, "Orientation (tap to rotate, reboots)");
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, &g_font_12, LV_PART_MAIN);
    lv_obj_set_pos(l, 2, y);
    y += 16;
    lv_obj_t* b_rot = lv_btn_create(body);
    lv_obj_set_size(b_rot, lv_pct(100),34);
    lv_obj_set_pos(b_rot, 2, y);
    styleButton(b_rot);
    lv_obj_add_event_cb(b_rot, rotateScreenCycleCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lr = lv_label_create(b_rot);
    lv_label_set_text(lr, uiRotationLabel());
    lv_obj_center(lr);
    y += 42;
  }

#if defined(HAS_TDECK_GT911)
  /* Lock screen: pick the wallpaper (internal /lock/ or SD) and the colour of
     the clock + lock text drawn over it. */
  {
    lv_obj_t* l = lv_label_create(body);
    lv_label_set_text(l, "Lock screen wallpaper");
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, &g_font_12, LV_PART_MAIN);
    lv_obj_set_pos(l, 2, y);
    y += 16;
    lv_obj_t* b_wall = lv_btn_create(body);
    lv_obj_set_size(b_wall, lv_pct(100), 34);
    lv_obj_set_pos(b_wall, 2, y);
    styleButton(b_wall);
    lv_obj_add_event_cb(b_wall, openLockWallPickerCb, LV_EVENT_CLICKED, nullptr);
    s_lockwall_btn_lbl = lv_label_create(b_wall);
    {
      char cur[TOUCH_LOCK_WALLPAPER_MAXLEN], disp[64];
      touchPrefsGetLockWallpaper(cur, sizeof cur);
      lockwallDisplayName(cur, disp, sizeof disp);
      lv_label_set_text(s_lockwall_btn_lbl, disp);
    }
    lv_label_set_long_mode(s_lockwall_btn_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_lockwall_btn_lbl, lv_pct(92));
    lv_obj_center(s_lockwall_btn_lbl);
    y += 40;

    lv_obj_t* lc = lv_label_create(body);
    lv_label_set_text(lc, "Lock text colour");
    lv_obj_set_style_text_color(lc, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(lc, &g_font_12, LV_PART_MAIN);
    lv_obj_set_pos(lc, 2, y);
    y += 16;
    const int ncol = (int)(sizeof(kLockColors) / sizeof(kLockColors[0]));
    const uint32_t curcol = touchPrefsGetLockTextColor();
    const int swz = 22, gap = 3;
    for (int i = 0; i < ncol; ++i) {
      lv_obj_t* sb = lv_btn_create(body);
      lv_obj_set_size(sb, swz, swz);
      lv_obj_set_pos(sb, 2 + i * (swz + gap), y);
      lv_obj_set_style_radius(sb, 5, LV_PART_MAIN);
      lv_obj_set_style_bg_color(sb, lv_color_hex(kLockColors[i]), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(sb, LV_OPA_COVER, LV_PART_MAIN);
      // Highlight the current colour with a white ring.
      const bool sel = (kLockColors[i] == curcol);
      lv_obj_set_style_border_width(sb, sel ? 2 : 1, LV_PART_MAIN);
      lv_obj_set_style_border_color(sb, lv_color_hex(sel ? 0xFFFFFF : 0x18191A), LV_PART_MAIN);
      lv_obj_add_event_cb(sb, lockColorChosenCb, LV_EVENT_CLICKED, (void*)(uintptr_t)kLockColors[i]);
    }
    y += swz + 10;
  }
#endif

  /* Time offset: nudge the displayed clock +/- whole hours on top of the
     automatic (NTP / companion / mesh) time. Display-only. Both boards. */
  {
    lv_obj_t* l = lv_label_create(body);
    lv_label_set_text(l, "Time offset (vs automatic)");
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, &g_font_12, LV_PART_MAIN);
    lv_obj_set_pos(l, 2, y);
    y += 16;

    lv_obj_t* bminus = lv_btn_create(body);
    lv_obj_set_size(bminus, 50, 34);
    lv_obj_set_pos(bminus, 2, y);
    styleButton(bminus);
    lv_obj_add_event_cb(bminus, timeOffsetMinusCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lm = lv_label_create(bminus); lv_label_set_text(lm, "-1 h"); lv_obj_center(lm);

    s_time_offset_lbl = lv_label_create(body);
    lv_obj_set_width(s_time_offset_lbl, 100);
    lv_obj_set_style_text_align(s_time_offset_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_time_offset_lbl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_time_offset_lbl, &g_font_16, LV_PART_MAIN);
    lv_obj_set_pos(s_time_offset_lbl, 58, y + 9);
    timeOffsetLabelRefresh();

    lv_obj_t* bplus = lv_btn_create(body);
    lv_obj_set_size(bplus, 50, 34);
    lv_obj_set_pos(bplus, 168, y);
    styleButton(bplus);
    lv_obj_add_event_cb(bplus, timeOffsetPlusCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lp = lv_label_create(bplus); lv_label_set_text(lp, "+1 h"); lv_obj_center(lp);
    y += 42;
  }

  // Re-run the first-boot setup flow (name / region / Wi-Fi) on demand.
  lv_obj_t* b_setup = lv_btn_create(body);
  lv_obj_set_size(b_setup, lv_pct(100), 34);
  lv_obj_set_pos(b_setup, 2, y);
  styleButton(b_setup);
  lv_obj_add_event_cb(b_setup, setupRerunCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l_setup = lv_label_create(b_setup);
  lv_label_set_text(l_setup, LV_SYMBOL_REFRESH "  Run setup again");
  lv_obj_center(l_setup);
  y += 42;

  lv_obj_t* b_reboot = lv_btn_create(body);
  lv_obj_set_size(b_reboot, lv_pct(100),34);
  lv_obj_set_pos(b_reboot, 2, y);
  styleButton(b_reboot);
  lv_obj_set_style_bg_color(b_reboot, lv_color_hex(0xC44B55), LV_PART_MAIN);
  lv_obj_set_style_bg_color(b_reboot, lv_color_hex(0xA13F47), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(b_reboot, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_add_event_cb(b_reboot, rebootCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l_reboot = lv_label_create(b_reboot);
  lv_label_set_text(l_reboot, "Reboot device");
  lv_obj_center(l_reboot);
  y += 42;

  // (Reboot-to-recovery button removed — the on-device recovery is retired.)
  y += 4;

  // Calibrate battery: capture the current voltage as 100% (for custom packs /
  // builds whose full voltage isn't 4.2 V). Tap = set 100%; long-press = reset.
  {
    lv_obj_t* b_cal = lv_btn_create(body);
    lv_obj_set_size(b_cal, lv_pct(100), 34);
    lv_obj_set_pos(b_cal, 2, y);
    styleButton(b_cal);
    lv_obj_set_style_bg_color(b_cal, lv_color_hex(0x2F6B57), LV_PART_MAIN);
    lv_obj_set_style_bg_color(b_cal, lv_color_hex(0x244F41), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(b_cal, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_add_event_cb(b_cal, calibrateBatteryCb, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_add_event_cb(b_cal, calibrateBatteryCb, LV_EVENT_LONG_PRESSED, nullptr);
    lv_obj_t* l_cal = lv_label_create(b_cal);
    lv_label_set_text(l_cal, LV_SYMBOL_BATTERY_FULL "  Calibrate battery (full = 100%)");
    lv_obj_center(l_cal);
    y += 46;
  }

  // ----- Live info panel (below action buttons) -----
  // Mirrors the web client's "Device (live)" block: firmware, model, public
  // key prefix, contacts/channels counts, battery, current device time. These
  // are snapshot-at-open — re-open the Device modal to refresh.
  {
    lv_obj_t* sep = lv_obj_create(body);
    lv_obj_set_size(sep, lv_pct(100),1);
    lv_obj_set_pos(sep, 2, y);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x18191A), LV_PART_MAIN);
    lv_obj_set_style_border_width(sep, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sep, 0, LV_PART_MAIN);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);
    y += 6;
  }

  auto mk_info = [&](const char* label, const char* value) {
    lv_obj_t* row = lv_label_create(body);
    char line[96];
    snprintf(line, sizeof(line), "%s  %s", label, value);
    lv_label_set_text(row, line);
    /* Constrain height to one line so LONG_DOT actually ellipsizes instead of
     * wrapping — long firmware strings were overlapping the next row. */
    lv_obj_set_size(row, lv_pct(100),14);
    lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(row, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(row, &g_font_12, LV_PART_MAIN);
    lv_obj_set_pos(row, 2, y);
    y += 16;
  };
  {
    char buf[96];
    // Firmware
    snprintf(buf, sizeof(buf), "%s · %s", FIRMWARE_VERSION, FIRMWARE_BUILD_DATE);
    mk_info("Firmware:", buf);
    // Device model (build-time constant for this variant)
#if defined(HELTEC_LORA_V4_TFT)
    mk_info("Model:", "Heltec LoRa32 V4 TFT (touch)");
#else
    mk_info("Model:", "Heltec LoRa32 V4");
#endif
    // Public key prefix (first 8 bytes = 16 hex)
    {
      const uint8_t* pk = the_mesh.getSelfPubKey();
      if (pk) {
        char hex[20];
        mesh::Utils::toHex(hex, pk, 8);
        hex[16] = '\0';
        snprintf(buf, sizeof(buf), "%s…", hex);
        mk_info("Public key:", buf);
      } else {
        mk_info("Public key:", "—");
      }
    }
    // Contacts / Channels with max limits
#ifdef MAX_CONTACTS
    snprintf(buf, sizeof(buf), "%d / %d", the_mesh.getNumContacts(), (int)MAX_CONTACTS);
#else
    snprintf(buf, sizeof(buf), "%d", the_mesh.getNumContacts());
#endif
    mk_info("Contacts:", buf);
#ifdef MAX_GROUP_CHANNELS
    snprintf(buf, sizeof(buf), "%d / %d", the_mesh.getNumChannels(), (int)MAX_GROUP_CHANNELS);
#else
    snprintf(buf, sizeof(buf), "%d", the_mesh.getNumChannels());
#endif
    mk_info("Channels:", buf);
    // Battery
    if (g_lv.task) {
      uint16_t mv = g_lv.task->getBattMilliVolts();
      if (mv > 0) {
        snprintf(buf, sizeof(buf), "%u mV", (unsigned)mv);
      } else {
        snprintf(buf, sizeof(buf), "—");
      }
      mk_info("Battery:", buf);
    }
    // Device time (formatted as YYYY-MM-DD HH:MM in local timezone)
    {
      mesh::RTCClock* rtc = the_mesh.getRTCClock();
      uint32_t now = rtc ? rtc->getCurrentTime() : 0;
      if (now > 0) {
        time_t t = (time_t)now;
        struct tm tmv;
        /* RTC stores UTC; show user-local time using TZ set via configTzTime
         * in main.cpp (CET-1CEST,M3.5.0,M10.5.0/3). */
        localtime_r(&t, &tmv);
        char ts[40];
        snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d %s",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min,
                 tmv.tm_isdst > 0 ? "CEST" : "CET");
        mk_info("Device time:", ts);
      } else {
        mk_info("Device time:", "not set");
      }
    }
  }
  y += 6;

  lv_obj_t* hint = lv_label_create(body);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(hint, 216);
  lv_obj_set_pos(hint, 2, y);
  lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(hint, &g_font_12, LV_PART_MAIN);
  lv_label_set_text(hint, "Diagnostics scroll on the main Set tab below.");
}

#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
static void trimWifiField(char* s) {
  if (!s) return;
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
  char* p = s;
  while (*p == ' ' || *p == '\t') ++p;
  if (p != s) memmove(s, p, strlen(p) + 1);
}

static void saveTransportWifiCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  if (!g_set_modal.wifi_ssid_ta || !g_set_modal.wifi_pwd_ta || !g_set_modal.wifi_sw) return;
  kbMirrorSyncToReal();

  char ssid[WIFI_CONFIG_SSID_MAX];
  char pwd[WIFI_CONFIG_PWD_MAX];
  strncpy(ssid, lv_textarea_get_text(g_set_modal.wifi_ssid_ta), sizeof(ssid) - 1);
  ssid[sizeof(ssid) - 1] = '\0';
  strncpy(pwd, lv_textarea_get_text(g_set_modal.wifi_pwd_ta), sizeof(pwd) - 1);
  pwd[sizeof(pwd) - 1] = '\0';
  trimWifiField(ssid);
  trimWifiField(pwd);

  if (!wifiConfigSetSsid(ssid)) {
    g_lv.task->showAlert("SSID too long", 1400);
    return;
  }
  if (!wifiConfigSetPwd(pwd)) {
    g_lv.task->showAlert("Password too long", 1400);
    return;
  }

  const bool radio_on = lv_obj_has_state(g_set_modal.wifi_sw, LV_STATE_CHECKED);
  wifiConfigSetRadioEnabled(radio_on);
  if (radio_on) {
    if (!wifiConfigHasRuntime() || ssid[0] == '\0') {
      g_lv.task->showAlert("Set an SSID first", 1400);
      return;
    }
    wifiConfigRequestApply();
    g_lv.task->showAlert("Wi-Fi saved, reconnecting", 1400);
  } else {
    wifiConfigRequestApply();
    g_lv.task->showAlert("Wi-Fi saved (radio off)", 1400);
  }
  refreshStatusLabels();
}
#endif

// ----- Confirm popup -----
// Small floating modal with Cancel + Confirm buttons. Used by the Wi-Fi and
// Bluetooth pages so the user doesn't need to scroll a long inline hint to
// reach the Save button; the explanation appears on click instead.
namespace {
typedef void (*SimpleCb)();
SimpleCb s_confirm_cb = nullptr;
lv_obj_t* s_confirm_modal = nullptr;

void confirmDismiss() {
  if (s_confirm_modal) {
    lv_obj_del(s_confirm_modal);
    s_confirm_modal = nullptr;
  }
  s_confirm_cb = nullptr;
}

void confirmCancelEvt(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  confirmDismiss();
}

void confirmOkEvt(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  SimpleCb cb = s_confirm_cb;
  confirmDismiss();
  if (cb) cb();
}
}  // namespace

static void showConfirm(const char* msg, const char* ok_label, SimpleCb on_confirm) {
  confirmDismiss();
  s_confirm_cb = on_confirm;

  // Backdrop: semi-opaque full-screen catcher so taps outside the card do nothing.
  s_confirm_modal = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_confirm_modal);
  // Backdrop starts below the global status bar — full screen so the card
  // centers on-screen and the tap-catcher covers everything. (When this was a
  // fixed 240-wide rect, in landscape the card landed partly off-screen with
  // its buttons unreachable while the catcher still ate every tap — the UI
  // looked "frozen".)
  lv_obj_set_size(s_confirm_modal, lv_disp_get_hor_res(nullptr),
                  lv_disp_get_ver_res(nullptr) - STATUSBAR_H);
  lv_obj_set_pos(s_confirm_modal, 0, STATUSBAR_H);
  lv_obj_set_style_bg_opa(s_confirm_modal, LV_OPA_60, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_confirm_modal, lv_color_black(), LV_PART_MAIN);
  lv_obj_clear_flag(s_confirm_modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_confirm_modal, LV_OBJ_FLAG_FLOATING);
  lv_obj_move_foreground(s_confirm_modal);

  // Card
  lv_obj_t* card = lv_obj_create(s_confirm_modal);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, 210, 160);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  styleSurface(card, COLOR_PANEL, 12);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 12, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  addCloseXBadge(card, confirmCancelEvt);   // X behaves like Cancel

  lv_obj_t* lbl = lv_label_create(card);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  // Shrink width so the first line doesn't slide under the top-right X.
  // Push the label down 4 px so the X glyph and the start of the text
  // baseline don't touch optically.
  lv_obj_set_width(lbl, 186 - 32);
  lv_label_set_text(lbl, msg);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl, &g_font_14, LV_PART_MAIN);
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t* b_cancel = lv_btn_create(card);
  lv_obj_set_size(b_cancel, 80, 34);
  lv_obj_align(b_cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  styleButton(b_cancel);
  lv_obj_set_style_bg_color(b_cancel, lv_color_hex(0x3A4A5C), LV_PART_MAIN);
  lv_obj_set_style_bg_color(b_cancel, lv_color_hex(0x2D3947), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(b_cancel, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_add_event_cb(b_cancel, confirmCancelEvt, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lc = lv_label_create(b_cancel);
  lv_label_set_text(lc, "Cancel");
  lv_obj_center(lc);

  lv_obj_t* b_ok = lv_btn_create(card);
  lv_obj_set_size(b_ok, 100, 34);
  lv_obj_align(b_ok, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  styleButton(b_ok);
  lv_obj_add_event_cb(b_ok, confirmOkEvt, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lo = lv_label_create(b_ok);
  lv_label_set_text(lo, ok_label ? ok_label : "OK");
  lv_obj_center(lo);
}

// ----- Bluetooth settings page -----
// BLE and Wi-Fi are mutually exclusive on this board (the ESP32-S3 internal
// heap can't fit Bluedroid + LVGL + WiFi DMA at the same time). The page
// shows current mode + BLE pin and offers a single toggle:
//   - In Wi-Fi mode: enabling BLE clears wifiConfigRadioEnabled and reboots,
//     so setup() picks BLE on the next boot.
//   - In BLE mode + Wi-Fi configured: disabling BLE re-enables Wi-Fi radio
//     and reboots; setup() picks Wi-Fi.
//   - In BLE mode + no Wi-Fi creds: disabling BLE would leave no companion
//     transport, so we refuse and prompt the user to configure Wi-Fi first.
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
// Captured at saveBluetoothCb time, consumed inside the confirm callback so
// the actual reboot happens after the user clicks "Reboot" in the popup.
static bool s_pending_ble_enable = false;

static void doApplyBluetooth() {
  if (s_pending_ble_enable) {
    // Switching to BLE mode: clear the Wi-Fi radio pref so setup() picks BLE
    // on the next boot. SSID/password stay in NVS for later re-enable.
    wifiConfigSetRadioEnabled(false);
    if (g_lv.task) g_lv.task->showAlert("Enabling BLE — rebooting", 1400);
  } else {
    // Switching to Wi-Fi mode (BLE off). Caller already verified creds exist.
    wifiConfigSetRadioEnabled(true);
    if (g_lv.task) g_lv.task->showAlert("Switching to Wi-Fi — rebooting", 1400);
  }
  wifiConfigRequestApply();
  if (g_lv.task) g_lv.task->persistHistoryNow();   // persist chat before reboot
  delay(1500);
  ESP.restart();
}

static void saveBluetoothCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  if (!g_set_modal.wifi_sw) return;
  const bool want_ble = lv_obj_has_state(g_set_modal.wifi_sw, LV_STATE_CHECKED);
  const bool ble_active_now = g_lv.task->hasBleCapability() && g_lv.task->isBleEnabled();

  if (want_ble && ble_active_now) {
    g_lv.task->showAlert("Bluetooth already active", 1200);
    return;
  }
  if (!want_ble && !ble_active_now) {
    g_lv.task->showAlert("Bluetooth already off", 1200);
    return;
  }
  s_pending_ble_enable = want_ble;
  // Switching to Wi-Fi with no creds yet is allowed: the radio comes up
  // scannable (wifiConfigWantsWifi via the "Wi-Fi chosen" flag set in
  // doApplyBluetooth), so the user can pick a network on-device — no need to
  // type an SSID first. The confirm just tells them that's what'll happen.
  const char* to_wifi = wifiConfigHasRuntime()
      ? "Switch to Wi-Fi and reboot? Bluetooth will be turned off."
      : "Switch to Wi-Fi and reboot? Bluetooth turns off; you can then scan and pick a network.";
  showConfirm(want_ble ? "Enable Bluetooth and reboot? Wi-Fi will be turned off." : to_wifi,
              "Reboot", doApplyBluetooth);
}
#endif

static void buildBluetoothSettings() {
  lv_obj_t* body = createSettingsModal("Bluetooth", SettingsModalKind::Bluetooth);
  int y = 0;

  // ---- Mode line ----
  const bool ble_active = g_lv.task && g_lv.task->hasBleCapability() && g_lv.task->isBleEnabled();
  lv_obj_t* mode = lv_label_create(body);
  if (ble_active) {
    lv_label_set_text(mode, "Mode: BLE active");
  } else {
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
    lv_label_set_text(mode, wifiConfigGetRadioEnabled()
                              ? "Mode: Wi-Fi active (BLE off)"
                              : "Mode: BLE off (Wi-Fi off too)");
#else
    lv_label_set_text(mode, "Mode: BLE off");
#endif
  }
  lv_obj_set_style_text_color(mode, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(mode, &g_font_12, LV_PART_MAIN);
  lv_obj_set_pos(mode, 2, y);
  y += 18;

  // ---- BLE pin line (visible regardless of mode — the pin is set at boot) ----
  if (g_lv.task) {
    char blebuf[40];
    const uint32_t ble_pin = the_mesh.getBLEPin();
    if (ble_pin > 0) snprintf(blebuf, sizeof(blebuf), "Pairing code: %06lu", static_cast<unsigned long>(ble_pin));
    else snprintf(blebuf, sizeof(blebuf), "Pairing code: n/a");
    lv_obj_t* ble_l = lv_label_create(body);
    lv_label_set_text(ble_l, blebuf);
    lv_obj_set_style_text_color(ble_l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(ble_l, &g_font_12, LV_PART_MAIN);
    lv_obj_set_pos(ble_l, 2, y);
    y += 22;
  }

#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  // ---- Enable switch + save button ----
  lv_obj_t* sw_lbl = lv_label_create(body);
  lv_label_set_text(sw_lbl, "Enable Bluetooth");
  lv_obj_set_style_text_color(sw_lbl, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_pos(sw_lbl, 2, y + 6);
  g_set_modal.wifi_sw = lv_switch_create(body);  // reuse the same slot — only one switch lives in a modal at a time
  lv_obj_align(g_set_modal.wifi_sw, LV_ALIGN_TOP_RIGHT, 0, y);   // flush right
  if (ble_active) lv_obj_add_state(g_set_modal.wifi_sw, LV_STATE_CHECKED);
  y += 38;

  // No inline reboot hint — saveBluetoothCb shows a confirmation popup with
  // the full explanation when the user actually presses Save, keeping the
  // page short enough that Save is visible without scrolling.

  lv_obj_t* b_save = lv_btn_create(body);
  lv_obj_set_size(b_save, lv_pct(100),34);
  lv_obj_set_pos(b_save, 2, y);
  styleButton(b_save);
  lv_obj_add_event_cb(b_save, saveBluetoothCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lbl = lv_label_create(b_save);
  lv_label_set_text(lbl, "Save");
  lv_obj_center(lbl);
#else
  (void)y;
#endif
  refreshStatusLabels();
}

// ----- Dedicated Wi-Fi settings page -----
// Shows live connection state (status, IP, RSSI), web-socket count, and the
// SSID / password fields with a single Save+reconnect button.
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
// State captured at saveWifiCb time, applied inside the confirm callback.
static char    s_pending_wifi_ssid[WIFI_CONFIG_SSID_MAX];
static char    s_pending_wifi_pwd[WIFI_CONFIG_PWD_MAX];
static bool    s_pending_wifi_radio_on = false;
static bool    s_pending_wifi_was_wifi = false;   // Wi-Fi already the live transport at save time?

static void doApplyWifi() {
  if (!wifiConfigSetSsid(s_pending_wifi_ssid)) {
    if (g_lv.task) g_lv.task->showAlert("SSID too long", 1400);
    return;
  }
  if (!wifiConfigSetPwd(s_pending_wifi_pwd)) {
    if (g_lv.task) g_lv.task->showAlert("Password too long", 1400);
    return;
  }
  wifiConfigSetRadioEnabled(s_pending_wifi_radio_on);
  wifiConfigRequestApply();

  // A reboot is only needed to switch transports: BLE->Wi-Fi (Bluedroid must be
  // torn down before esp_wifi_init can grab its heap) or Wi-Fi->BLE (BLE only
  // inits in setup()). When Wi-Fi is already live and stays on, the main loop
  // re-begins with the new creds (consumeApplyRequest path) — no reboot needed.
  const bool reboot_needed = !(s_pending_wifi_was_wifi && s_pending_wifi_radio_on);
  if (!reboot_needed) {
    if (g_lv.task) g_lv.task->showAlert("Saved — reconnecting\xE2\x80\xA6", 1600);
    refreshStatusLabels();
    return;
  }
  if (g_lv.task) {
    g_lv.task->showAlert("Saved — rebooting", 1400);
    g_lv.task->persistHistoryNow();   // persist chat before reboot
  }
  refreshStatusLabels();
  delay(1500);
  ESP.restart();
}

// Forward decl so wifiSlotSaveCb can refresh the modal after writing.
static void buildWifiSettings();

// Saved Wi-Fi profile slot: tap to load. Fills the SSID/PWD textareas with
// the stored creds — does NOT auto-apply, so the operator can still tweak
// (or change their mind by tapping Close) before the main "Save / turn on"
// commits and reboots.
static void wifiSlotLoadCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  intptr_t idx = (intptr_t)lv_event_get_user_data(e);
  if (!g_set_modal.wifi_ssid_ta || !g_set_modal.wifi_pwd_ta) return;
  char label[TOUCH_WIFI_LABEL_MAX];
  char ssid_b[WIFI_CONFIG_SSID_MAX];
  char pwd_b[WIFI_CONFIG_PWD_MAX];
  if (!touchPrefsGetWifiSlot((int)idx, label, sizeof(label),
                             ssid_b, sizeof(ssid_b),
                             pwd_b, sizeof(pwd_b))) {
    if (g_lv.task) g_lv.task->showAlert("Slot empty", 1000);
    return;
  }
  if (ssid_b[0] == '\0') {
    if (g_lv.task) g_lv.task->showAlert("Slot empty", 1000);
    return;
  }
  lv_textarea_set_text(g_set_modal.wifi_ssid_ta, ssid_b);
  lv_textarea_set_text(g_set_modal.wifi_pwd_ta,  pwd_b);
  if (g_set_modal.wifi_sw) lv_obj_add_state(g_set_modal.wifi_sw, LV_STATE_CHECKED);
  char msg[40];
  snprintf(msg, sizeof(msg), "Loaded slot %d — Save to apply", (int)idx + 1);
  if (g_lv.task) g_lv.task->showAlert(msg, 1300);
}

// Save the current SSID/PWD form contents into a slot. Label defaults to
// "Slot N" so the row text isn't blank; user can rename via the same flow
// after editing (no inline rename UI — keeps the page short).
static void wifiSlotSaveCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  intptr_t idx = (intptr_t)lv_event_get_user_data(e);
  if (!g_set_modal.wifi_ssid_ta || !g_set_modal.wifi_pwd_ta) return;
  kbMirrorSyncToReal();
  const char* ssid = lv_textarea_get_text(g_set_modal.wifi_ssid_ta);
  const char* pwd  = lv_textarea_get_text(g_set_modal.wifi_pwd_ta);
  if (!ssid || !ssid[0]) {
    if (g_lv.task) g_lv.task->showAlert("Enter SSID first", 1200);
    return;
  }
  // Use the SSID as the slot label so the row text reads naturally even
  // without an explicit naming step.
  char label[TOUCH_WIFI_LABEL_MAX];
  strncpy(label, ssid, sizeof(label) - 1);
  label[sizeof(label) - 1] = '\0';
  if (!touchPrefsSetWifiSlot((int)idx, label, ssid, pwd)) {
    if (g_lv.task) g_lv.task->showAlert("Save failed", 1200);
    return;
  }
  char msg[40];
  snprintf(msg, sizeof(msg), "Saved to slot %d", (int)idx + 1);
  if (g_lv.task) g_lv.task->showAlert(msg, 1100);
  // Re-open the modal so the row text refreshes. Cheap: existing state will
  // pull the new slot back from NVS via touchPrefsGetWifiSlot.
  closeSettingsModal();
  buildWifiSettings();   // re-open (modal here; inline_parent is null in this callback)
}

static void saveWifiCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  if (!g_set_modal.wifi_ssid_ta || !g_set_modal.wifi_pwd_ta || !g_set_modal.wifi_sw) return;
  kbMirrorSyncToReal();

  strncpy(s_pending_wifi_ssid, lv_textarea_get_text(g_set_modal.wifi_ssid_ta), sizeof(s_pending_wifi_ssid) - 1);
  s_pending_wifi_ssid[sizeof(s_pending_wifi_ssid) - 1] = '\0';
  strncpy(s_pending_wifi_pwd, lv_textarea_get_text(g_set_modal.wifi_pwd_ta), sizeof(s_pending_wifi_pwd) - 1);
  s_pending_wifi_pwd[sizeof(s_pending_wifi_pwd) - 1] = '\0';
  trimWifiField(s_pending_wifi_ssid);
  trimWifiField(s_pending_wifi_pwd);

  // If the user entered an SSID, treat that as intent to connect — force the
  // radio switch on even if they forgot to toggle it. Otherwise honour switch.
  s_pending_wifi_radio_on = lv_obj_has_state(g_set_modal.wifi_sw, LV_STATE_CHECKED);
  if (!s_pending_wifi_radio_on && s_pending_wifi_ssid[0] != '\0') {
    s_pending_wifi_radio_on = true;
    lv_obj_add_state(g_set_modal.wifi_sw, LV_STATE_CHECKED);
  }
  // Capture the live transport now, before doApplyWifi flips any pref. If Wi-Fi
  // is already up and stays on, saving new creds is a hot reconnect — apply
  // straight away (the main loop re-begins), no reboot and no confirm to
  // interrupt. Only a transport switch (to/from BLE) reboots, so keep the
  // confirm prompt for that case.
  s_pending_wifi_was_wifi = wifiConfigWantsWifi();
  if (s_pending_wifi_was_wifi && s_pending_wifi_radio_on) {
    doApplyWifi();
    return;
  }
  showConfirm(s_pending_wifi_radio_on
                ? "Save Wi-Fi and reboot? Bluetooth will be turned off."
                : "Save Wi-Fi (radio off) and reboot?",
              "Reboot", doApplyWifi);
}
#endif

static void wifiScanPopupClose() {
  if (s_wifi_scan_popup) { lv_obj_del(s_wifi_scan_popup); s_wifi_scan_popup = nullptr; }
  s_wifi_scan_list = nullptr;
}
static void wifiScanPopupCloseCb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) wifiScanPopupClose();
}

// Tap a scanned SSID -> drop it into the SSID field + close the popup.
static void wifiScanSsidCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const int idx = (int)(intptr_t)lv_event_get_user_data(e);
  const bool ok = (idx >= 0 && idx < s_wifiscan_count && g_set_modal.wifi_ssid_ta);
  // setTextareaSynced (not plain set_text): if the keyboard mirror is bound to
  // the SSID field, a later sync would clobber this back to the old SSID.
  if (ok) setTextareaSynced(g_set_modal.wifi_ssid_ta, s_wifiscan_ssids[idx]);
  wifiScanPopupClose();
  if (ok && g_lv.task) g_lv.task->showAlert("SSID set — enter password, then Save", 1700);
}

// (Re)draw the scan-results list inside the popup from the latest scan.
static void wifiScanFillList() {
  if (!s_wifi_scan_list) return;
  lv_obj_clean(s_wifi_scan_list);
  const lv_coord_t rw = lv_disp_get_hor_res(nullptr) - 28;
  if (s_wifiscan_count <= 0) {
    lv_obj_t* l = lv_label_create(s_wifi_scan_list);
    lv_label_set_text(l, "No networks found (2.4 GHz only)");
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, &g_font_14, LV_PART_MAIN);
    return;
  }
  for (int i = 0; i < s_wifiscan_count; ++i) {
    lv_obj_t* r = lv_btn_create(s_wifi_scan_list);
    lv_obj_set_size(r, rw, 40);   // tall rows = easy to tap + scroll
    styleButton(r);
    lv_obj_set_style_bg_color(r, lv_color_hex(0x1A1B1C), LV_PART_MAIN);
    lv_obj_add_event_cb(r, wifiScanSsidCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_t* l = lv_label_create(r);
    lv_label_set_text(l, s_wifiscan_ssids[i]);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, rw - 16);
    lv_obj_set_style_text_font(l, &g_font_14, LV_PART_MAIN);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
  }
}

// Full-screen scan overlay (covers the bottom bar + sub-tabs) so the SSID list
// is big and easy to scroll. Sits below the status bar, on lv_layer_top.
static void openWifiScanPopup() {
  wifiScanPopupClose();
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_wifi_scan_popup = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_wifi_scan_popup);
  lv_obj_set_size(s_wifi_scan_popup, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_wifi_scan_popup, 0, STATUSBAR_H);
  styleSurface(s_wifi_scan_popup, COLOR_BG, 0);
  lv_obj_set_style_pad_all(s_wifi_scan_popup, 0, LV_PART_MAIN);
  lv_obj_clear_flag(s_wifi_scan_popup, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(s_wifi_scan_popup);
  lv_label_set_text(title, "Select network");
  lv_obj_set_style_text_font(title, &g_font_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_pos(title, 8, 9);

  lv_obj_t* close = lv_btn_create(s_wifi_scan_popup);
  lv_obj_set_size(close, 60, 30);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -6, 4);
  styleButton(close);
  lv_obj_add_event_cb(close, wifiScanPopupCloseCb, LV_EVENT_CLICKED, nullptr);
  { lv_obj_t* cl = lv_label_create(close); lv_label_set_text(cl, "Close");
    lv_obj_set_style_text_font(cl, &g_font_12, LV_PART_MAIN); lv_obj_center(cl); }

  s_wifi_scan_list = lv_obj_create(s_wifi_scan_popup);
  lv_obj_remove_style_all(s_wifi_scan_list);
  lv_obj_set_size(s_wifi_scan_list, sw - 12, sh - STATUSBAR_H - 44);
  lv_obj_set_pos(s_wifi_scan_list, 6, 40);
  lv_obj_set_flex_flow(s_wifi_scan_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_wifi_scan_list, 6, LV_PART_MAIN);
  lv_obj_set_scroll_dir(s_wifi_scan_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_wifi_scan_list, LV_SCROLLBAR_MODE_AUTO);
}

// Open the picker and kick a fresh scan on the core-0 worker. Shows the last
// results instantly (if any) while the rescan lands. Shared by the Settings
// Wi-Fi page, the setup wizard's Scan button, and the wizard's auto-scan on
// arrival. Caller must have already confirmed wifiConfigWantsWifi() (radio up).
static void wifiScanOpenAndKick() {
  hideKb();   // unbind the keyboard mirror so it can't later revert the picked SSID
  openWifiScanPopup();
  if (s_wifiscan_count > 0) {
    wifiScanFillList();   // show last results immediately, refresh when the rescan lands
  } else if (s_wifi_scan_list) {
    lv_obj_t* l = lv_label_create(s_wifi_scan_list);
    lv_label_set_text(l, "Scanning\xE2\x80\xA6");
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, &g_font_14, LV_PART_MAIN);
  }
  ensureTileFetchTaskRunning();   // the worker idles out after 5 s — respawn it
  s_wifiscan_request = true;
}

// Switch into Wi-Fi mode (reboot) so a scan can run — WITHOUT needing creds
// first. Used when the user taps Scan while BLE is the active transport.
// setRadioEnabled(true) marks Wi-Fi "chosen", so wantsWifi() brings the radio
// up STA (scannable) on the next boot even with no SSID saved.
#if defined(MULTI_TRANSPORT_COMPANION)
static void doSwitchToWifiForScan() {
  wifiConfigSetRadioEnabled(true);            // marks Wi-Fi chosen -> scannable boot
  if (g_lv.task) g_lv.task->rebootDevice();   // persists chat history first
}
#endif

// "Scan" button -> open the popup + queue a scan on the core-0 worker.
static void wifiScanStartCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
#if defined(MULTI_TRANSPORT_COMPANION)
  if (!wifiConfigWantsWifi()) {
    // BLE is the active transport — the radio is down, so we can't scan. Offer
    // to switch to Wi-Fi (reboots; comes up scannable even with no creds) so
    // the user doesn't have to type an SSID blind.
    showConfirm("Wi-Fi is off (Bluetooth is on). Switch to Wi-Fi and reboot so "
                "you can scan and pick a network?",
                "Switch", doSwitchToWifiForScan);
    return;
  }
  wifiScanOpenAndKick();
#endif
}

// Loop service: when the worker finishes a scan, repopulate the popup list.
static void wifiScanService() {
  if (!s_wifiscan_done) return;
  s_wifiscan_done = false;
  wifiScanFillList();   // no-op if the popup was closed
}

static void buildWifiSettings() {
  lv_obj_t* body = createSettingsModal("Wi-Fi", SettingsModalKind::Wifi);
  int y = 0;
  const lv_coord_t cw = s_settings_content_w;

#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  // ---- Status (+ Wi-Fi radio toggle on the same row to save vertical space) ----
  lv_obj_t* sec1 = lv_label_create(body);
  lv_label_set_text(sec1, "Status");
  lv_obj_set_style_text_color(sec1, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(sec1, &g_font_12, LV_PART_MAIN);
  lv_obj_set_pos(sec1, 2, y + 6);
  g_set_modal.wifi_sw = lv_switch_create(body);
  lv_obj_align(g_set_modal.wifi_sw, LV_ALIGN_TOP_RIGHT, 0, y);   // toggle next to "Status"
  if (wifiConfigGetRadioEnabled()) lv_obj_add_state(g_set_modal.wifi_sw, LV_STATE_CHECKED);
  y += 30;

  g_set_modal.wifi_sta_status_l = lv_label_create(body);
  lv_label_set_long_mode(g_set_modal.wifi_sta_status_l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(g_set_modal.wifi_sta_status_l, lv_pct(100));
  lv_obj_set_style_text_color(g_set_modal.wifi_sta_status_l, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(g_set_modal.wifi_sta_status_l, &g_font_14, LV_PART_MAIN);
  lv_obj_set_pos(g_set_modal.wifi_sta_status_l, 2, y);
  lv_label_set_text(g_set_modal.wifi_sta_status_l, "Loading...");
  y += 42;   // room for the 2-line connected status so it can't overlap the SSID field

  // ---- SSID + Scan (same row: field on the left, Scan opens the picker) ----
  lv_obj_t* wssid_l = lv_label_create(body);
  lv_label_set_text(wssid_l, "Network name (SSID)");
  lv_obj_set_style_text_color(wssid_l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(wssid_l, &g_font_12, LV_PART_MAIN);
  lv_obj_set_pos(wssid_l, 2, y);
  y += 16;
  const int scan_w = 76, scan_g = 6;
  g_set_modal.wifi_ssid_ta = lv_textarea_create(body);
  lv_obj_set_size(g_set_modal.wifi_ssid_ta, cw - scan_w - scan_g, 32);
  lv_obj_set_pos(g_set_modal.wifi_ssid_ta, 0, y);
  lv_textarea_set_one_line(g_set_modal.wifi_ssid_ta, true);
  lv_textarea_set_placeholder_text(g_set_modal.wifi_ssid_ta, "Network name");
  lv_textarea_set_max_length(g_set_modal.wifi_ssid_ta, WIFI_CONFIG_SSID_MAX - 1);
  attachSettingsTaEvents(g_set_modal.wifi_ssid_ta);
  lv_obj_t* scan_btn = lv_btn_create(body);
  lv_obj_set_size(scan_btn, scan_w, 32);
  lv_obj_set_pos(scan_btn, cw - scan_w, y);
  styleButton(scan_btn);
  lv_obj_add_event_cb(scan_btn, wifiScanStartCb, LV_EVENT_CLICKED, nullptr);
  { lv_obj_t* sl = lv_label_create(scan_btn);
    lv_label_set_text(sl, LV_SYMBOL_WIFI " Scan");
    lv_obj_set_style_text_font(sl, &g_font_12, LV_PART_MAIN); lv_obj_center(sl); }
  y += 38;

  // ---- Password ----
  lv_obj_t* wpwd_l = lv_label_create(body);
  lv_label_set_text(wpwd_l, "Password (empty = open)");
  lv_obj_set_style_text_color(wpwd_l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(wpwd_l, &g_font_12, LV_PART_MAIN);
  lv_obj_set_pos(wpwd_l, 2, y);
  y += 16;
  g_set_modal.wifi_pwd_ta = lv_textarea_create(body);
  lv_obj_set_size(g_set_modal.wifi_pwd_ta, lv_pct(100),30);
  lv_obj_set_pos(g_set_modal.wifi_pwd_ta, 2, y);
  lv_textarea_set_one_line(g_set_modal.wifi_pwd_ta, true);
  lv_textarea_set_password_mode(g_set_modal.wifi_pwd_ta, true);
  lv_textarea_set_placeholder_text(g_set_modal.wifi_pwd_ta, "PSK");
  lv_textarea_set_max_length(g_set_modal.wifi_pwd_ta, WIFI_CONFIG_PWD_MAX - 1);
  attachSettingsTaEvents(g_set_modal.wifi_pwd_ta);
  y += 36;

  {
    char ssid_buf[WIFI_CONFIG_SSID_MAX];
    char pwd_buf[WIFI_CONFIG_PWD_MAX];
    wifiConfigGetSsid(ssid_buf, sizeof(ssid_buf));
    wifiConfigGetPwd(pwd_buf, sizeof(pwd_buf));
    lv_textarea_set_text(g_set_modal.wifi_ssid_ta, ssid_buf);
    lv_textarea_set_text(g_set_modal.wifi_pwd_ta, pwd_buf);
  }

  // No inline reboot hint — saveWifiCb shows a confirmation popup with the
  // full explanation when the user actually presses Save. Keeps the page
  // short enough that the button stays visible above the fold.
  lv_obj_t* b_save = lv_btn_create(body);
  lv_obj_set_size(b_save, lv_pct(100),36);
  lv_obj_set_pos(b_save, 2, y);
  styleButton(b_save);
  lv_obj_set_style_bg_color(b_save, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_set_style_bg_color(b_save, lv_color_hex(0x3B7039), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(b_save, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_add_event_cb(b_save, saveWifiCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lsave = lv_label_create(b_save);
  lv_label_set_text(lsave, "Save / turn on");
  lv_obj_center(lsave);
  y += 42;

  // ---- Saved profiles (3 slots) ----
  // Each row: "<label> (<ssid>)" load button on the left, "Save" capture
  // button on the right. Tapping load fills the SSID/PWD textareas with the
  // slot's stored creds; the user can then edit + press the main Save button
  // to actually switch over. Tapping Save copies the *current* textarea
  // contents (plus the slot's label or "Slot N" if blank) into the slot.
  lv_obj_t* sec_p = lv_label_create(body);
  lv_label_set_text(sec_p, "Saved profiles");
  lv_obj_set_style_text_color(sec_p, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(sec_p, &g_font_12, LV_PART_MAIN);
  lv_obj_set_pos(sec_p, 2, y);
  y += 16;
  for (int i = 0; i < TOUCH_WIFI_SLOT_COUNT; ++i) {
    char label[TOUCH_WIFI_LABEL_MAX];
    char ssid_b[WIFI_CONFIG_SSID_MAX];
    char pwd_b[WIFI_CONFIG_PWD_MAX];
    touchPrefsGetWifiSlot(i, label, sizeof(label),
                           ssid_b, sizeof(ssid_b),
                           pwd_b, sizeof(pwd_b));
    bool empty = (ssid_b[0] == '\0');
    char row_text[80];
    if (empty) {
      snprintf(row_text, sizeof(row_text), "%d: (empty)", i + 1);
    } else if (label[0]) {
      snprintf(row_text, sizeof(row_text), "%d: %s (%s)", i + 1, label, ssid_b);
    } else {
      snprintf(row_text, sizeof(row_text), "%d: %s", i + 1, ssid_b);
    }

    const int slot_save_w = 56, slot_gap = 6;
    const int slot_row_w  = cw - slot_save_w - slot_gap;   // load row fills, Save sits at the right
    lv_obj_t* row = lv_btn_create(body);
    lv_obj_set_size(row, slot_row_w, 30);
    lv_obj_set_pos(row, 0, y);
    styleButton(row);
    lv_obj_set_style_bg_color(row, lv_color_hex(empty ? 0x0C0D0E : 0x1A1B1C), LV_PART_MAIN);
    lv_obj_add_event_cb(row, wifiSlotLoadCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_t* lt = lv_label_create(row);
    lv_label_set_text(lt, row_text);
    lv_label_set_long_mode(lt, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lt, slot_row_w - 16);
    lv_obj_set_style_text_color(lt, lv_color_hex(empty ? COLOR_SUB : COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(lt, &g_font_12, LV_PART_MAIN);
    lv_obj_align(lt, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t* sbtn = lv_btn_create(body);
    lv_obj_set_size(sbtn, slot_save_w, 30);
    lv_obj_set_pos(sbtn, cw - slot_save_w, y);
    styleButton(sbtn);
    lv_obj_set_style_bg_color(sbtn, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
    lv_obj_add_event_cb(sbtn, wifiSlotSaveCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_t* sl = lv_label_create(sbtn);
    lv_label_set_text(sl, "Save");
    lv_obj_set_style_text_font(sl, &g_font_12, LV_PART_MAIN);
    lv_obj_center(sl);

    y += 34;
  }
#else
  (void)body;
  (void)y;
  lv_obj_t* unsup = lv_label_create(body);
  lv_label_set_text(unsup, "Wi-Fi is not enabled in this build.");
  lv_obj_set_style_text_color(unsup, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_pos(unsup, 2, 8);
#endif
  refreshStatusLabels();
}

static void openLogModalCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t* body = createSettingsModal("Logs", SettingsModalKind::Log);
  int y = 0;

  g_set_modal.log_rx_btn = lv_btn_create(body);
  lv_obj_set_size(g_set_modal.log_rx_btn, 108, 32);
  lv_obj_set_pos(g_set_modal.log_rx_btn, 2, y);
  styleButton(g_set_modal.log_rx_btn);
  lv_obj_add_event_cb(g_set_modal.log_rx_btn, logModeRxCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* rx_lbl = lv_label_create(g_set_modal.log_rx_btn);
  lv_label_set_text(rx_lbl, "RX log");
  lv_obj_center(rx_lbl);

  g_set_modal.log_raw_btn = lv_btn_create(body);
  lv_obj_set_size(g_set_modal.log_raw_btn, 108, 32);
  lv_obj_set_pos(g_set_modal.log_raw_btn, 114, y);
  styleButton(g_set_modal.log_raw_btn);
  lv_obj_add_event_cb(g_set_modal.log_raw_btn, logModeRawCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* raw_lbl = lv_label_create(g_set_modal.log_raw_btn);
  lv_label_set_text(raw_lbl, "Raw log");
  lv_obj_center(raw_lbl);
  y += 38;

  g_set_modal.log_view_ta = lv_textarea_create(body);
  lv_obj_set_size(g_set_modal.log_view_ta, lv_pct(100),220);
  lv_obj_set_pos(g_set_modal.log_view_ta, 2, y);
  lv_textarea_set_one_line(g_set_modal.log_view_ta, false);
  lv_textarea_set_cursor_click_pos(g_set_modal.log_view_ta, false);
  lv_obj_clear_flag(g_set_modal.log_view_ta, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_text_font(g_set_modal.log_view_ta, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_bg_color(g_set_modal.log_view_ta, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_text_color(g_set_modal.log_view_ta, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);

  g_set_modal.log_mode = 0;
  refreshLogModalView();
}

// goToTab defined after all callbacks that need it
static void goToTab(int idx) {
  if (!g_lv.tabview) return;
  lv_tabview_set_act(g_lv.tabview, static_cast<uint32_t>(idx), LV_ANIM_ON);
  scheduleHeavyRefresh(170);
  // `onLvTabChanged` is invoked from `tabChangedCb` when the tabview value changes.
}


// ---- Contacts search overlay (magnifier icon) -------------------------------
static lv_obj_t* s_contacts_search_sheet = nullptr;
static lv_obj_t* s_contacts_search_ta    = nullptr;

static void closeContactsSearchSheet() {
  if (s_contacts_search_sheet) {
    // hideKb() *first* so the keyboard mirror's bind pointer (s_kb_bind_ta)
    // is cleared before we destroy the textarea it points at. Skipping this
    // crashed the device when the keyboard fired another sync into the
    // freed textarea after the sheet had been deleted.
    hideKb();
    // Async delete so we don't free the sheet while LVGL is still walking
    // the event chain that called us (same pattern as the action sheet).
    lv_obj_del_async(s_contacts_search_sheet);
    s_contacts_search_sheet = nullptr;
    s_contacts_search_ta    = nullptr;
  }
}

static void contactsSearchApplyCb(lv_event_t* e) {
  // Fire on both CLICKED (the Apply button) and READY (keyboard Enter), so
  // pressing Enter on the on-screen keyboard does the obvious thing.
  const lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_CLICKED && code != LV_EVENT_READY) return;
  // Pull the latest text out of the keyboard mirror *before* anything
  // tears the sheet down — otherwise hideKb() inside closeContactsSearchSheet
  // races us to read s_kb_mirror_ta's text.
  kbMirrorSyncToReal();
  if (s_contacts_search_ta) {
    const char* text = lv_textarea_get_text(s_contacts_search_ta);
    strncpy(g_lv.contacts_search, text ? text : "",
            sizeof(g_lv.contacts_search) - 1);
    g_lv.contacts_search[sizeof(g_lv.contacts_search) - 1] = '\0';
  }
  closeContactsSearchSheet();
  refreshContactsList();
  if (g_lv.contacts_list) lv_obj_scroll_to(g_lv.contacts_list, 0, 0, LV_ANIM_OFF);
}

static void contactsSearchClearCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  g_lv.contacts_search[0] = '\0';
  closeContactsSearchSheet();
  refreshContactsList();
  if (g_lv.contacts_list) lv_obj_scroll_to(g_lv.contacts_list, 0, 0, LV_ANIM_OFF);
}

static void contactsSearchSheetCloseCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  // Swallow the trailing touch and let async delete clean up next tick.
  lv_indev_t* act = lv_indev_get_act();
  if (act) lv_indev_wait_release(act);
  closeContactsSearchSheet();
}

static void openContactsSearchSheetCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeContactsSearchSheet();

  lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_contacts_search_sheet = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_contacts_search_sheet);
  // Backdrop starts below the global status bar.
  lv_obj_set_size(s_contacts_search_sheet, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_contacts_search_sheet, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_contacts_search_sheet, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_contacts_search_sheet, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_contacts_search_sheet, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_contacts_search_sheet, contactsSearchSheetCloseCb, LV_EVENT_CLICKED, nullptr);

  const int card_w = 220;
  const int card_h = 130;
  lv_obj_t* card = lv_obj_create(s_contacts_search_sheet);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, -40);
  lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  addCloseXBadge(card, contactsSearchSheetCloseCb);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, LV_SYMBOL_EYE_OPEN "  Search contacts");
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &g_font_14, LV_PART_MAIN);
  lv_obj_set_pos(title, 0, 0);

  s_contacts_search_ta = lv_textarea_create(card);
  lv_obj_set_size(s_contacts_search_ta, card_w - 20, 34);
  lv_obj_set_pos(s_contacts_search_ta, 0, 28);
  styleCard(s_contacts_search_ta);
  lv_textarea_set_one_line(s_contacts_search_ta, true);
  lv_textarea_set_max_length(s_contacts_search_ta, (uint32_t)(sizeof(g_lv.contacts_search) - 1));
  lv_textarea_set_placeholder_text(s_contacts_search_ta, "Name fragment");
  lv_textarea_set_text(s_contacts_search_ta, g_lv.contacts_search);
  lv_obj_set_style_text_color(s_contacts_search_ta, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_contacts_search_ta, &g_font_14, LV_PART_MAIN);
  // attachSettingsTaEvents binds the global keyboard mirror (via
  // kbMirrorBind) on FOCUSED / CLICKED / PRESSED. composerFocusCb (used by
  // the chat textarea) only shows the keyboard while a chat panel detail
  // is open, so it silently no-op'd here.
  attachSettingsTaEvents(s_contacts_search_ta);
  // Eager bind so the keyboard appears the instant the overlay opens —
  // matches the behaviour of the other modals that pop up textareas.
  if (g_lv.keyboard) kbMirrorBind(s_contacts_search_ta);

  lv_obj_t* clear_btn = lv_btn_create(card);
  lv_obj_set_size(clear_btn, 88, 34);
  lv_obj_set_pos(clear_btn, 0, 70);
  styleButton(clear_btn);
  lv_obj_add_event_cb(clear_btn, contactsSearchClearCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* cl = lv_label_create(clear_btn);
  lv_label_set_text(cl, "Clear");
  lv_obj_center(cl);

  lv_obj_t* apply_btn = lv_btn_create(card);
  lv_obj_set_size(apply_btn, 110, 34);
  lv_obj_set_pos(apply_btn, card_w - 20 - 110, 70);
  styleButton(apply_btn);
  lv_obj_set_style_bg_color(apply_btn, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_add_event_cb(apply_btn, contactsSearchApplyCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* al = lv_label_create(apply_btn);
  lv_label_set_text(al, "Apply");
  lv_obj_center(al);
}


// Action-sheet shown when tapping a contact in the Contacts list. Lets the
// user pick: send message / ping (repeaters only) / delete. State lives in
// statics so the lv event handlers can look the contact up by mesh index.
static uint32_t s_action_sheet_mesh_idx = 0;
static bool     s_action_sheet_is_repeater = false;
static lv_obj_t* s_action_sheet_root = nullptr;

static void closeActionSheet() {
  if (s_action_sheet_root) {
    // lv_obj_del_async — safe from inside an event callback. Synchronous
    // lv_obj_del while we're still walking the event chain can leave LVGL
    // input state pointing at a freed object, which manifested as "next
    // contact tap does nothing" after closing the sheet by tapping
    // outside.
    lv_obj_del_async(s_action_sheet_root);
    s_action_sheet_root = nullptr;
  }
}

static void actionSheetSendMsgCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  uint32_t idx = s_action_sheet_mesh_idx;
  // Swallow the trailing touch so the upcoming tab switch + overlay show
  // doesn't have the cap-touch driver's stale "swipe in progress" state
  // bleeding into the freshly-opened chat detail (otherwise the detail
  // looked frozen until the next manual tap).
  lv_indev_t* act = lv_indev_get_act();
  if (act) lv_indev_wait_release(act);
  closeActionSheet();
  g_lv.task->openMeshContactDm(idx);
}

// UI-side ping timeout state. If the reply doesn't arrive in N ms we show a
// "No reply from <name>" toast and cancel the_mesh's pending slot.
static unsigned long s_ui_ping_deadline_ms = 0;
static char s_ui_ping_target_name[24] = {0};
static constexpr unsigned long UI_PING_TIMEOUT_MS = 30000;  // 30 s for flood paths

static void actionSheetPingCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  ContactInfo c;
  bool ok = the_mesh.getContactByIdx(s_action_sheet_mesh_idx, c);
  closeActionSheet();
  if (!ok) { g_lv.task->showAlert("Contact gone", 1200); return; }
  /* sendStatusPingWithGuestLoginForUI() pipelines a blank-password LOGIN
   * before the STATUS REQ. Repeaters refuse to decrypt a PAYLOAD_TYPE_REQ
   * from a sender that isn't already in their ACL, and the ACL is only
   * populated by a successful sendLogin. A blank-password login matches
   * repeaters with guest_password = "" (typical default) and adds us as a
   * guest; subsequent REQs from this device then decrypt cleanly. The
   * follow-up STATUS REQ also registers _ui_pending_status so the reply
   * routes back via UITask::onPingReply (not eaten by the companion-serial
   * pending_status branch). */
  int r = the_mesh.sendStatusPingWithGuestLoginForUI(c);
  if (r == MSG_SEND_SENT_FLOOD || r == MSG_SEND_SENT_DIRECT) {
    copyUtf8ReplacingMissingGlyphs(&g_font_14, s_ui_ping_target_name,
                                    sizeof(s_ui_ping_target_name),
                                    c.name[0] ? c.name : "repeater");
    s_ui_ping_deadline_ms = millis() + UI_PING_TIMEOUT_MS;
    g_lv.task->showAlert(r == MSG_SEND_SENT_DIRECT
                         ? "Pinged (direct) — waiting…"
                         : "Pinged (flood) — waiting…", 1400);
  } else {
    g_lv.task->showAlert("Ping failed", 1200);
  }
}

static void actionSheetDeleteCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  ContactInfo c;
  bool ok = the_mesh.getContactByIdx(s_action_sheet_mesh_idx, c);
  closeActionSheet();
  if (!ok) { g_lv.task->showAlert("Contact gone", 1200); return; }
  if (the_mesh.removeContact(c)) {
    g_lv.task->showAlert("Contact deleted", 1000);
    refreshContactsList();
  } else {
    g_lv.task->showAlert("Delete failed", 1200);
  }
}

// "Reset path" — wipes the cached return path so the next send will re-flood
// instead of routing through a stale hop list. Mirrors MCterm's RSTPath.
static void actionSheetResetPathCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  ContactInfo c;
  bool ok = the_mesh.getContactByIdx(s_action_sheet_mesh_idx, c);
  closeActionSheet();
  if (!ok) { g_lv.task->showAlert("Contact gone", 1200); return; }
  if (the_mesh.uiResetContactPath(c.id.pub_key)) {
    g_lv.task->showAlert("Path reset", 1000);
    refreshContactsList();
  } else {
    g_lv.task->showAlert("Path reset failed", 1200);
  }
}

// "Telemetry" — fire REQ_TYPE_GET_TELEMETRY_DATA at the selected contact.
// The reply lands on UITask::onPingReply (same as STATUS) with the raw LPP
// payload; the user sees "<name>: reply (N bytes)" until proper LPP
// decoding lands. State-pings + telemetry-requests share _ui_pending_status,
// so the timeout / single-flight handling already in place applies.
static void actionSheetTelemetryCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  ContactInfo c;
  bool ok = the_mesh.getContactByIdx(s_action_sheet_mesh_idx, c);
  closeActionSheet();
  if (!ok) { g_lv.task->showAlert("Contact gone", 1200); return; }
  // Chain a guest LOGIN ahead of the telemetry REQ — same reason as ping:
  // repeaters & sensors require us in their ACL before they will decrypt a
  // PAYLOAD_TYPE_REQ. See sendStatusPingWithGuestLoginForUI for the full
  // explanation.
  int r = the_mesh.sendTelemetryRequestWithGuestLoginForUI(c);
  if (r == MSG_SEND_SENT_FLOOD || r == MSG_SEND_SENT_DIRECT) {
    copyUtf8ReplacingMissingGlyphs(&g_font_14, s_ui_ping_target_name,
                                    sizeof(s_ui_ping_target_name),
                                    c.name[0] ? c.name : "node");
    s_ui_ping_deadline_ms = millis() + UI_PING_TIMEOUT_MS;
    g_lv.task->showAlert(r == MSG_SEND_SENT_DIRECT
                         ? "Telemetry req (direct)…"
                         : "Telemetry req (flood)…", 1400);
  } else {
    g_lv.task->showAlert("Telemetry req failed", 1200);
  }
}

// ---- Repeater admin console (login + CLI passthrough) ----------------------
//
// State for the admin login + console modals. We only support one open at a
// time. After a successful sendLogin, the console modal opens with a
// scrolling log + a textarea so the operator can type CLI commands ("ver",
// "neighbours", "log", etc.). Replies arrive asynchronously via
// onAdminCommandReply and append to the log.
static lv_obj_t* s_admin_pw_root   = nullptr;
static lv_obj_t* s_admin_pw_ta     = nullptr;
static lv_obj_t* s_admin_pw_remember = nullptr;  // checkbox
static lv_obj_t* s_admin_root      = nullptr;
static lv_obj_t* s_admin_log_label = nullptr;
static lv_obj_t* s_admin_log_box   = nullptr;
static lv_obj_t* s_admin_cmd_ta    = nullptr;
static uint8_t   s_admin_pub32[32] = {0};
static char      s_admin_name[24]  = {0};
static char      s_admin_log[1024] = {0};  // ring-ish log buffer
// When the login prompt is opened to JOIN a room server (not admin a repeater),
// this holds that room's contact index so onAdminLoginResult can open the room
// chat on success. -1 = the current login is a normal repeater admin login.
static int       s_room_join_idx   = -1;
static int       s_admin_log_len   = 0;

// Static help banner shown when the console opens. Lists the common
// CommonCLI commands plus the simple_repeater-specific extras. The repeater
// fork the user is talking to might not implement all of these — unknown
// commands just produce a brief error reply.
static const char* k_admin_help_banner =
  "[logged in]\n"
  "Info:\n"
  "  ver       firmware version\n"
  "  board     hardware id\n"
  "  clock     show clock\n"
  "Radio / mesh:\n"
  "  advert            flood advert\n"
  "  advert.zerohop    0-hop advert\n"
  "  neighbors         list neighbours\n"
  "  discover.neighbors  active scan\n"
  "  neighbor.remove <hex>\n"
  "  tempradio f bw sf cr mins\n"
  "  clear stats\n"
  "Settings:\n"
  "  get <key> / set <key> <val>\n"
  "  sensor list / get / set\n"
  "Admin:\n"
  "  get acl       list clients\n"
  "  setperm <pubkey> <perms>\n"
  "  password <new>\n"
  "  reboot / poweroff\n"
  "  clock sync / time <epoch>\n"
  "  start ota";

// Repeater CLI catalogue presented in the picker modal. Each entry is the
// template text to stuff into the textarea on tap — including placeholder
// tokens like `<key>` and `<value>` so the operator knows what to fill in.
// Sorted roughly by usefulness within categories so the most common ones
// stay at the top of each section. Categories themselves separated by
// "─" headers so the picker doesn't read as one big wall of commands.
struct AdminCmdEntry {
  const char* label;       // shown on the picker row (may include hint text)
  const char* command;     // template stuffed into the textarea, or nullptr for a header
};
// Curated to commands that are universally available on a stock
// simple_repeater build. Anything hardware-conditional (gps*, powersaving*,
// sensor *) or build-flag-conditional (log start/stop/erase need
// LOG_STORE_ENABLED, set radio.rxgain is SX126x-only) is left out — listing
// them in the picker only to have the repeater reply "not found" is
// confusing. If you DO want to drive GPS / sensors / logs on a node that
// supports them, just type the command into the textarea directly.
static const AdminCmdEntry k_admin_cmds[] = {
  // Info
  { "[ INFO ]", nullptr },
  { "ver - firmware version",       "ver" },
  { "board - hardware id",          "board" },
  { "clock - show clock",           "clock" },
  // Radio / mesh
  { "[ RADIO / MESH ]", nullptr },
  { "advert - flood self advert",        "advert" },
  { "advert.zerohop - 0-hop advert",      "advert.zerohop" },
  { "neighbors - list neighbours",        "neighbors" },
  { "discover.neighbors - active scan",   "discover.neighbors" },
  { "neighbor.remove <hex>",              "neighbor.remove " },
  { "tempradio <freq> <bw> <sf> <cr> <m>", "tempradio " },
  { "clear stats",                        "clear stats" },
  // Settings / radio prefs (always available — these target NodePrefs,
  // not sensor hardware)
  { "[ RADIO PREFS ]", nullptr },
  { "set name <new>",                     "set name " },
  { "set radio <freq> <bw> <sf> <cr>",    "set radio " },
  { "set repeat on/off",                  "set repeat " },
  { "set advert.interval <minutes>",      "set advert.interval " },
  { "set flood.advert.interval <hours>",  "set flood.advert.interval " },
  { "set dutycycle <1-100>",              "set dutycycle " },
  { "set af <factor>",                    "set af " },
  // Admin
  { "[ ADMIN ]", nullptr },
  { "get acl - list clients",             "get acl" },
  { "setperm <pubkey> <perms>",           "setperm " },
  { "password <new>",                     "password " },
  { "set guest.password <new>",           "set guest.password " },
  { "clock sync",                         "clock sync" },
  { "time <epoch_secs>",                  "time " },
  { "start ota",                          "start ota" },
  { "reboot",                             "reboot" },
  { "poweroff",                           "poweroff" },
};
constexpr int k_admin_cmds_count = sizeof(k_admin_cmds) / sizeof(k_admin_cmds[0]);

static lv_obj_t* s_admin_picker_root = nullptr;

static void closeAdminCmdPicker() {
  if (s_admin_picker_root) {
    lv_obj_del_async(s_admin_picker_root);
    s_admin_picker_root = nullptr;
  }
}

static void closeAdminPwPrompt() {
  if (s_admin_pw_root) {
    hideKb();
    lv_obj_del_async(s_admin_pw_root);
    s_admin_pw_root      = nullptr;
    s_admin_pw_ta        = nullptr;
    s_admin_pw_remember  = nullptr;
  }
}

static void closeAdminConsole() {
  if (s_admin_root) {
    hideKb();
    closeAdminCmdPicker();
    lv_obj_del_async(s_admin_root);
    s_admin_root      = nullptr;
    s_admin_log_label = nullptr;
    s_admin_log_box   = nullptr;
    s_admin_cmd_ta    = nullptr;
  }
}

static void adminLogAppend(const char* prefix, const char* text) {
  if (!text) return;
  size_t cap = sizeof(s_admin_log);
  // Ring behaviour: if appending would overflow, drop the first half so we
  // keep recent output (the operator cares about the latest reply, not the
  // first one). Cheap memmove rather than a proper ring.
  size_t add = (prefix ? strlen(prefix) : 0) + strlen(text) + 2;  // + "\n\0"
  if ((size_t)s_admin_log_len + add >= cap) {
    size_t keep_from = cap / 2;
    if ((size_t)s_admin_log_len > keep_from) {
      memmove(s_admin_log, s_admin_log + keep_from,
              (size_t)s_admin_log_len - keep_from);
      s_admin_log_len -= (int)keep_from;
      s_admin_log[s_admin_log_len] = '\0';
    } else {
      s_admin_log_len = 0;
      s_admin_log[0] = '\0';
    }
  }
  int n = snprintf(s_admin_log + s_admin_log_len, cap - (size_t)s_admin_log_len,
                   "%s%s\n", prefix ? prefix : "", text);
  if (n > 0) s_admin_log_len += n;
  if (s_admin_log_label) {
    lv_label_set_text(s_admin_log_label, s_admin_log);
  }
  if (s_admin_log_box) {
    lv_obj_scroll_to_y(s_admin_log_box, LV_COORD_MAX, LV_ANIM_OFF);
  }
}

// Open a scrollable list of CLI commands. Tapping an entry stuffs the
// template into the admin textarea and dismisses the picker; the operator
// can then edit the placeholder tokens before sending. Headers (entries
// with command==nullptr) are non-interactive separators.
static void openAdminCmdPicker() {
  closeAdminCmdPicker();
  lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_admin_picker_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_admin_picker_root);
  // Backdrop starts below the global status bar so the near-full-height
  // command-picker card fits without its top row clipping behind the bar.
  lv_obj_set_size(s_admin_picker_root, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_admin_picker_root, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_admin_picker_root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_admin_picker_root, LV_OPA_70, LV_PART_MAIN);
  lv_obj_clear_flag(s_admin_picker_root, LV_OBJ_FLAG_SCROLLABLE);
  // Tap on the dim backdrop dismisses the picker (matches the action
  // sheet / search overlay convention).
  lv_obj_add_event_cb(s_admin_picker_root, [](lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_indev_t* a = lv_indev_get_act();
    if (a) lv_indev_wait_release(a);
    closeAdminCmdPicker();
  }, LV_EVENT_CLICKED, nullptr);

  const int card_w = sw - 20;
  // Use the *visible* area height (sh - STATUSBAR_H), not raw sh — the
  // root now starts below the status bar so a card sized to full sh would
  // overflow the bottom of the root.
  const int card_h = (sh - STATUSBAR_H) - 40;
  lv_obj_t* card = lv_obj_create(s_admin_picker_root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 6, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "Commands");
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &g_font_14, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 4);

  // Replaced the old 60x24 "Close" button with the standard X badge so
  // every popup dismisses the same way.
  addCloseXBadge(card, +[](lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    closeAdminCmdPicker();
  });

  lv_obj_t* list = lv_list_create(card);
  lv_obj_set_size(list, card_w - 12, card_h - 12 - 28);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 28);
  lv_obj_set_style_bg_color(list, lv_color_hex(COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);

  for (int i = 0; i < k_admin_cmds_count; ++i) {
    const AdminCmdEntry& e = k_admin_cmds[i];
    if (!e.command) {
      // Header / section break — non-interactive label. lv_list_add_text
      // returns a label with the LVGL-default light theme background;
      // re-skin so the section header reads as a dim divider on the dark
      // panel instead of a white bar.
      lv_obj_t* h = lv_list_add_text(list, e.label);
      lv_obj_set_style_text_color(h, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
      lv_obj_set_style_text_font(h, &g_font_12, LV_PART_MAIN);
      lv_obj_set_style_bg_color(h, lv_color_hex(0x0F1722), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(h, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_width(h, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_ver(h, 6, LV_PART_MAIN);
      lv_obj_set_style_pad_left(h, 10, LV_PART_MAIN);
      continue;
    }
    lv_obj_t* btn = lv_list_add_btn(list, nullptr, e.label);
    lv_obj_set_style_text_font(btn, &g_font_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(btn, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    // lv_list_add_btn uses a light theme bg by default — flip to the
    // panel-dark palette so the text is actually legible.
    lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x141516), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x141516), LV_PART_MAIN);
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_min_height(btn, 30, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_left(btn, 10, LV_PART_MAIN);
    // Stash the index in user_data; the click handler reads the template.
    lv_obj_add_event_cb(btn, [](lv_event_t* ev) {
      if (lv_event_get_code(ev) != LV_EVENT_CLICKED) return;
      intptr_t idx = (intptr_t)lv_event_get_user_data(ev);
      if (idx < 0 || idx >= k_admin_cmds_count) return;
      const char* tpl = k_admin_cmds[idx].command;
      if (!tpl || !s_admin_cmd_ta) {
        closeAdminCmdPicker();
        return;
      }
      lv_textarea_set_text(s_admin_cmd_ta, tpl);
      lv_textarea_set_cursor_pos(s_admin_cmd_ta, LV_TEXTAREA_CURSOR_LAST);
      closeAdminCmdPicker();
      // Don't pop the keyboard automatically — most picked commands are
      // run as-is (no placeholders to fill), and an unwanted keyboard
      // covers the log. The operator can tap the textarea if they need
      // to edit the template before sending.
      hideKb();
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
}

static void openAdminConsole(const ContactInfo& c) {
  closeAdminPwPrompt();
  closeAdminConsole();
  memcpy(s_admin_pub32, c.id.pub_key, 32);
  copyUtf8ReplacingMissingGlyphs(&g_font_14, s_admin_name, sizeof(s_admin_name),
                                  c.name[0] ? c.name : "repeater");

  lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_admin_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_admin_root);
  // Start below the global status bar so the "Admin: <name>" header + Close
  // button at y=0 aren't clipped by the time/battery row.
  lv_obj_set_size(s_admin_root, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_admin_root, 0, STATUSBAR_H);
  styleSurface(s_admin_root, COLOR_BG, 0);
  lv_obj_clear_flag(s_admin_root, LV_OBJ_FLAG_SCROLLABLE);

  // Header
  lv_obj_t* hdr = lv_obj_create(s_admin_root);
  lv_obj_remove_style_all(hdr);
  lv_obj_set_size(hdr, sw, 36);
  lv_obj_set_pos(hdr, 0, 0);
  styleSurface(hdr, COLOR_PANEL, 0);
  lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_style_border_width(hdr, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(hdr, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(hdr);
  char title_buf[40];
  snprintf(title_buf, sizeof(title_buf), "Admin: %s", s_admin_name);
  lv_label_set_text(title, title_buf);
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &g_font_14, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

  // Standard close-X badge on the admin console header — same affordance
  // as every other popup, no more bespoke "Close" button.
  addCloseXBadge(hdr, +[](lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    closeAdminConsole();
  });
  // Trim the title's right side so a long repeater name can't slide under
  // the X. Header is sw=240 wide; reserve 36 px for the X badge.
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, sw - 8 - 36);

  // Scrolling log area. Heights are relative to the SHIFTED root (which
  // starts at y=STATUSBAR_H and is sh-STATUSBAR_H tall) — using bare `sh`
  // here would push the bottom row off the root and re-clip behind the
  // status bar in the new geometry.
  const lv_coord_t admin_h = sh - STATUSBAR_H;
  s_admin_log_box = lv_obj_create(s_admin_root);
  lv_obj_remove_style_all(s_admin_log_box);
  lv_obj_set_size(s_admin_log_box, sw - 8, admin_h - 36 - 8 - 44);
  lv_obj_set_pos(s_admin_log_box, 4, 40);
  styleSurface(s_admin_log_box, 0x0A0B0C, 6);
  lv_obj_set_style_border_color(s_admin_log_box, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_admin_log_box, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_admin_log_box, 6, LV_PART_MAIN);
  lv_obj_set_scroll_dir(s_admin_log_box, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_admin_log_box, LV_SCROLLBAR_MODE_AUTO);

  s_admin_log_label = lv_label_create(s_admin_log_box);
  lv_label_set_long_mode(s_admin_log_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_admin_log_label, sw - 8 - 12);
  lv_obj_set_style_text_color(s_admin_log_label, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_admin_log_label, &g_font_12, LV_PART_MAIN);
  // Reset the log buffer to the help banner so the operator sees the
  // full command vocabulary up front instead of staring at an empty box.
  s_admin_log_len = 0;
  s_admin_log[0] = '\0';
  adminLogAppend("", k_admin_help_banner);

  // Command row at the bottom
  lv_obj_t* row = lv_obj_create(s_admin_root);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, sw, 44);
  lv_obj_set_pos(row, 0, admin_h - 44);
  styleSurface(row, COLOR_PANEL, 0);
  lv_obj_set_style_border_side(row, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(row, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  // "?" button on the left opens the command picker. The textarea
  // shrinks to make room.
  lv_obj_t* picker_btn = lv_btn_create(row);
  lv_obj_set_size(picker_btn, 32, 32);
  lv_obj_align(picker_btn, LV_ALIGN_LEFT_MID, 4, 0);
  styleButton(picker_btn);
  lv_obj_set_style_bg_color(picker_btn, lv_color_hex(0x1A1B1C), LV_PART_MAIN);
  lv_obj_set_style_pad_all(picker_btn, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(picker_btn, [](lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    openAdminCmdPicker();
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* picker_lbl = lv_label_create(picker_btn);
  lv_label_set_text(picker_lbl, LV_SYMBOL_LIST);
  lv_obj_set_style_text_font(picker_lbl, &g_font_14, LV_PART_MAIN);
  lv_obj_center(picker_lbl);

  s_admin_cmd_ta = lv_textarea_create(row);
  // Layout: 4 px pad + 32 picker + 4 gap + ta + 4 gap + 56 send + 4 pad.
  // sw = 240 → ta width = 240 - (4+32+4+56+4+4) = 136.
  lv_obj_set_size(s_admin_cmd_ta, sw - 104, 32);
  lv_obj_align(s_admin_cmd_ta, LV_ALIGN_LEFT_MID, 40, 0);
  styleCard(s_admin_cmd_ta);
  lv_textarea_set_one_line(s_admin_cmd_ta, true);
  lv_textarea_set_max_length(s_admin_cmd_ta, 64);
  lv_textarea_set_placeholder_text(s_admin_cmd_ta, "command");
  lv_obj_set_style_text_color(s_admin_cmd_ta, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_admin_cmd_ta, &g_font_14, LV_PART_MAIN);
  attachSettingsTaEvents(s_admin_cmd_ta);

  lv_obj_t* send_btn = lv_btn_create(row);
  lv_obj_set_size(send_btn, 56, 32);
  lv_obj_align(send_btn, LV_ALIGN_RIGHT_MID, -4, 0);
  styleButton(send_btn);
  lv_obj_set_style_bg_color(send_btn, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_add_event_cb(send_btn, [](lv_event_t* e) {
    const lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_READY) return;
    kbMirrorSyncToReal();
    if (!s_admin_cmd_ta) return;
    const char* text = lv_textarea_get_text(s_admin_cmd_ta);
    if (!text || !text[0]) return;
    // Resolve the contact each time — pointer-stale-after-rebuild is the
    // standard gotcha (saved contacts can move when a refresh runs).
    ContactInfo* c = the_mesh.lookupContactByPubKey(s_admin_pub32, PUB_KEY_SIZE);
    if (!c) {
      adminLogAppend("[err] ", "contact missing");
      return;
    }
    int r = the_mesh.uiSendAdminCommand(*c, text);
    char prompt_line[80];
    snprintf(prompt_line, sizeof(prompt_line), "> %s", text);
    adminLogAppend("", prompt_line);
    if (r != MSG_SEND_SENT_FLOOD && r != MSG_SEND_SENT_DIRECT) {
      adminLogAppend("[err] ", "send failed");
    }
    lv_textarea_set_text(s_admin_cmd_ta, "");
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(send_btn, [](lv_event_t* e) {
    lv_event_send(static_cast<lv_obj_t*>(lv_event_get_user_data(e)),
                  LV_EVENT_CLICKED, nullptr);
  }, LV_EVENT_READY, send_btn);  // Enter key on textarea sends
  lv_obj_t* send_lbl = lv_label_create(send_btn);
  lv_label_set_text(send_lbl, LV_SYMBOL_RIGHT);
  lv_obj_center(send_lbl);
}

// Captured password + remember-flag at the moment Login was tapped, so we
// can persist (or clear) the entry once the async login result lands. The
// password prompt may already be torn down by then.
static char  s_admin_pw_attempt[TOUCH_REPEATER_PW_LEN] = {0};
static bool  s_admin_pw_remember_flag = false;

static void adminPwSubmitCb(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_CLICKED && code != LV_EVENT_READY) return;
  kbMirrorSyncToReal();
  if (!s_admin_pw_ta) return;
  const char* pw = lv_textarea_get_text(s_admin_pw_ta);
  ContactInfo* c = the_mesh.lookupContactByPubKey(s_admin_pub32, PUB_KEY_SIZE);
  if (!c) {
    if (g_lv.task) g_lv.task->showAlert("Contact missing", 1200);
    return;
  }
  // Stash the attempted password + remember preference for the async
  // success/fail handler. Snapshot the checkbox state now because the
  // prompt closes as soon as the result lands.
  strncpy(s_admin_pw_attempt, pw ? pw : "", sizeof(s_admin_pw_attempt) - 1);
  s_admin_pw_attempt[sizeof(s_admin_pw_attempt) - 1] = '\0';
  s_admin_pw_remember_flag = (s_admin_pw_remember &&
                              lv_obj_has_state(s_admin_pw_remember, LV_STATE_CHECKED));
  int r = the_mesh.uiSendAdminLogin(*c, s_admin_pw_attempt);
  if (r == MSG_SEND_SENT_FLOOD || r == MSG_SEND_SENT_DIRECT) {
    if (g_lv.task) g_lv.task->showAlert("Logging in\xe2\x80\xa6", 1500);
  } else {
    if (g_lv.task) g_lv.task->showAlert("Send failed", 1200);
  }
}

static void adminPwCancelCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeAdminPwPrompt();
}

static void openAdminLoginPrompt(const ContactInfo& c) {
  closeAdminPwPrompt();
  closeAdminConsole();
  memcpy(s_admin_pub32, c.id.pub_key, 32);

  lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_admin_pw_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_admin_pw_root);
  // Backdrop starts below the global status bar.
  lv_obj_set_size(s_admin_pw_root, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_admin_pw_root, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_admin_pw_root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_admin_pw_root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_admin_pw_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_admin_pw_root, [](lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_indev_t* a = lv_indev_get_act();
    if (a) lv_indev_wait_release(a);
    closeAdminPwPrompt();
  }, LV_EVENT_CLICKED, nullptr);

  const int card_w = 220;
  const int card_h = 180;   // taller now: room for the Remember checkbox
  lv_obj_t* card = lv_obj_create(s_admin_pw_root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  // Pin near the top of the area below the status bar. This 180px card is too
  // tall to center with a -40 lift (its title/X clipped off the top). The
  // on-screen keyboard always rises from the bottom, so a top-anchored card
  // never clips and keeps the title + password field visible above it.
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  addCloseXBadge(card, +[](lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_indev_t* a = lv_indev_get_act();
    if (a) lv_indev_wait_release(a);
    closeAdminPwPrompt();
  });

  const bool is_room = (c.type == ADV_TYPE_ROOM);
  char hdr_buf[40];
  snprintf(hdr_buf, sizeof(hdr_buf), "%s %.20s", is_room ? "Join:" : "Login:", c.name);
  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, hdr_buf);
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &g_font_14, LV_PART_MAIN);
  // Trim 32 px on the right so long repeater names don't slide under the X.
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, card_w - 20 - 32);
  lv_obj_set_pos(title, 0, 0);

  lv_obj_t* sub = lv_label_create(card);
  lv_label_set_text(sub, is_room ? "Room password (blank = guest)"
                                 : "Password (blank = guest)");
  lv_obj_set_style_text_color(sub, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(sub, &g_font_12, LV_PART_MAIN);
  lv_obj_set_pos(sub, 0, 22);

  s_admin_pw_ta = lv_textarea_create(card);
  lv_obj_set_size(s_admin_pw_ta, card_w - 20, 32);
  lv_obj_set_pos(s_admin_pw_ta, 0, 42);
  styleCard(s_admin_pw_ta);
  lv_textarea_set_one_line(s_admin_pw_ta, true);
  lv_textarea_set_password_mode(s_admin_pw_ta, true);
  lv_textarea_set_max_length(s_admin_pw_ta, 15);
  lv_textarea_set_placeholder_text(s_admin_pw_ta, "");
  lv_obj_set_style_text_color(s_admin_pw_ta, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_admin_pw_ta, &g_font_14, LV_PART_MAIN);
  attachSettingsTaEvents(s_admin_pw_ta);

  // Prefill from NVS if we've remembered a password for this repeater.
  // The state of the Remember checkbox follows: if a saved password
  // exists, Remember stays on (re-saving on next login is a no-op);
  // otherwise default to on so the typical case (type once) just works.
  bool has_saved_pw = false;
#if defined(ESP32)
  {
    char saved[TOUCH_REPEATER_PW_LEN];
    int n = touchPrefsGetRepeaterPassword(c.id.pub_key, saved, sizeof(saved));
    if (n > 0) {
      lv_textarea_set_text(s_admin_pw_ta, saved);
      has_saved_pw = true;
    }
  }
#endif

  // Eager-bind the keyboard ONLY when there's no saved password — the
  // operator clearly needs to type. If we already have the password from
  // NVS the typical action is "tap Login", so keep the keyboard down and
  // leave space for the Remember checkbox + buttons. The textarea still
  // attaches the normal CLICKED handler (see attachSettingsTaEvents) so
  // tapping the field explicitly opens the keyboard on demand.
  if (g_lv.keyboard && !has_saved_pw) kbMirrorBind(s_admin_pw_ta);
  // Pressing Enter on the keyboard submits.
  lv_obj_add_event_cb(s_admin_pw_ta, adminPwSubmitCb, LV_EVENT_READY, nullptr);

  // Remember checkbox sits between the password field and the buttons. On
  // a successful login we read its state in onAdminLoginResult and save
  // (or clear) the NVS entry accordingly.
  s_admin_pw_remember = lv_checkbox_create(card);
  lv_checkbox_set_text(s_admin_pw_remember, "Remember password");
  lv_obj_set_style_text_color(s_admin_pw_remember, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_admin_pw_remember, &g_font_12, LV_PART_MAIN);
  lv_obj_set_pos(s_admin_pw_remember, 0, 82);
  lv_obj_add_state(s_admin_pw_remember, LV_STATE_CHECKED);

  lv_obj_t* cancel_btn = lv_btn_create(card);
  lv_obj_set_size(cancel_btn, 88, 32);
  lv_obj_set_pos(cancel_btn, 0, 116);
  styleButton(cancel_btn);
  lv_obj_add_event_cb(cancel_btn, adminPwCancelCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* cl = lv_label_create(cancel_btn);
  lv_label_set_text(cl, "Cancel");
  lv_obj_center(cl);

  lv_obj_t* login_btn = lv_btn_create(card);
  lv_obj_set_size(login_btn, 100, 32);
  lv_obj_set_pos(login_btn, card_w - 20 - 100, 116);
  styleButton(login_btn);
  lv_obj_set_style_bg_color(login_btn, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_add_event_cb(login_btn, adminPwSubmitCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* ll = lv_label_create(login_btn);
  lv_label_set_text(ll, is_room ? "Join" : "Login");
  lv_obj_center(ll);
}

static void actionSheetAdminCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  ContactInfo c;
  bool ok = the_mesh.getContactByIdx(s_action_sheet_mesh_idx, c);
  closeActionSheet();
  if (!ok) { g_lv.task->showAlert("Contact gone", 1200); return; }
  s_room_join_idx = -1;   // repeater admin login, not a room join
  openAdminLoginPrompt(c);
}

// "Join" a room server: same login mechanism as repeater admin (sendLogin is
// room-aware and sends our sync_since so the server replays room history), but
// on success we open the room's chat instead of the admin console. Stash the
// contact index for onAdminLoginResult to reopen.
static void actionSheetJoinRoomCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  ContactInfo c;
  bool ok = the_mesh.getContactByIdx(s_action_sheet_mesh_idx, c);
  s_room_join_idx = ok ? (int)s_action_sheet_mesh_idx : -1;
  closeActionSheet();
  if (!ok) { g_lv.task->showAlert("Contact gone", 1200); return; }
  openAdminLoginPrompt(c);
}

// Toggle a contact's favorite state. Persists in NVS via TouchPrefsStore so
// both the star marker in the contact row and the favorites filter pick it
// up. Pure UI metadata; the firmware contact table isn't touched.
static void actionSheetFavoriteCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  ContactInfo c;
  bool ok = the_mesh.getContactByIdx(s_action_sheet_mesh_idx, c);
  closeActionSheet();
  if (!ok) { g_lv.task->showAlert("Contact gone", 1200); return; }
#if defined(ESP32)
  bool was_fav = touchPrefsIsFavorite(c.id.pub_key);
  bool now_fav = touchPrefsSetFavorite(c.id.pub_key, !was_fav);
  g_lv.task->showAlert(now_fav ? "Added to favorites" : "Removed from favorites", 1100);
  // Force a list rebuild so the star (or its removal) shows immediately.
  refreshContactsList();
#else
  g_lv.task->showAlert("Favorites unsupported", 1100);
#endif
}

// "Trace ping" — send a PAYLOAD_TYPE_TRACE direct packet with a single-hop
// path (the contact's hash). When the contact is a repeater (or any node
// with allowPacketForward enabled) it appends its RX SNR and retransmits.
// We pick that retransmission off the air and the dispatcher fires
// onTraceRecv at us; MyMesh routes the SNRs to UITask::onTracePingResult,
// which raises a 5 s alert showing both directions. Useful for diagnosing
// link quality without exchanging a DM. Repeater-only (chat peers don't
// forward).
static void actionSheetTracePingCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  ContactInfo c;
  bool ok = the_mesh.getContactByIdx(s_action_sheet_mesh_idx, c);
  closeActionSheet();
  if (!ok) { g_lv.task->showAlert("Contact gone", 1200); return; }
  uint32_t tag = the_mesh.uiSendTracePing(c.id.pub_key);
  if (tag == 0) {
    g_lv.task->showAlert("Trace failed", 1200);
  } else {
    g_lv.task->showAlert("Trace sent\xe2\x80\xa6", 1200);
  }
}

// "Range test" — send a recognisable DM the recipient app can echo back so
// the user can read off RTT. Drops a normal text frame.
static void actionSheetRangeTestCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  ContactInfo c;
  bool ok = the_mesh.getContactByIdx(s_action_sheet_mesh_idx, c);
  closeActionSheet();
  if (!ok) { g_lv.task->showAlert("Contact gone", 1200); return; }
  const uint32_t ts = the_mesh.getRTCClock()->getCurrentTime();
  uint32_t est = 0, hash4 = 0;
  uint32_t ack_hash = 0;
  int r = the_mesh.sendMessage(c, ts, 0, "RangeTest \xe2\x80\x94 ACK?", ack_hash, est, &hash4);
  if (r == MSG_SEND_SENT_FLOOD || r == MSG_SEND_SENT_DIRECT) {
    g_lv.task->showAlert(r == MSG_SEND_SENT_DIRECT ? "RangeTest sent (direct)"
                                                   : "RangeTest sent (flood)", 1400);
  } else {
    g_lv.task->showAlert("RangeTest failed", 1200);
  }
}

static void actionSheetCloseCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  // Swallow the rest of this touch so the click event doesn't bubble down
  // to whatever widget is underneath after the sheet root is gone.
  // Without this, the next contact tap silently failed because LVGL still
  // had the just-deleted sheet root in its pressed-object slot.
  lv_indev_t* act = lv_indev_get_act();
  if (act) lv_indev_wait_release(act);
  closeActionSheet();
}

// ===== Line-of-sight analyzer ===========================================
//
// Terrain-aware LOS between our node and a contact. We sample the great
// circle between the two GPS points, fetch ground elevation for each
// sample from the proxy's /elev endpoint, add the earth-curvature "bulge"
// (4/3-earth-radius radio model) to the terrain, then compare against a
// straight antenna-to-antenna sight line. Fresnel-zone clearance (0.6·F1)
// separates "clear" from "marginal".
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
static constexpr int     k_los_samples  = 24;   // path sample points (was 40:
                                                 // shorter request = faster,
                                                 // cross-section still smooth)
static constexpr double  k_los_re_eff   = 8504000.0;  // 4/3 × 6371 km (m)
// Antenna heights above ground (m), operator-adjustable via the +/- buttons
// on the LOS graph. Persist for the session so "your" antenna height sticks
// across analyses; tweak the peer side per contact.
static float s_los_ant_self = 2.0f;
static float s_los_ant_peer = 2.0f;

static lv_obj_t* s_los_root = nullptr;
static lv_obj_t* s_los_card = nullptr;
static lv_obj_t* s_los_msg  = nullptr;
static lv_obj_t* s_los_plot = nullptr;        // graph box, recreated each draw
static lv_obj_t* s_los_verdict = nullptr;     // verdict label, text updated
static lv_obj_t* s_los_self_h_lbl = nullptr;  // "2m" value labels
static lv_obj_t* s_los_peer_h_lbl = nullptr;
// lv_line keeps the caller's point array by pointer, so these must persist.
static lv_point_t s_los_terrain_pts[k_los_samples];
static lv_point_t s_los_sight_pts[2];

// Async elevation fetch — runs on a core-0 worker so the HTTP round-trip
// (up to several seconds) never blocks the LVGL/UI thread. The modal shows
// "Analyzing…" while busy; UITask::loop calls losPoll() each tick and
// renders the result once it lands.
static double s_los_self_lat = 0, s_los_self_lon = 0;
static double s_los_peer_lat = 0, s_los_peer_lon = 0;
static double s_los_slat[k_los_samples], s_los_slon[k_los_samples];
static float  s_los_elev[k_los_samples];
static volatile int  s_los_got          = 0;
static volatile bool s_los_busy         = false;   // worker mid-fetch
static volatile bool s_los_result_ready = false;   // worker -> UI handoff
static volatile bool s_los_request      = false;   // UI -> worker trigger
static volatile uint32_t s_los_req_ms   = 0;       // when the fetch started
static volatile int  s_los_attempt      = 0;       // 1-based attempt # (UI progress)
static volatile int  s_los_dbg_parsed   = 0;       // diag: CSV fields returned
static volatile int  s_los_dbg_valid    = 0;       // diag: non-null among them
static void losRenderResult();              // fwd
// The elevation fetch is serviced by the SHARED tile-fetch worker (core 0)
// rather than a dedicated task — they never run concurrently (map tab vs
// contacts tab) and internal DRAM can't afford two ~8 KB task stacks.
static bool ensureTileFetchTaskRunning();   // fwd (defined with the map code)

static double losBearingDeg(double lat1, double lon1, double lat2, double lon2) {
  const double p1 = lat1 * M_PI / 180.0, p2 = lat2 * M_PI / 180.0;
  const double dl = (lon2 - lon1) * M_PI / 180.0;
  const double y = sin(dl) * cos(p2);
  const double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  double b = atan2(y, x) * 180.0 / M_PI;
  if (b < 0) b += 360.0;
  return b;
}
static const char* losCompass(double deg) {
  static const char* d[] = {"N","NE","E","SE","S","SW","W","NW"};
  return d[(int)((deg + 22.5) / 45.0) & 7];
}

// GET <server>/elev?locations=lat,lon|...  -> CSV of metres. Returns the
// number of samples parsed into out_m (0 on failure). Blocking call.
static volatile int s_los_dbg_code = 0;   // last HTTP code (-1 WiFi, -2 begin)
static int losFetchElevations(const double* lat, const double* lon, int n,
                              float* out_m) {
  if (WiFi.status() != WL_CONNECTED) { s_los_dbg_code = -1; return 0; }
  char base[TOUCH_TILE_SERVER_MAXLEN];
  touchPrefsGetTileServer(base, sizeof(base));
  size_t blen = strlen(base);
  while (blen > 0 && base[blen - 1] == '/') base[--blen] = '\0';
  static char url[1100];
  int o = snprintf(url, sizeof(url), "%s/elev?locations=", base);
  for (int i = 0; i < n && o < (int)sizeof(url) - 24; ++i) {
    o += snprintf(url + o, sizeof(url) - o, "%s%.5f,%.5f",
                  i ? "|" : "", lat[i], lon[i]);
  }
  WiFiClient client;
  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(5000);
  // Bounded so a slow/stuck fetch can't pin the shared worker (and block a
  // SECOND line-of-sight request) for too long. The worker now retries a few
  // times (see the LOS branch in tileFetchTaskFn): attempt 1 warms the
  // proxy's nginx cache (30-day /elev cache, keyed on the exact query), so
  // the retry hits the cache and returns fast. Three attempts at this read
  // timeout still fit under the UI busy-watchdog.
  http.setTimeout(9000);
  if (!http.begin(client, url)) { s_los_dbg_code = -2; return 0; }
  const int code = http.GET();
  s_los_dbg_code = code;
  if (code != HTTP_CODE_OK) { http.end(); client.stop(); return 0; }
  String body = http.getString();
  http.end();
  client.stop();
  int cnt = 0;
  const char* p = body.c_str();
  while (*p && cnt < n) {
    while (*p == ' ') ++p;
    if (!strncmp(p, "null", 4)) { out_m[cnt++] = NAN; p += 4; }
    else {
      char* end = nullptr;
      const double v = strtod(p, &end);
      if (end == p) break;
      out_m[cnt++] = (float)v;
      p = end;
    }
    while (*p && *p != ',') ++p;
    if (*p == ',') ++p;
  }
  return cnt;
}

// NaN-only check (we store NaN for the backend's "null" no-data points; real
// elevations are never NaN). Avoids <cmath> isfinite namespace ambiguity.
static inline bool losReal(float v) { return v == v; }

// Repair an elevation array in place so all n samples are finite, tolerating
// the backend returning "null" for no-data points or fewer points than asked.
// `parsed` = number of CSV fields actually returned. Leading/trailing gaps are
// clamped to the nearest real sample; interior gaps are linearly interpolated.
// Returns false only when too little real data came back to be meaningful — the
// caller then retries (or reports failure). This is what lets a path with the
// odd null sample still render instead of failing the whole analysis.
static bool losRepairElevations(float* m, int n, int parsed) {
  for (int i = parsed; i < n; ++i) m[i] = NAN;   // trailing missing points
  int valid = 0, first = -1, last = -1;
  for (int i = 0; i < n; ++i) {
    if (losReal(m[i])) { ++valid; if (first < 0) first = i; last = i; }
  }
  // Need at least ~1/3 real coverage; below that the profile would be mostly
  // fabricated, so treat it as a fetch failure worth retrying.
  if (valid < 2 || valid * 3 < n) return false;
  for (int i = 0; i < first; ++i) m[i] = m[first];   // clamp leading gap
  for (int i = last + 1; i < n; ++i) m[i] = m[last];  // clamp trailing gap
  int i = first;
  while (i <= last) {
    if (losReal(m[i])) { ++i; continue; }
    const int lo = i - 1;                 // last real sample before the gap
    int hi = i;
    while (hi <= last && !losReal(m[hi])) ++hi;  // first real sample after
    const float a = m[lo], b = m[hi];
    const int span = hi - lo;
    for (int k = lo + 1; k < hi; ++k)
      m[k] = a + (b - a) * (float)(k - lo) / (float)span;
    i = hi;
  }
  return true;
}

static void closeLosModal() {
  if (s_los_root) { lv_obj_del_async(s_los_root); s_los_root = nullptr; }
  s_los_card = nullptr;
  s_los_msg  = nullptr;
  s_los_plot = nullptr;
  s_los_verdict = nullptr;
  s_los_self_h_lbl = nullptr;
  s_los_peer_h_lbl = nullptr;
}
static void losModalCloseCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_indev_t* act = lv_indev_get_act();
  if (act) lv_indev_wait_release(act);
  closeLosModal();
}

// (Re)compute the analysis with the current antenna heights and (re)draw
// the cross-section + verdict. Cheap — uses the already-fetched elevation
// profile, so the +/- height buttons call it directly (no network).
static void losDrawPlot() {
  if (!s_los_card || s_los_got < k_los_samples) return;
  lv_obj_t* card = s_los_card;
  const int card_w = 230;

  const double self_lat = s_los_self_lat, self_lon = s_los_self_lon;
  const double peer_lat = s_los_peer_lat, peer_lon = s_los_peer_lon;
  const float* elev = s_los_elev;

  const double dist_km = contactDistanceKm(self_lat, self_lon, peer_lat, peer_lon);
  const double D = dist_km * 1000.0;
  const double brg = losBearingDeg(self_lat, self_lon, peer_lat, peer_lon);
  NodePrefs* prefs = the_mesh.getNodePrefs();
  const double freq_mhz = (prefs && prefs->freq > 1.0) ? prefs->freq : 868.0;
  const double lambda = 300.0 / freq_mhz;
  const double h0 = elev[0] + s_los_ant_self;
  const double hN = elev[k_los_samples - 1] + s_los_ant_peer;

  double min_clear = 1e9; int worst_i = -1; double worst_d1 = 0;
  double min_fres_margin = 1e9; double worst_f1 = 0;
  for (int i = 1; i < k_los_samples - 1; ++i) {
    const double f  = (double)i / (k_los_samples - 1);
    const double d1 = f * D, d2 = (1.0 - f) * D;
    const double bulge = d1 * d2 / (2.0 * k_los_re_eff);
    const double terr  = (double)elev[i] + bulge;
    const double sight = h0 + f * (hN - h0);
    const double clear = sight - terr;
    const double F1    = sqrt(lambda * d1 * d2 / D);
    if (clear < min_clear) { min_clear = clear; worst_i = i; worst_d1 = d1; }
    const double fm = clear - 0.6 * F1;
    if (fm < min_fres_margin) { min_fres_margin = fm; worst_f1 = F1; }
  }

  enum { V_CLEAR, V_MARGINAL, V_BLOCKED } verdict;
  if (min_clear < 0)            verdict = V_BLOCKED;
  else if (min_fres_margin < 0) verdict = V_MARGINAL;
  else                          verdict = V_CLEAR;

  // ---- Cross-section drawing area (recreated each draw) ----
  // Shorter graph in landscape so the antenna controls + verdict still fit the
  // capped card height. cy in losRenderResult must stay = gy + gh + 6.
  const int gx = 0, gy = 28, gw = card_w - 16, gh = chatLandscape() ? 58 : 80;
  if (s_los_plot) { lv_obj_del(s_los_plot); s_los_plot = nullptr; }
  lv_obj_t* graph = lv_obj_create(card);
  s_los_plot = graph;
  lv_obj_remove_style_all(graph);
  lv_obj_set_size(graph, gw, gh);
  lv_obj_set_pos(graph, gx, gy);
  lv_obj_set_style_bg_color(graph, lv_color_hex(0x0B0D0F), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(graph, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(graph, 4, LV_PART_MAIN);
  lv_obj_clear_flag(graph, LV_OBJ_FLAG_SCROLLABLE);

  double vmin = 1e9, vmax = -1e9;
  auto bump = [&](double v){ if (v < vmin) vmin = v; if (v > vmax) vmax = v; };
  for (int i = 0; i < k_los_samples; ++i) {
    const double f  = (double)i / (k_los_samples - 1);
    const double d1 = f * D, d2 = (1.0 - f) * D;
    bump((double)elev[i] + d1 * d2 / (2.0 * k_los_re_eff));
  }
  bump(h0); bump(hN);
  if (vmax - vmin < 1.0) vmax = vmin + 1.0;
  const int pad = 6;
  auto X = [&](int i){ return (lv_coord_t)(i * (gw - 1) / (k_los_samples - 1)); };
  auto Y = [&](double v){
    const double t = (v - vmin) / (vmax - vmin);
    return (lv_coord_t)(gh - pad - t * (gh - 2 * pad));
  };
  for (int i = 0; i < k_los_samples; ++i) {
    const double f  = (double)i / (k_los_samples - 1);
    const double d1 = f * D, d2 = (1.0 - f) * D;
    const double terr = (double)elev[i] + d1 * d2 / (2.0 * k_los_re_eff);
    s_los_terrain_pts[i].x = X(i);
    s_los_terrain_pts[i].y = Y(terr);
  }
  lv_obj_t* tline = lv_line_create(graph);
  lv_line_set_points(tline, s_los_terrain_pts, k_los_samples);
  lv_obj_set_style_line_color(tline, lv_color_hex(0x6FBF73), LV_PART_MAIN);
  lv_obj_set_style_line_width(tline, 2, LV_PART_MAIN);
  lv_obj_set_style_line_rounded(tline, true, LV_PART_MAIN);

  s_los_sight_pts[0] = { X(0), Y(h0) };
  s_los_sight_pts[1] = { X(k_los_samples - 1), Y(hN) };
  lv_obj_t* sline = lv_line_create(graph);
  lv_line_set_points(sline, s_los_sight_pts, 2);
  lv_obj_set_style_line_color(sline,
      lv_color_hex(verdict == V_BLOCKED ? 0xD7574E :
                   verdict == V_MARGINAL ? 0xC8A030 : 0x4DA8FF), LV_PART_MAIN);
  lv_obj_set_style_line_width(sline, 2, LV_PART_MAIN);

  if (verdict != V_CLEAR && worst_i >= 0) {
    lv_obj_t* dot = lv_obj_create(graph);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_style_radius(dot, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xD7574E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    const double f = (double)worst_i / (k_los_samples - 1);
    const double d1 = f * D, d2 = (1.0 - f) * D;
    lv_obj_set_pos(dot, X(worst_i) - 3,
                   Y((double)elev[worst_i] + d1 * d2 / (2.0 * k_los_re_eff)) - 3);
  }

  // ---- Height value labels ----
  if (s_los_self_h_lbl) lv_label_set_text_fmt(s_los_self_h_lbl, "%dm", (int)s_los_ant_self);
  if (s_los_peer_h_lbl) lv_label_set_text_fmt(s_los_peer_h_lbl, "%dm", (int)s_los_ant_peer);

  // ---- Verdict text ----
  if (s_los_verdict) {
#if defined(ESP32)
    const bool miles = touchPrefsGetUseMiles();
#else
    const bool miles = false;
#endif
    char dist_s[16];
    if (miles) snprintf(dist_s, sizeof(dist_s), "%.1f mi", dist_km * 0.621371);
    else       snprintf(dist_s, sizeof(dist_s), "%.1f km", dist_km);

    const char* vstr = (verdict == V_BLOCKED)  ? "NO LINE OF SIGHT"
                     : (verdict == V_MARGINAL) ? "MARGINAL (Fresnel)"
                                               : "LINE OF SIGHT";
    const uint32_t vcol = (verdict == V_BLOCKED)  ? 0xD7574E
                        : (verdict == V_MARGINAL) ? 0xC8A030 : 0x6FBF73;

    char body[180];
    int bo = snprintf(body, sizeof(body),
        "#a0a6ad %s  \xc2\xb7  brg %03d\xc2\xb0 %s#\n",
        dist_s, (int)(brg + 0.5), losCompass(brg));
    if (verdict == V_BLOCKED) {
      bo += snprintf(body + bo, sizeof(body) - bo,
          "#a0a6ad Blocked @ %.1f km, %dm over sight#\n",
          worst_d1 / 1000.0, (int)(-min_clear + 0.5));
    } else if (verdict == V_MARGINAL) {
      bo += snprintf(body + bo, sizeof(body) - bo,
          "#a0a6ad Grazes terrain \xc2\xb7 F1=%dm#\n", (int)(worst_f1 + 0.5));
    } else {
      bo += snprintf(body + bo, sizeof(body) - bo,
          "#a0a6ad Clear by %dm at tightest#\n", (int)(min_clear + 0.5));
    }
    snprintf(body + bo, sizeof(body) - bo,
        "#%06x %s#\n#5b6168 you %dm \xc2\xb7 peer %dm \xc2\xb7 %.0f MHz#",
        (unsigned)vcol, vstr,
        (int)s_los_ant_self, (int)s_los_ant_peer, freq_mhz);
    lv_label_set_text(s_los_verdict, body);
  }
}

// Antenna +/- handlers — adjust by 1 m (clamped 0..150) and redraw. No
// network: reuses the cached elevation profile.
static void losSelfMinusCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (s_los_ant_self >= 1.0f) s_los_ant_self -= 1.0f;
  losDrawPlot();
}
static void losSelfPlusCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (s_los_ant_self < 150.0f) s_los_ant_self += 1.0f;
  losDrawPlot();
}
static void losPeerMinusCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (s_los_ant_peer >= 1.0f) s_los_ant_peer -= 1.0f;
  losDrawPlot();
}
static void losPeerPlusCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (s_los_ant_peer < 150.0f) s_los_ant_peer += 1.0f;
  losDrawPlot();
}

// Runs on the UI thread once the worker has the elevation profile. Builds
// the persistent controls (height +/- and verdict label), then draws.
static void losRenderResult() {
  if (!s_los_card) return;
  lv_obj_t* card = s_los_card;
  const int card_w = 230;
  const int gw = card_w - 16;

  if (s_los_got < k_los_samples) {
    if (s_los_msg)
      lv_label_set_text(s_los_msg,
          "Couldn't fetch terrain data.\nCheck the connection and the\n"
          "elevation server, then\ntry again.");
    return;
  }
  if (s_los_msg) lv_obj_add_flag(s_los_msg, LV_OBJ_FLAG_HIDDEN);

  // Antenna height controls, directly under each end of the graph:
  //   left = your antenna, right = the contact's. [-] value [+]
  // Just below the graph: gy(28) + gh + 6. gh is 58 in landscape, 80 portrait.
  const int cy = chatLandscape() ? 92 : 114;
  auto mk_h_btn = [&](const char* sym, int x, lv_event_cb_t cb) {
    lv_obj_t* b = lv_btn_create(card);
    lv_obj_set_size(b, 26, 24);
    lv_obj_set_pos(b, x, cy);
    styleButton(b);
    lv_obj_set_style_pad_all(b, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_set_style_text_font(l, &g_font_16, LV_PART_MAIN);
    lv_obj_center(l);
  };
  auto mk_h_val = [&](int x) {
    lv_obj_t* l = lv_label_create(card);
    lv_obj_set_size(l, 42, 24);
    lv_obj_set_pos(l, x, cy + 4);
    lv_obj_set_style_text_font(l, &g_font_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(l, "2m");
    return l;
  };
  // Left cluster (you)
  mk_h_btn("-", 0, losSelfMinusCb);
  s_los_self_h_lbl = mk_h_val(28);
  mk_h_btn("+", 72, losSelfPlusCb);
  // Right cluster (peer)
  mk_h_btn("-", gw - 98, losPeerMinusCb);
  s_los_peer_h_lbl = mk_h_val(gw - 70);
  mk_h_btn("+", gw - 26, losPeerPlusCb);

  // Verdict label (text updated by losDrawPlot).
  s_los_verdict = lv_label_create(card);
  lv_label_set_long_mode(s_los_verdict, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_los_verdict, gw);
  lv_obj_set_style_text_font(s_los_verdict, &g_font_12, LV_PART_MAIN);
  lv_label_set_recolor(s_los_verdict, true);
  lv_obj_set_pos(s_los_verdict, 0, cy + 30);

  losDrawPlot();
}

// Called every UITask::loop tick — picks up the worker's result without
// blocking the UI. Renders into the modal only if it's still open.
static void losPoll() {
  // While the worker is fetching, reflect retry progress so a slow cold path
  // reads as work-in-progress rather than a freeze.
  if (s_los_busy) {
    if (s_los_root && s_los_msg) {
      static int shown = -1;
      const int a = s_los_attempt;
      if (a != shown) {
        shown = a;
        if (a >= 2)
          lv_label_set_text_fmt(s_los_msg, "Analyzing terrain\xe2\x80\xa6\n(retry %d of 3)", a);
        else
          lv_label_set_text(s_los_msg, "Analyzing terrain\xe2\x80\xa6");
      }
    }
    return;
  }
  if (!s_los_result_ready) return;
  s_los_result_ready = false;
  // One diag line per analysis so a failing path can be diagnosed: peer coords,
  // last HTTP code (-1 WiFi down, -2 begin failed), CSV fields returned, and
  // how many were non-null. code=200 + low valid => path has no SRTM data;
  // code!=200 => proxy rejected the query (e.g. 400 = coords out of range).
  {
    char d[80];
    snprintf(d, sizeof(d), "LOS %.4f,%.4f code=%d got=%d ok=%d",
             s_los_peer_lat, s_los_peer_lon,
             s_los_dbg_code, s_los_dbg_parsed, s_los_dbg_valid);
    pushDiagLine(d);
  }
  if (s_los_root) losRenderResult();
}

static void openLosModal(uint32_t mesh_idx) {
  closeLosModal();
  ContactInfo c;
  if (!the_mesh.getContactByIdx(mesh_idx, c)) return;
  s_los_self_lat = g_lv.task ? g_lv.task->getNodeLat() : 0.0;
  s_los_self_lon = g_lv.task ? g_lv.task->getNodeLon() : 0.0;
  s_los_peer_lat = (double)c.gps_lat / 1.0e6;
  s_los_peer_lon = (double)c.gps_lon / 1.0e6;

  // ---- Modal shell ----
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_los_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_los_root);
  lv_obj_set_size(s_los_root, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_los_root, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_los_root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_los_root, LV_OPA_70, LV_PART_MAIN);
  lv_obj_clear_flag(s_los_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_foreground(s_los_root);
  lv_obj_add_event_cb(s_los_root, losModalCloseCb, LV_EVENT_CLICKED, nullptr);

  // Cap the card to the usable height so it fits the shorter landscape screen
  // (the graph + controls shrink to match — see chatLandscape() in losDrawPlot
  // / losRenderResult).
  const int card_w = 230;
  int card_h = 248;
  if (card_h > modalAvailH()) card_h = modalAvailH();
  lv_obj_t* card = lv_obj_create(s_los_root);
  s_los_card = card;
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 8, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  addCloseXBadge(card, losModalCloseCb);

  char nm[36];
  copyUtf8ReplacingMissingGlyphs(&g_font_14, nm, sizeof(nm), c.name);
  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text_fmt(title, LV_SYMBOL_GPS "  LOS \xe2\x86\x92 %s", nm[0] ? nm : "?");
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &g_font_14, LV_PART_MAIN);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, card_w - 16 - 28);
  lv_obj_set_pos(title, 0, 0);

  lv_obj_t* msg = lv_label_create(card);
  s_los_msg = msg;
  lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(msg, card_w - 16);
  lv_obj_set_style_text_font(msg, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(msg, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_pos(msg, 0, 24);

  // Early outs that need no network.
  if (s_los_self_lat == 0.0 && s_los_self_lon == 0.0) {
    lv_label_set_text(msg, "Your location is unknown.\nSet GPS / position in\nSettings \xe2\x86\x92 Profile first.");
    return;
  }
  if (s_los_peer_lat == 0.0 && s_los_peer_lon == 0.0) {
    lv_label_set_text(msg, "This contact hasn't shared\na GPS position.");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    lv_label_set_text(msg, "Wi-Fi needed to fetch the\nterrain profile for this path.\nConnect in Settings \xe2\x86\x92 Wi-Fi.");
    return;
  }
  // Busy-watchdog: only block a new request if a fetch genuinely started
  // recently. If the worker has been "busy" too long (slow/hung fetch),
  // treat it as stale and let this request proceed rather than locking the
  // UI out indefinitely.
  if (s_los_busy && (uint32_t)(millis() - s_los_req_ms) < 48000u) {
    lv_label_set_text(msg, "Still analyzing the previous\npath\xe2\x80\xa6 try again in a moment.");
    return;
  }

  // Sample the great circle (linear lat/lon interp is fine < 100 km).
  for (int i = 0; i < k_los_samples; ++i) {
    const double f = (double)i / (k_los_samples - 1);
    s_los_slat[i] = s_los_self_lat + f * (s_los_peer_lat - s_los_self_lat);
    s_los_slon[i] = s_los_self_lon + f * (s_los_peer_lon - s_los_self_lon);
  }
  lv_label_set_text(msg, "Analyzing terrain\xe2\x80\xa6");
  s_los_result_ready = false;
  // Hand off to the shared tile-fetch worker (core 0). It checks
  // s_los_request between tile fetches and runs the elevation fetch on its
  // own stack — no extra task/stack, so no "low memory" failure. losPoll()
  // renders when it's done; the UI stays responsive meanwhile.
  if (!ensureTileFetchTaskRunning()) {
    lv_label_set_text(msg, "Couldn't start the analyzer.\nTry again.");
    return;
  }
  s_los_req_ms  = millis();
  s_los_attempt = 0;            // reset progress so losPoll shows "Analyzing…"
  s_los_busy    = true;
  s_los_request = true;
}

static void actionSheetLosCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const uint32_t idx = s_action_sheet_mesh_idx;
  lv_indev_t* act = lv_indev_get_act();
  if (act) lv_indev_wait_release(act);
  closeActionSheet();
  openLosModal(idx);
}
#endif  // ESP32 && MULTI_TRANSPORT_COMPANION

static void openContactActionSheet(uint32_t mesh_idx, bool is_repeater, const char* name) {
  s_action_sheet_mesh_idx = mesh_idx;
  s_action_sheet_is_repeater = is_repeater;
  closeActionSheet();

  lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_coord_t sh = lv_disp_get_ver_res(nullptr);

  s_action_sheet_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_action_sheet_root);
  // Backdrop starts below the global status bar so the centered card is
  // centered in the visible 298 px area (not the full 320 — that pushed
  // the top of the 8-row repeater card behind the time/battery row).
  lv_obj_set_size(s_action_sheet_root, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_action_sheet_root, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_action_sheet_root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_action_sheet_root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_action_sheet_root, 0, LV_PART_MAIN);
  lv_obj_clear_flag(s_action_sheet_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_foreground(s_action_sheet_root);
  // Tap outside closes the sheet
  lv_obj_add_event_cb(s_action_sheet_root, actionSheetCloseCb, LV_EVENT_CLICKED, nullptr);

  const int card_w = 232;
  // Two-column button grid so the (now up to 9) actions fit without
  // clipping below the status bar. Delete spans the full width as the
  // bottom danger row. Repeater worst case: 8 grid items -> 4 rows + 1
  // delete row = 5 * (30+6) + title 28 + pad 6 = 214 px (fits in 298).
  const int btn_h = 30;
  const int btn_gap = 6;
  const int title_h = 28;
  const int padding = 6;
  // msg/ping + telemetry + (trace ping + admin, repeaters only) + range
  // test + favorite/unfavorite + reset path + delete. Chat peers get 6
  // rows; repeaters get 8 (Trace SNR + Admin). +1 for "Line of sight" when
  // both this contact and our node have a GPS fix.
  bool has_los = false;
  bool is_room = false;
  {
    ContactInfo _tc;
    if (the_mesh.getContactByIdx(mesh_idx, _tc)) is_room = (_tc.type == ADV_TYPE_ROOM);
  }
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  {
    ContactInfo _lc;
    const bool peer_gps = the_mesh.getContactByIdx(mesh_idx, _lc) &&
                          (_lc.gps_lat != 0 || _lc.gps_lon != 0);
    const bool self_gps = g_lv.task &&
                          (g_lv.task->getNodeLat() != 0.0 || g_lv.task->getNodeLon() != 0.0);
    has_los = peer_gps && self_gps;
  }
#endif
  // Everything except Delete goes in a 2-column grid; Delete is a full-width
  // bottom row. Grid items = msg/ping + telemetry + range + favorite +
  // reset (5), + trace/admin for repeaters (2), + Join for rooms (1), +
  // line-of-sight (1).
  const int grid_items = 5 + (is_repeater ? 2 : 0) + (is_room ? 1 : 0) + (has_los ? 1 : 0);
  const int grid_rows  = (grid_items + 1) / 2;          // ceil
  const int card_h = title_h + (grid_rows + 1) * (btn_h + btn_gap) + padding;
  lv_obj_t* card = lv_obj_create(s_action_sheet_root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, padding, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  addCloseXBadge(card, actionSheetCloseCb);

  lv_obj_t* title = lv_label_create(card);
  char nm[40];
  copyUtf8ReplacingMissingGlyphs(&g_font_14, nm, sizeof(nm), name ? name : "");
  lv_label_set_text_fmt(title, "%s%s",
                        is_repeater ? LV_SYMBOL_CHARGE "  " :
                        is_room     ? LV_SYMBOL_LOOP   "  " : LV_SYMBOL_ENVELOPE "  ",
                        nm[0] ? nm : "(unnamed)");
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &g_font_14, LV_PART_MAIN);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  // Trim 28 px on the right so the close-X badge doesn't sit on top of
  // the contact name when it's long.
  lv_obj_set_width(title, card_w - 2 * padding - 28);
  lv_obj_set_pos(title, 0, 0);

  const int col_gap = 6;
  const int half_w  = (card_w - 2 * padding - col_gap) / 2;
  int y   = title_h;
  int col = 0;   // 0 = left column, 1 = right column

  // Half-width grid button. Advances column, wrapping to the next row.
  auto mk_btn = [&](const char* label, lv_event_cb_t cb, uint32_t bg) {
    lv_obj_t* b = lv_btn_create(card);
    lv_obj_set_size(b, half_w, btn_h);
    lv_obj_set_pos(b, col == 0 ? 0 : (half_w + col_gap), y);
    styleButton(b);
    lv_obj_set_style_pad_ver(b, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(b, 4, LV_PART_MAIN);
    if (bg) lv_obj_set_style_bg_color(b, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, &g_font_12, LV_PART_MAIN);
    lv_obj_center(l);
    if (col == 0) col = 1;
    else { col = 0; y += btn_h + btn_gap; }
  };
  // Full-width row (Delete). Closes any half-open grid row first.
  auto mk_btn_full = [&](const char* label, lv_event_cb_t cb, uint32_t bg) {
    if (col == 1) { col = 0; y += btn_h + btn_gap; }
    lv_obj_t* b = lv_btn_create(card);
    lv_obj_set_size(b, card_w - 2 * padding, btn_h);
    lv_obj_set_pos(b, 0, y);
    styleButton(b);
    lv_obj_set_style_pad_ver(b, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(b, 8, LV_PART_MAIN);
    if (bg) lv_obj_set_style_bg_color(b, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, &g_font_12, LV_PART_MAIN);
    lv_obj_center(l);
    y += btn_h + btn_gap;
  };

  // Room servers: "Join" (login) is the headline action — green so it reads as
  // the thing to do first; the server only accepts posts + pushes you the room
  // history once you've joined. "Open chat" sits next to it.
  if (is_room) {
    mk_btn(LV_SYMBOL_LOOP "  Join", actionSheetJoinRoomCb, COLOR_STATUS_OK);
    mk_btn(LV_SYMBOL_ENVELOPE "  Open chat", actionSheetSendMsgCb, 0);
  } else if (is_repeater) {
    mk_btn(LV_SYMBOL_REFRESH "  Ping", actionSheetPingCb, 0);
  } else {
    mk_btn(LV_SYMBOL_ENVELOPE "  Message", actionSheetSendMsgCb, 0);
  }
  mk_btn(LV_SYMBOL_BATTERY_3 "  Telemetry", actionSheetTelemetryCb, 0);
  if (is_repeater) {
    mk_btn(LV_SYMBOL_GPS      "  Trace SNR", actionSheetTracePingCb, 0);
    mk_btn(LV_SYMBOL_SETTINGS "  Admin",     actionSheetAdminCb,     0);
  }
  mk_btn(LV_SYMBOL_WIFI "  Range test", actionSheetRangeTestCb, 0);
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  // Terrain-aware line-of-sight (needs both GPS fixes; gated by has_los).
  if (has_los) mk_btn(LV_SYMBOL_GPS "  Sightline", actionSheetLosCb, 0);
#endif
  // Favorite toggle: label flips based on current state.
#if defined(ESP32)
  bool _is_fav_now = false;
  {
    ContactInfo _c;
    if (the_mesh.getContactByIdx(s_action_sheet_mesh_idx, _c)) {
      _is_fav_now = touchPrefsIsFavorite(_c.id.pub_key);
    }
  }
  mk_btn(_is_fav_now ? TOUCH_SYM_STAR "  Unfav"
                     : TOUCH_SYM_STAR "  Favorite",
         actionSheetFavoriteCb, 0);
#else
  mk_btn(TOUCH_SYM_STAR "  Favorite", actionSheetFavoriteCb, 0);
#endif
  mk_btn(LV_SYMBOL_LOOP  "  Reset path", actionSheetResetPathCb, 0);
  mk_btn_full(LV_SYMBOL_TRASH "  Delete", actionSheetDeleteCb, 0xB23A48);
}

static void contactSelectCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  // Guard against scroll-triggered taps. LVGL's default click threshold is
  // forgiving on flicks — operator complaint was the action sheet opened
  // every time they tried to scroll the contact list. If the indev was in
  // a scroll state during this press, treat as scroll not tap.
  lv_indev_t* act = lv_indev_get_act();
  if (act) {
    // lv_indev_get_scroll_obj returns the object currently being scrolled
    // (or NULL). If anything is being scrolled, this CLICKED was the tail
    // end of a flick, not a deliberate tap.
    lv_obj_t* scroll_obj = lv_indev_get_scroll_obj(act);
    if (scroll_obj) return;
    lv_dir_t sdir = lv_indev_get_scroll_dir(act);
    if (sdir != LV_DIR_NONE) return;
  }
  auto* ctx = static_cast<LvContactButtonCtx*>(lv_event_get_user_data(e));
  if (!ctx || !g_lv.task) return;
  ContactInfo c;
  if (!the_mesh.getContactByIdx(ctx->mesh_idx, c)) {
    g_lv.task->showAlert("Contact gone", 1200);
    return;
  }
  openContactActionSheet(ctx->mesh_idx, ctx->is_repeater, c.name);
}

// ============================================================
// Chats "+" → add-channel menu and modals
// Mirrors the web client's "Add channel" dropdown: create a private channel
// (random or supplied 32-hex secret), join a private channel by secret, join
// the public channel (PUBLIC_GROUP_PSK), or join a hashtag channel where the
// key is the first 16 bytes of SHA-256("#" + lowercased name).
// State pointers are forward-declared at the top of the file so
// closeSettingsModal() can null them on close.
// ============================================================

static void closeAddChannelSheet() {
  if (s_addch_sheet) {
    lv_obj_del(s_addch_sheet);
    s_addch_sheet = nullptr;
  }
}

// Convert 32 hex chars in `hex` to 16 raw bytes. Returns false if input is
// not exactly 32 hex chars.
static bool hexToSecret16(const char* hex, uint8_t out[16]) {
  if (!hex) return false;
  int n = 0;
  while (hex[n]) ++n;
  if (n != 32) return false;
  for (int i = 0; i < 16; ++i) {
    auto v = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
      if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
      return -1;
    };
    int hi = v(hex[i*2]);
    int lo = v(hex[i*2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

static void setAddChannelError(const char* msg) {
  if (s_addch_error_l) lv_label_set_text(s_addch_error_l, msg ? msg : "");
}

// ---- Create-private channel ----
static void createPrivateChannelSubmitCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  kbMirrorSyncToReal();
  if (!s_addch_name_ta || !s_addch_secret_ta) return;
  char name[32];
  strncpy(name, lv_textarea_get_text(s_addch_name_ta), sizeof(name) - 1);
  name[sizeof(name) - 1] = '\0';
  // Trim trailing whitespace and default to "Private" if empty.
  for (int i = (int)strlen(name) - 1; i >= 0 && (name[i] == ' ' || name[i] == '\t'); --i) name[i] = '\0';
  if (name[0] == '\0') strncpy(name, "Private", sizeof(name));

  const char* sec_raw = lv_textarea_get_text(s_addch_secret_ta);
  char hex[33]; int hn = 0;
  for (const char* p = sec_raw; *p && hn < 32; ++p) {
    if (*p == ' ' || *p == '\t' || *p == '\n') continue;
    hex[hn++] = *p;
  }
  hex[hn] = '\0';

  uint8_t secret[16];
  if (hn == 0) {
#if defined(ESP32)
    esp_fill_random(secret, sizeof(secret));
#else
    for (int i = 0; i < 16; ++i) secret[i] = static_cast<uint8_t>(rand() & 0xFF);
#endif
  } else if (!hexToSecret16(hex, secret)) {
    setAddChannelError("Secret must be 32 hex chars (or empty).");
    return;
  }

  const int slot = the_mesh.findFirstEmptyChannelSlot();
  if (slot < 0) { setAddChannelError("Channel table is full."); return; }
  if (!the_mesh.uiAddOrUpdateChannel(slot, name, secret)) {
    setAddChannelError("Failed to save channel.");
    return;
  }
  // Don't wait for the loop's deferred refresh — pull the channel into the
  // thread list right now so the chats list shows the new entry the instant
  // the modal closes.
  if (g_lv.task) {
    g_lv.task->refreshThreadsFromMesh();
    g_lv.dirty_threads = true;
  }
  closeSettingsModal();
  if (g_lv.task) g_lv.task->showAlert("Channel created", 1200);
}

// ===== Contacts → Add manually (pubkey + name) ==============================
// Add a peer we want to talk to before we've heard their advert. Convert a
// 64-hex pubkey into raw bytes and ask MyMesh::uiAddManualContact to wedge
// a synthetic chat-type contact into the table. State pointers are
// declared earlier in the file so closeSettingsModal can null them.
static bool hexToPubkey32(const char* hex, uint8_t out[32]) {
  if (!hex) return false;
  int n = 0;
  while (hex[n]) ++n;
  if (n != 64) return false;
  for (int i = 0; i < 32; ++i) {
    auto v = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
      if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
      return -1;
    };
    int hi = v(hex[i*2]);
    int lo = v(hex[i*2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

static void addContactSubmitCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  kbMirrorSyncToReal();
  if (!s_addct_pub_ta || !s_addct_name_ta) return;
  const char* pub_raw  = lv_textarea_get_text(s_addct_pub_ta);
  const char* name_raw = lv_textarea_get_text(s_addct_name_ta);

  char hex[80]; int hn = 0;
  for (const char* p = pub_raw; *p && hn < 65; ++p) {
    if (*p == ' ' || *p == ':' || *p == '\n' || *p == '\t' || *p == '\r') continue;
    hex[hn++] = *p;
  }
  hex[hn] = '\0';

  uint8_t pub[32];
  if (!hexToPubkey32(hex, pub)) {
    if (s_addct_error_l) lv_label_set_text(s_addct_error_l, "Pubkey must be 64 hex chars.");
    return;
  }
  char name[32] = {0};
  strncpy(name, name_raw, sizeof(name) - 1);
  // trim trailing whitespace
  for (int i = (int)strlen(name) - 1; i >= 0 && (name[i] == ' ' || name[i] == '\t'); --i) name[i] = '\0';
  if (name[0] == '\0') {
    if (s_addct_error_l) lv_label_set_text(s_addct_error_l, "Name can't be empty.");
    return;
  }
  if (!the_mesh.uiAddManualContact(pub, name)) {
    if (s_addct_error_l) lv_label_set_text(s_addct_error_l, "Already exists or table full.");
    return;
  }
  closeSettingsModal();
  if (g_lv.task) g_lv.task->showAlert("Contact added", 1200);
  refreshContactsList();
}

static void openAddContactModalCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t* body = createSettingsModal("Add contact", SettingsModalKind::AddContact);
  int y = 0;

  lv_obj_t* hint = lv_label_create(body);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(hint, lv_pct(100));
  lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(hint, &g_font_12, LV_PART_MAIN);
  lv_label_set_text(hint, "Paste the 64-hex public key and a name. Useful when you want to DM someone before their advert has arrived.");
  lv_obj_set_pos(hint, 2, y);
  y += 48;

  lv_obj_t* pub_l = lv_label_create(body);
  lv_label_set_text(pub_l, "Public key (64 hex chars)");
  lv_obj_set_style_text_color(pub_l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(pub_l, &g_font_12, LV_PART_MAIN);
  lv_obj_set_pos(pub_l, 2, y);
  y += 16;
  s_addct_pub_ta = lv_textarea_create(body);
  lv_obj_set_size(s_addct_pub_ta, lv_pct(100),56);
  lv_obj_set_pos(s_addct_pub_ta, 2, y);
  lv_textarea_set_one_line(s_addct_pub_ta, false);
  lv_textarea_set_placeholder_text(s_addct_pub_ta, "0123456789abcdef…");
  lv_textarea_set_max_length(s_addct_pub_ta, 80);
  attachSettingsTaEvents(s_addct_pub_ta);
  y += 60;

  lv_obj_t* name_l = lv_label_create(body);
  lv_label_set_text(name_l, "Name");
  lv_obj_set_style_text_color(name_l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(name_l, &g_font_12, LV_PART_MAIN);
  lv_obj_set_pos(name_l, 2, y);
  y += 16;
  s_addct_name_ta = lv_textarea_create(body);
  lv_obj_set_size(s_addct_name_ta, lv_pct(100),30);
  lv_obj_set_pos(s_addct_name_ta, 2, y);
  lv_textarea_set_one_line(s_addct_name_ta, true);
  lv_textarea_set_placeholder_text(s_addct_name_ta, "Display name");
  lv_textarea_set_max_length(s_addct_name_ta, 31);
  attachSettingsTaEvents(s_addct_name_ta);
  y += 36;

  s_addct_error_l = lv_label_create(body);
  lv_label_set_long_mode(s_addct_error_l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_addct_error_l, lv_pct(100));
  lv_obj_set_style_text_color(s_addct_error_l, lv_color_hex(0xE08080), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_addct_error_l, &g_font_12, LV_PART_MAIN);
  lv_label_set_text(s_addct_error_l, "");
  lv_obj_set_pos(s_addct_error_l, 2, y);
  y += 24;

  lv_obj_t* b = lv_btn_create(body);
  lv_obj_set_size(b, lv_pct(100),36);
  lv_obj_set_pos(b, 2, y);
  styleButton(b);
  lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x3B7039), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_add_event_cb(b, addContactSubmitCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* bl = lv_label_create(b);
  lv_label_set_text(bl, "Add contact");
  lv_obj_center(bl);
}

static void openCreatePrivateChannelModal() {
  lv_obj_t* body = createSettingsModal("Create private channel", SettingsModalKind::ChCreatePrv);
  int y = 0;

  lv_obj_t* hint = lv_label_create(body);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(hint, lv_pct(100));
  lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(hint, &g_font_12, LV_PART_MAIN);
  lv_label_set_text(hint, "Share the 32-char secret so others can join. Leave the secret empty to generate a random one.");
  lv_obj_set_pos(hint, 2, y);
  y += 44;

  lv_obj_t* name_l = lv_label_create(body);
  lv_label_set_text(name_l, "Channel name");
  lv_obj_set_style_text_color(name_l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(name_l, &g_font_12, LV_PART_MAIN);
  lv_obj_set_pos(name_l, 2, y);
  y += 16;
  s_addch_name_ta = lv_textarea_create(body);
  lv_obj_set_size(s_addch_name_ta, lv_pct(100),30);
  lv_obj_set_pos(s_addch_name_ta, 2, y);
  lv_textarea_set_one_line(s_addch_name_ta, true);
  lv_textarea_set_placeholder_text(s_addch_name_ta, "e.g. Family");
  lv_textarea_set_max_length(s_addch_name_ta, 31);
  attachSettingsTaEvents(s_addch_name_ta);
  y += 36;

  lv_obj_t* sec_l = lv_label_create(body);
  lv_label_set_text(sec_l, "Secret (32 hex, optional)");
  lv_obj_set_style_text_color(sec_l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(sec_l, &g_font_12, LV_PART_MAIN);
  lv_obj_set_pos(sec_l, 2, y);
  y += 16;
  s_addch_secret_ta = lv_textarea_create(body);
  lv_obj_set_size(s_addch_secret_ta, lv_pct(100),30);
  lv_obj_set_pos(s_addch_secret_ta, 2, y);
  lv_textarea_set_one_line(s_addch_secret_ta, true);
  lv_textarea_set_placeholder_text(s_addch_secret_ta, "leave empty to generate");
  lv_textarea_set_max_length(s_addch_secret_ta, 32);
  attachSettingsTaEvents(s_addch_secret_ta);
  y += 36;

  s_addch_error_l = lv_label_create(body);
  lv_label_set_long_mode(s_addch_error_l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_addch_error_l, lv_pct(100));
  lv_obj_set_style_text_color(s_addch_error_l, lv_color_hex(0xE08080), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_addch_error_l, &g_font_12, LV_PART_MAIN);
  lv_label_set_text(s_addch_error_l, "");
  lv_obj_set_pos(s_addch_error_l, 2, y);
  y += 24;

  lv_obj_t* b = lv_btn_create(body);
  lv_obj_set_size(b, lv_pct(100),36);
  lv_obj_set_pos(b, 2, y);
  styleButton(b);
  lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x3B7039), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_add_event_cb(b, createPrivateChannelSubmitCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* bl = lv_label_create(b);
  lv_label_set_text(bl, "Create");
  lv_obj_center(bl);
}

// ---- Join-private channel ----
static void joinPrivateChannelSubmitCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  kbMirrorSyncToReal();
  if (!s_addch_secret_ta) return;
  char jname[32] = "Joined";   // default if the user leaves Name blank
  if (s_addch_name_ta) {
    strncpy(jname, lv_textarea_get_text(s_addch_name_ta), sizeof(jname) - 1);
    jname[sizeof(jname) - 1] = '\0';
    for (int i = (int)strlen(jname) - 1; i >= 0 && (jname[i] == ' ' || jname[i] == '\t'); --i) jname[i] = '\0';
    if (jname[0] == '\0') strncpy(jname, "Joined", sizeof(jname));
  }
  const char* raw = lv_textarea_get_text(s_addch_secret_ta);
  char hex[33]; int hn = 0;
  for (const char* p = raw; *p && hn < 32; ++p) {
    if (*p == ' ' || *p == '\t' || *p == '\n') continue;
    hex[hn++] = *p;
  }
  hex[hn] = '\0';
  uint8_t secret[16];
  if (!hexToSecret16(hex, secret)) {
    setAddChannelError("Enter exactly 32 hex characters.");
    return;
  }
  const int slot = the_mesh.findFirstEmptyChannelSlot();
  if (slot < 0) { setAddChannelError("Channel table is full."); return; }
  if (!the_mesh.uiAddOrUpdateChannel(slot, jname, secret)) {
    setAddChannelError("Failed to save channel.");
    return;
  }
  if (g_lv.task) {
    g_lv.task->refreshThreadsFromMesh();
    g_lv.dirty_threads = true;
  }
  closeSettingsModal();
  if (g_lv.task) g_lv.task->showAlert("Channel joined", 1200);
}

static void openJoinPrivateChannelModal() {
  lv_obj_t* body = createSettingsModal("Join private channel", SettingsModalKind::ChJoinPrv);
  int y = 0;

  lv_obj_t* hint = lv_label_create(body);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(hint, lv_pct(100));
  lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(hint, &g_font_12, LV_PART_MAIN);
  lv_label_set_text(hint, "Enter the 32-hex secret shared by the channel creator.");
  lv_obj_set_pos(hint, 2, y);
  y += 32;

  lv_obj_t* name_l = lv_label_create(body);
  lv_label_set_text(name_l, "Name");
  lv_obj_set_style_text_color(name_l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(name_l, &g_font_12, LV_PART_MAIN);
  lv_obj_set_pos(name_l, 2, y);
  y += 16;
  s_addch_name_ta = lv_textarea_create(body);
  lv_obj_set_size(s_addch_name_ta, lv_pct(100), 30);
  lv_obj_set_pos(s_addch_name_ta, 2, y);
  lv_textarea_set_one_line(s_addch_name_ta, true);
  lv_textarea_set_placeholder_text(s_addch_name_ta, "Channel name");
  lv_textarea_set_max_length(s_addch_name_ta, 30);
  attachSettingsTaEvents(s_addch_name_ta);
  y += 36;

  lv_obj_t* sec_l = lv_label_create(body);
  lv_label_set_text(sec_l, "Secret (32 hex chars)");
  lv_obj_set_style_text_color(sec_l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(sec_l, &g_font_12, LV_PART_MAIN);
  lv_obj_set_pos(sec_l, 2, y);
  y += 16;
  s_addch_secret_ta = lv_textarea_create(body);
  lv_obj_set_size(s_addch_secret_ta, lv_pct(100),30);
  lv_obj_set_pos(s_addch_secret_ta, 2, y);
  lv_textarea_set_one_line(s_addch_secret_ta, true);
  lv_textarea_set_placeholder_text(s_addch_secret_ta, "32 hex characters");
  lv_textarea_set_max_length(s_addch_secret_ta, 32);
  attachSettingsTaEvents(s_addch_secret_ta);
  y += 36;

  s_addch_error_l = lv_label_create(body);
  lv_label_set_long_mode(s_addch_error_l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_addch_error_l, lv_pct(100));
  lv_obj_set_style_text_color(s_addch_error_l, lv_color_hex(0xE08080), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_addch_error_l, &g_font_12, LV_PART_MAIN);
  lv_label_set_text(s_addch_error_l, "");
  lv_obj_set_pos(s_addch_error_l, 2, y);
  y += 24;

  lv_obj_t* b = lv_btn_create(body);
  lv_obj_set_size(b, lv_pct(100),36);
  lv_obj_set_pos(b, 2, y);
  styleButton(b);
  lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x3B7039), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_add_event_cb(b, joinPrivateChannelSubmitCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* bl = lv_label_create(b);
  lv_label_set_text(bl, "Join");
  lv_obj_center(bl);
}

// ---- Join hashtag channel ----
static void joinHashtagChannelSubmitCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  kbMirrorSyncToReal();
  if (!s_addch_hashtag_ta) return;
  // Normalize: strip leading '#'s, drop whitespace, ASCII-lowercase. Matches
  // Meshcomod-client/apps/web-client/src/main.ts:deriveHashtagChannelSecret.
  const char* raw = lv_textarea_get_text(s_addch_hashtag_ta);
  char norm[40]; int nn = 0;
  const char* p = raw;
  while (*p == '#') ++p;
  while (*p && nn < (int)sizeof(norm) - 1) {
    char c = *p++;
    if (c == ' ' || c == '\t' || c == '\n') continue;
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    norm[nn++] = c;
  }
  norm[nn] = '\0';
  if (nn == 0) strncpy(norm, "public", sizeof(norm));
  char hashed[42] = "#";
  strncat(hashed, norm, sizeof(hashed) - 2);

  uint8_t secret[16];
  mesh::Utils::sha256(secret, sizeof(secret),
                      reinterpret_cast<const uint8_t*>(hashed),
                      (int)strlen(hashed));

  const int slot = the_mesh.findFirstEmptyChannelSlot();
  if (slot < 0) { setAddChannelError("Channel table is full."); return; }
  if (!the_mesh.uiAddOrUpdateChannel(slot, hashed, secret)) {
    setAddChannelError("Failed to save channel.");
    return;
  }
  if (g_lv.task) {
    g_lv.task->refreshThreadsFromMesh();
    g_lv.dirty_threads = true;
  }
  closeSettingsModal();
  if (g_lv.task) g_lv.task->showAlert("Channel joined", 1200);
}

static void openJoinHashtagChannelModal() {
  lv_obj_t* body = createSettingsModal("Join hashtag channel", SettingsModalKind::ChJoinTag);
  int y = 0;

  lv_obj_t* hint = lv_label_create(body);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(hint, lv_pct(100));
  lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(hint, &g_font_12, LV_PART_MAIN);
  lv_label_set_text(hint, "Anyone can join. Key is derived from the hashtag (lowercase).");
  lv_obj_set_pos(hint, 2, y);
  y += 32;

  lv_obj_t* name_l = lv_label_create(body);
  lv_label_set_text(name_l, "Hashtag name");
  lv_obj_set_style_text_color(name_l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(name_l, &g_font_12, LV_PART_MAIN);
  lv_obj_set_pos(name_l, 2, y);
  y += 16;
  s_addch_hashtag_ta = lv_textarea_create(body);
  lv_obj_set_size(s_addch_hashtag_ta, lv_pct(100),30);
  lv_obj_set_pos(s_addch_hashtag_ta, 2, y);
  lv_textarea_set_one_line(s_addch_hashtag_ta, true);
  lv_textarea_set_placeholder_text(s_addch_hashtag_ta, "e.g. mesh");
  lv_textarea_set_text(s_addch_hashtag_ta, "#");
  lv_textarea_set_max_length(s_addch_hashtag_ta, 31);
  attachSettingsTaEvents(s_addch_hashtag_ta);
  y += 36;

  s_addch_error_l = lv_label_create(body);
  lv_label_set_long_mode(s_addch_error_l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_addch_error_l, lv_pct(100));
  lv_obj_set_style_text_color(s_addch_error_l, lv_color_hex(0xE08080), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_addch_error_l, &g_font_12, LV_PART_MAIN);
  lv_label_set_text(s_addch_error_l, "");
  lv_obj_set_pos(s_addch_error_l, 2, y);
  y += 24;

  lv_obj_t* b = lv_btn_create(body);
  lv_obj_set_size(b, lv_pct(100),36);
  lv_obj_set_pos(b, 2, y);
  styleButton(b);
  lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x3B7039), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_add_event_cb(b, joinHashtagChannelSubmitCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* bl = lv_label_create(b);
  lv_label_set_text(bl, "Join");
  lv_obj_center(bl);
}

// ---- "+" action sheet on the Chats tab ----
static void addChannelSheetDismissCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeAddChannelSheet();
}

static void addChannelCreatePrivateCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeAddChannelSheet();
  openCreatePrivateChannelModal();
}

static void addChannelJoinPrivateCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeAddChannelSheet();
  openJoinPrivateChannelModal();
}

static void addChannelJoinPublicCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeAddChannelSheet();
  if (the_mesh.uiJoinPublicChannel()) {
    if (g_lv.task) {
      g_lv.task->refreshThreadsFromMesh();
      g_lv.dirty_threads = true;
      g_lv.task->showAlert("Public channel ready", 1200);
    }
  } else {
    if (g_lv.task) g_lv.task->showAlert("Channel table is full", 1400);
  }
}

static void addChannelJoinHashtagCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeAddChannelSheet();
  openJoinHashtagChannelModal();
}

static void openAddChannelSheet() {
  closeAddChannelSheet();
  s_addch_sheet = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_addch_sheet);
  // Backdrop starts below the global status bar — full screen so it covers
  // everything and the card centers on-screen in either orientation.
  lv_obj_set_size(s_addch_sheet, lv_disp_get_hor_res(nullptr),
                  lv_disp_get_ver_res(nullptr) - STATUSBAR_H);
  lv_obj_set_pos(s_addch_sheet, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_addch_sheet, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_addch_sheet, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_addch_sheet, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_foreground(s_addch_sheet);
  lv_obj_add_event_cb(s_addch_sheet, addChannelSheetDismissCb, LV_EVENT_CLICKED, nullptr);

  const int rows  = 4;
  const int pad   = 10;
  // Shrink the button height if the four rows + header won't fit the (shorter)
  // landscape viewport, so the card never runs off-screen.
  int btn_h = 38;
  if (36 + rows * (btn_h + 6) + pad > modalAvailH())
    btn_h = ((modalAvailH() - 36 - pad) / rows) - 6;
  if (btn_h < 26) btn_h = 26;
  int card_w = 220;
  if (card_w > modalAvailW()) card_w = modalAvailW();
  const int card_h = 36 + rows * (btn_h + 6) + pad;
  lv_obj_t* card = lv_obj_create(s_addch_sheet);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, pad, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  addCloseXBadge(card, addChannelSheetDismissCb);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "Add channel");
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &g_font_14, LV_PART_MAIN);
  lv_obj_set_pos(title, 0, 0);

  int y = 28;
  auto mk = [&](const char* label, lv_event_cb_t cb) {
    lv_obj_t* b = lv_btn_create(card);
    lv_obj_set_size(b, card_w - 2 * pad, btn_h);
    lv_obj_set_pos(b, 0, y);
    styleButton(b);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, &g_font_14, LV_PART_MAIN);
    lv_obj_center(l);
    y += btn_h + 6;
  };
  mk("Create a private channel", addChannelCreatePrivateCb);
  mk("Join a private channel",   addChannelJoinPrivateCb);
  mk("Join the public channel",  addChannelJoinPublicCb);
  mk("Join a hashtag channel",   addChannelJoinHashtagCb);
}

static void chatsAddBtnCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  openAddChannelSheet();
}

static void markAllReadApply() {
  if (!g_lv.task) return;
  g_lv.task->markAllThreadsRead();
  g_lv.dirty_threads = true;             // rebuild the list -> badges clear
  g_lv.task->showAlert("All marked read", 1000);
}
static void chatsMarkAllReadBtnCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  showConfirm("Mark all chats and channels as read?", "Mark read", markAllReadApply);
}

// ---- Share-my-contact QR popup ------------------------------------------
//
// Renders an lv_qrcode encoding our own pubkey + node name so another
// touch device can scan it (with a phone or another firmware build that
// grows a camera/scanner later) and import us as a contact.
//
// Payload format: `meshcore://add?p=<64-hex pubkey>&n=<name>`. Compact
// URL-ish form so a generic phone QR app can show it as a tappable link
// (firmware scanners can lift the p= / n= fields directly).
static lv_obj_t* s_share_my_root = nullptr;

static void closeShareMyContact() {
  if (s_share_my_root) {
    lv_obj_del_async(s_share_my_root);
    s_share_my_root = nullptr;
  }
}
static void shareMyContactBackdropCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_indev_t* a = lv_indev_get_act();
  if (a) lv_indev_wait_release(a);
  closeShareMyContact();
}

static void openShareMyContactPopup() {
  closeShareMyContact();

  // Collect self identity. Fallbacks keep the popup useful even when the
  // node name hasn't been set yet (the QR will just say "node").
  const uint8_t* pub = the_mesh.getSelfPubKey();
  const char* node_name = (g_lv.task ? g_lv.task->getNodeNameCstr() : "");
  const char* name = (node_name && node_name[0]) ? node_name : "node";

  char pub_hex[65] = {0};
  for (int i = 0; i < 32; ++i) {
    static const char* k_hex = "0123456789abcdef";
    pub_hex[i*2 + 0] = k_hex[(pub[i] >> 4) & 0xF];
    pub_hex[i*2 + 1] = k_hex[ pub[i]       & 0xF];
  }
  // URL-encode the name: only the chars likely to appear (spaces, punct)
  // really need it; this is a pragmatic subset. Touch firmware will read
  // the raw bytes back anyway. 96-byte buffer = up to ~32 char name with
  // %xx escapes.
  char name_enc[96] = {0};
  {
    int o = 0;
    for (int i = 0; name[i] && o + 4 < (int)sizeof(name_enc); ++i) {
      const unsigned char c = (unsigned char)name[i];
      const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                         || (c >= '0' && c <= '9') || c == '-' || c == '_';
      if (plain) {
        name_enc[o++] = c;
      } else {
        static const char* k_hex = "0123456789ABCDEF";
        name_enc[o++] = '%';
        name_enc[o++] = k_hex[(c >> 4) & 0xF];
        name_enc[o++] = k_hex[ c       & 0xF];
      }
    }
    name_enc[o] = '\0';
  }
  char payload[256];
  snprintf(payload, sizeof(payload), "meshcore://add?p=%s&n=%s", pub_hex, name_enc);

  lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_share_my_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_share_my_root);
  // Sit below the global status bar — same pattern as every other popup.
  lv_obj_set_size(s_share_my_root, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_share_my_root, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_share_my_root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_share_my_root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_share_my_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_share_my_root, shareMyContactBackdropCb, LV_EVENT_CLICKED, nullptr);

  // Card: title row at top, QR centered, hex pubkey under it (handy when
  // the operator wants to dictate it over voice and there's no camera).
  // Clamp to the usable area and scale the QR to fit so the popup never runs
  // off the shorter landscape screen.
  int card_w = 220;
  if (card_w > modalAvailW()) card_w = modalAvailW();
  int card_h = 270;
  if (card_h > modalAvailH()) card_h = modalAvailH();
  int qr_size = card_h - 34 - 30 - 20;   // title row + hex caption + padding
  if (qr_size > 160) qr_size = 160;
  if (qr_size > card_w - 20) qr_size = card_w - 20;
  if (qr_size < 96) qr_size = 96;
  lv_obj_t* card = lv_obj_create(s_share_my_root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  addCloseXBadge(card, shareMyContactBackdropCb);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text_fmt(title, "Share: %s", name);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, card_w - 20 - 32);   // trim for close-X
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &g_font_14, LV_PART_MAIN);
  lv_obj_set_pos(title, 0, 4);

  // QR widget — 160 px in a 220 px card with 10 px padding leaves 20 px
  // gutters left/right. Dark = pure black, light = white-ish; LVGL needs
  // strong contrast for the camera-side decoder.
  lv_obj_t* qr = lv_qrcode_create(card, qr_size,
                                  lv_color_hex(0x000000),
                                  lv_color_hex(0xFFFFFF));
  lv_qrcode_update(qr, payload, strlen(payload));
  lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, 32);
  // White frame around the QR so the camera-side QR detector doesn't get
  // confused by the dark card bleeding into the quiet zone.
  lv_obj_set_style_border_color(qr, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_border_width(qr, 4, LV_PART_MAIN);

  // Short pubkey prefix below the QR — quick visual check that two
  // devices show the same identity.
  char prefix_buf[24];
  snprintf(prefix_buf, sizeof(prefix_buf), "%02X%02X%02X%02X%02X%02X",
           pub[0], pub[1], pub[2], pub[3], pub[4], pub[5]);
  lv_obj_t* hex_lbl = lv_label_create(card);
  lv_label_set_text(hex_lbl, prefix_buf);
  lv_obj_set_style_text_color(hex_lbl, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(hex_lbl, &g_font_12, LV_PART_MAIN);
  // Anchor under the QR (not the card bottom) so it can't ride up onto the
  // code when the QR shrinks in landscape.
  lv_obj_align_to(hex_lbl, qr, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
}

static void shareMyContactBtnCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  openShareMyContactPopup();
}

// ============================================================
// UI Builders
// ============================================================
// ---- Home battery icon + heartbeat + TX/RX chart ----
static lv_obj_t* s_home_batt_icon = nullptr;
static lv_obj_t* s_home_batt_pct  = nullptr;
static lv_obj_t* s_home_clock     = nullptr;
// Small pulsing dot next to the title — animates while the LVGL timer is alive.
static lv_obj_t* s_home_heartbeat = nullptr;
// TX / RX activity chart on Home. Two series: TX (sent, green) and RX (recv,
// blue). Refreshed on each refreshStatusLabels tick from the_mesh's dispatcher
// counters; chart is a rolling window of the last N samples (auto-shift).
static lv_obj_t*       s_home_chart       = nullptr;
static lv_chart_series_t* s_home_chart_tx = nullptr;
static lv_chart_series_t* s_home_chart_rx = nullptr;
// Compact legend label above the chart showing live TX/RX totals.
static lv_obj_t* s_home_chart_legend = nullptr;

static void heartbeatAnimOpa(void* var, int32_t v) {
  lv_obj_set_style_bg_opa(static_cast<lv_obj_t*>(var),
                          static_cast<lv_opa_t>(v), LV_PART_MAIN);
}
static void heartbeatAnimSize(void* var, int32_t v) {
  // v is the diameter (and radius via styleCard logic below).
  lv_obj_set_size(static_cast<lv_obj_t*>(var), v, v);
  lv_obj_set_style_radius(static_cast<lv_obj_t*>(var), v / 2, LV_PART_MAIN);
}

// ---- Battery / charge state ----
// A single Li-ion cell can't rest above ~4.2 V. When USB is plugged the charger
// drives the rail higher: a reading above this threshold means "on charger".
// This charge-from-voltage trick + the EMA smoothing are tuned for the T-Deck's
// calibrated analogReadMilliVolts. The Heltec V4 uses a different, noisier ADC
// path (raw analogRead × a fixed scale) whose plugged-in reading already sat at
// ~100 %; running the EMA over that noise dragged the average DOWN (≈64 %) and
// it never crossed the charge threshold. So gate both to the T-Deck and let the
// V4 keep its prior direct read.

// User-calibrated "full" voltage (the reading captured when the pack was fully
// charged) = 100%. 0 = use the 4200 mV default Li-ion full point. Lets custom
// packs / builds read 100%. Cached so the per-tick % calc doesn't hit NVS;
// updated in place by the Calibrate-battery action.
static uint16_t s_batt_full_mv = 0;
static bool     s_batt_full_loaded = false;
static uint16_t batteryFullMv() {
  if (!s_batt_full_loaded) { s_batt_full_mv = touchPrefsGetBattFullMv(); s_batt_full_loaded = true; }
  return s_batt_full_mv ? s_batt_full_mv : 4200;
}

#if defined(HAS_TDECK_GT911)
static constexpr uint16_t kBattChargingMv = 4250;

// Per-board battery sampler: EMA over the noisy ADC so the value doesn't jitter
// ±1 every tick. Wrapped by batteryMvSmoothed() (below), which only publishes a
// fresh value every 20 s. Returns 0 if unsupported.
static uint16_t batteryMvSampled() {
  if (!g_lv.task) return 0;
  const uint16_t raw = g_lv.task->getBattMilliVolts();
  if (raw == 0) return 0;
  static float s_ema = 0.0f;
  if (s_ema < 1.0f) {
    s_ema = (float)raw;                            // seed on first read
  } else {
    const float d = (float)raw - s_ema;
    // A charger plug/unplug steps the rail ~100 mV+ — snap straight to it so the
    // charge state is recognized within a tick instead of crawling there over
    // the EMA. Smaller deltas are ADC noise, so keep smoothing those.
    if (d > 70.0f || d < -70.0f) s_ema = (float)raw;
    else                         s_ema += d * 0.15f;   // ~7-sample time constant
  }
  return (uint16_t)(s_ema + 0.5f);
}
static bool batteryIsCharging(uint16_t mv) { return mv >= (uint16_t)(batteryFullMv() + 50); }
#else
// V4 (and any non-T-Deck touch board): direct read, no charge-from-voltage.
static uint16_t batteryMvSampled() { return g_lv.task ? g_lv.task->getBattMilliVolts() : 0; }
static bool batteryIsCharging(uint16_t) { return false; }
#endif

// Hold the battery reading steady: publish a fresh value only every 20 s so the
// %, icon and voltage stop twitching tick-to-tick. The per-board sampler above
// still runs on every call (keeps the T-Deck EMA fed); every readout just sees
// the same held value for 20 s. The first call publishes immediately so there's
// no blank battery at boot.
static uint16_t batteryMvSmoothed() {
  const uint16_t cur = batteryMvSampled();
  if (cur == 0) return 0;                          // not ready / unsupported — don't latch a 0
  static uint16_t      s_pub = 0;
  static unsigned long s_pub_ms = 0;
  const unsigned long  now = millis();
  // Normally hold for 20 s to kill jitter — BUT publish at once when the charge
  // state flips, so plugging/unplugging the charger is recognized immediately
  // instead of waiting out the hold window.
  const bool charge_flip = (s_pub != 0) && (batteryIsCharging(cur) != batteryIsCharging(s_pub));
  if (s_pub == 0 || charge_flip || (unsigned long)(now - s_pub_ms) >= 20000UL) {
    s_pub    = cur;
    s_pub_ms = now;
  }
  return s_pub;
}

static int batteryPercentFromMv(uint16_t mv);   // fwd decl (icon derives from %)
static const char* batteryGlyphForMv(uint16_t mv) {
  if (mv == 0)               return LV_SYMBOL_BATTERY_EMPTY;
  if (batteryIsCharging(mv)) return LV_SYMBOL_CHARGE;   // on USB power
  // Derive the icon from the (calibrated) percent so custom packs whose full
  // voltage differs from 4.2 V still fill the icon at their real 100%.
  const int pct = batteryPercentFromMv(mv);
  if (pct >= 90) return LV_SYMBOL_BATTERY_FULL;
  if (pct >= 65) return LV_SYMBOL_BATTERY_3;
  if (pct >= 40) return LV_SYMBOL_BATTERY_2;
  if (pct >= 15) return LV_SYMBOL_BATTERY_1;
  return LV_SYMBOL_BATTERY_EMPTY;
}

static int batteryPercentFromMv(uint16_t mv) {
  // Li-ion curve: 3.30 V empty, FULL = the calibrated full (default 4.20 V).
  // While charging the rail reads above full (charger-driven) — cap at 100
  // rather than show a bogus number; the real cell % isn't observable here.
  if (mv == 0) return -1;
  const uint16_t full = batteryFullMv();
  if (mv >= full)  return 100;
  if (mv <= 3300)  return 0;
  return (int)((mv - 3300) * 100 / (full - 3300));
}

// Calibrate-battery action (Device settings). SHORT_CLICKED captures the current
// voltage as the new 100%; LONG_PRESSED resets to the default Li-ion curve. Same
// on V4 + T-Deck. SHORT_CLICKED (not CLICKED) so a long-press to reset doesn't
// also fall through and calibrate.
static void calibrateBatteryCb(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_LONG_PRESSED) {
    touchPrefsSetBattFullMv(0);
    s_batt_full_mv = 0; s_batt_full_loaded = true;
    if (g_lv.task) g_lv.task->showAlert("Battery calibration reset to default", 2200);
    return;
  }
  if (code != LV_EVENT_SHORT_CLICKED) return;
  // Average a short burst so one noisy ADC sample (esp. the V4 direct read)
  // doesn't skew the reference.
  uint32_t sum = 0; int n = 0;
  for (int i = 0; i < 8; ++i) { uint16_t s = batteryMvSampled(); if (s) { sum += s; ++n; } delay(20); }
  const uint16_t mv = n ? (uint16_t)(sum / n) : 0;
  if (mv < 3500) {   // implausibly low for a "full" pack — refuse rather than store junk
    if (g_lv.task) g_lv.task->showAlert("Battery read too low — charge fully first", 2600);
    return;
  }
  touchPrefsSetBattFullMv(mv);
  s_batt_full_mv = mv; s_batt_full_loaded = true;
  char b[72];
  snprintf(b, sizeof b, "Calibrated: 100%% = %u.%02u V  (long-press to reset)",
           (unsigned)(mv / 1000), (unsigned)((mv % 1000) / 10));
  if (g_lv.task) g_lv.task->showAlert(b, 3000);
}

static void refreshHomeBattery() {
  if (!g_lv.task) return;
  uint16_t mv = batteryMvSmoothed();
  const bool charging = batteryIsCharging(mv);
  // Icon: update only when the glyph actually changes (avoids hammering
  // lv_label_set_text every tick which froze the loop with multi-byte
  // FontAwesome symbols).
  static const char* s_last_glyph = nullptr;
  if (s_home_batt_icon) {
    const char* g = batteryGlyphForMv(mv);
    if (g != s_last_glyph) {
      lv_label_set_text(s_home_batt_icon, g);
      s_last_glyph = g;
    }
  }
  // Percent: ASCII-only text. Cache last pct so we don't churn the label.
  if (s_home_batt_pct) {
    static int s_last_pct = -9999;
    static bool s_last_chg = false;
    int pct = batteryPercentFromMv(mv);
    if (pct != s_last_pct || charging != s_last_chg) {
      char buf[12];
      if (pct < 0)       snprintf(buf, sizeof(buf), "?");
      else if (charging) snprintf(buf, sizeof(buf), "CHG");
      else               snprintf(buf, sizeof(buf), "%d%%", pct);
      lv_label_set_text(s_home_batt_pct, buf);
      s_last_pct = pct;
      s_last_chg = charging;
    }
  }
  // Live HH:MM clock from the RTC (which gets NTP-synced over Wi-Fi). Only
  // touch lv_label_set_text when the rendered minute actually changes.
  if (s_home_clock) {
    static int s_last_min = -1;
#if defined(ESP32)
    time_t now_t = time(nullptr);
    if (now_t > 1700000000) {  // ~ "RTC has been bootstrapped" sentinel
      struct tm tm_loc;
      localtime_r(&now_t, &tm_loc);
      const int min_of_day = tm_loc.tm_hour * 60 + tm_loc.tm_min;
      if (min_of_day != s_last_min) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", tm_loc.tm_hour, tm_loc.tm_min);
        lv_label_set_text(s_home_clock, buf);
        s_last_min = min_of_day;
      }
    }
#endif
  }
}

// Long-press the home battery glyph → details popup.
static void homeBatteryLongPressCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_LONG_PRESSED || !g_lv.task) return;
  lv_indev_t* act = lv_indev_get_act();
  if (act) lv_indev_wait_release(act);
  const uint16_t mv = g_lv.task->getBattMilliVolts();
  const int pct = batteryPercentFromMv(mv);
  // Rough charging heuristic: voltage above 4.10 V on this board usually
  // means USB power is in.
  const char* state = (mv == 0)        ? "no reading"
                    : (mv >= 4100)     ? "charging / USB"
                    : (pct < 15)       ? "low"
                    : "discharging";
  char msg[120];
  if (pct < 0) {
    snprintf(msg, sizeof(msg), "Battery\n  %u mV\n  %s", (unsigned)mv, state);
  } else {
    snprintf(msg, sizeof(msg), "Battery\n  %u mV  (%d%%)\n  %s",
             (unsigned)mv, pct, state);
  }
  g_lv.task->showAlert(msg, 3500);
}

#if defined(HAS_TDECK_GT911)
// ---- Full-screen tool views (Terminal / File explorer) ----
// A fullscreen overlay below the status bar, with a Home button that returns to
// the Home tab. Body is returned so the (future) content can be added into it.
static lv_obj_t* s_fullscreen_view  = nullptr;
static char      s_fullscreen_title[40] = {0};   // shown in the status bar while open

// ===== On-device MeshCore terminal =========================================
// A real CLI that runs commands on THIS node (the_mesh.runLocalCli) and streams
// every reply line into a scrollable log. Output is captured via
// MyMesh::setTerminalSink. Output runs on the main loop task (main.cpp calls
// ui_task.loop() and the_mesh.loop() sequentially, never concurrently with the
// LVGL render), so each line is built as its own coloured child label here.
static lv_obj_t*     s_term_log_box     = nullptr;   // scrollable flex-column of line labels
static lv_obj_t*     s_term_input_ta    = nullptr;
static lv_obj_t*     s_term_picker_root = nullptr;
static const uint32_t TERM_MAX_LINES    = 150;       // prune oldest lines past this
// Current terminal recipient, set by `to <name>` (a DM contact OR a channel).
static bool          s_term_to_set        = false;
static bool          s_term_to_is_channel = false;
static uint8_t       s_term_to_pub[32]    = {0};
static int16_t       s_term_to_chan_slot  = -1;
static char          s_term_to_name[40]   = {0};

// ===== File manager =========================================================
static lv_obj_t* s_fm_list      = nullptr;  // scrollable list of entries
static lv_obj_t* s_fm_path_lbl  = nullptr;  // address-bar / current location
static lv_obj_t* s_fm_search_ta = nullptr;  // inline search field (when active)
static lv_obj_t* s_fm_sort_lbl  = nullptr;  // sort button label (shows current mode)
static fs::FS*   s_fm_fs        = nullptr;  // current filesystem (nullptr = roots screen)
static char      s_fm_path[160]  = {0};     // current dir within s_fm_fs (e.g. "/" or "/foo")
static char      s_fm_store[12]  = {0};     // storage label ("Internal" / "SD")
static char      s_fm_filter[40] = {0};     // active search filter (empty = none)
static uint8_t   s_fm_sort       = 0;       // 0 Name A-Z, 1 Z-A, 2 Size, 3 Type
struct FmEntry { char name[64]; uint32_t size; bool isdir; };
static const int FM_MAX_ENTRIES  = 192;
static FmEntry*  s_fm_entries    = nullptr; // PSRAM-allocated while the file manager is open
static int       s_fm_count      = 0;
static bool      s_fm_show_hidden = false;  // reveal MeshCore system files on flat SPIFFS (toggle in + menu)
static lv_obj_t* s_editor_root   = nullptr; // text editor overlay
static lv_obj_t* s_editor_ta     = nullptr;
static char      s_editor_path[200] = {0};
static lv_obj_t* s_fm_img_root   = nullptr; // read-only image viewer overlay
static uint8_t*  s_fm_img_buf    = nullptr; // decoded RGB565 (PSRAM); freed on close
static lv_img_dsc_t s_fm_img_dsc;           // wraps s_fm_img_buf for the lv_img widget
static lv_obj_t* s_fm_img_widget = nullptr; // the scaled lv_img inside the viewer
static lv_obj_t* s_fm_img_hdr    = nullptr; // filename/size label (windowed only)
static lv_obj_t* s_fm_img_close  = nullptr; // close (X) button (windowed only)
static lv_obj_t* s_fm_img_full   = nullptr; // full-screen toggle button (windowed only)
static lv_obj_t* s_fm_img_hint   = nullptr; // "tap to exit" hint (full-screen only)
static int       s_fm_img_w = 0, s_fm_img_h = 0; // decoded image dimensions
static bool      s_fm_img_fs = false;       // viewer is in full-screen mode
static bool          s_sd_mounted   = false;   // microSD mount state
static uint64_t      s_sd_size      = 0;       // card capacity (bytes)
static unsigned long s_sd_retry_after_ms = 0;  // don't re-probe SD before this (backoff)
static int       s_sd_format_pending = 0;   // deferred FAT32 format (runs after notice paints)
static lv_obj_t* s_fm_fmt_overlay = nullptr; // full-screen "formatting..." notice
static lv_obj_t* s_fm_actions    = nullptr;  // per-entry action sheet
static lv_obj_t* s_fm_prompt     = nullptr;  // text-input modal (rename / new folder)
static lv_obj_t* s_fm_prompt_ta  = nullptr;
static void    (*s_fm_prompt_cb)(const char*) = nullptr;
static char      s_fm_sel_name[64] = {0};    // entry the action sheet targets
static bool      s_fm_sel_isdir  = false;
static int       s_fm_paste_pending = 0;     // deferred copy/move (runs after notice paints)
// Copy/Cut clipboard.
static struct { fs::FS* fs; char path[200]; char name[64]; bool isdir; bool is_cut; bool active; }
  s_fm_clip = {};

static const char* k_term_banner =
  "Meshcomod terminal - CLI + chat on this node.\n"
  "Type a command or tap the list icon for the picker.\n"
  "Chat: 'to <name>' joins a contact/channel, then just\n"
  "      type to send. 'exit' leaves. list / channels.\n"
  "      All incoming msgs print live.\n"
  "Config: ver clock status get advert reboot\n"
  "        set <name|freq|bw|sf|cr|tx> <value>\n";

// Quick-pick catalogue (mirrors the repeater admin picker). Commands here are
// the ones MyMesh::handleMeshcomodCommand understands natively when run
// locally. Tapping a row stuffs the template into the input; "set ..." rows
// carry a trailing space so the value can be typed straight after.
static const AdminCmdEntry k_term_cmds[] = {
  { "[ CHAT ]", nullptr },
  { "list - list contacts",           "list" },
  { "channels - list channels",       "channels" },
  { "to <name> - join contact/chan",  "to " },
  { "send <text> - msg recipient",    "send " },
  { "public <text> - public channel", "public " },
  { "exit - leave current chat",      "exit" },
  { "[ INFO ]", nullptr },
  { "help - command list",            "help" },
  { "ver - firmware version",         "ver" },
  { "clock - show RTC time",          "clock" },
  { "status - device status",         "status" },
  { "get - show radio params",        "get" },
  { "[ RADIO / MESH ]", nullptr },
  { "advert - flood advert",          "advert" },
  { "advert.zerohop - 0-hop advert",  "advert.zerohop" },
  { "set name <new>",                 "set name " },
  { "set freq <MHz>",                 "set freq " },
  { "set bw <kHz>",                   "set bw " },
  { "set sf <7-12>",                  "set sf " },
  { "set cr <5-8>",                   "set cr " },
  { "set tx <dBm>",                   "set tx " },
  { "[ CONNECTIVITY ]", nullptr },
  { "wifi status",                    "wifi status" },
  { "wifi scan",                      "wifi scan" },
  { "tcp status",                     "tcp status" },
  { "ble status",                     "ble status" },
  { "ota start",                      "ota start" },
  { "[ SYSTEM ]", nullptr },
  { "reboot",                         "reboot" },
  { "bootloader - download mode",     "bootloader" },
};
constexpr int k_term_cmds_count = sizeof(k_term_cmds) / sizeof(k_term_cmds[0]);

// Per-line colors (0xRRGGBB, applied as the label's real text color).
static const uint32_t TERM_C_INPUT  = 0x7fe0ff;  // "> cmd" echo (cyan)
static const uint32_t TERM_C_TX     = 0x63d863;  // your sent messages (green)
static const uint32_t TERM_C_RX_DM  = 0x4fc3f7;  // incoming DM (light blue)
static const uint32_t TERM_C_RX_CH  = 0xffcc66;  // incoming channel msg (amber)
static const uint32_t TERM_C_ERR    = 0xff6b6b;  // errors (red)
static const uint32_t TERM_C_INFO   = 0x9aa7b0;  // info / status (gray)
static const uint32_t TERM_C_REPLY  = 0xcfd6dc;  // config-CLI replies (light)
static const uint32_t TERM_C_BANNER = 0x7f8c99;  // opening banner (dim)

// Append a line to the terminal as its own coloured label (a child of the
// scrollable flex-column box). No recolor markup, so message text can contain
// any character. Safe to build LVGL objects here: all callers run on the main
// loop task (see the s_term_log_box note above). Oldest lines prune past
// TERM_MAX_LINES; the view always scrolls to the newest line.
static void termLogAppendC(uint32_t color, const char* prefix, const char* text) {
  if (!s_term_log_box || !text) return;
  char buf[512];
  snprintf(buf, sizeof buf, "%s%s", prefix ? prefix : "", text);
  lv_obj_t* lbl = lv_label_create(s_term_log_box);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, lv_pct(100));
  lv_obj_set_style_text_color(lbl, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl, &g_font_12, LV_PART_MAIN);
  lv_label_set_text(lbl, buf);
  uint32_t n = lv_obj_get_child_cnt(s_term_log_box);
  while (n > TERM_MAX_LINES) { lv_obj_del(lv_obj_get_child(s_term_log_box, 0)); --n; }
  lv_obj_scroll_to_y(s_term_log_box, LV_COORD_MAX, LV_ANIM_OFF);
}

// Default-coloured append (config-CLI replies / uncategorised lines).
static void termLogAppend(const char* prefix, const char* text) {
  termLogAppendC(TERM_C_REPLY, prefix, text);
}

// Output sink registered with MyMesh while the terminal is open.
static void terminalSink(const char* line) {
  if (line) termLogAppend(nullptr, line);
}

static void closeTermCmdPicker() {
  if (s_term_picker_root) {
    lv_obj_del_async(s_term_picker_root);
    s_term_picker_root = nullptr;
  }
}

static void fmImageClose();   // fwd — defined in the file-manager section

static void closeFullscreenView() {
  closeTermCmdPicker();
  // Detach the sink + null the widgets BEFORE the async delete so a late reply
  // (or the loop renderer) can't write into a freed label/box.
  MyMesh::setTerminalSink(nullptr);
  const bool had_kb = (s_term_input_ta || s_fm_search_ta || s_fm_prompt_ta);
  if (s_editor_root) {
    if (g_lv.keyboard) lv_keyboard_set_textarea(g_lv.keyboard, nullptr);
    lv_obj_del_async(s_editor_root); s_editor_root = nullptr; s_editor_ta = nullptr;
  }
  fmImageClose();   // tear down the image viewer + free its PSRAM buffer (sync del)
  if (s_fm_actions) { lv_obj_del_async(s_fm_actions); s_fm_actions = nullptr; }
  if (s_fm_prompt)  { lv_obj_del_async(s_fm_prompt);  s_fm_prompt  = nullptr; }
  s_term_log_box   = nullptr;
  s_term_input_ta  = nullptr;
  s_fm_list        = nullptr;   // file-manager widgets live in the same body
  s_fm_path_lbl    = nullptr;
  s_fm_search_ta   = nullptr;
  s_fm_sort_lbl    = nullptr;
  s_fm_prompt_ta   = nullptr;
  s_fm_fs          = nullptr;
  s_fm_filter[0]   = '\0';
  if (s_fm_entries) { free(s_fm_entries); s_fm_entries = nullptr; }   // release the PSRAM cache
  s_fm_count = 0;
  if (had_kb) hideKb();   // unbind the keyboard mirror from the (soon-freed) input/field
  if (s_fullscreen_view) { lv_obj_del_async(s_fullscreen_view); s_fullscreen_view = nullptr; }
  s_fullscreen_title[0] = '\0';
  updateGlobalStatusBar();  // restore MESHCOMOD / unread in the status bar
}

static void fullscreenHomeCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeFullscreenView();
  if (g_lv.tabview) lv_tabview_set_act(g_lv.tabview, 0, LV_ANIM_OFF);   // back to Home
}

static lv_obj_t* openFullscreenView(const char* title) {
  closeFullscreenView();
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_fullscreen_view = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_fullscreen_view);
  lv_obj_set_size(s_fullscreen_view, sw, sh - STATUSBAR_H);   // keep the status bar visible
  lv_obj_set_pos(s_fullscreen_view, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_fullscreen_view, lv_color_hex(COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_fullscreen_view, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_fullscreen_view, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_foreground(s_fullscreen_view);

  // Show the view name in the status bar's left zone (where MESHCOMOD sits)
  // rather than a dedicated title row — that reclaims the vertical space.
  strncpy(s_fullscreen_title, title ? title : "", sizeof(s_fullscreen_title) - 1);
  s_fullscreen_title[sizeof(s_fullscreen_title) - 1] = '\0';
  updateGlobalStatusBar();

  // Body fills the whole view (no header row).
  lv_obj_t* body = lv_obj_create(s_fullscreen_view);
  lv_obj_remove_style_all(body);
  lv_obj_set_size(body, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(body, 0, 0);
  lv_obj_set_style_pad_all(body, 6, LV_PART_MAIN);

  // Home button floats as a small overlay over the top-right of the body.
  lv_obj_t* home = lv_btn_create(s_fullscreen_view);
  lv_obj_set_size(home, 40, 28);
  lv_obj_align(home, LV_ALIGN_TOP_RIGHT, -6, 4);
  styleButton(home);
  lv_obj_add_event_cb(home, fullscreenHomeCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* hl = lv_label_create(home);
  lv_label_set_text(hl, LV_SYMBOL_HOME);
  lv_obj_center(hl);
  lv_obj_move_foreground(home);
  return body;
}

// ---- meshcore-cli-style chat commands (to / send / public / list / channels) ----
// Transmit `text` to a DM contact (is_channel=false) or a channel slot. Reuses
// the same the_mesh send primitives the Chats composer uses; echoes a TX line.
static void termDoSend(bool is_channel, const uint8_t* pub, int16_t chan_slot,
                       const char* disp, const char* text) {
  static uint32_t s_last_term_tx_ts = 0;
  static uint8_t  s_term_attempt    = 4;
  const char* sender = the_mesh.getNodePrefs()->node_name;
  if (!sender || !sender[0]) sender = "me";
  size_t tlen = strlen(text);
  if (tlen > MAX_TEXT_LEN) tlen = MAX_TEXT_LEN;
  char body[MAX_TEXT_LEN + 1];
  memcpy(body, text, tlen);
  body[tlen] = '\0';

  uint32_t ts = the_mesh.getRTCClock()->getCurrentTimeUnique();
  if (ts <= s_last_term_tx_ts) ts = s_last_term_tx_ts + 1;
  s_last_term_tx_ts = ts;

  char r[MAX_TEXT_LEN + 48];
  if (is_channel) {
    ChannelDetails cd;
    if (chan_slot < 0 || !the_mesh.getChannel(chan_slot, cd) || !cd.name[0]) {
      termLogAppendC(TERM_C_ERR, nullptr, "channel not found");
      return;
    }
    if (!the_mesh.sendGroupMessage(ts, cd.channel, sender, body, (int)strlen(body))) {
      termLogAppendC(TERM_C_ERR, nullptr, "send failed");
      return;
    }
    snprintf(r, sizeof r, "TX [%s] %s", disp, body);
    termLogAppendC(TERM_C_TX, nullptr, r);
  } else {
    ContactInfo* by = the_mesh.lookupContactByPubKey((uint8_t*)pub, PUB_KEY_SIZE);
    if (!by) { termLogAppendC(TERM_C_ERR, nullptr, "contact missing"); return; }
    ContactInfo rcpt = *by;
    rcpt.out_path_len = OUT_PATH_UNKNOWN;   // force flood routing (matches Chats TX)
    uint8_t attempt = s_term_attempt++;
    if (attempt < 4) { attempt = 4; s_term_attempt = 5; }
    uint32_t expected_ack = 0, est_timeout = 0;
    int rr = the_mesh.sendMessage(rcpt, ts, attempt, body, expected_ack, est_timeout);
    if (rr == MSG_SEND_FAILED) { termLogAppendC(TERM_C_ERR, nullptr, "send failed"); return; }
    the_mesh.uiRegisterExpectedAck(expected_ack, by->id.pub_key);
    snprintf(r, sizeof r, "TX -> %s: %s", disp, body);
    termLogAppendC(TERM_C_TX, nullptr, r);
  }
}

// `to <name>`: resolve a name to a DM contact (by name prefix) or a channel
// (by name), and remember it as the current recipient.
static void termCmdTo(const char* arg) {
  while (*arg == ' ' || *arg == '\t') ++arg;
  if (!*arg) {
    if (s_term_to_set) {
      char r[64];
      snprintf(r, sizeof r, "recipient: %s%s",
               s_term_to_is_channel ? "[chan] " : "", s_term_to_name);
      termLogAppendC(TERM_C_INFO, nullptr, r);
    } else {
      termLogAppendC(TERM_C_INFO, nullptr, "no recipient set - 'to <name>'");
    }
    return;
  }
  size_t alen = strlen(arg);
  int nc = the_mesh.getNumContacts();
  for (int i = 0; i < nc; ++i) {
    ContactInfo c{};
    if (the_mesh.getContactByIdx((uint32_t)i, c) && c.name[0] &&
        strncasecmp(c.name, arg, alen) == 0) {
      memcpy(s_term_to_pub, c.id.pub_key, 32);
      s_term_to_is_channel = false;
      s_term_to_set = true;
      strncpy(s_term_to_name, c.name, sizeof(s_term_to_name) - 1);
      s_term_to_name[sizeof(s_term_to_name) - 1] = '\0';
      char r[64];
      snprintf(r, sizeof r, "now talking to %s", s_term_to_name);
      termLogAppendC(TERM_C_INFO, nullptr, r);
      termLogAppendC(TERM_C_INFO, nullptr, "(type to send; 'exit' leaves)");
      return;
    }
  }
  for (int s = 0; s < MAX_GROUP_CHANNELS; ++s) {
    ChannelDetails cd;
    if (the_mesh.getChannel(s, cd) && cd.name[0] &&
        strncasecmp(cd.name, arg, alen) == 0) {
      s_term_to_chan_slot = (int16_t)s;
      s_term_to_is_channel = true;
      s_term_to_set = true;
      strncpy(s_term_to_name, cd.name, sizeof(s_term_to_name) - 1);
      s_term_to_name[sizeof(s_term_to_name) - 1] = '\0';
      char r[64];
      snprintf(r, sizeof r, "now on channel %s", s_term_to_name);
      termLogAppendC(TERM_C_INFO, nullptr, r);
      termLogAppendC(TERM_C_INFO, nullptr, "(type to send; 'exit' leaves)");
      return;
    }
  }
  char r[80];
  snprintf(r, sizeof r, "no contact/channel matching '%s'", arg);
  termLogAppendC(TERM_C_ERR, nullptr, r);
}

static void termCmdList() {
  int nc = the_mesh.getNumContacts();
  char hdr[40];
  snprintf(hdr, sizeof hdr, "contacts (%d):", nc);
  termLogAppendC(TERM_C_INFO, nullptr, hdr);
  int shown = 0;
  for (int i = 0; i < nc && shown < 60; ++i) {
    ContactInfo c{};
    if (the_mesh.getContactByIdx((uint32_t)i, c) && c.name[0]) {
      termLogAppend("  ", c.name);
      ++shown;
    }
  }
}

static void termCmdChannels() {
  termLogAppendC(TERM_C_INFO, nullptr, "channels:");
  for (int s = 0; s < MAX_GROUP_CHANNELS; ++s) {
    ChannelDetails cd;
    if (the_mesh.getChannel(s, cd) && cd.name[0]) {
      char line[48];
      snprintf(line, sizeof line, "  [%d] %s", s, cd.name);
      termLogAppend(nullptr, line);
    }
  }
}

static void termCmdSend(const char* text) {
  while (*text == ' ' || *text == '\t') ++text;
  if (!*text) { termLogAppendC(TERM_C_ERR, nullptr, "usage: send <text>"); return; }
  if (!s_term_to_set) { termLogAppendC(TERM_C_ERR, nullptr, "no recipient - use 'to <name>' first"); return; }
  termDoSend(s_term_to_is_channel, s_term_to_pub, s_term_to_chan_slot, s_term_to_name, text);
}

static void termCmdPublic(const char* text) {
  while (*text == ' ' || *text == '\t') ++text;
  if (!*text) { termLogAppendC(TERM_C_ERR, nullptr, "usage: public <text>"); return; }
  ChannelDetails cd;
  if (!the_mesh.getChannel(0, cd) || !cd.name[0]) { termLogAppendC(TERM_C_ERR, nullptr, "no public channel"); return; }
  termDoSend(true, nullptr, 0, cd.name, text);
}

// `exit` / `leave`: drop the current recipient so bare text is a command again.
static void termCmdExit() {
  if (!s_term_to_set) { termLogAppendC(TERM_C_INFO, nullptr, "not in a chat"); return; }
  char r[64];
  snprintf(r, sizeof r, "left %s", s_term_to_name);
  s_term_to_set        = false;
  s_term_to_is_channel = false;
  s_term_to_chan_slot  = -1;
  s_term_to_name[0]    = '\0';
  termLogAppendC(TERM_C_INFO, nullptr, r);
}

// Is `word` (length `len`) one of the local config-CLI keywords? Used so that
// while we're "in" a chat, typed config commands still run instead of being
// sent as a message — only non-keyword lines are treated as chat text.
static bool termIsCliKeyword(const char* word, size_t len) {
  static const char* kw[] = {
    "help", "ver", "version", "clock", "time", "status", "get", "set",
    "advert", "advert.zerohop", "reboot", "wifi", "tcp", "ble", "ota",
    "ok", "cancel",
  };
  for (const char* k : kw) {
    if (strlen(k) == len && strncasecmp(word, k, len) == 0) return true;
  }
  return false;
}

// Returns true if `cmd` was handled as a chat command or chat message; false
// lets it fall through to the local config CLI (the_mesh.runLocalCli).
static bool terminalRunChatCommand(const char* cmd) {
  while (*cmd == ' ' || *cmd == '\t') ++cmd;
  auto is = [](const char* s, const char* name, const char** rest) -> bool {
    size_t n = strlen(name);
    if (strncasecmp(s, name, n) == 0 && (s[n] == '\0' || s[n] == ' ' || s[n] == '\t')) {
      if (rest) { const char* r = s + n; while (*r == ' ' || *r == '\t') ++r; *rest = r; }
      return true;
    }
    return false;
  };
  const char* rest = nullptr;
  if (is(cmd, "list", &rest) || is(cmd, "contacts", &rest)) { termCmdList();     return true; }
  if (is(cmd, "channels", &rest))                           { termCmdChannels(); return true; }
  if (is(cmd, "to", &rest))                                 { termCmdTo(rest);   return true; }
  if (is(cmd, "exit", &rest) || is(cmd, "leave", &rest))    { termCmdExit();     return true; }
  if (is(cmd, "send", &rest))                               { termCmdSend(rest); return true; }
  if (is(cmd, "public", &rest))                             { termCmdPublic(rest); return true; }
  // Room mode: once a recipient is selected, a line whose first word isn't a
  // config-CLI keyword is sent straight to that recipient as a chat message.
  // (To send text that starts with a keyword, use `send <text>`.)
  if (s_term_to_set && *cmd) {
    size_t wl = 0;
    while (cmd[wl] && cmd[wl] != ' ' && cmd[wl] != '\t') ++wl;
    if (!termIsCliKeyword(cmd, wl)) {
      termDoSend(s_term_to_is_channel, s_term_to_pub, s_term_to_chan_slot, s_term_to_name, cmd);
      return true;
    }
  }
  return false;
}

// Run whatever is in the input: chat commands handled here, everything else
// falls through to the local config CLI. Replies stream back via terminalSink;
// we just echo the prompt line and clear the field.
static void terminalSubmit() {
  if (!s_term_input_ta) return;
  kbMirrorSyncToReal();   // pull the latest text out of the keyboard mirror
  const char* text = lv_textarea_get_text(s_term_input_ta);
  if (!text || !text[0]) return;
  char cmd[128];
  strncpy(cmd, text, sizeof(cmd) - 1);
  cmd[sizeof(cmd) - 1] = '\0';
  termLogAppendC(TERM_C_INPUT, "> ", cmd);
  if (!terminalRunChatCommand(cmd)) the_mesh.runLocalCli(cmd);
  lv_textarea_set_text(s_term_input_ta, "");
  // Re-bind so the cleared mirror tracks the field and the next Enter submits.
  if (g_lv.keyboard) kbMirrorBind(s_term_input_ta);
}

// Scrollable command picker, modelled on openAdminCmdPicker. Tapping a row
// stuffs the template into the terminal input and keeps it focused so Enter
// (or the value typed after a "set ..." template) sends it.
static void openTermCmdPicker() {
  closeTermCmdPicker();
  lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_term_picker_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_term_picker_root);
  lv_obj_set_size(s_term_picker_root, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_term_picker_root, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_term_picker_root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_term_picker_root, LV_OPA_70, LV_PART_MAIN);
  lv_obj_clear_flag(s_term_picker_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_term_picker_root, [](lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_indev_t* a = lv_indev_get_act();
    if (a) lv_indev_wait_release(a);
    closeTermCmdPicker();
  }, LV_EVENT_CLICKED, nullptr);

  const int card_w = sw - 20;
  const int card_h = (sh - STATUSBAR_H) - 40;
  lv_obj_t* card = lv_obj_create(s_term_picker_root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 6, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "Commands");
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &g_font_14, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 4);

  addCloseXBadge(card, +[](lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    closeTermCmdPicker();
  });

  lv_obj_t* list = lv_list_create(card);
  lv_obj_set_size(list, card_w - 12, card_h - 12 - 28);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 28);
  lv_obj_set_style_bg_color(list, lv_color_hex(COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);

  for (int i = 0; i < k_term_cmds_count; ++i) {
    const AdminCmdEntry& e = k_term_cmds[i];
    if (!e.command) {
      lv_obj_t* h = lv_list_add_text(list, e.label);
      lv_obj_set_style_text_color(h, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
      lv_obj_set_style_text_font(h, &g_font_12, LV_PART_MAIN);
      lv_obj_set_style_bg_color(h, lv_color_hex(0x0F1722), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(h, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_width(h, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_ver(h, 6, LV_PART_MAIN);
      lv_obj_set_style_pad_left(h, 10, LV_PART_MAIN);
      continue;
    }
    lv_obj_t* btn = lv_list_add_btn(list, nullptr, e.label);
    lv_obj_set_style_text_font(btn, &g_font_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(btn, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x141516), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x141516), LV_PART_MAIN);
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_min_height(btn, 30, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_left(btn, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, [](lv_event_t* ev) {
      if (lv_event_get_code(ev) != LV_EVENT_CLICKED) return;
      intptr_t idx = (intptr_t)lv_event_get_user_data(ev);
      if (idx < 0 || idx >= k_term_cmds_count) return;
      const char* tpl = k_term_cmds[idx].command;
      if (!tpl || !s_term_input_ta) { closeTermCmdPicker(); return; }
      lv_textarea_set_text(s_term_input_ta, tpl);
      lv_textarea_set_cursor_pos(s_term_input_ta, LV_TEXTAREA_CURSOR_LAST);
      closeTermCmdPicker();
      // Keep the input focused so the operator can type a value / press Enter.
      if (g_lv.keyboard) kbMirrorBind(s_term_input_ta);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
}

// Build the terminal into the fullscreen body: scrolling log on top, an input
// row (picker button + textarea + send) at the bottom.
static void buildTerminal(lv_obj_t* body) {
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  const lv_coord_t bw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t bh = (lv_disp_get_ver_res(nullptr) - STATUSBAR_H);  // body fills the view
  const lv_coord_t row_h = 40;

  s_term_log_box = lv_obj_create(body);
  lv_obj_remove_style_all(s_term_log_box);
  lv_obj_set_size(s_term_log_box, bw - 8, bh - row_h - 4);
  lv_obj_set_pos(s_term_log_box, 4, 2);
  styleSurface(s_term_log_box, 0x0A0B0C, 6);
  lv_obj_set_style_border_color(s_term_log_box, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_term_log_box, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_term_log_box, 6, LV_PART_MAIN);
  lv_obj_set_scroll_dir(s_term_log_box, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_term_log_box, LV_SCROLLBAR_MODE_AUTO);
  // Stack each log line as its own coloured label.
  lv_obj_set_flex_flow(s_term_log_box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_term_log_box, 1, LV_PART_MAIN);

  // Seed the banner (fresh each open) — must come AFTER the box exists.
  termLogAppendC(TERM_C_BANNER, nullptr, k_term_banner);

  // Input row
  lv_obj_t* row = lv_obj_create(body);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, bw, row_h);
  lv_obj_set_pos(row, 0, bh - row_h);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* picker_btn = lv_btn_create(row);
  lv_obj_set_size(picker_btn, 32, 32);
  lv_obj_align(picker_btn, LV_ALIGN_LEFT_MID, 4, 0);
  styleButton(picker_btn);
  lv_obj_set_style_bg_color(picker_btn, lv_color_hex(0x1A1B1C), LV_PART_MAIN);
  lv_obj_set_style_pad_all(picker_btn, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(picker_btn, [](lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    openTermCmdPicker();
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* picker_lbl = lv_label_create(picker_btn);
  lv_label_set_text(picker_lbl, LV_SYMBOL_LIST);
  lv_obj_set_style_text_font(picker_lbl, &g_font_14, LV_PART_MAIN);
  lv_obj_center(picker_lbl);

  s_term_input_ta = lv_textarea_create(row);
  lv_obj_set_size(s_term_input_ta, bw - 104, 32);
  lv_obj_align(s_term_input_ta, LV_ALIGN_LEFT_MID, 40, 0);
  styleCard(s_term_input_ta);
  lv_textarea_set_one_line(s_term_input_ta, true);
  lv_textarea_set_max_length(s_term_input_ta, 96);
  lv_textarea_set_placeholder_text(s_term_input_ta, "command");
  lv_obj_set_style_text_color(s_term_input_ta, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_term_input_ta, &g_font_14, LV_PART_MAIN);
  attachSettingsTaEvents(s_term_input_ta);

  lv_obj_t* send_btn = lv_btn_create(row);
  lv_obj_set_size(send_btn, 56, 32);
  lv_obj_align(send_btn, LV_ALIGN_RIGHT_MID, -4, 0);
  styleButton(send_btn);
  lv_obj_set_style_bg_color(send_btn, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_add_event_cb(send_btn, [](lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    terminalSubmit();
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* send_lbl = lv_label_create(send_btn);
  lv_label_set_text(send_lbl, LV_SYMBOL_RIGHT);
  lv_obj_center(send_lbl);

  // Capture replies + auto-focus the input so HW keys type immediately.
  MyMesh::setTerminalSink(&terminalSink);
  if (g_lv.keyboard) kbMirrorBind(s_term_input_ta);
}

static void homeTerminalCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t* body = openFullscreenView("Terminal");
  buildTerminal(body);
}

// ---- File manager (Phase 1 + header: Back / address bar / Sort / Find) ----
static void fmRefresh();
static void fmRender();
static void fmShowRoots();
static void fmOpenEditor(const char* name);

static const char* k_fm_sort_names[] = { "A-Z", "Z-A", "Size", "Type" };

static void fmFmtSize(size_t bytes, char* out, size_t outsz) {
  if (bytes < 1024)                 snprintf(out, outsz, "%u B", (unsigned)bytes);
  else if (bytes < 1024UL * 1024)   snprintf(out, outsz, "%.1f KB", bytes / 1024.0);
  else                              snprintf(out, outsz, "%.1f MB", bytes / (1024.0 * 1024.0));
}

// Style a list button to the dark palette (matches the command picker).
static void fmStyleRow(lv_obj_t* btn, uint32_t text_color) {
  lv_obj_set_style_text_font(btn, &g_font_14, LV_PART_MAIN);
  lv_obj_set_style_text_color(btn, lv_color_hex(text_color), LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x141516), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(btn, lv_color_hex(0x141516), LV_PART_MAIN);
  lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
  lv_obj_set_style_min_height(btn, 34, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(btn, 5, LV_PART_MAIN);
}

// Case-insensitive substring test for the search filter.
static bool fmContainsCI(const char* hay, const char* needle) {
  if (!needle || !needle[0]) return true;
  size_t nl = strlen(needle);
  for (const char* p = hay; *p; ++p) if (strncasecmp(p, needle, nl) == 0) return true;
  return false;
}

// qsort comparator honouring s_fm_sort. Dirs sort before files for Size/Type.
static int fmCmp(const void* a, const void* b) {
  const FmEntry* x = (const FmEntry*)a;
  const FmEntry* y = (const FmEntry*)b;
  switch (s_fm_sort) {
    case 1: return strcasecmp(y->name, x->name);                       // Name Z-A
    case 2:                                                            // Size (dirs first)
      if (x->isdir != y->isdir) return x->isdir ? -1 : 1;
      if (x->isdir) return strcasecmp(x->name, y->name);
      return (y->size > x->size) ? 1 : (y->size < x->size ? -1 : 0);
    case 3:                                                            // Type (dirs first)
      if (x->isdir != y->isdir) return x->isdir ? -1 : 1;
      return strcasecmp(x->name, y->name);
    default: return strcasecmp(x->name, y->name);                     // Name A-Z
  }
}

// Append `name` onto the current path (handles the root "/" case).
static void fmEnterDir(const char* base) {
  size_t n = strlen(s_fm_path);
  if (n == 0) { snprintf(s_fm_path, sizeof s_fm_path, "/%s", base); }
  else if (s_fm_path[n - 1] == '/') {
    snprintf(s_fm_path + n, sizeof(s_fm_path) - n, "%s", base);
  } else {
    snprintf(s_fm_path + n, sizeof(s_fm_path) - n, "/%s", base);
  }
  fmRefresh();
}

static void fmUp() {
  if (!s_fm_fs) return;                       // already at the roots screen
  if (strcmp(s_fm_path, "/") == 0) { fmShowRoots(); return; }   // root -> roots
  char* slash = strrchr(s_fm_path, '/');
  if (slash == s_fm_path) s_fm_path[1] = '\0';   // keep leading "/"
  else if (slash)         *slash = '\0';
  fmRefresh();
}

// Free the strdup'd basename stashed on a directory row when it's deleted.
static void fmRowFreeCb(lv_event_t* e) {
  void* ud = lv_obj_get_user_data(lv_event_get_target(e));
  if (ud) free(ud);
}
static void fmUpCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmUp();
}

static void fmOpenStorage(fs::FS* fs, const char* store, const char* path) {
  s_fm_fs = fs;
  snprintf(s_fm_store, sizeof s_fm_store, "%s", store);
  snprintf(s_fm_path, sizeof s_fm_path, "%s", path);
  fmRefresh();
}
static void fmInternalClickCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmOpenStorage(&SPIFFS, "Internal", "/");
}

// 64-bit size formatter for card capacity (cards routinely exceed 4 GB).
static void fmFmtSize64(uint64_t bytes, char* out, size_t outsz) {
  if (bytes < 1024ULL * 1024)              snprintf(out, outsz, "%.0f KB", bytes / 1024.0);
  else if (bytes < 1024ULL * 1024 * 1024)  snprintf(out, outsz, "%.0f MB", bytes / (1024.0 * 1024));
  else                                     snprintf(out, outsz, "%.1f GB", bytes / (1024.0 * 1024 * 1024));
}

// Mount the microSD on the shared LoRa SPI bus. Safe to call repeatedly (no-op
// once mounted). SD.begin's internal spi.begin() is a no-op because the bus is
// already initialised by the radio, so the radio's pins are untouched.
static bool fmSdTryMount() {
  if (s_sd_mounted) return true;
  SPIClass* spi = tdeckSharedSPI();
  if (!spi) return false;
  // Cold microSD cards — especially the first mount after boot — often fail
  // the initial SD.begin and historically only recovered after a physical
  // reinsert (which power-cycles the card). The T-Deck shares ONE power rail
  // (GPIO10) across radio + display + SD, so we can't power-cycle the card on
  // its own. Instead coax it: several SD.begin attempts, each preceded by an
  // SD.end() (de-inits the SD host so the next begin re-runs the full card
  // wake-up handshake) and a progressively longer settle delay. A healthy card
  // mounts on the first attempt and returns immediately; only a flaky/cold card
  // walks the whole ladder (~1.1 s worst case). The cumulative delay can
  // approach the loop watchdog window, so drop the WDT around the loop. 4 MHz
  // is reliable on the shared LoRa bus; max_files=3 keeps the VFS footprint
  // small (dir + at most 2 files).
  // Ladder of (settle, clock) attempts. The first three use the fast 4 MHz the
  // shared LoRa bus handles well; a cold / cheap card that won't wake at 4 MHz
  // then gets progressively longer settles AND a lower clock (1 MHz, then
  // 400 kHz) — many such cards only complete their power-up handshake at a slow
  // clock, and previously needed a physical reinsert (power-cycle) to mount. A
  // healthy card mounts on attempt 1 and returns immediately; only a stubborn
  // one walks the whole ladder (~2.7 s worst case, loop WDT dropped below). The
  // clock that succeeds becomes the operating clock — slower is fine here (the
  // file list + a settings backup are small; map tiles live on internal flash).
  static const struct { uint16_t settle_ms; uint32_t hz; } kMountLadder[] = {
    {  40, 4000000 }, { 120, 4000000 }, { 200, 4000000 },
    { 300, 1000000 }, { 450, 1000000 }, { 650,  400000 }, { 900, 400000 },
  };
  const int kAttempts = (int)(sizeof(kMountLadder) / sizeof(kMountLadder[0]));
  bool mounted = false;
  disableLoopWDT();
  for (int attempt = 0; attempt < kAttempts; ++attempt) {
    SD.end();
    delay(kMountLadder[attempt].settle_ms);
    if (SD.begin(PIN_SD_CS, *spi, kMountLadder[attempt].hz, "/sd", 3) && SD.cardType() != CARD_NONE) {
      mounted = true;
      break;
    }
  }
  enableLoopWDT();
  if (mounted) {
    s_sd_mounted = true;
    s_sd_size = SD.cardSize();
    s_sd_retry_after_ms = 0;                    // clear any prior backoff
    return true;
  }
  SD.end();                                     // clean up on failure
  s_sd_retry_after_ms = millis() + 10000;       // back off so we don't hammer the card
  return false;
}
static void fmSdUnmount() {
  if (s_sd_mounted) { SD.end(); s_sd_mounted = false; s_sd_size = 0; }
  s_sd_retry_after_ms = 0;   // a reinsert should be able to mount right away
}
static void fmSdClickCb(lv_event_t* e) {
  // SHORT_CLICKED (not CLICKED) so a long-press — which formats the card — does
  // not also fall through and open it.
  if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
  if (s_sd_mounted) fmOpenStorage(&SD, "SD", "/");
}

// Full-screen "formatting" notice. The display shares the SPI bus and f_mkfs
// blocks the loop, so the screen can't animate during the format — this notice
// is painted first (see the deferred run in UITask::loop) and held frozen until
// the format returns.
static void fmShowBusyOverlay(const char* msg) {
  if (s_fm_fmt_overlay) return;
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_fm_fmt_overlay = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_fm_fmt_overlay);
  lv_obj_set_size(s_fm_fmt_overlay, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_fm_fmt_overlay, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_fm_fmt_overlay, lv_color_hex(0x0E1216), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_fm_fmt_overlay, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_fm_fmt_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* l = lv_label_create(s_fm_fmt_overlay);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, sw - 36);
  lv_label_set_text(l, msg);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(l, lv_color_hex(0xFFCC66), LV_PART_MAIN);
  lv_obj_set_style_text_font(l, &g_font_14, LV_PART_MAIN);
  lv_obj_center(l);
}
static void fmHideFormatOverlay() {
  if (s_fm_fmt_overlay) { lv_obj_del(s_fm_fmt_overlay); s_fm_fmt_overlay = nullptr; }
}

// Confirm callback: paint the formatting notice, then defer the (blocking)
// f_mkfs to UITask::loop so the notice is on-screen before the loop freezes.
static void fmSdDoFormat() {
  fmShowBusyOverlay("Formatting SD as MESHCOMOD (FAT32)\n\n"
                    "Creates the core folders too.\n"
                    "Do NOT power off, disconnect,\nor remove the card.\n\n"
                    "This can take up to a minute...");
  s_sd_format_pending = 2;
}
// Tap on an unmounted SD row: try to mount, and if the card is unreadable
// (e.g. exFAT, which this build can't read — only FAT16/FAT32) offer to format.
static void fmSdMountOrFormatCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (fmSdTryMount()) { fmShowRoots(); return; }
  showConfirm("Format SD as MESHCOMOD (FAT32)?\nAll data on the card will be erased.",
              "Format", fmSdDoFormat);
}

// Seed the core folder layout on a freshly-formatted MESHCOMOD card. These back
// the recovery/launcher: BINS (flashable firmware), RECBCK (recovery backups),
// SETTINGS (exported config), plus MAPS/LOGS. Idempotent (mkdir-if-absent), so
// it's also safe to re-run on an already-set-up card.
static void sdEnsureMeshcomodFolders() {
  static const char* const folders[] = { "/BINS", "/UPDATES", "/RECBCK", "/SETTINGS", "/MAPS", "/LOGS" };
  for (unsigned i = 0; i < sizeof(folders) / sizeof(folders[0]); ++i)
    if (!SD.exists(folders[i])) SD.mkdir(folders[i]);
  if (!SD.exists("/README.TXT")) {
    File rf = SD.open("/README.TXT", FILE_WRITE);
    if (rf) {
      rf.print("MESHCOMOD recovery card\r\n\r\n"
               "BINS/     firmware images for the recovery launcher\r\n"
               "UPDATES/  firmware update images the launcher applies\r\n"
               "RECBCK/   recovery backups (identity / settings snapshots)\r\n"
               "SETTINGS/ exported settings (meshcore-backup.json)\r\n"
               "MAPS/     offline map tile packs\r\n"
               "LOGS/     diagnostic / packet logs\r\n");
      rf.close();
    }
  }
}

// Set the FAT volume label by hand (the prebuilt ESP-IDF has FF_USE_LABEL=0, so
// f_setlabel isn't linkable): patch the boot-sector BS_VolLab and add/replace
// the root-directory volume-label entry via the Arduino SD raw-sector API. Call
// right after f_mkfs with the card initialized at `pdrv` and the FS NOT mounted
// (so there's no FATFS cache to fight). Best-effort — silently no-ops on any
// layout it doesn't recognise. Handles both MBR-partitioned and superfloppy.
static void sdWriteFatLabel(uint8_t pdrv, const char* label) {
  uint8_t sec[512];
  if (!sd_read_raw(pdrv, sec, 0)) return;
  if (sec[510] != 0x55 || sec[511] != 0xAA) return;
  uint32_t part_lba = 0;
  const bool boot_at_0 = (sec[0] == 0xEB || sec[0] == 0xE9) &&
                         (uint16_t)(sec[11] | (sec[12] << 8)) == 512;
  if (!boot_at_0) {  // MBR: first partition entry @446, LBA-first @ +8 (454)
    part_lba = (uint32_t)sec[454] | ((uint32_t)sec[455] << 8) |
               ((uint32_t)sec[456] << 16) | ((uint32_t)sec[457] << 24);
    if (!part_lba) return;
  }
  uint8_t bs[512];
  if (!sd_read_raw(pdrv, bs, part_lba)) return;
  if (bs[510] != 0x55 || bs[511] != 0xAA) return;
  const uint16_t bps     = bs[11] | (bs[12] << 8);
  const uint8_t  spc     = bs[13];
  const uint16_t rsvd    = bs[14] | (bs[15] << 8);
  const uint8_t  nfats   = bs[16];
  const uint16_t fatsz16 = bs[22] | (bs[23] << 8);
  const uint32_t fatsz32 = (uint32_t)bs[36] | ((uint32_t)bs[37] << 8) |
                           ((uint32_t)bs[38] << 16) | ((uint32_t)bs[39] << 24);
  const uint32_t rootcl  = (uint32_t)bs[44] | ((uint32_t)bs[45] << 8) |
                           ((uint32_t)bs[46] << 16) | ((uint32_t)bs[47] << 24);
  const uint32_t fatsz = fatsz16 ? fatsz16 : fatsz32;
  if (bps != 512 || !spc || !nfats || !fatsz || rootcl < 2) return;  // not the FAT32 we expect

  char lab[11]; memset(lab, ' ', sizeof lab);
  for (int i = 0; i < 11 && label[i]; ++i) {
    char c = label[i]; if (c >= 'a' && c <= 'z') c -= 32; lab[i] = c;
  }
  memcpy(bs + 71, lab, 11);                 // BS_VolLab (FAT32 boot sector)
  sd_write_raw(pdrv, bs, part_lba);

  const uint32_t root_lba = part_lba + rsvd + (uint32_t)nfats * fatsz +
                            (rootcl - 2) * spc;
  uint8_t rd[512];
  if (!sd_read_raw(pdrv, rd, root_lba)) return;
  int slot = -1;
  for (int i = 0; i < 512; i += 32) {
    if (rd[i] == 0x00 || rd[i] == 0xE5) { slot = i; break; }   // free slot
    if (rd[i + 11] == 0x08) { slot = i; break; }               // existing volume label -> replace
  }
  if (slot < 0) return;
  memset(rd + slot, 0, 32);
  memcpy(rd + slot, lab, 11);
  rd[slot + 11] = 0x08;                      // ATTR_VOLUME_ID
  sd_write_raw(pdrv, rd, root_lba);
}

// Long-press the SD card row -> confirm -> explicit reformat (vs the tap, which
// only mounts, or formats an already-unreadable card).
static void fmSdLongPressFormatCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_LONG_PRESSED) return;
  showConfirm("Reformat SD as MESHCOMOD (FAT32)?\nALL data on the card will be erased.",
              "Format", fmSdDoFormat);
}

// ---- File operations (Phase 4a: delete / rename / new folder) ----
struct FmRowData { char name[64]; bool isdir; };

// Build an absolute path for `name` inside the current directory.
static void fmFullPath(const char* name, char* out, size_t outsz) {
  if (strcmp(s_fm_path, "/") == 0) snprintf(out, outsz, "/%s", name);
  else                             snprintf(out, outsz, "%s/%s", s_fm_path, name);
}

// Recursively delete a file or directory tree.
static void fmRmRecursive(fs::FS* fs, const char* path) {
  File d = fs->open(path);
  const bool isdir = d && d.isDirectory();
  if (isdir) {
    File e = d.openNextFile();
    while (e) {
      const char* full = e.name();
      const char* base = strrchr(full, '/');
      base = base ? base + 1 : full;
      const bool cdir = e.isDirectory();
      char child[200];
      if (base[0]) snprintf(child, sizeof child, "%s/%s", path, base);
      e.close();
      if (base[0]) { if (cdir) fmRmRecursive(fs, child); else fs->remove(child); }
      e = d.openNextFile();
    }
    d.close();
    fs->rmdir(path);
  } else {
    if (d) d.close();
    fs->remove(path);
  }
}

// ----- text-input modal (rename / new folder) -----
static void fmPromptClose() {
  if (s_fm_prompt) {
    hideKb();
    lv_obj_del(s_fm_prompt);
    s_fm_prompt = nullptr;
    s_fm_prompt_ta = nullptr;
  }
}
static void fmPromptOkCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (!s_fm_prompt_ta) { fmPromptClose(); return; }
  kbMirrorSyncToReal();
  char buf[80];
  const char* t = lv_textarea_get_text(s_fm_prompt_ta);
  snprintf(buf, sizeof buf, "%s", t ? t : "");
  void (*cb)(const char*) = s_fm_prompt_cb;
  fmPromptClose();
  if (cb && buf[0]) cb(buf);
}
static void fmPromptCancelCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmPromptClose();
}
static void fmTextPrompt(const char* title, const char* initial, void (*cb)(const char*)) {
  fmPromptClose();
  s_fm_prompt_cb = cb;
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_fm_prompt = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_fm_prompt);
  lv_obj_set_size(s_fm_prompt, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_fm_prompt, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_fm_prompt, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_fm_prompt, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_fm_prompt, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* card = lv_obj_create(s_fm_prompt);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, sw - 30, 124);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 8);
  styleSurface(card, COLOR_PANEL, 10);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x2A2E33), LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* tl = lv_label_create(card);
  lv_label_set_text(tl, title);
  lv_obj_set_style_text_color(tl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(tl, &g_font_14, LV_PART_MAIN);
  lv_obj_align(tl, LV_ALIGN_TOP_LEFT, 2, 0);

  s_fm_prompt_ta = lv_textarea_create(card);
  lv_obj_set_size(s_fm_prompt_ta, sw - 30 - 20, 32);
  lv_obj_align(s_fm_prompt_ta, LV_ALIGN_TOP_MID, 0, 24);
  styleCard(s_fm_prompt_ta);
  lv_textarea_set_one_line(s_fm_prompt_ta, true);
  lv_textarea_set_max_length(s_fm_prompt_ta, 63);
  lv_obj_set_style_text_font(s_fm_prompt_ta, &g_font_14, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_fm_prompt_ta, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  if (initial) lv_textarea_set_text(s_fm_prompt_ta, initial);
  attachSettingsTaEvents(s_fm_prompt_ta);

  lv_obj_t* bc = lv_btn_create(card);
  lv_obj_set_size(bc, 80, 32);
  lv_obj_align(bc, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  styleButton(bc);
  lv_obj_set_style_bg_color(bc, lv_color_hex(0x3A4A5C), LV_PART_MAIN);
  lv_obj_add_event_cb(bc, fmPromptCancelCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lc = lv_label_create(bc); lv_label_set_text(lc, "Cancel"); lv_obj_center(lc);

  lv_obj_t* bo = lv_btn_create(card);
  lv_obj_set_size(bo, 80, 32);
  lv_obj_align(bo, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  styleButton(bo);
  lv_obj_set_style_bg_color(bo, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_add_event_cb(bo, fmPromptOkCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lo = lv_label_create(bo); lv_label_set_text(lo, "OK"); lv_obj_center(lo);

  if (g_lv.keyboard) kbMirrorBind(s_fm_prompt_ta);
}

// ----- operations (act on s_fm_sel_name in the current dir) -----
static void fmDoDelete() {
  if (!s_fm_fs || !s_fm_sel_name[0]) return;
  char path[200];
  fmFullPath(s_fm_sel_name, path, sizeof path);
  if (s_fm_sel_isdir) fmRmRecursive(s_fm_fs, path);
  else                s_fm_fs->remove(path);
  if (g_lv.task) g_lv.task->showAlert("Deleted", 1200);
  fmRefresh();
}
static void fmRenameApply(const char* newname) {
  if (!s_fm_fs || !s_fm_sel_name[0] || !newname[0]) return;
  char oldp[200], newp[200];
  fmFullPath(s_fm_sel_name, oldp, sizeof oldp);
  fmFullPath(newname,       newp, sizeof newp);
  bool ok = s_fm_fs->rename(oldp, newp);
  if (g_lv.task) g_lv.task->showAlert(ok ? "Renamed" : "Rename failed", ok ? 1200 : 1600);
  fmRefresh();
}
static void fmNewFolderApply(const char* name) {
  if (!s_fm_fs || !name[0]) return;
  if (s_fm_fs == &SPIFFS) { if (g_lv.task) g_lv.task->showAlert("Internal has no folders", 1800); return; }
  char p[200];
  fmFullPath(name, p, sizeof p);
  bool ok = s_fm_fs->mkdir(p);
  if (g_lv.task) g_lv.task->showAlert(ok ? "Folder created" : "mkdir failed", ok ? 1200 : 1600);
  fmRefresh();
}
static void fmNewFileApply(const char* name) {
  if (!s_fm_fs || !name[0]) return;
  char p[200];
  fmFullPath(name, p, sizeof p);
  if (s_fm_fs->exists(p)) { if (g_lv.task) g_lv.task->showAlert("Already exists", 1600); return; }
  File f = s_fm_fs->open(p, "w");          // create an empty file
  if (!f) { if (g_lv.task) g_lv.task->showAlert("Create failed", 1600); return; }
  f.close();
  fmRefresh();
  fmOpenEditor(name);                      // jump straight into the editor
}

// Stream a single file src -> dst (any fs to any fs). feedLoopWDT each chunk so
// a large file can't trip the watchdog mid-copy.
static bool fmCopyFile(fs::FS* sf, const char* sp, fs::FS* df, const char* dp) {
  File in = sf->open(sp, "r");
  if (!in) return false;
  File out = df->open(dp, "w");
  if (!out) { in.close(); return false; }
  static uint8_t buf[2048];
  bool ok = true;
  int n;
  while ((n = in.read(buf, sizeof buf)) > 0) {
    if (out.write(buf, (size_t)n) != (size_t)n) { ok = false; break; }
    feedLoopWDT();
  }
  out.close();
  in.close();
  return ok;
}
// Recursively copy a file or directory tree src -> dst.
static bool fmCopyPath(fs::FS* sf, const char* sp, fs::FS* df, const char* dp, bool isdir) {
  if (!isdir) return fmCopyFile(sf, sp, df, dp);
  df->mkdir(dp);
  File d = sf->open(sp);
  if (!d) return false;
  bool ok = true;
  File e = d.openNextFile();
  while (e) {
    const char* full = e.name();
    const char* base = strrchr(full, '/');
    base = base ? base + 1 : full;
    const bool cdir = e.isDirectory();
    char cs[200], cd[200];
    if (base[0]) { snprintf(cs, sizeof cs, "%s/%s", sp, base); snprintf(cd, sizeof cd, "%s/%s", dp, base); }
    e.close();
    if (base[0]) ok = fmCopyPath(sf, cs, df, cd, cdir) && ok;
    e = d.openNextFile();
  }
  d.close();
  return ok;
}
// Perform the pending paste (copy or move) from s_fm_clip into the current dir.
// Returns true on success. Runs from UITask::loop with the loop WDT disabled.
static bool fmDoPaste() {
  if (!s_fm_clip.active || !s_fm_fs) return false;

  // Source's parent dir — used to detect "paste into the same folder".
  char srcparent[200];
  snprintf(srcparent, sizeof srcparent, "%s", s_fm_clip.path);
  char* sl = strrchr(srcparent, '/');
  if (sl == srcparent) srcparent[1] = '\0';
  else if (sl)         *sl = '\0';
  const bool same_dir = (s_fm_clip.fs == s_fm_fs) && (strcmp(srcparent, s_fm_path) == 0);
  if (s_fm_clip.is_cut && same_dir) { s_fm_clip.active = false; return true; }   // already here

  // Pick a non-colliding destination name.
  char name[80];
  char dst[200];
  snprintf(name, sizeof name, "%s", s_fm_clip.name);
  fmFullPath(name, dst, sizeof dst);
  for (int k = 1; k < 1000 && s_fm_fs->exists(dst); ++k) {
    snprintf(name, sizeof name, "%s-%d", s_fm_clip.name, k);
    fmFullPath(name, dst, sizeof dst);
  }

  bool ok;
  if (s_fm_clip.is_cut && s_fm_clip.fs == s_fm_fs) {
    ok = s_fm_fs->rename(s_fm_clip.path, dst);          // same-volume move = rename
    if (!ok) {                                          // fallback: copy then delete
      ok = fmCopyPath(s_fm_clip.fs, s_fm_clip.path, s_fm_fs, dst, s_fm_clip.isdir);
      if (ok) fmRmRecursive(s_fm_clip.fs, s_fm_clip.path);
    }
  } else {
    ok = fmCopyPath(s_fm_clip.fs, s_fm_clip.path, s_fm_fs, dst, s_fm_clip.isdir);
    if (ok && s_fm_clip.is_cut) fmRmRecursive(s_fm_clip.fs, s_fm_clip.path);
  }
  if (s_fm_clip.is_cut) s_fm_clip.active = false;        // a move consumes the clipboard
  return ok;
}

// ----- action sheet -----
static lv_obj_t* fmActionBtn(lv_obj_t* parent, const char* text, lv_event_cb_t cb, uint32_t bg) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_width(b, lv_pct(100));
  lv_obj_set_height(b, 34);
  styleButton(b);
  lv_obj_set_style_bg_color(b, lv_color_hex(bg), LV_PART_MAIN);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  return b;
}
static void fmCloseActions() {
  if (s_fm_actions) { lv_obj_del_async(s_fm_actions); s_fm_actions = nullptr; }
}
static void fmActRenameCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmCloseActions();
  fmTextPrompt("Rename", s_fm_sel_name, fmRenameApply);
}
static void fmActDeleteCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmCloseActions();
  char m[110];
  snprintf(m, sizeof m, "Delete %s%s?%s", s_fm_sel_isdir ? "folder " : "", s_fm_sel_name,
           s_fm_sel_isdir ? "\n(and everything inside)" : "");
  showConfirm(m, "Delete", fmDoDelete);
}
static void fmActNewFolderCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmCloseActions();
  fmTextPrompt("New folder name", "", fmNewFolderApply);
}
static void fmActNewFileCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmCloseActions();
  fmTextPrompt("New file name", "note.txt", fmNewFileApply);
}
static void fmClipSet(bool cut) {
  s_fm_clip.fs = s_fm_fs;
  fmFullPath(s_fm_sel_name, s_fm_clip.path, sizeof s_fm_clip.path);
  snprintf(s_fm_clip.name, sizeof s_fm_clip.name, "%s", s_fm_sel_name);
  s_fm_clip.isdir  = s_fm_sel_isdir;
  s_fm_clip.is_cut = cut;
  s_fm_clip.active = true;
  if (g_lv.task) g_lv.task->showAlert(cut ? "Cut - Paste in a folder" : "Copied - Paste in a folder", 1600);
}
static void fmActCopyCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmCloseActions();
  fmClipSet(false);
}
static void fmActCutCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmCloseActions();
  fmClipSet(true);
}
static void fmActPasteCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmCloseActions();
  if (!s_fm_clip.active || !s_fm_fs) return;
  fmShowBusyOverlay(s_fm_clip.is_cut ? "Moving...\n\nDo NOT power off or\nremove the card."
                                     : "Copying...\n\nDo NOT power off or\nremove the card.");
  s_fm_paste_pending = 2;
}
// name==nullptr -> folder-level menu only (used by the ".." row).
static void fmActToggleHiddenCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  s_fm_show_hidden = !s_fm_show_hidden;
  fmCloseActions();
  fmRefresh();
}

static void fmOpenActions(const char* name, bool isdir) {
  fmCloseActions();
  if (name) { snprintf(s_fm_sel_name, sizeof s_fm_sel_name, "%s", name); s_fm_sel_isdir = isdir; }
  else      { s_fm_sel_name[0] = '\0'; s_fm_sel_isdir = false; }

  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_fm_actions = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_fm_actions);
  lv_obj_set_size(s_fm_actions, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_fm_actions, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_fm_actions, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_fm_actions, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_fm_actions, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_fm_actions, [](lv_event_t* ev) {
    if (lv_event_get_code(ev) != LV_EVENT_CLICKED) return;
    lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
    fmCloseActions();
  }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* card = lv_obj_create(s_fm_actions);
  lv_obj_remove_style_all(card);
  lv_obj_set_width(card, sw - 50);
  lv_obj_set_height(card, LV_SIZE_CONTENT);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  styleSurface(card, COLOR_PANEL, 10);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x2A2E33), LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
  lv_obj_set_style_max_height(card, (sh - STATUSBAR_H) - 16, LV_PART_MAIN);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, 5, LV_PART_MAIN);
  lv_obj_set_scroll_dir(card, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);

  lv_obj_t* tl = lv_label_create(card);
  lv_label_set_long_mode(tl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(tl, lv_pct(100));
  lv_label_set_text(tl, name ? name : "Folder");
  lv_obj_set_style_text_color(tl, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(tl, &g_font_12, LV_PART_MAIN);

  if (name) {
    fmActionBtn(card, "Rename", fmActRenameCb, 0x2B3440);
    fmActionBtn(card, "Copy",   fmActCopyCb,   0x2B3440);
    fmActionBtn(card, "Cut",    fmActCutCb,    0x2B3440);
    fmActionBtn(card, "Delete", fmActDeleteCb, 0x5A2D2D);
  } else {
    fmActionBtn(card, "New file",   fmActNewFileCb,   0x2B3440);
    fmActionBtn(card, "New folder", fmActNewFolderCb, 0x2B3440);
#if defined(ESP32)
    if (s_fm_fs == &SPIFFS)   // Internal only: toggle MeshCore's hidden system files
      fmActionBtn(card, s_fm_show_hidden ? "Hide system files" : "Show system files",
                  fmActToggleHiddenCb, 0x2B3440);
#endif
  }
  if (s_fm_clip.active) {
    fmActionBtn(card, s_fm_clip.is_cut ? "Paste (move)" : "Paste (copy)", fmActPasteCb, 0x2D4A2D);
  }
}

// ----- text editor (Phase 5) -----
static const size_t FM_EDIT_MAX = 8192;   // size cap for on-device editing
static void fmEditorClose() {
  if (!s_editor_root) return;
  if (g_lv.keyboard) lv_keyboard_set_textarea(g_lv.keyboard, nullptr);
  lv_obj_del(s_editor_root);
  s_editor_root = nullptr;
  s_editor_ta = nullptr;
}
static void fmEditorCancelCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmEditorClose();
}
static void fmEditorSaveCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (!s_editor_ta || !s_fm_fs) { fmEditorClose(); return; }
  const char* txt = lv_textarea_get_text(s_editor_ta);
  size_t len = txt ? strlen(txt) : 0;
  File f = s_fm_fs->open(s_editor_path, "w");
  bool ok = false;
  if (f) { ok = (f.write((const uint8_t*)txt, len) == len); f.close(); }
  if (g_lv.task) g_lv.task->showAlert(ok ? "Saved" : "Save failed", ok ? 1300 : 1800);
  fmEditorClose();
  fmRefresh();
}
static void fmOpenEditor(const char* name) {
  if (!s_fm_fs || !name || !name[0]) return;
  char path[200];
  fmFullPath(name, path, sizeof path);
  File f = s_fm_fs->open(path, "r");
  if (!f) { if (g_lv.task) g_lv.task->showAlert("Cannot open file", 1500); return; }
  size_t sz = f.size();
  if (sz > FM_EDIT_MAX) { f.close(); if (g_lv.task) g_lv.task->showAlert("Too large to edit (>8 KB)", 2200); return; }
  char* buf = (char*)heap_caps_malloc(sz + 1, MALLOC_CAP_SPIRAM);
  if (!buf) buf = (char*)malloc(sz + 1);
  if (!buf) { f.close(); if (g_lv.task) g_lv.task->showAlert("Out of memory", 1500); return; }
  size_t rd = f.readBytes(buf, sz);
  buf[rd] = '\0';
  f.close();
  snprintf(s_editor_path, sizeof s_editor_path, "%s", path);

  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_editor_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_editor_root);
  lv_obj_set_size(s_editor_root, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_editor_root, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_editor_root, lv_color_hex(COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_editor_root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_editor_root, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* fn = lv_label_create(s_editor_root);
  lv_label_set_long_mode(fn, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(fn, 6, 9);
  lv_obj_set_width(fn, sw - 6 - 112);
  lv_label_set_text(fn, name);
  lv_obj_set_style_text_font(fn, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(fn, lv_color_hex(COLOR_SUB), LV_PART_MAIN);

  lv_obj_t* save = lv_btn_create(s_editor_root);
  lv_obj_set_size(save, 58, 28);
  lv_obj_align(save, LV_ALIGN_TOP_RIGHT, -50, 4);
  styleButton(save);
  lv_obj_set_style_bg_color(save, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_add_event_cb(save, fmEditorSaveCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* sl = lv_label_create(save); lv_label_set_text(sl, "Save");
  lv_obj_set_style_text_font(sl, &g_font_12, LV_PART_MAIN); lv_obj_center(sl);

  lv_obj_t* cancel = lv_btn_create(s_editor_root);
  lv_obj_set_size(cancel, 44, 28);
  lv_obj_align(cancel, LV_ALIGN_TOP_RIGHT, -3, 4);
  styleButton(cancel);
  lv_obj_set_style_bg_color(cancel, lv_color_hex(0x3A4A5C), LV_PART_MAIN);
  lv_obj_add_event_cb(cancel, fmEditorCancelCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* cl = lv_label_create(cancel); lv_label_set_text(cl, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_font(cl, &g_font_12, LV_PART_MAIN); lv_obj_center(cl);

  s_editor_ta = lv_textarea_create(s_editor_root);
  lv_obj_set_size(s_editor_ta, sw - 8, (sh - STATUSBAR_H) - 38 - 4);
  lv_obj_set_pos(s_editor_ta, 4, 36);
  lv_textarea_set_one_line(s_editor_ta, false);
  lv_textarea_set_max_length(s_editor_ta, FM_EDIT_MAX);
  styleCard(s_editor_ta);
  lv_obj_set_style_text_font(s_editor_ta, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_editor_ta, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_textarea_set_text(s_editor_ta, buf);
  free(buf);

  // Bind the physical keyboard directly (no one-line mirror); the on-screen
  // keyboard stays hidden on the T-Deck, handleHwKey routes keys to this ta.
  if (g_lv.keyboard) {
    s_kb_bind_ta = nullptr;
    if (s_kb_mirror_root) lv_obj_add_flag(s_kb_mirror_root, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(g_lv.keyboard, s_editor_ta);
  }
}

// ---- Read-only image viewer (PNG / JPEG; decoders enabled in lv_conf.h) -----
static const size_t FM_IMG_MAX = 4u * 1024 * 1024;   // encoded-file read cap

static bool fmIsImage(const char* name) {
  if (!name) return false;
  const char* dot = strrchr(name, '.');
  if (!dot) return false;
  return !strcasecmp(dot, ".png")  || !strcasecmp(dot, ".jpg") ||
         !strcasecmp(dot, ".jpeg") || !strcasecmp(dot, ".sjpg");
}

// Defined further down (with the map tile code); used here by the image viewer.
static uint8_t* decodeJpegToRgb565(const uint8_t* jpeg, size_t jpeg_len,
                                   int* out_w, int* out_h);

static void fmImageClose() {
  // Sync delete (not async): the lv_img references s_fm_img_buf, so the widget
  // must be gone before we free the buffer it draws from.
  if (s_fm_img_root) { lv_obj_del(s_fm_img_root); s_fm_img_root = nullptr; }  // also deletes the children below
  if (s_fm_img_buf)  { lvglPsramFree(s_fm_img_buf); s_fm_img_buf = nullptr; }   // decoded RGB565 (lvglPsramAlloc)
  s_fm_img_widget = s_fm_img_hdr = s_fm_img_close = s_fm_img_full = s_fm_img_hint = nullptr;
  s_fm_img_fs = false;
  if (g_statusbar.root) lv_obj_clear_flag(g_statusbar.root, LV_OBJ_FLAG_HIDDEN);  // ensure it's back
}
static void fmImageCloseCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);   // swallow trailing click
  fmImageClose();
}

static void fmShowObj(lv_obj_t* o, bool show) {
  if (!o) return;
  if (show) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  else      lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// Position the image + chrome for the current mode. Windowed: slim header with
// the close/full buttons, image scaled to fit below it (never upscaled). Full
// screen: chrome hidden, image fitted to the whole display, a "tap to exit"
// hint at the bottom.
static void fmImageRelayout() {
  if (!s_fm_img_root || !s_fm_img_widget) return;
  const int w = s_fm_img_w, h = s_fm_img_h;
  if (w <= 0 || h <= 0) return;
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  const bool fs = s_fm_img_fs;

  fmShowObj(s_fm_img_hdr,   !fs);
  fmShowObj(s_fm_img_close, !fs);
  fmShowObj(s_fm_img_full,  !fs);
  fmShowObj(s_fm_img_hint,   fs);
  // The status bar lives on lv_layer_sys (above this overlay), so hide it
  // outright for a true full-screen image; restore it otherwise.
  fmShowObj(g_statusbar.root, !fs);

  if (fs) {
    lv_obj_set_pos(s_fm_img_root, 0, 0);
    lv_obj_set_size(s_fm_img_root, sw, sh);
    // Fit the full display, preserving aspect (letterbox); allow upscaling.
    uint32_t zx = (uint32_t)sw * 256u / (uint32_t)w;
    uint32_t zy = (uint32_t)sh * 256u / (uint32_t)h;
    uint32_t zoom = (zx < zy) ? zx : zy;
    if (zoom < 1)    zoom = 1;
    if (zoom > 1024) zoom = 1024;
    lv_img_set_zoom(s_fm_img_widget, (uint16_t)zoom);
    lv_obj_align(s_fm_img_widget, LV_ALIGN_CENTER, 0, 0);
  } else {
    lv_obj_set_pos(s_fm_img_root, 0, STATUSBAR_H);
    lv_obj_set_size(s_fm_img_root, sw, sh - STATUSBAR_H);
    const int hdr_h = 30;
    uint32_t zx = (uint32_t)sw * 256u / (uint32_t)w;
    uint32_t zy = (uint32_t)((sh - STATUSBAR_H) - hdr_h) * 256u / (uint32_t)h;
    uint32_t zoom = (zx < zy) ? zx : zy;
    if (zoom > 256) zoom = 256;   // never upscale in windowed mode
    if (zoom < 1)   zoom = 1;
    lv_img_set_zoom(s_fm_img_widget, (uint16_t)zoom);
    lv_obj_align(s_fm_img_widget, LV_ALIGN_CENTER, 0, hdr_h / 2);
  }
}
static void fmImageFullCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
  s_fm_img_fs = true;
  fmImageRelayout();
}
static void fmImageRootClickCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (!s_fm_img_fs) return;          // windowed: background taps do nothing
  s_fm_img_fs = false;               // full screen: any tap returns to windowed
  fmImageRelayout();
}

static void fmOpenImage(const char* name) {
  if (!s_fm_fs || !name || !name[0]) return;
  char path[200];
  fmFullPath(name, path, sizeof path);
  File f = s_fm_fs->open(path, "r");
  if (!f) { if (g_lv.task) g_lv.task->showAlert("Cannot open file", 1500); return; }
  size_t sz = f.size();
  if (sz == 0 || sz > FM_IMG_MAX) {
    f.close();
    if (g_lv.task) g_lv.task->showAlert(sz ? "Image too large (>4 MB)" : "Empty file", 2000);
    return;
  }
  uint8_t* enc = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
  if (!enc) enc = (uint8_t*)malloc(sz);
  if (!enc) { f.close(); if (g_lv.task) g_lv.task->showAlert("Out of memory", 1500); return; }
  size_t rd = f.readBytes((char*)enc, sz);
  f.close();

  // This board's lv_png decoder produces RGB565 noise, so PNG can't be shown.
  // Detect the PNG signature and say so instead of rendering garbage.
  if (rd >= 4 && enc[0] == 0x89 && enc[1] == 'P' && enc[2] == 'N' && enc[3] == 'G') {
    free(enc);
    if (g_lv.task) g_lv.task->showAlert("PNG isn't supported here\nUse a JPEG", 2600);
    return;
  }

  // Decode the JPEG to a full RGB565 buffer up front. LVGL's SJPG decoder only
  // exposes a line-by-line reader, which a scaled/zoomed lv_img draw can't use
  // (it needs the whole image in memory) — that renders solid black. Decoding
  // to a TRUE_COLOR buffer is the same path the map tiles use, and it scales.
  int dw = 0, dh = 0;
  uint8_t* rgb = decodeJpegToRgb565(enc, rd, &dw, &dh);
  free(enc);             // encoded bytes no longer needed once decoded
  if (!rgb || dw <= 0 || dh <= 0) {
    if (rgb) lvglPsramFree(rgb);
    if (g_lv.task) g_lv.task->showAlert("Can't display image\n(JPEG only, <= 1024 px)", 2600);
    return;
  }

  fmImageClose();        // drop any previous viewer (frees its buffer)
  s_fm_img_buf = rgb;    // decoded RGB565 pixels; freed on close
  memset(&s_fm_img_dsc, 0, sizeof s_fm_img_dsc);
  s_fm_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  s_fm_img_dsc.header.w  = (uint32_t)dw;
  s_fm_img_dsc.header.h  = (uint32_t)dh;
  s_fm_img_dsc.data      = s_fm_img_buf;
  s_fm_img_dsc.data_size = (uint32_t)dw * (uint32_t)dh * sizeof(lv_color_t);
  const int w = dw, h = dh;

  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_fm_img_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_fm_img_root);
  lv_obj_set_size(s_fm_img_root, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_fm_img_root, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_fm_img_root, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_fm_img_root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_fm_img_root, LV_OBJ_FLAG_SCROLLABLE);
  // A tap on the backdrop leaves full screen (no-op while windowed).
  lv_obj_add_flag(s_fm_img_root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_fm_img_root, fmImageRootClickCb, LV_EVENT_CLICKED, nullptr);

  // The image itself; zoom + position are applied by fmImageRelayout().
  lv_obj_t* img = lv_img_create(s_fm_img_root);
  lv_img_set_src(img, &s_fm_img_dsc);
  lv_img_set_antialias(img, true);
  lv_img_set_pivot(img, w / 2, h / 2);            // scale around the image centre
  lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);  // let taps fall through to the backdrop

  // Header: filename + native dimensions (windowed only).
  char hdr[88];
  snprintf(hdr, sizeof hdr, "%.48s   %dx%d", name, w, h);
  lv_obj_t* fn = lv_label_create(s_fm_img_root);
  lv_label_set_long_mode(fn, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(fn, 6, 8);
  lv_obj_set_width(fn, sw - 6 - 96);
  lv_label_set_text(fn, hdr);
  lv_obj_set_style_text_font(fn, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(fn, lv_color_hex(COLOR_SUB), LV_PART_MAIN);

  // Close (X), top-right.
  lv_obj_t* close = lv_btn_create(s_fm_img_root);
  lv_obj_set_size(close, 30, 26);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -3, 3);
  styleButton(close);
  lv_obj_set_style_bg_color(close, lv_color_hex(0x3A4A5C), LV_PART_MAIN);
  lv_obj_add_event_cb(close, fmImageCloseCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* cl = lv_label_create(close); lv_label_set_text(cl, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_font(cl, &g_font_12, LV_PART_MAIN); lv_obj_center(cl);

  // Full-screen toggle, just left of the close (X).
  lv_obj_t* full = lv_btn_create(s_fm_img_root);
  lv_obj_set_size(full, 52, 26);
  lv_obj_align(full, LV_ALIGN_TOP_RIGHT, -3 - 30 - 4, 3);
  styleButton(full);
  lv_obj_set_style_bg_color(full, lv_color_hex(0x3A4A5C), LV_PART_MAIN);
  lv_obj_add_event_cb(full, fmImageFullCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* fll = lv_label_create(full); lv_label_set_text(fll, "Full");
  lv_obj_set_style_text_font(fll, &g_font_12, LV_PART_MAIN); lv_obj_center(fll);

  // "tap to exit" hint, shown only in full-screen mode.
  lv_obj_t* hint = lv_label_create(s_fm_img_root);
  lv_label_set_text(hint, "tap to exit full screen");
  lv_obj_set_style_text_font(hint, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(hint, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_color(hint, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(hint, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(hint, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(hint, 3, LV_PART_MAIN);
  lv_obj_set_style_radius(hint, 4, LV_PART_MAIN);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

  s_fm_img_widget = img;
  s_fm_img_hdr    = fn;
  s_fm_img_close  = close;
  s_fm_img_full   = full;
  s_fm_img_hint   = hint;
  s_fm_img_w = w; s_fm_img_h = h;
  s_fm_img_fs = false;
  fmImageRelayout();   // apply the windowed layout (zoom, positions, visibility)

  if (g_lv.keyboard) lv_keyboard_set_textarea(g_lv.keyboard, nullptr);   // no text entry here
}

// ----- row interaction -----
static void fmRowClickCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  FmRowData* rd = (FmRowData*)lv_obj_get_user_data(lv_event_get_target(e));
  if (!rd) return;
  if (rd->isdir)                fmEnterDir(rd->name);
  else if (fmIsImage(rd->name)) fmOpenImage(rd->name);   // images -> read-only viewer
  else                          fmOpenEditor(rd->name);  // text -> editor; long-press -> manage
}
static void fmRowLongPressCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_LONG_PRESSED) return;
  lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);   // swallow trailing click
  FmRowData* rd = (FmRowData*)lv_obj_get_user_data(lv_event_get_target(e));
  if (rd) fmOpenActions(rd->name, rd->isdir);
  else    fmOpenActions(nullptr, false);            // ".." row -> folder actions
}
// Folder-level menu: the "+" header button (CLICKED) and a long-press on the
// list's empty area (LONG_PRESSED). Only meaningful inside a filesystem.
static void fmFolderMenuCb(lv_event_t* e) {
  const lv_event_code_t c = lv_event_get_code(e);
  if (c != LV_EVENT_CLICKED && c != LV_EVENT_LONG_PRESSED) return;
  if (c == LV_EVENT_LONG_PRESSED) { lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a); }
  if (s_fm_fs) fmOpenActions(nullptr, false);
}

// Draw the current entries into the list, applying the active sort + filter.
// (No FS access — works off the cached s_fm_entries so sort/search are instant.)
static void fmRender() {
  if (!s_fm_list) return;
  lv_obj_clean(s_fm_list);
  if (s_fm_path_lbl) {
    char buf[180];
    snprintf(buf, sizeof buf, "%s:%s", s_fm_store, s_fm_path);
    lv_label_set_text(s_fm_path_lbl, buf);
  }

  lv_obj_t* up = lv_list_add_btn(s_fm_list, LV_SYMBOL_LEFT, "..");
  fmStyleRow(up, COLOR_SUB);
  lv_obj_add_event_cb(up, fmUpCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(up, fmRowLongPressCb, LV_EVENT_LONG_PRESSED, nullptr);   // folder actions

  if (s_fm_count > 1) qsort(s_fm_entries, s_fm_count, sizeof(FmEntry), fmCmp);

  int shown = 0;
  for (int i = 0; i < s_fm_count; ++i) {
    FmEntry& en = s_fm_entries[i];
    if (!fmContainsCI(en.name, s_fm_filter)) continue;
    char label[96];
    if (en.isdir) {
      snprintf(label, sizeof label, "%s", en.name);
    } else {
      char sz[16];
      fmFmtSize((size_t)en.size, sz, sizeof sz);
      snprintf(label, sizeof label, "%s   %s", en.name, sz);
    }
    lv_obj_t* row = lv_list_add_btn(s_fm_list, en.isdir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE, label);
    fmStyleRow(row, COLOR_TEXT);
    FmRowData* rd = (FmRowData*)malloc(sizeof(FmRowData));
    if (rd) { snprintf(rd->name, sizeof rd->name, "%s", en.name); rd->isdir = en.isdir; }
    lv_obj_set_user_data(row, rd);
    lv_obj_add_event_cb(row, fmRowFreeCb,      LV_EVENT_DELETE,       nullptr);
    lv_obj_add_event_cb(row, fmRowClickCb,     LV_EVENT_CLICKED,      nullptr);
    lv_obj_add_event_cb(row, fmRowLongPressCb, LV_EVENT_LONG_PRESSED, nullptr);
    ++shown;
  }
  if (shown == 0) {
    lv_obj_t* empty = lv_list_add_text(s_fm_list, s_fm_filter[0] ? "(no matches)" : "(empty)");
    lv_obj_set_style_text_color(empty, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  }
}

// Read the current directory into s_fm_entries, then render.
// MeshCore/touch internal data files, hidden from the Internal view by default
// (toggle via the + menu). The /bl/<hash> contact blobs are the bulk of the
// clutter; the rest are a handful of named stores.
static bool fmIsSystemPath(const char* full) {
  if (!full) return false;
  if (!strncmp(full, "/bl/", 4) || !strcmp(full, "/bl")) return true;
  const char* base = strrchr(full, '/'); base = base ? base + 1 : full;
  static const char* const kSys[] = {
    "contacts3", "channels2", "adv_blobs", "com_prefs", "new_prefs", "node_prefs",
    "regions2", "s_contacts", "packet_log", "log", "identity", "ui_chat_history_v1.bin",
  };
  for (unsigned i = 0; i < sizeof(kSys)/sizeof(kSys[0]); ++i)
    if (!strcmp(base, kSys[i])) return true;
  return false;
}
// OS-metadata cruft on removable FAT/exFAT cards: macOS AppleDouble sidecars
// ("._<name>") and dot-files (.DS_Store, .Spotlight-V100, .Trashes, .fseventsd,
// .TemporaryItems…), the Finder "__MACOSX" ZIP folder, and Windows' "System
// Volume Information". Hidden in the file manager unless "Show system files" is
// on — same toggle that reveals MeshCore's own SPIFFS files.
static bool fmIsHiddenName(const char* base) {
  if (!base || !base[0]) return false;
  if (base[0] == '.') return true;                            // ._* AppleDouble + all dot-files
  if (!strcmp(base, "__MACOSX")) return true;
  if (!strcasecmp(base, "System Volume Information")) return true;
  return false;
}
// Append an entry, de-duplicating by (name,isdir) so synthesised virtual folders collapse.
static void fmAddEntry(const char* name, uint32_t size, bool isdir) {
  if (!name[0] || s_fm_count >= FM_MAX_ENTRIES) return;
  for (int i = 0; i < s_fm_count; ++i)
    if (s_fm_entries[i].isdir == isdir && !strcmp(s_fm_entries[i].name, name)) return;
  FmEntry& en = s_fm_entries[s_fm_count++];
  snprintf(en.name, sizeof en.name, "%s", name);
  en.size = size; en.isdir = isdir;
}

static void fmRefresh() {
  if (!s_fm_list) return;
  if (!s_fm_fs) { fmShowRoots(); return; }
  s_fm_count = 0;
  if (!s_fm_entries) { fmRender(); return; }
#if defined(ESP32)
  const bool flat = (s_fm_fs == &SPIFFS);   // SPIFFS is flat: synthesise folders from path prefixes + hide system files
#else
  const bool flat = false;
#endif
  if (flat) {
    // Prefix of the current virtual folder: "" at root, "lock/" inside /lock, etc.
    char pfx[200];
    if (s_fm_path[0] == '\0' || (s_fm_path[0] == '/' && s_fm_path[1] == '\0')) pfx[0] = '\0';
    else snprintf(pfx, sizeof pfx, "%s/", s_fm_path + 1);
    const size_t pfxlen = strlen(pfx);
    File root = s_fm_fs->open("/");
    if (root) {
      File e = root.openNextFile();
      while (e) {
        const char* full = e.path();   // full path incl. leading '/' (name() is basename-only on this core)
        const uint32_t esz = (uint32_t)e.size();
        if (s_fm_show_hidden || !fmIsSystemPath(full)) {
          const char* rel = (full[0] == '/') ? full + 1 : full;
          if (!strncmp(rel, pfx, pfxlen)) {                 // belongs in the current virtual folder
            const char* sub = rel + pfxlen;
            if (sub[0]) {
              const char* slash = strchr(sub, '/');
              if (slash) {                                  // deeper path -> a sub-folder
                char seg[64]; size_t n = (size_t)(slash - sub);
                if (n >= sizeof seg) n = sizeof seg - 1;
                memcpy(seg, sub, n); seg[n] = '\0';
                fmAddEntry(seg, 0, true);
              } else {
                fmAddEntry(sub, esz, false);                // a file in this folder
              }
            }
          }
        }
        e.close();
        e = root.openNextFile();
      }
      root.close();
    }
  } else {
    File dir = s_fm_fs->open(s_fm_path);                    // SD / FAT: real directory listing
    if (dir) {
      File e = dir.openNextFile();
      while (e && s_fm_count < FM_MAX_ENTRIES) {
        const char* full = e.name();
        const char* base = strrchr(full, '/'); base = base ? base + 1 : full;
        if (s_fm_show_hidden || !fmIsHiddenName(base))
          fmAddEntry(base, (uint32_t)e.size(), e.isDirectory());
        e.close();
        e = dir.openNextFile();
      }
      dir.close();
    }
  }
  fmRender();
}

// Roots screen: list the available storages.
static void fmShowRoots() {
  s_fm_fs = nullptr;
  s_fm_path[0] = '\0';
  s_fm_store[0] = '\0';
  s_fm_count = 0;
  if (!s_fm_list) return;
  lv_obj_clean(s_fm_list);
  if (s_fm_path_lbl) lv_label_set_text(s_fm_path_lbl, "Storage");

  char sub[48], us[16], ts[16];
  fmFmtSize(SPIFFS.usedBytes(),  us, sizeof us);
  fmFmtSize(SPIFFS.totalBytes(), ts, sizeof ts);
  snprintf(sub, sizeof sub, "Internal storage   %s / %s", us, ts);
  lv_obj_t* b = lv_list_add_btn(s_fm_list, LV_SYMBOL_DRIVE, sub);
  fmStyleRow(b, COLOR_TEXT);
  lv_obj_add_event_cb(b, fmInternalClickCb, LV_EVENT_CLICKED, nullptr);

  // Probe the SD only when not in a mount-backoff window, so a persistently
  // unmountable card doesn't re-grind the full retry ladder on every render of
  // this page. Tapping the row below (fmSdMountOrFormatCb) bypasses the gate.
  if ((s_sd_mounted || millis() >= s_sd_retry_after_ms) && fmSdTryMount()) {
    char sdl[48], cs[16];
    fmFmtSize64(s_sd_size, cs, sizeof cs);
    snprintf(sdl, sizeof sdl, "SD card   %s   (hold: format)", cs);
    lv_obj_t* sd = lv_list_add_btn(s_fm_list, LV_SYMBOL_SD_CARD, sdl);
    fmStyleRow(sd, COLOR_TEXT);
    lv_obj_add_event_cb(sd, fmSdClickCb, LV_EVENT_SHORT_CLICKED, nullptr);   // tap = open
    lv_obj_add_event_cb(sd, fmSdLongPressFormatCb, LV_EVENT_LONG_PRESSED, nullptr);  // hold = format
  } else {
    lv_obj_t* sd = lv_list_add_btn(s_fm_list, LV_SYMBOL_SD_CARD, "SD card   (tap to mount/format)");
    fmStyleRow(sd, COLOR_SUB);
    lv_obj_add_event_cb(sd, fmSdMountOrFormatCb, LV_EVENT_CLICKED, nullptr);
  }
}

static void fmSortBtnCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  s_fm_sort = (uint8_t)((s_fm_sort + 1) % 4);
  if (s_fm_sort_lbl) lv_label_set_text(s_fm_sort_lbl, k_fm_sort_names[s_fm_sort]);
  fmRender();
}

static void fmSearchChangedCb(lv_event_t* e) {
  (void)e;
  if (!s_fm_search_ta) return;
  const char* t = lv_textarea_get_text(s_fm_search_ta);
  snprintf(s_fm_filter, sizeof s_fm_filter, "%s", t ? t : "");
  fmRender();
}

// Toggle the inline search field over the address bar.
static void fmToggleSearch() {
  if (s_fm_search_ta) {
    hideKb();
    lv_obj_del(s_fm_search_ta);
    s_fm_search_ta = nullptr;
    s_fm_filter[0] = '\0';
    if (s_fm_path_lbl) lv_obj_clear_flag(s_fm_path_lbl, LV_OBJ_FLAG_HIDDEN);
    fmRender();
    return;
  }
  if (!s_fm_path_lbl) return;
  lv_obj_t* parent = lv_obj_get_parent(s_fm_path_lbl);
  lv_coord_t x = lv_obj_get_x(s_fm_path_lbl);
  lv_coord_t y = lv_obj_get_y(s_fm_path_lbl);
  lv_coord_t w = lv_obj_get_width(s_fm_path_lbl);
  lv_obj_add_flag(s_fm_path_lbl, LV_OBJ_FLAG_HIDDEN);

  s_fm_search_ta = lv_textarea_create(parent);
  lv_obj_set_pos(s_fm_search_ta, x, y - 5);
  lv_obj_set_size(s_fm_search_ta, w, 26);
  styleCard(s_fm_search_ta);
  lv_textarea_set_one_line(s_fm_search_ta, true);
  lv_textarea_set_placeholder_text(s_fm_search_ta, "search");
  lv_textarea_set_max_length(s_fm_search_ta, sizeof(s_fm_filter) - 1);
  lv_obj_set_style_text_font(s_fm_search_ta, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_fm_search_ta, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_pad_ver(s_fm_search_ta, 2, LV_PART_MAIN);
  lv_obj_add_event_cb(s_fm_search_ta, fmSearchChangedCb, LV_EVENT_VALUE_CHANGED, nullptr);
  attachSettingsTaEvents(s_fm_search_ta);
  if (g_lv.keyboard) kbMirrorBind(s_fm_search_ta);
}

static void fmBackCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (s_fm_search_ta) { fmToggleSearch(); return; }   // close search first
  if (!s_fm_fs) {   // already on the Storage (roots) page: Back == Home
    closeFullscreenView();
    if (g_lv.tabview) lv_tabview_set_act(g_lv.tabview, 0, LV_ANIM_OFF);
    return;
  }
  fmUp();
}
static void fmSearchBtnCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  fmToggleSearch();
}

// Write the bundled placeholder into SPIFFS /lock/ once, so the "lock" folder
// exists for the (future) lockscreen and is viewable now. SPIFFS is flat, so
// writing "/lock/placeholder.png" implicitly creates the folder.
static void fmSeedLockFolder() {
#if defined(ESP32)
  static bool tried = false;
  if (tried) return;
  tried = true;
  // Drop the stale PNG placeholder from earlier builds (PNG renders as noise here).
  if (SPIFFS.exists("/lock/placeholder.png")) SPIFFS.remove("/lock/placeholder.png");
  // Seed the JPEG placeholder if missing or the wrong size.
  File chk = SPIFFS.open("/lock/placeholder.jpg", "r");
  const bool good = chk && ((size_t)chk.size() == lockscreen_placeholder_jpg_len);
  if (chk) chk.close();
  if (good) return;
  File f = SPIFFS.open("/lock/placeholder.jpg", "w");
  if (!f) return;
  f.write(lockscreen_placeholder_jpg, lockscreen_placeholder_jpg_len);
  f.close();
#endif
}

static void buildFileManager(lv_obj_t* body) {
  fmSeedLockFolder();   // ensure /lock/placeholder.png exists (once per boot)
  // Keep the entry cache in PSRAM — it's 12 KB, and internal DRAM is tight
  // enough that holding it there can push SD mounting into an OOM abort.
  if (!s_fm_entries) {
    s_fm_entries = (FmEntry*)heap_caps_malloc(sizeof(FmEntry) * FM_MAX_ENTRIES, MALLOC_CAP_SPIRAM);
    if (!s_fm_entries) s_fm_entries = (FmEntry*)heap_caps_malloc(sizeof(FmEntry) * FM_MAX_ENTRIES, MALLOC_CAP_8BIT);
  }
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  const lv_coord_t bw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t bh = (lv_disp_get_ver_res(nullptr) - STATUSBAR_H);
  const lv_coord_t HDR_H = 34, BTN_W = 38, BTN_H = 28, HOME_RES = 48;

  // Back button (far left): closes search if open, else goes up a folder.
  lv_obj_t* back = lv_btn_create(body);
  lv_obj_set_size(back, 30, BTN_H);
  lv_obj_set_pos(back, 3, 3);
  styleButton(back);
  lv_obj_set_style_bg_color(back, lv_color_hex(0x1A1B1C), LV_PART_MAIN);
  lv_obj_set_style_pad_all(back, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(back, fmBackCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* backl = lv_label_create(back);
  lv_label_set_text(backl, LV_SYMBOL_LEFT);
  lv_obj_center(backl);

  // "+" button (next to Back): opens the folder menu (New folder / Paste).
  lv_obj_t* add = lv_btn_create(body);
  lv_obj_set_size(add, 30, BTN_H);
  lv_obj_set_pos(add, 36, 3);
  styleButton(add);
  lv_obj_set_style_bg_color(add, lv_color_hex(0x1A1B1C), LV_PART_MAIN);
  lv_obj_set_style_pad_all(add, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(add, fmFolderMenuCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* addl = lv_label_create(add);
  lv_label_set_text(addl, LV_SYMBOL_PLUS);
  lv_obj_center(addl);

  // Find (search) button — rightmost before the floating Home.
  const lv_coord_t find_x = bw - HOME_RES - BTN_W - 3;
  lv_obj_t* find = lv_btn_create(body);
  lv_obj_set_size(find, BTN_W, BTN_H);
  lv_obj_set_pos(find, find_x, 3);
  styleButton(find);
  lv_obj_set_style_bg_color(find, lv_color_hex(0x1A1B1C), LV_PART_MAIN);
  lv_obj_set_style_pad_all(find, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(find, fmSearchBtnCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* findl = lv_label_create(find);
  lv_label_set_text(findl, "Find");
  lv_obj_set_style_text_font(findl, &g_font_12, LV_PART_MAIN);
  lv_obj_center(findl);

  // Sort button — cycles A-Z / Z-A / Size / Type, label shows the mode.
  const lv_coord_t sort_x = find_x - BTN_W - 3;
  lv_obj_t* sort = lv_btn_create(body);
  lv_obj_set_size(sort, BTN_W, BTN_H);
  lv_obj_set_pos(sort, sort_x, 3);
  styleButton(sort);
  lv_obj_set_style_bg_color(sort, lv_color_hex(0x1A1B1C), LV_PART_MAIN);
  lv_obj_set_style_pad_all(sort, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(sort, fmSortBtnCb, LV_EVENT_CLICKED, nullptr);
  s_fm_sort_lbl = lv_label_create(sort);
  lv_label_set_text(s_fm_sort_lbl, k_fm_sort_names[s_fm_sort]);
  lv_obj_set_style_text_font(s_fm_sort_lbl, &g_font_12, LV_PART_MAIN);
  lv_obj_center(s_fm_sort_lbl);

  // Address bar (between the +/Back group and Sort), styled like a URL field.
  const lv_coord_t loc_x = 36 + 30 + 4;   // past Back(3+30) and "+"(36+30)
  const lv_coord_t loc_w = sort_x - 4 - loc_x;
  s_fm_path_lbl = lv_label_create(body);
  lv_label_set_long_mode(s_fm_path_lbl, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(s_fm_path_lbl, loc_x, 6);
  lv_obj_set_width(s_fm_path_lbl, loc_w);
  lv_obj_set_style_text_font(s_fm_path_lbl, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_fm_path_lbl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_fm_path_lbl, lv_color_hex(0x101418), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_fm_path_lbl, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(s_fm_path_lbl, lv_color_hex(0x2A2E33), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_fm_path_lbl, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(s_fm_path_lbl, 5, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(s_fm_path_lbl, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(s_fm_path_lbl, 3, LV_PART_MAIN);
  lv_label_set_text(s_fm_path_lbl, "Storage");

  // Entry list fills the rest.
  s_fm_list = lv_list_create(body);
  lv_obj_set_size(s_fm_list, bw - 8, bh - HDR_H - 2);
  lv_obj_set_pos(s_fm_list, 4, HDR_H);
  lv_obj_set_style_bg_color(s_fm_list, lv_color_hex(COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_fm_list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_fm_list, 0, LV_PART_MAIN);
  // Long-press on the list's empty area opens the folder menu too.
  lv_obj_add_event_cb(s_fm_list, fmFolderMenuCb, LV_EVENT_LONG_PRESSED, nullptr);

  fmShowRoots();
}

static void homeFilesCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t* body = openFullscreenView("Files");
  buildFileManager(body);
}
#endif

static void makeHome(lv_obj_t* tab) {
  // Layout (240 wide × 282 tall): title + heartbeat + battery at top, status
  // lines, TX/RX chart in the middle, Send Advert button at the bottom.
  // No tile grid — bottom tab bar already gives quick access to Chats /
  // Contacts / Settings.
  styleSurface(tab, COLOR_BG);
  lv_obj_set_style_pad_all(tab, 10, LV_PART_MAIN);
  lv_obj_set_scroll_dir(tab, LV_DIR_NONE);
  lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

  // Content width = screen width minus the 10-px tab padding on each side.
  // Tracks rotation (220 portrait / 300 landscape).
  const int cw = tabContentW() - 20;
  // Landscape is short and wide: park the Send-advert button in the empty
  // right-hand strip (top-right) instead of a full-width bottom row, so the
  // TX/RX chart can use the freed vertical space and be ~2x taller.
  const bool home_land = chatLandscape();

  // The previous in-tab status row (heartbeat dot, MESHCOMOD title, clock,
  // battery %, battery icon) is now replaced by the always-visible global
  // status bar on lv_layer_sys. Home body content starts at y=0 inside
  // the tab; the tabview's own y-offset already keeps everything below
  // the status bar.
  s_home_clock      = nullptr;
  s_home_heartbeat  = nullptr;
  s_home_batt_pct   = nullptr;
  s_home_batt_icon  = nullptr;

  g_lv.home_state = lv_label_create(tab);
  lv_label_set_text(g_lv.home_state, "Connecting...");
  lv_obj_set_style_text_color(g_lv.home_state, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_align(g_lv.home_state, LV_ALIGN_TOP_LEFT, 0, 4);

  g_lv.home_stats = lv_label_create(tab);
  lv_label_set_text(g_lv.home_stats, "");
  lv_obj_set_style_text_color(g_lv.home_stats, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  // In landscape, keep the status text clear of the top-right button column.
  lv_obj_set_width(g_lv.home_stats, home_land ? (cw - 110) : cw);
  lv_label_set_long_mode(g_lv.home_stats, LV_LABEL_LONG_WRAP);
  lv_obj_align(g_lv.home_stats, LV_ALIGN_TOP_LEFT, 0, 22);
  // home_dc_label/bar reserved for a future bar widget; meter for now is
  // appended into home_stats so it lives in the same scarce vertical band
  // and doesn't collide with the TX/RX chart legend at y=64.
  g_lv.home_dc_label = nullptr;
  g_lv.home_dc_bar   = nullptr;

  // TX / RX rolling chart. 60 samples, two series (TX green, RX blue).
  // Compact legend strip above the chart shows live totals — updated on every
  // refreshStatusLabels tick. With home_state now at y=36 and home_stats
  // (two-line WRAP) anchored at y=54, the bottom of the DC line sits around
  // y=82. Chart legend therefore starts at y=94 to keep a small gap below
  // the duty-cycle text.
  // home_state at y=4, home_stats (two-line WRAP) at y=22 reaches roughly
  // y=50; chart group starts at y=60 with breathing room. Used to be y=94
  // when the title/clock/battery row lived inside the tab.
  constexpr int chart_y = 60;
  s_home_chart_legend = lv_label_create(tab);
  lv_label_set_text(s_home_chart_legend, "TX 0  /  RX 0");
  lv_obj_set_style_text_color(s_home_chart_legend, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_home_chart_legend, &g_font_12, LV_PART_MAIN);
  lv_obj_align(s_home_chart_legend, LV_ALIGN_TOP_LEFT, 0, chart_y);

#if defined(HAS_TDECK_GT911)
  // Landscape T-Deck: the right column holds Advert + Terminal + Files, so the
  // chart leaves that strip clear.
  const int chart_w = home_land ? (cw - 110) : cw;
#else
  const int chart_w = cw;
#endif
  // Fit the chart in the remaining vertical space: content height minus the
  // tab padding, the chart's top offset, and the Send-advert button + gaps.
  // Portrait keeps the full 96 px; landscape (short screen) shrinks it so the
  // button doesn't run off the bottom.
  const int home_avail = tabContentH() - 20;                 // inside 10-px pad
  // Landscape: button sits in the right column, so the chart runs to the
  // bottom (no reserved button row). Portrait: reserve the button row below.
  int chart_h = home_avail - (chart_y + 16) - 4 - (home_land ? 0 : (8 + 36));
  if (chart_h > 96) chart_h = 96;
  if (chart_h < 28) chart_h = 28;
  s_home_chart = lv_chart_create(tab);
  if (s_home_chart) {
    lv_obj_set_size(s_home_chart, chart_w, chart_h);
    lv_obj_align(s_home_chart, LV_ALIGN_TOP_LEFT, 0, chart_y + 16);
    lv_chart_set_type(s_home_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_home_chart, 60);
    lv_chart_set_update_mode(s_home_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_range(s_home_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 10);
    // No gridlines — keep the chart visually clean.
    lv_chart_set_div_line_count(s_home_chart, 0, 0);
    // Chart sits on pure BG (was COLOR_PANEL which still rendered as a
    // perceptible lighter rectangle on the home tab). Faint 1-px border
    // hints at the chart bounds without painting a brighter rectangle.
    lv_obj_set_style_bg_color(s_home_chart, lv_color_hex(COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_home_chart, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_home_chart, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_home_chart, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_home_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_home_chart, 6, LV_PART_MAIN);
    lv_obj_set_style_size(s_home_chart, 0, LV_PART_INDICATOR);
    s_home_chart_tx = lv_chart_add_series(s_home_chart, lv_color_hex(COLOR_STATUS_OK),
                                          LV_CHART_AXIS_PRIMARY_Y);
    s_home_chart_rx = lv_chart_add_series(s_home_chart, lv_color_hex(0x4F94CD),
                                          LV_CHART_AXIS_PRIMARY_Y);
  }

  // Send Advert button. Portrait: full-width row below the chart. Landscape:
  // a compact button parked in the top-right strip (next to the status text),
  // which is why the chart above was allowed to run full-height.
  const int adv_y = chart_y + 16 + chart_h + 8;
  lv_obj_t* adv = lv_btn_create(tab);
  styleButton(adv);
  lv_obj_set_style_bg_color(adv, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_set_style_bg_color(adv, lv_color_hex(0x3B7039), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(adv, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  if (home_land) {
    lv_obj_set_size(adv, 100, 46);
    lv_obj_align(adv, LV_ALIGN_TOP_LEFT, cw - 100, 4);   // top aligns with the status text
  } else {
    lv_obj_set_size(adv, cw, 36);
    lv_obj_align(adv, LV_ALIGN_TOP_LEFT, 0, adv_y);
  }
  lv_obj_add_event_cb(adv, openAdvertModalCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* adv_l = lv_label_create(adv);
  // Shorter label in the narrow landscape button so it doesn't clip.
  lv_label_set_text(adv_l, home_land ? LV_SYMBOL_UPLOAD "  Advert"
                                      : LV_SYMBOL_UPLOAD "  Send advert");
  lv_obj_set_style_text_font(adv_l, &g_font_14, LV_PART_MAIN);
  lv_obj_center(adv_l);

#if defined(HAS_TDECK_GT911)
  // Terminal + File explorer launchers, stacked under Advert in the right column
  // (same 46-px height + 8-px gap as Advert so the column reads as one set).
  if (home_land) {
    auto make_launcher = [&](const char* label, int ly, lv_event_cb_t cb) {
      lv_obj_t* b = lv_btn_create(tab);
      styleButton(b);
      lv_obj_set_size(b, 100, 46);
      lv_obj_align(b, LV_ALIGN_TOP_LEFT, cw - 100, ly);
      lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
      lv_obj_t* l = lv_label_create(b);
      lv_label_set_text(l, label);
      lv_obj_set_style_text_font(l, &g_font_12, LV_PART_MAIN);
      lv_obj_set_style_text_color(l, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
      lv_obj_center(l);
    };
    make_launcher(">_  Terminal", 58, homeTerminalCb);
    make_launcher(LV_SYMBOL_DIRECTORY "  Files", 112, homeFilesCb);
  }
#endif
}

// ----- Chat list (full-page thread chooser inside a tab) -----
static void makeChatList(lv_obj_t* tab, LvChatPanel& p, bool channel_mode, bool inbox_combined = false) {
  p.channel_mode     = channel_mode;
  p.inbox_combined   = inbox_combined;
  p.detail_open      = false;
  p.overlay          = nullptr;
  p.header_name      = nullptr;
  p.msgs             = nullptr;
  p.composer_row     = nullptr;
  p.composer_ta      = nullptr;

  // Disable tab-level scrolling; the list handles its own scroll.
  lv_obj_set_scroll_dir(tab, LV_DIR_NONE);
  lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);
  // Swiping vertically used to call lv_obj_scroll_by on this tab; that still applied
  // raw scroll even with DIR_NONE. Drop SCROLLABLE so the tab never acts as a scroll
  // container, and reset any stale offset from older builds.
  lv_obj_scroll_to(tab, 0, 0, LV_ANIM_OFF);
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(tab, COLOR_BG, 0);
  lv_obj_set_style_pad_all(tab, 0, LV_PART_MAIN);

  // Inbox tab (the only one that gets a "+" button) has a 32-pixel header
  // row with the add-channel button at the right; the list sits below.
  // The DM-detail panel (channel_mode + non-combined) stays full-height.
  const int hdr_h = inbox_combined ? 32 : 0;
  if (inbox_combined) {
    lv_obj_t* add_btn = lv_btn_create(tab);
    lv_obj_set_size(add_btn, 34, 28);
    lv_obj_set_pos(add_btn, tabContentW() - 34 - 4, 2);
    styleButton(add_btn);
    lv_obj_set_style_bg_color(add_btn, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(add_btn, lv_color_hex(0x3B7039), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(add_btn, chatsAddBtnCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(add_btn);
    lv_label_set_text(l, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, &g_font_16, LV_PART_MAIN);
    lv_obj_center(l);

    // "Mark all as read" (✓) — sits just left of the + button.
    lv_obj_t* mar_btn = lv_btn_create(tab);
    lv_obj_set_size(mar_btn, 34, 28);
    lv_obj_set_pos(mar_btn, tabContentW() - 34 - 4 - 34 - 4, 2);
    styleButton(mar_btn);
    lv_obj_add_event_cb(mar_btn, chatsMarkAllReadBtnCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* ml = lv_label_create(mar_btn);
    lv_label_set_text(ml, LV_SYMBOL_OK);
    lv_obj_set_style_text_color(ml, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(ml, &g_font_14, LV_PART_MAIN);
    lv_obj_center(ml);

    // "Share my contact" QR — mirrors the + button on the LEFT edge so
    // the header reads as [share | ... | add]. Tapping opens a popup with
    // a QR encoding our pubkey + node name, scan-friendly by other
    // meshcomod / MeshCore touch firmware.
    lv_obj_t* qr_btn = lv_btn_create(tab);
    lv_obj_set_size(qr_btn, 34, 28);
    lv_obj_set_pos(qr_btn, 4, 2);
    styleButton(qr_btn);
    lv_obj_add_event_cb(qr_btn, shareMyContactBtnCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* ql = lv_label_create(qr_btn);
    // No bundled QR glyph in Montserrat — LV_SYMBOL_IMAGE reads as a
    // "scan / square code" enough for the purpose. Could be swapped for
    // a custom font subset if it grates.
    lv_label_set_text(ql, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_color(ql, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(ql, &g_font_14, LV_PART_MAIN);
    lv_obj_center(ql);
  }

  p.list_cont = lv_list_create(tab);
  lv_obj_set_size(p.list_cont, tabContentW(), tabContentH() - hdr_h);
  lv_obj_set_pos(p.list_cont, 0, hdr_h);
  styleSurface(p.list_cont, COLOR_BG, 0);
  lv_obj_set_style_border_width(p.list_cont, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(p.list_cont, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(p.list_cont, 1, LV_PART_MAIN);
  lv_obj_add_event_cb(p.list_cont, scrollClampOnEndCb, LV_EVENT_SCROLL_END, nullptr);
}

// ----- Contacts header: segmented filter + overflow menu -----
//
// The four filter segments map to g_lv.contacts_filter values:
//   index 0 "All" -> 0,  1 "★" -> 3,  2 "RPT" -> 1,  3 "Peer" -> 2.
// Tapping a segment sets that filter directly (no toggle-to-clear — "All"
// is its own segment). The active segment is filled; the rest read as
// faint chips. Search / Found / + moved into the "⋯" overflow sheet so
// the filter row stays uncluttered and the list gets maximum height.
static const uint8_t k_contacts_seg_filter[4] = { 0, 3, 1, 2 };
static lv_obj_t*     s_contacts_seg_btns[4]   = { nullptr, nullptr, nullptr, nullptr };
static lv_obj_t*     s_contacts_overflow_root = nullptr;

static void styleContactsSegment(lv_obj_t* b, bool active) {
  if (active) {
    // Filled, brighter — clearly the current view.
    lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_opa(b, LV_OPA_0, LV_PART_MAIN);
  } else {
    // Faint — etched out of the bezel like the other chips.
    lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_10, LV_PART_MAIN);
    lv_obj_set_style_border_color(b, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(b, LV_OPA_40, LV_PART_MAIN);
  }
}

static void updateContactsFilterSegments() {
  for (int i = 0; i < 4; ++i) {
    if (!s_contacts_seg_btns[i]) continue;
    styleContactsSegment(s_contacts_seg_btns[i],
                         g_lv.contacts_filter == k_contacts_seg_filter[i]);
  }
}

static void contactsSegmentCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const uintptr_t f = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  if (f > 3) return;
  g_lv.contacts_filter = static_cast<uint8_t>(f);
  updateContactsFilterSegments();
  refreshContactsList();
  if (g_lv.contacts_list) lv_obj_scroll_to(g_lv.contacts_list, 0, 0, LV_ANIM_OFF);
}

// ---- Overflow ("⋯") sheet: Search / Found / + ----
static void closeContactsOverflowSheet() {
  if (s_contacts_overflow_root) {
    lv_obj_del_async(s_contacts_overflow_root);
    s_contacts_overflow_root = nullptr;
  }
}
static void contactsOverflowDismissCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_indev_t* act = lv_indev_get_act();
  if (act) lv_indev_wait_release(act);
  closeContactsOverflowSheet();
}
static void contactsOverflowSearchCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeContactsOverflowSheet();
  openContactsSearchSheetCb(e);
}
static void contactsOverflowFoundCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeContactsOverflowSheet();
  openDiscoveredModalCb(e);
}
static void contactsOverflowAddCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeContactsOverflowSheet();
  openAddContactModalCb(e);
}
static void openContactsOverflowSheetCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeContactsOverflowSheet();

  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_contacts_overflow_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_contacts_overflow_root);
  lv_obj_set_size(s_contacts_overflow_root, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_contacts_overflow_root, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_contacts_overflow_root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_contacts_overflow_root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_contacts_overflow_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_foreground(s_contacts_overflow_root);
  lv_obj_add_event_cb(s_contacts_overflow_root, contactsOverflowDismissCb, LV_EVENT_CLICKED, nullptr);

  const int card_w = 200, btn_h = 36, btn_gap = 6, title_h = 26, padding = 8;
  const int rows = 3;
  const int card_h = title_h + rows * (btn_h + btn_gap) + padding;
  lv_obj_t* card = lv_obj_create(s_contacts_overflow_root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  // Anchor near the top-right under the header where the "⋯" lives.
  lv_obj_align(card, LV_ALIGN_TOP_RIGHT, -6, 8);
  lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, padding, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "Contacts");
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &g_font_12, LV_PART_MAIN);
  lv_obj_set_pos(title, 0, 0);

  int y = title_h;
  auto mk = [&](const char* label, lv_event_cb_t cb, uint32_t bg) {
    lv_obj_t* b = lv_btn_create(card);
    lv_obj_set_size(b, card_w - 2 * padding, btn_h);
    lv_obj_set_pos(b, 0, y);
    styleButton(b);
    lv_obj_set_style_pad_ver(b, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(b, 10, LV_PART_MAIN);
    if (bg) lv_obj_set_style_bg_color(b, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, &g_font_14, LV_PART_MAIN);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    y += btn_h + btn_gap;
  };
  mk(LV_SYMBOL_EYE_OPEN "  Search", contactsOverflowSearchCb, 0);
  mk(LV_SYMBOL_LIST     "  Found",  contactsOverflowFoundCb,  0);
  mk(LV_SYMBOL_PLUS     "  Add contact", contactsOverflowAddCb, 0);
}

static void makeContactsTab(lv_obj_t* tab) {
  g_lv.contacts_list   = nullptr;
  // Default to the "All" segment — show every contact, ordered by most
  // recently heard (favorites still pin to the top via the sort
  // comparator). The ★ segment is one tap away for the favorites-only view.
  g_lv.contacts_filter = 0;
  g_lv.contacts_search[0] = '\0';
  g_lv.contacts_search_indicator = nullptr;
  for (int i = 0; i < 4; ++i) s_contacts_seg_btns[i] = nullptr;
  s_contacts_overflow_root = nullptr;

  lv_obj_set_scroll_dir(tab, LV_DIR_NONE);
  lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);
  lv_obj_scroll_to(tab, 0, 0, LV_ANIM_OFF);
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(tab, COLOR_BG, 0);
  lv_obj_set_style_pad_all(tab, 0, LV_PART_MAIN);

  lv_obj_t* row = lv_obj_create(tab);
  lv_obj_remove_style_all(row);
  // Row now matches the Chat tab's combined-inbox header (32 px tall, with
  // a 28-tall "+" button inset). Was 44 px previously, which made the
  // Contacts tab feel chunkier than the Chats tab. Full screen width so the
  // SPACE_BETWEEN flex pins the overflow button to the right edge in either
  // orientation.
  lv_obj_set_size(row, tabContentW(), 32);
  lv_obj_set_pos(row, 0, 0);
  lv_obj_set_style_pad_hor(row, 4, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(row, 2, LV_PART_MAIN);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 4, LV_PART_MAIN);

  // Row layout: a wide segmented filter on the left, a "⋯" overflow button
  // on the right. SPACE_BETWEEN pins them to the two edges.
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  constexpr lv_coord_t kChipH = 28;

  // ---- Segmented filter: All / ★ / RPT / Peer ----
  // Four equal segments (flex_grow) so they're big, evenly-sized tap
  // targets. The active segment is filled; tapping one switches the view
  // directly (no hidden toggle/cycle gestures — that was the "hard to
  // operate" complaint).
  lv_obj_t* seg = lv_obj_create(row);
  lv_obj_remove_style_all(seg);
  lv_obj_set_size(seg, 192, kChipH);
  // Grow to fill the row width (minus the overflow button) so the four
  // segments stretch across the screen in landscape instead of leaving a gap.
  lv_obj_set_flex_grow(seg, 1);
  lv_obj_set_flex_flow(seg, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(seg, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(seg, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(seg, 2, LV_PART_MAIN);
  lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);

  auto mk_seg = [&](int idx, const char* label, bool star) {
    lv_obj_t* b = lv_btn_create(seg);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_height(b, kChipH);
    lv_obj_set_style_radius(b, 4, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(b, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(b, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_pad_all(b, 0, LV_PART_MAIN);
    void* ud = reinterpret_cast<void*>(static_cast<uintptr_t>(k_contacts_seg_filter[idx]));
    lv_obj_add_event_cb(b, contactsSegmentCb, LV_EVENT_CLICKED, ud);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, label);
    if (star) {
      lv_obj_set_style_text_font(l, &star_font_14, LV_PART_MAIN);
      lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    } else {
      lv_obj_set_style_text_font(l, &g_font_12, LV_PART_MAIN);
    }
    lv_obj_center(l);
    s_contacts_seg_btns[idx] = b;
  };
  mk_seg(0, "All",            false);
  mk_seg(1, TOUCH_SYM_STAR_BIG, true);
  mk_seg(2, "RPT",            false);
  mk_seg(3, "Peer",           false);
  updateContactsFilterSegments();   // paint the active (favorites) segment

  // ---- "⋯" overflow: Search / Found / Add contact ----
  {
    lv_obj_t* b = lv_btn_create(row);
    lv_obj_set_size(b, 34, kChipH);
    styleButton(b);
    lv_obj_set_style_pad_all(b, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(b, openContactsOverflowSheetCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, LV_SYMBOL_LIST);   // "⋯"-style list/overflow glyph
    lv_obj_set_style_text_font(l, &g_font_16, LV_PART_MAIN);
    lv_obj_center(l);
    // "search active" indicator — a small accent line under the overflow
    // button, since the search control now lives inside this menu. Stays
    // visible whenever a name filter is applied so the operator isn't
    // confused by a short list.
    g_lv.contacts_search_indicator = lv_obj_create(b);
    lv_obj_remove_style_all(g_lv.contacts_search_indicator);
    lv_obj_set_size(g_lv.contacts_search_indicator, 18, 3);
    lv_obj_align(g_lv.contacts_search_indicator, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_bg_color(g_lv.contacts_search_indicator, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_lv.contacts_search_indicator, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(g_lv.contacts_search_indicator, LV_OBJ_FLAG_HIDDEN);
  }

  g_lv.contacts_list = lv_list_create(tab);
  // Row shrunk from 44 to 32 px; bump the list up 12 px to recover the
  // vertical real estate. (Also keeps the contacts list height in sync
  // with the Chats list, which sits below a 32-px header.) Sized from the
  // live content area so it fills the screen in either orientation.
  lv_obj_set_size(g_lv.contacts_list, tabContentW(), tabContentH() - 34);
  lv_obj_set_pos(g_lv.contacts_list, 0, 34);
  styleSurface(g_lv.contacts_list, COLOR_BG, 0);
  lv_obj_set_style_border_width(g_lv.contacts_list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(g_lv.contacts_list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(g_lv.contacts_list, 1, LV_PART_MAIN);
  lv_obj_add_event_cb(g_lv.contacts_list, scrollClampOnEndCb, LV_EVENT_SCROLL_END, nullptr);
}

// ============================================================
// Map tab (Phase 4.1)
// ============================================================
// MVP skeleton: shows the self GPS + contact-with-position count + a
// placeholder where the tile grid will land. The tile renderer, pan/zoom
// and marker layer follow in subsequent steps.
//
// Layout (240 wide × 260 tall above the 38-px tabbar minus 22-px status bar
// = 240×260 usable):
//   - Map canvas: 240×226, anchored to (0,0)
//   - Bottom info strip: 240×34, anchored bottom — shows lat/lon, zoom, and
//     a "no tile pack" badge until /tiles/ exists on SPIFFS.
static lv_obj_t* s_map_canvas      = nullptr;   // full-screen tile surface (behind the tabview)
static lv_obj_t* s_map_page        = nullptr;   // the Map tab page (holds touch catcher + overlay UI)
static lv_obj_t* s_map_touch       = nullptr;   // transparent full-page touch catcher for pan/drag
static lv_obj_t* s_map_info_lbl    = nullptr;   // coords read-out (bottom-left corner)
static lv_obj_t* s_map_count_lbl   = nullptr;   // marker / download count (bottom-right corner)
static lv_obj_t* s_map_status_lbl  = nullptr;

#if defined(ESP32)
// Dedicated LittleFS instance for the map tile pack — mounts the "tiles"
// partition defined in variants/heltec_v4/partitions_tft_touch.csv.
// SEPARATE from the global LittleFS / SPIFFS instances so refreshing the
// tile pack via scripts/build/upload-tiles.py doesn't touch the SPIFFS
// partition that holds /new_prefs (Profile + Radio + Auto-add settings),
// /contacts3, /channels2, and the chat history blob.
//
// formatOnFail=true on first mount because the freshly-flashed partition
// is filled with 0xFF and LittleFS would otherwise refuse to come up.
// After format, subsequent boots find a valid (empty) FS and mount fast.
static fs::LittleFSFS s_tiles_fs;
static bool           s_tiles_fs_ready = false;

// ----- Wi-Fi tile fetcher (Phase 4.1c) -----
//
// When the user is on the Map tab AND Wi-Fi is up, any tile that fails
// to load from the LittleFS cache is queued for download from OSM. A
// FreeRTOS task pinned to core 0 (the Wi-Fi core) drains the queue,
// HTTP-GETs each tile from https://tile.openstreetmap.org/<z>/<x>/<y>.png,
// writes it to LittleFS, and sets s_tile_fetch_dirty. The LVGL refresh
// loop notices the dirty flag and re-renders the map, so freshly-
// downloaded tiles appear without the user having to interact.
//
// OSM tile-usage policy compliance: identifying User-Agent header,
// ~2 req/sec rate cap, no bulk downloads. Each tile is ~10-30 KB; the
// 4.75 MB partition holds a couple hundred.
#if defined(MULTI_TRANSPORT_COMPANION)
struct TileFetchReq { uint8_t z; int32_t x; int32_t y; };
static constexpr int     k_tile_fetch_queue_size = 32;
static QueueHandle_t     s_tile_fetch_queue   = nullptr;
static TaskHandle_t      s_tile_fetch_task    = nullptr;
static volatile bool     s_tile_fetch_dirty   = false;
volatile uint16_t s_tile_fetch_pending = 0;   // non-static: tdeckPlayNotify reads it via extern (above)
static volatile uint16_t s_tile_fetch_ok      = 0;
static volatile uint16_t s_tile_fetch_failed  = 0;
// On-screen heartbeat: bumped each loop iteration of the fetch task so we
// can tell whether it's alive at all (USB serial debug is unreadable on
// this build because the companion binary protocol shares the UART).
// `step` is a single-letter code for *what the task was last doing*:
//   '-' before entry, 'q' waiting for queue, 'g' before http.GET,
//   'r' reading body, 'w' wrote file, 'x' task spawned but not yet looped,
//   'd' DNS/connect fail seen, '!' WiFi went down mid-loop.
static volatile uint16_t s_tile_fetch_iters   = 0;
static volatile char     s_tile_fetch_step    = '-';
static volatile bool     s_tile_fetch_spawn_ok = false;
// Last HTTP response code from http.GET(). Helps tell apart -1 (TCP/TLS
// failure), 4xx (no such tile), 5xx (server), 200 (ok) when serial is
// unreadable. Negative values come from HTTPClient itself.
static volatile int16_t  s_tile_fetch_last_code = 0;
// Recently-queued dedup ring — prevents the same (z,x,y) being enqueued
// dozens of times on rapid pan. 16 entries is enough for the 9 visible
// tiles + recent history.
static constexpr int     k_tile_fetch_dedup_size = 16;
static uint32_t          s_tile_fetch_dedup[k_tile_fetch_dedup_size] = {0};
static int               s_tile_fetch_dedup_head = 0;

static inline uint32_t tileFetchDedupKey(uint8_t z, int32_t x, int32_t y) {
  // z in 4 bits, x in 14, y in 14 — fits at zooms up to 16.
  return (((uint32_t)z & 0xF) << 28) | (((uint32_t)x & 0x3FFF) << 14) | ((uint32_t)y & 0x3FFF);
}
static bool tileFetchSeenRecently(uint8_t z, int32_t x, int32_t y) {
  const uint32_t k = tileFetchDedupKey(z, x, y);
  for (int i = 0; i < k_tile_fetch_dedup_size; ++i) {
    if (s_tile_fetch_dedup[i] == k) return true;
  }
  s_tile_fetch_dedup[s_tile_fetch_dedup_head] = k;
  s_tile_fetch_dedup_head = (s_tile_fetch_dedup_head + 1) % k_tile_fetch_dedup_size;
  return false;
}
#endif  // MULTI_TRANSPORT_COMPANION
#endif  // ESP32

// ----- Mercator helpers + tile cache -----
//
// Standard Web-Mercator slippy tiles. World is laid out as a square of
// 2^zoom × 2^zoom tiles, each tile 256×256 px. A coordinate's "world pixel"
// is its position in the full virtual canvas (e.g. at zoom 14 the world is
// 4_194_304 px on a side). Tile (tx, ty) covers world px (tx*256, ty*256)
// → (tx*256+255, ty*256+255).
//
// All transforms below use doubles since 1e-7 degrees of longitude already
// exceeds float precision at high zoom.
constexpr int     k_map_tile_size     = 256;
// Map viewport size. Not constexpr: set to the real tab content area in
// makeMapTab() so the map fills the screen in either orientation (240x226
// portrait / full-width landscape). The tile projection + marker math read
// these, so they must reflect the actual canvas. The 3x3 tile grid below
// covers 768 px and is generous enough for either viewport.
static int        k_map_canvas_w      = 240;
static int        k_map_canvas_h      = 226;
constexpr uint8_t k_map_zoom_min      = 12;
constexpr uint8_t k_map_zoom_max      = 16;
constexpr uint8_t k_map_zoom_default  = 14;
// 3×3 grid covers a 768×768-px slab — ~130 px of pan buffer on each side
// of the 240×226 viewport. We tried 5×5 (25 tiles, 3.2 MB PSRAM) for the
// bigger buffer but it pushed PSRAM usage to the edge — some tile decodes
// silently failed alloc and stayed black. Stuck back at 3×3 (9 tiles,
// 1.15 MB) for reliability; the dark-edge issue on big pans is a smaller
// problem than randomly-missing tiles.
constexpr int     k_map_grid_radius   = 1;     // tiles each side of center
constexpr int     k_map_visible_tiles_max =
    (2 * k_map_grid_radius + 1) * (2 * k_map_grid_radius + 1);

// Per-tile: we hold the *decoded* RGB565 in PSRAM, not the JPEG. Decoding
// happens ONCE at load time (in our control). The lv_img widget is then
// CF_TRUE_COLOR, so every subsequent redraw is a pure blit — no decoder
// call. Crucial for live-pan, where LVGL otherwise re-decodes all 9 JPEGs
// on every PRESSING tick (~67 Hz).
//
// Memory cost: 256 × 256 × 2 B = 128 KB per tile × 9 visible = ~1.15 MB
// from PSRAM (we have 8 MB, so this is fine).
struct MapTile {
  uint8_t   z;
  int32_t   x, y;
  uint8_t*  rgb565;      // PSRAM, 256×256×2 B
  int       w, h;
  lv_img_dsc_t dsc;
  lv_obj_t* img;
  bool      in_use;      // true = slot's (z,x,y) is valid; false = empty slot
};

static MapTile  s_map_tiles[k_map_visible_tiles_max] = {};
// Pan layer: a single transparent lv_obj that holds tile widgets + marker
// widgets. Live-pan just translates THIS — one set_pos = one invalidation,
// regardless of how many children sit on top.
static lv_obj_t* s_map_pan_layer = nullptr;
// Last renderMapTiles() gap count (visible tiles that weren't on disk).
// Drives the compact download/Wi-Fi hint in the bottom info bar.
static int       s_map_last_missing = 0;

static void freeMapTileSlot(MapTile& t) {
  if (t.img)    { lv_obj_del(t.img); t.img = nullptr; }
  // CRITICAL: invalidate LVGL's image cache entry for this dsc before
  // freeing the underlying buffer. The cache is keyed by lv_img_dsc_t* and
  // each tile slot's dsc lives at a STABLE address in s_map_tiles[]. When
  // we reuse the slot for a different (z, x, y), the dsc address is the
  // same so LVGL's cache hits and returns the old decoder result whose
  // img_data still points to the buffer we're about to free below — a
  // classic use-after-free that surfaced as RGB565 noise in tile bottoms
  // and "tiles in the wrong place" (new widget rendering old content).
  lv_img_cache_invalidate_src(&t.dsc);
  if (t.rgb565) { lvglPsramFree(t.rgb565); t.rgb565 = nullptr; }
  t.w = t.h  = 0;
  t.in_use   = false;
}

// Decode a JPEG (in-memory) into a freshly-PSRAM-alloced RGB565 buffer.
// Returns NULL on failure. Caller owns the buffer; lvglPsramFree() it.
// The decode is one-shot via LVGL's image decoder API (which routes to
// SJPG's tinyjpeg backend since data starts with 0xFFD8).
static uint8_t* decodeJpegToRgb565(const uint8_t* jpeg, size_t jpeg_len,
                                    int* out_w, int* out_h) {
  lv_img_dsc_t src;
  memset(&src, 0, sizeof(src));
  src.header.cf = LV_IMG_CF_RAW;
  src.data      = jpeg;
  src.data_size = jpeg_len;

  lv_img_decoder_dsc_t dec;
  memset(&dec, 0, sizeof(dec));
  // Color arg is only used by the alpha-only-image decoders to colorize
  // the alpha channel. We're decoding RGB JPEGs; it's ignored.
  if (lv_img_decoder_open(&dec, &src, lv_color_make(0, 0, 0), 0) != LV_RES_OK) {
    return nullptr;
  }
  const int w = dec.header.w;
  const int h = dec.header.h;
  if (w <= 0 || h <= 0 || w > 1024 || h > 1024) {
    lv_img_decoder_close(&dec);
    return nullptr;
  }
  const size_t buf_size = (size_t)w * h * sizeof(lv_color_t);  // RGB565 out
  uint8_t* buf = (uint8_t*)lvglPsramAlloc(buf_size);
  if (!buf) {
    lv_img_decoder_close(&dec);
    return nullptr;
  }
  // The decoder output format depends on the codec:
  //   • SJPG (JPEG)  → LV_IMG_CF_TRUE_COLOR       = 2 B/px (pure RGB565)
  //   • lv_png (PNG) → LV_IMG_CF_TRUE_COLOR_ALPHA = 3 B/px at depth 16
  //                    (RGB565 low, RGB565 high, alpha)
  // Our output buffer is pure RGB565 (2 B/px) and the lv_img widget is
  // tagged LV_IMG_CF_TRUE_COLOR, so for PNG we must drop the per-pixel
  // alpha byte. Copying the raw 3 B/px stream as if it were 2 B/px is
  // exactly what produced the RGB565 "static / noise" tiles.
  if (dec.img_data) {
    if (dec.header.cf == LV_IMG_CF_TRUE_COLOR_ALPHA) {
      const uint8_t* s = (const uint8_t*)dec.img_data;
      const size_t px = (size_t)w * h;
      for (size_t i = 0; i < px; ++i) {
        buf[i * 2 + 0] = s[i * 3 + 0];   // RGB565 low
        buf[i * 2 + 1] = s[i * 3 + 1];   // RGB565 high  (alpha s[i*3+2] dropped)
      }
    } else {
      memcpy(buf, dec.img_data, buf_size);   // already RGB565
    }
  } else {
    // Streaming fallback (unused by SJPG for standalone JPEGs, but
    // harmless to keep).
    for (int y = 0; y < h; ++y) {
      if (lv_img_decoder_read_line(&dec, 0, y, w,
              buf + (size_t)y * w * sizeof(lv_color_t)) != LV_RES_OK) {
        lvglPsramFree(buf);
        lv_img_decoder_close(&dec);
        return nullptr;
      }
    }
  }
  lv_img_decoder_close(&dec);
  *out_w = w;
  *out_h = h;
  return buf;
}
static double   s_map_center_lat = 0.0;
static double   s_map_center_lon = 0.0;
static uint8_t  s_map_zoom       = k_map_zoom_default;
static bool     s_map_has_pack   = false;   // toggles placeholder visibility

// lat/lon → world pixel at given zoom (Web Mercator).
static void latLonToWorldPx(double lat, double lon, uint8_t zoom,
                             double* world_x, double* world_y) {
  const double n = (double)(1u << zoom);
  *world_x = (lon + 180.0) / 360.0 * 256.0 * n;
  const double lat_rad = lat * M_PI / 180.0;
  const double sinlat  = sin(lat_rad);
  // Clamp sinlat to avoid log(0) at the poles.
  const double s = sinlat > 0.9999 ? 0.9999 : (sinlat < -0.9999 ? -0.9999 : sinlat);
  *world_y = (0.5 - log((1 + s) / (1 - s)) / (4 * M_PI)) * 256.0 * n;
}

// (kept for the pan/zoom step — converts a touch position back to lat/lon.)
static void __attribute__((unused))
worldPxToLatLon(double world_x, double world_y, uint8_t zoom,
                double* lat, double* lon) {
  const double n = (double)(1u << zoom);
  *lon = world_x / (256.0 * n) * 360.0 - 180.0;
  const double y_norm = world_y / (256.0 * n);
  const double lat_rad = atan(sinh(M_PI * (1 - 2 * y_norm)));
  *lat = lat_rad * 180.0 / M_PI;
}

// Background fetch task — drains s_tile_fetch_queue, downloads each
// tile from OSM, writes it to LittleFS. Pinned to core 0 (Wi-Fi core)
// so the LVGL thread on core 1 isn't blocked by network I/O. Self-
// terminates after a 5 s idle period to release the stack.
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
static void ensureTilesDirPath(uint8_t z, int32_t x) {
  // LittleFS requires explicit mkdir; opening a deep path won't auto-
  // create the parents.
  char p[40];
  snprintf(p, sizeof(p), "/tiles");                       s_tiles_fs.mkdir(p);
  snprintf(p, sizeof(p), "/tiles/%u", (unsigned)z);        s_tiles_fs.mkdir(p);
  snprintf(p, sizeof(p), "/tiles/%u/%ld",
           (unsigned)z, (long)x);                          s_tiles_fs.mkdir(p);
}

// ----- Firmware version check (reuses the core-0 fetch worker) -----
// The web flasher lists releases under prebuilt/releases/TOUCH/ via the GitHub
// API. The device can't do HTTPS, but the meshcomod proxy exposes that same API
// over plain HTTP at app.meshcomod.com/api-github/. We GET the TOUCH listing,
// scan for the highest pre-alpha_<N>, and compare to our embedded release tag.
// (s_verchk_request / s_verchk_done / s_verchk_latest_n are declared globally
// near the version-check UI helpers above.)

// Stream the TOUCH releases listing and return the highest pre-alpha_<N> dir
// number, or -1 on failure. Char-by-char scan (the body is a few KB) so we
// never buffer the whole JSON; entries are alphabetical so the max isn't last.
// Reuses the worker's own WiFiClient/HTTPClient (passed in) rather than putting
// a second pair on the ~8 KB worker stack — nesting them overflowed it.
static int verchkFetchLatest(WiFiClient& client, HTTPClient& http) {
  http.setReuse(false);
  http.setConnectTimeout(8000);
  http.setTimeout(12000);
  http.setUserAgent("meshcomod-touch");
  if (!http.begin(client, "http://app.meshcomod.com/api-github/contents/prebuilt/releases/TOUCH")) {
    return -1;
  }
  const int code = http.GET();
  if (code != 200) { http.end(); return -1; }
  WiFiClient* st = http.getStreamPtr();
  static const char PAT[] = "beta_";
  const int patlen = (int)sizeof(PAT) - 1;
  int mp = 0, best = -1, curnum = -1;
  bool innum = false;
  uint32_t total = 0;
  const uint32_t LIMIT = 32768;
  const unsigned long t0 = millis();
  while (http.connected() && total < LIMIT && (millis() - t0) < 12000) {
    int avail = st ? st->available() : 0;
    if (avail <= 0) {
      if (!st || !st->connected()) break;
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    while (avail-- > 0 && total < LIMIT) {
      const int c = st->read();
      if (c < 0) break;
      ++total;
      if (innum) {
        if (c >= '0' && c <= '9') { curnum = (curnum < 0 ? 0 : curnum) * 10 + (c - '0'); continue; }
        if (curnum > best) best = curnum;
        innum = false; curnum = -1; mp = 0;
      }
      if (c == PAT[mp]) { if (++mp == patlen) { mp = 0; innum = true; curnum = -1; } }
      else            { mp = (c == (int)PAT[0]) ? 1 : 0; }
    }
  }
  if (innum && curnum > best) best = curnum;
  http.end();
  return best;
}

static void tileFetchTaskFn(void* arg) {
  (void)arg;
  // Plain HTTP, not HTTPS. We can't do HTTPS on this device: mbedTLS
  // wants ~30 KB internal heap during the handshake and after Wi-Fi
  // associates only ~5 KB is free. OSM itself only serves HTTPS, but a
  // tiny user-side proxy (Render / Fly / nginx — ~30 lines) forwards
  // http://tiles.meshcomod.com/{z}/{x}/{y}.png  →  https://tile.osm.org.
  // Default base URL is hard-coded; user can override via Settings.
  WiFiClient client;
  HTTPClient http;
  // setReuse(false): close the TCP socket after every tile. Keeping it
  // open (reuse=true) held ~6 KB of lwip RX/TX buffers in internal DMA
  // RAM the whole time the task ran — and with only ~16 KB internal free
  // when the Map tab is open, that left ~2 KB. When the render thread
  // (core 1) then decoded a freshly-arrived tile *while* the socket was
  // still open, the Wi-Fi driver couldn't allocate a DMA RX buffer and
  // the device rebooted. Closing between tiles gives the render window
  // the socket's memory back.
  http.setReuse(false);
  http.setConnectTimeout(8000);
  http.setTimeout(15000);
  Serial.println("[TILE] fetcher task started");
  s_tile_fetch_step = 'x';

  TileFetchReq req;
  for (;;) {
    // User-initiated line-of-sight comes FIRST — it must never wait behind a
    // background history write. Reuses this worker's stack (no second task).
    if (s_los_request) {
      s_los_request = false;
      // Retry up to 3×. The proxy caches /elev (nginx, 30 days, keyed on the
      // exact query) and rides the backend's ~1 req/s limit with its own 2
      // server-side retries — so attempt 1 warms the cache and a quick retry
      // hits it, while the inter-attempt gap also steps across the upstream
      // rate limit when a cold path 502s. losRepairElevations() fills any
      // null/missing samples so a single no-data point no longer fails the
      // whole analysis. Bounded (3 × 9 s read + gaps) under the UI watchdog.
      int parsed = 0;
      bool ok = false;
      for (int attempt = 1; attempt <= 3 && !ok; ++attempt) {
        s_los_attempt = attempt;
        parsed = losFetchElevations(s_los_slat, s_los_slon, k_los_samples, s_los_elev);
        // Count real (non-null) samples BEFORE repair fills them in — this is
        // what the diag line reports so we can tell a proxy error (parsed=0)
        // from a no-data path (parsed=24 but few valid).
        int v = 0;
        for (int i = 0; i < parsed; ++i) if (s_los_elev[i] == s_los_elev[i]) ++v;
        s_los_dbg_parsed = parsed;
        s_los_dbg_valid  = v;
        ok = losRepairElevations(s_los_elev, k_los_samples, parsed);
        if (!ok && attempt < 3) vTaskDelay(pdMS_TO_TICKS(900));
      }
      s_los_got = ok ? k_los_samples : 0;
      s_los_busy = false;
      s_los_result_ready = true;
      continue;
    }
    // Firmware update check (one-shot, infrequent). Reuses this worker's stack.
    if (s_verchk_request) {
      s_verchk_request = false;
      s_verchk_latest_n = verchkFetchLatest(client, http);   // reuse the worker's client/http
      s_verchk_done = true;
      continue;
    }
    // Wi-Fi scan (user-initiated from the Network tab / setup wizard). Blocking
    // here on the Wi-Fi core, not the UI thread. Mirrors the CLI `wifi scan`.
    if (s_wifiscan_request) {
      s_wifiscan_request = false;
      s_wifiscan_count = 0;
      // Never bring the Wi-Fi driver up from here when BLE owns the radio (fresh
      // device on BLE => Bluedroid holds the internal heap). WiFi.mode(STA)/
      // esp_wifi_init would then OOM-panic (BLE-vs-Wi-Fi mutex, see main.cpp).
      // wantsWifi() mirrors the boot transport choice: true only when Wi-Fi is
      // the active transport (incl. touch "Wi-Fi chosen, no creds yet" scan
      // mode, where the radio booted up STA). Report "no networks" otherwise.
      if (!wifiConfigWantsWifi()) {
        s_wifiscan_done = true;
        continue;
      }
      if ((WiFi.getMode() & WIFI_MODE_STA) == 0) { WiFi.mode(WIFI_STA); vTaskDelay(pdMS_TO_TICKS(180)); }
      else                                        { vTaskDelay(pdMS_TO_TICKS(40)); }
      WiFi.scanDelete();
      int found = WiFi.scanNetworks(false, true, false, 300, 0);   // sync, show-hidden, active
      if (found <= 0 && WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect(false, false); vTaskDelay(pdMS_TO_TICKS(120));
        found = WiFi.scanNetworks(false, true, false, 300, 0);
      }
      int n = 0;
      for (int idx = 0; idx < found && n < kWifiScanMax; ++idx) {
        String s = WiFi.SSID(idx);
        if (s.length() == 0) continue;
        bool dup = false;
        for (int k = 0; k < n; ++k) if (strcmp(s_wifiscan_ssids[k], s.c_str()) == 0) { dup = true; break; }
        if (dup) continue;
        strncpy(s_wifiscan_ssids[n], s.c_str(), WIFI_CONFIG_SSID_MAX - 1);
        s_wifiscan_ssids[n][WIFI_CONFIG_SSID_MAX - 1] = '\0';
        ++n;
      }
      WiFi.scanDelete();
      s_wifiscan_count = n;
      s_wifiscan_done  = true;
      continue;
    }
    s_tile_fetch_step = 'q';
    // Finite timeout (not portMAX_DELAY) so the loop wakes periodically to
    // pick up LOS requests even when no tiles are queued.
    if (xQueueReceive(s_tile_fetch_queue, &req, pdMS_TO_TICKS(250)) != pdTRUE) {
      continue;
    }
    ++s_tile_fetch_iters;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.printf("[TILE] skip z=%u x=%ld y=%ld: WiFi down\n",
                    (unsigned)req.z, (long)req.x, (long)req.y);
      s_tile_fetch_step = '!';
      ++s_tile_fetch_failed;
      if (s_tile_fetch_pending > 0) --s_tile_fetch_pending;
      continue;
    }

    // Skip if we already have the JPEG. A stale .png (from an earlier
    // build that fetched PNGs) is deleted and re-fetched as .jpg, since
    // the device can't decode PNG.
    char path_jpg[48], path_png[48];
    snprintf(path_jpg, sizeof(path_jpg),
             "/tiles/%u/%ld/%ld.jpg", (unsigned)req.z, (long)req.x, (long)req.y);
    snprintf(path_png, sizeof(path_png),
             "/tiles/%u/%ld/%ld.png", (unsigned)req.z, (long)req.x, (long)req.y);
    if (s_tiles_fs.exists(path_jpg)) {
      ++s_tile_fetch_ok;
      if (s_tile_fetch_pending > 0) --s_tile_fetch_pending;
      continue;
    }
    if (s_tiles_fs.exists(path_png)) {
      s_tiles_fs.remove(path_png);   // stale noise-tile — reclaim the space
    }

    // Heap-safety gate: opening a TCP socket costs ~6 KB of internal DMA
    // RAM, and the Wi-Fi driver needs more on top for RX. If internal
    // heap is critically low (render thread mid-decode, etc.) wait it
    // out rather than push the system into an OOM reboot. Bail after a
    // few tries so we don't spin forever on a genuinely starved heap.
    // Threshold raised (14→18 KB) because the immersive full-screen map
    // composites two transparent bars over the tiles each redraw, which
    // transiently needs more internal DMA RAM than the old opaque chrome.
    {
      int waits = 0;
      while (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 18 * 1024
             && waits < 24) {
        s_tile_fetch_step = 'h';   // 'h' = heap-wait
        vTaskDelay(pdMS_TO_TICKS(150));
        ++waits;
      }
      if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 12 * 1024) {
        // Still too tight — skip this tile; it'll be re-requested on the
        // next render miss. Keep counters coherent.
        ++s_tile_fetch_failed;
        if (s_tile_fetch_pending > 0) --s_tile_fetch_pending;
        continue;
      }
    }

    ensureTilesDirPath(req.z, req.x);

    char base[TOUCH_TILE_SERVER_MAXLEN];
    touchPrefsGetTileServer(base, sizeof(base));
    // Strip trailing slash so we don't emit `//{z}` paths some servers
    // mis-handle.
    size_t blen = strlen(base);
    while (blen > 0 && base[blen - 1] == '/') base[--blen] = '\0';
    // Request .jpg: the meshcomod proxy transcodes OSM's PNG → JPEG so the
    // device decodes via the light SJPG/TJpgDec path (stripe-based, RGB565
    // direct) instead of lodepng (full 256 KB ARGB8888 decode per tile,
    // which both rendered as noise and bogged the UI down).
    char url[160];
    snprintf(url, sizeof(url), "%s/%u/%ld/%ld.jpg",
             base, (unsigned)req.z, (long)req.x, (long)req.y);
    http.begin(client, url);
    // OSM policy: identifying User-Agent required; vague UAs get blocked.
    http.addHeader("User-Agent", "meshcomod-touch/0.4 (https://github.com/ALLFATHER-BV/meshcomod)");
    Serial.printf("[TILE] GET %s\n", url);
    s_tile_fetch_step = 'g';
    int code = http.GET();
    s_tile_fetch_last_code = (int16_t)code;
    Serial.printf("[TILE]  -> HTTP %d (size=%d)\n", code, http.getSize());
    bool wrote = false;
    if (code != HTTP_CODE_OK) {
      s_tile_fetch_step = 'd';
    }
    if (code == HTTP_CODE_OK) {
      s_tile_fetch_step = 'r';
      int content_len = http.getSize();
      // Sanity cap — refuse anything > 100 KB. Tiles are typically 10-30 KB.
      if (content_len > 0 && content_len <= 100 * 1024) {
        File f = s_tiles_fs.open(path_jpg, "w");
        if (f) {
          WiFiClient* stream = http.getStreamPtr();
          uint8_t buf[1024];
          int remaining = content_len;
          while (remaining > 0 && http.connected()) {
            const size_t want = (size_t)(remaining > (int)sizeof(buf) ? sizeof(buf) : remaining);
            const int n = stream->readBytes(buf, want);
            if (n <= 0) break;
            f.write(buf, n);
            remaining -= n;
          }
          f.close();
          if (remaining == 0) wrote = true;
          else                s_tiles_fs.remove(path_jpg);  // partial write — discard
        }
      }
    }
    // Close the socket and free its lwip buffers BEFORE we signal the
    // render thread. The render of a freshly-downloaded tile must happen
    // with the socket DOWN — otherwise core 1's decode + the Wi-Fi
    // driver's DMA RX buffers collide in the ~2 KB of internal heap that
    // remains and the device reboots.
    http.end();
    client.stop();
    if (s_tile_fetch_pending > 0) --s_tile_fetch_pending;

    if (wrote) {
      Serial.printf("[TILE]  -> wrote %s\n", path_jpg);
      s_tile_fetch_step = 'w';
      ++s_tile_fetch_ok;
      s_tile_fetch_dirty = true;        // render thread picks this up
    } else {
      Serial.printf("[TILE]  -> FAILED %s\n", path_jpg);
      ++s_tile_fetch_failed;
    }

    // Give core 1 a clear window (socket closed = ~8 KB more internal
    // heap) to decode+blit the tile we just wrote before we reopen a
    // socket for the next one. 500 ms also keeps us at OSM's ≤2/sec
    // policy on cache-misses (the proxy forwards our rate upstream; most
    // hits are served from the proxy's cache so this rarely reaches OSM).
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  // Never exit. The task sits forever on xQueueReceive; idle sleep on a
  // FreeRTOS queue is free, and re-creating a STATIC task is risky —
  // the FreeRTOS idle reaper may still hold a reference to the TCB
  // when we try to recreate it, and the stack alloc is the bottleneck
  // we don't want to repeat anyway.
}

// Stack allocated at UITask::begin() time (see reserveTileFetchStack
// below). Pre-reserving in BSS bootloops because it competes with
// Bluedroid's late-init DMA-DRAM allocations; PSRAM stacks crash mbedTLS.
// Runtime alloc in the small window after Bluedroid is up but before
// Wi-Fi associates hits the sweet spot: heap is still ~150 KB free then,
// and once we hold the 14 KB it stays ours.
static size_t            s_tile_fetch_stack_bytes = 0;
static StackType_t*      s_tile_fetch_stack       = nullptr;
static StaticTask_t      s_tile_fetch_tcb;

void reserveTileFetchStack() {
  if (s_tile_fetch_stack) return;
  // Plain-HTTP tile fetcher (mbedTLS gone) needs only ~5 KB, but this worker
  // also runs the version check and the Wi-Fi scan (deep scanNetworks path).
  // 8 KB gives headroom — a too-small stack here overflowed into adjacent
  // globals and zeroed the chat-history buffer (empty chats after a scan).
  static const size_t k_try[] = { 8*1024, 7*1024, 6*1024 };
  for (size_t s : k_try) {
    s_tile_fetch_stack = (StackType_t*)heap_caps_malloc(
        s, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_tile_fetch_stack) { s_tile_fetch_stack_bytes = s; break; }
  }
  Serial.printf("[TILE] reserve stack %p sz=%u (free_int now %u)\n",
                s_tile_fetch_stack, (unsigned)s_tile_fetch_stack_bytes,
                (unsigned)ESP.getFreeHeap());
}

static bool ensureTileFetchTaskRunning() {
  if (s_tile_fetch_queue == nullptr) {
    s_tile_fetch_queue = xQueueCreate(k_tile_fetch_queue_size, sizeof(TileFetchReq));
    if (!s_tile_fetch_queue) return false;
  }
  if (s_tile_fetch_task != nullptr) return true;
  // Stack was reserved early (begin()); if that failed, give up cleanly
  // rather than crash inside xTaskCreateStatic.
  if (s_tile_fetch_stack == nullptr) {
    reserveTileFetchStack();
    if (s_tile_fetch_stack == nullptr) return false;
  }

  s_tile_fetch_step     = '-';
  s_tile_fetch_spawn_ok = false;

  s_tile_fetch_task = xTaskCreateStaticPinnedToCore(
      tileFetchTaskFn, "tile_fetch",
      s_tile_fetch_stack_bytes / sizeof(StackType_t),
      nullptr, 1,
      s_tile_fetch_stack, &s_tile_fetch_tcb,
      0 /*core 0 = wifi core*/);
  s_tile_fetch_spawn_ok = (s_tile_fetch_task != nullptr);
  Serial.printf("[TILE] fetch task spawned %p free_int=%u free_psram=%u\n",
                s_tile_fetch_task, (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getFreePsram());
  return s_tile_fetch_task != nullptr;
}

// Queue a missing tile for download. No-op when Wi-Fi is down or the
// tile was queued recently. Called from renderMapTiles after a SPIFFS
// miss; the actual fetch happens off-thread.
static void queueTileForFetch(uint8_t z, int32_t x, int32_t y) {
  if (s_tiles_from_sd) return;            // microSD tile source: never fetch from the server
  if (WiFi.status() != WL_CONNECTED) return;
  if (tileFetchSeenRecently(z, x, y)) return;
  ensureTileFetchTaskRunning();
  if (!s_tile_fetch_queue) return;
  TileFetchReq req = {z, x, y};
  if (xQueueSend(s_tile_fetch_queue, &req, 0) == pdTRUE) {
    ++s_tile_fetch_pending;
  }
}

// "Zoom packs": for the settled map center, opportunistically cache the tile
// at the min and max zoom (a wide overview + a close detail) through the same
// background fetch path as the live map — so every place you browse online is
// kept for offline use at two zoom levels, not just the one you're viewing.
// Two tiles per location; queueTileForFetch's Wi-Fi guard + dedup ring and the
// on-disk checks here keep it cheap, and because it runs *after* the visible
// tiles are queued, FIFO ordering means the tiles you're actually looking at
// always download first.
static void queueZoomPackForCenter() {
  if (s_map_center_lat == 0.0 && s_map_center_lon == 0.0) return;
  if (!s_tiles_fs_ready) return;
  const uint8_t levels[2] = { k_map_zoom_min, k_map_zoom_max };
  for (int i = 0; i < 2; ++i) {
    const uint8_t z = levels[i];
    if (z == s_map_zoom) continue;            // current zoom: renderMapTiles already queues it
    double wx, wy;
    latLonToWorldPx(s_map_center_lat, s_map_center_lon, z, &wx, &wy);
    const int32_t tx = (int32_t)floor(wx / 256.0);
    const int32_t ty = (int32_t)floor(wy / 256.0);
    char path[48];
    snprintf(path, sizeof path, "/tiles/%u/%ld/%ld.jpg", (unsigned)z, (long)tx, (long)ty);
    if (s_tiles_fs.exists(path)) continue;    // already have an offline pack tile
    snprintf(path, sizeof path, "/tiles/%u/%ld/%ld.png", (unsigned)z, (long)tx, (long)ty);
    if (s_tiles_fs.exists(path)) continue;    // already Wi-Fi-fetched
    queueTileForFetch(z, tx, ty);
  }
}
#endif  // ESP32 && MULTI_TRANSPORT_COMPANION

#if defined(ESP32)
// Decode a PNG to a PSRAM RGB565 buffer via lodepng DIRECTLY. LVGL's lv_png
// decoder produces RGB565 noise on this board, so we bypass it: lodepng ->
// RGBA8888 (its malloc lands in PSRAM thanks to the >4 KB SPIRAM-malloc
// threshold), then a manual RGBA->RGB565 using lv_color_make() so the display's
// colour format (incl. byte-swap) is honoured. This is what lets the SD map use
// standard /maps/osm/{z}/{x}/{y}.png tiles (the Meshtastic/MeshCore convention).
extern "C" unsigned lodepng_decode32(unsigned char** out, unsigned* w, unsigned* h,
                                      const unsigned char* in, size_t insize);
static uint8_t* decodePngToRgb565(const uint8_t* png, size_t png_len, int* out_w, int* out_h) {
  unsigned char* rgba = nullptr; unsigned w = 0, h = 0;
  if (lodepng_decode32(&rgba, &w, &h, png, png_len) != 0 || !rgba) { if (rgba) free(rgba); return nullptr; }
  if (w == 0 || h == 0 || w > 1024 || h > 1024) { free(rgba); return nullptr; }
  const size_t npx = (size_t)w * h;
  uint16_t* rgb = (uint16_t*)lvglPsramAlloc(npx * sizeof(uint16_t));
  if (!rgb) { free(rgba); return nullptr; }
  for (size_t i = 0; i < npx; i++) rgb[i] = lv_color_make(rgba[i*4+0], rgba[i*4+1], rgba[i*4+2]).full;
  free(rgba);   // lodepng allocates with the system malloc/free
  *out_w = (int)w; *out_h = (int)h;
  return (uint8_t*)rgb;
}
#endif

// Read /tiles/<z>/<x>/<y>.{jpg,png} from the tiles LittleFS partition
// into a freshly-PSRAM-alloced buffer. Returns false when neither file
// exists or alloc fails. Tries .jpg first (offline packs from tile-pack
// .py) and falls back to .png (Wi-Fi-fetched tiles from OSM).
//
// The LVGL decoder pipeline is format-agnostic: src.header.cf =
// LV_IMG_CF_RAW + data starting with 0xFFD8 (JPEG) or 0x89 0x50 0x4E
// 0x47 (PNG) is auto-routed to SJPG or lv_png respectively. So this
// function just reads bytes — decode happens later in
// decodeJpegToRgb565 (which is a misnomer now; it handles both).
static bool loadTileJpeg(uint8_t z, int32_t x, int32_t y,
                         uint8_t** out_data, size_t* out_len) {
#if defined(ESP32)
  // JPEG only. We deliberately do NOT fall back to .png: this device can't decode PNG
  // without RGB565 noise + a 256 KB ARGB8888 buffer per tile. Offline packs + the Wi-Fi
  // proxy are both .jpg, so an SD tile tree must be .jpg too.
  char path[48];
  snprintf(path, sizeof(path), "/tiles/%u/%ld/%ld.jpg",
           (unsigned)z, (long)x, (long)y);
#if defined(HAS_TDECK_GT911)
  if (s_tiles_from_sd) {
    // Tile source = microSD: read straight off the card (fully offline, no server fetch).
    if (!fmSdTryMount()) return false;
    // Prefer the Meshtastic/MeshCore standard layout /maps/osm/{z}/{x}/{y}.png
    // (decoded via lodepng); fall back to the legacy /tiles/{z}/{x}/{y}.jpg.
    char ppath[56];
    snprintf(ppath, sizeof(ppath), "/maps/osm/%u/%ld/%ld.png", (unsigned)z, (long)x, (long)y);
    File fsd = SD.open(ppath, FILE_READ);
    if (!fsd) fsd = SD.open(path, FILE_READ);
    if (!fsd) return false;
    const size_t szsd = fsd.size();
    if (szsd == 0 || szsd > 256 * 1024) { fsd.close(); return false; }   // PNG tiles run larger than JPEG
    uint8_t* bufsd = (uint8_t*)lvglPsramAlloc(szsd);
    if (!bufsd) { fsd.close(); return false; }
    const size_t nsd = fsd.read(bufsd, szsd);
    fsd.close();
    if (nsd != szsd) { lvglPsramFree(bufsd); return false; }
    *out_data = bufsd; *out_len = szsd;
    return true;
  }
#endif
  if (!s_tiles_fs_ready) return false;
  if (!s_tiles_fs.exists(path)) return false;
  File f = s_tiles_fs.open(path, "r");
  if (!f) return false;
  const size_t sz = f.size();
  if (sz == 0 || sz > 80 * 1024) { f.close(); return false; }  // sanity cap
  uint8_t* buf = (uint8_t*)lvglPsramAlloc(sz);
  if (!buf) { f.close(); return false; }
  const size_t n = f.read(buf, sz);
  f.close();
  if (n != sz) { lvglPsramFree(buf); return false; }
  *out_data = buf;
  *out_len  = sz;
  return true;
#else
  (void)z; (void)x; (void)y; (void)out_data; (void)out_len;
  return false;
#endif
}

// Free everything currently in the tile cache.
static void freeMapTiles() {
  for (auto& t : s_map_tiles) freeMapTileSlot(t);
}

// Probe the pack for the highest zoom level that has a tile near `lat,lon`.
// Used as an auto-fallback so a missing top zoom (e.g. the operator packed
// z12+z13 but s_map_zoom defaults to z14) doesn't make the whole tab look
// empty. Returns 0 when no tile is found at any zoom in the supported range.
static uint8_t bestAvailableZoom(double lat, double lon) {
#if defined(ESP32)
  if (!s_tiles_fs_ready) return 0;
  for (int z = (int)k_map_zoom_max; z >= (int)k_map_zoom_min; --z) {
    double wx, wy;
    latLonToWorldPx(lat, lon, (uint8_t)z, &wx, &wy);
    const int32_t tx = (int32_t)floor(wx / 256.0);
    const int32_t ty = (int32_t)floor(wy / 256.0);
    char path[48];
    snprintf(path, sizeof(path), "/tiles/%d/%ld/%ld.jpg", z, (long)tx, (long)ty);
    if (s_tiles_fs.exists(path)) return (uint8_t)z;
  }
  return 0;
#else
  (void)lat; (void)lon;
  return 0;
#endif
}

// Build the visible tile grid for the current center+zoom. Cheap: only
// runs on tab activation or pan/zoom (not on every refresh tick).
static void renderMapTiles() {
  if (!s_map_canvas) return;

  // Fall back to placeholder when no usable GPS center.
  if (s_map_center_lat == 0.0 && s_map_center_lon == 0.0) {
    freeMapTiles();
    s_map_has_pack = false;
    if (s_map_status_lbl) {
      lv_label_set_text(s_map_status_lbl,
          "Map — set your location\nin Settings \xe2\x86\x92 Profile to\nshow the map here.");
      lv_obj_clear_flag(s_map_status_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }

  // (Auto-snap to best available zoom moved to onMapTabActivated. Here we
  // honour whatever the user picked via the zoom buttons — even if the
  // requested zoom has no tiles, we show the empty grid + "no tile pack"
  // hint so the user can tap − to go back, instead of silently fighting
  // the buttons.)

  // World pixel of the center coordinate.
  double cwx, cwy;
  latLonToWorldPx(s_map_center_lat, s_map_center_lon, s_map_zoom, &cwx, &cwy);
  const int32_t ctx = (int32_t)floor(cwx / 256.0);
  const int32_t cty = (int32_t)floor(cwy / 256.0);

  // Slot reuse: panning by less than a tile shares 6-9 of the 9 visible
  // tiles between renders. Re-creating those widgets (SPIFFS read + JPEG
  // decode each) is what made the v1 unusable — ~3 s every release. We
  // now MATCH wanted tiles against the existing slots and only reload the
  // newcomers. A full pan still loads 9 tiles, but small pans cost ~1 new
  // tile (~300 ms).
  //
  // Algorithm:
  //   1. Build the wanted set: 9 (z, tx, ty) coords.
  //   2. For each slot in s_map_tiles, decide:
  //      - matches a wanted coord → keep, reposition.
  //      - doesn't match → free the slot (becomes "empty", available below).
  //   3. For each wanted coord not yet present in any slot, load into an
  //      empty slot.
  struct Wanted { int32_t tx; int32_t ty; bool placed; };
  Wanted wanted[k_map_visible_tiles_max];
  int n_wanted = 0;
  for (int dy = -k_map_grid_radius; dy <= k_map_grid_radius; ++dy) {
    for (int dx = -k_map_grid_radius; dx <= k_map_grid_radius; ++dx) {
      if (n_wanted >= k_map_visible_tiles_max) break;
      wanted[n_wanted++] = { ctx + dx, cty + dy, false };
    }
  }

  // Whenever we end up here, the pan layer must be reset to (0,0) — its
  // offset only exists during a live drag.
  if (s_map_pan_layer) lv_obj_set_pos(s_map_pan_layer, 0, 0);

  // Pass 1 — keep matching slots, free non-matching ones.
  for (auto& t : s_map_tiles) {
    if (!t.in_use) continue;
    if (t.z != s_map_zoom) { freeMapTileSlot(t); continue; }
    bool kept = false;
    for (int i = 0; i < n_wanted; ++i) {
      if (!wanted[i].placed && wanted[i].tx == t.x && wanted[i].ty == t.y) {
        // Re-anchor the existing img widget to its new screen position.
        const int sx = (int)((double)t.x * 256.0 - cwx + k_map_canvas_w / 2);
        const int sy = (int)((double)t.y * 256.0 - cwy + k_map_canvas_h / 2);
        lv_obj_set_pos(t.img, sx, sy);
        wanted[i].placed = true;
        kept = true;
        break;
      }
    }
    if (!kept) freeMapTileSlot(t);
  }

  // Pass 2 — fill remaining wanted tiles into empty slots. For each new
  // tile we read JPEG → decode to RGB565 → free JPEG → attach CF_TRUE_COLOR
  // dsc to the widget. Subsequent draws are pure blits.
  lv_obj_t* parent = s_map_pan_layer ? s_map_pan_layer : s_map_canvas;
  bool any_loaded = false;
  int  n_missing  = 0;   // visible tiles we couldn't load from disk
  for (int i = 0; i < n_wanted; ++i) {
    if (wanted[i].placed) { any_loaded = true; continue; }
    // Find an empty slot.
    MapTile* dst = nullptr;
    for (auto& t : s_map_tiles) {
      if (!t.in_use) { dst = &t; break; }
    }
    if (!dst) break;  // shouldn't happen — wanted count == slot count
    uint8_t* jpeg = nullptr;
    size_t   jlen = 0;
    if (!loadTileJpeg(s_map_zoom, wanted[i].tx, wanted[i].ty, &jpeg, &jlen)) {
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
      // Tile not on disk — queue an OSM download if Wi-Fi is up. No-op
      // when offline; if/when Wi-Fi comes up later, the next render
      // pass will queue again.
      queueTileForFetch(s_map_zoom, wanted[i].tx, wanted[i].ty);
#endif
      ++n_missing;
      continue;
    }
    int dw = 0, dh = 0;
    // Sniff the format: PNG (0x89 'P' 'N' 'G') -> direct-lodepng path (LVGL's
    // lv_png is broken here); otherwise JPEG/SJPG.
    uint8_t* rgb = (jlen >= 4 && jpeg[0] == 0x89 && jpeg[1] == 'P' && jpeg[2] == 'N' && jpeg[3] == 'G')
                     ? decodePngToRgb565(jpeg, jlen, &dw, &dh)
                     : decodeJpegToRgb565(jpeg, jlen, &dw, &dh);
    lvglPsramFree(jpeg);
    if (!rgb) { ++n_missing; continue; }
    dst->z = s_map_zoom; dst->x = wanted[i].tx; dst->y = wanted[i].ty;
    dst->rgb565 = rgb;
    dst->w = dw; dst->h = dh;
    memset(&dst->dsc, 0, sizeof(dst->dsc));
    dst->dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    dst->dsc.header.w  = (uint32_t)dw;
    dst->dsc.header.h  = (uint32_t)dh;
    dst->dsc.data      = rgb;
    dst->dsc.data_size = (uint32_t)((size_t)dw * dh * sizeof(lv_color_t));
    dst->img = lv_img_create(parent);
    lv_img_set_src(dst->img, &dst->dsc);
    const int sx = (int)((double)wanted[i].tx * 256.0 - cwx + k_map_canvas_w / 2);
    const int sy = (int)((double)wanted[i].ty * 256.0 - cwy + k_map_canvas_h / 2);
    lv_obj_set_pos(dst->img, sx, sy);
    // Sink the tile to the back of the pan layer. JPEG tiles are opaque,
    // so without this a tile created in a later (dirty) render pass would
    // paint over the contact / self markers that were created earlier on
    // the same layer — they'd vanish behind the map. Tiles never overlap
    // each other (grid), so their relative order doesn't matter; what
    // matters is that ALL tiles stay below ALL markers.
    lv_obj_move_background(dst->img);
    dst->in_use = true;
    wanted[i].placed = true;
    any_loaded = true;
    // Progressive render: paint this tile before loading the next one, so the
    // map fills in tile-by-tile instead of all at once. BUT skip it while a
    // download is in flight: on the immersive full-screen map each lv_refr_now
    // also alpha-composites the (transparent) status + tab bars over the whole
    // screen, which is heavy on internal DMA RAM — doing that per-tile WHILE the
    // Wi-Fi worker holds a socket pushed the heap into an OOM reboot. When tiles
    // are arriving from the network, let LVGL batch one redraw at the end.
    if (s_tile_fetch_pending == 0) lv_refr_now(NULL);
  }

#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  // Warm the min/max zoom cache for this location (overview + detail) in the
  // background. Queued after the visible tiles above so they keep priority.
  queueZoomPackForCenter();
#endif

  // ---- Status overlay ----
  // Cases:
  //   • some tiles rendered (any_loaded) → map is usable; hide the overlay.
  //     Missing edge tiles just fill in as they download.
  //   • nothing rendered (panned into an un-tiled area) → show a clear,
  //     human message that depends on whether we can actually fetch.
  s_map_has_pack = any_loaded;
  // Remember the gap count so the bottom info bar can show a compact
  // "downloading" / "Wi-Fi off" hint even when the map is partially loaded
  // (any_loaded true) — that's the common "panned toward the edge of my
  // saved area" case where a big centered overlay would be too intrusive.
  s_map_last_missing = n_missing;

  bool wifi_up = false;
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  wifi_up = (WiFi.status() == WL_CONNECTED);
#endif

  if (!s_map_status_lbl) return;

  if (any_loaded) {
    lv_obj_add_flag(s_map_status_lbl, LV_OBJ_FLAG_HIDDEN);
    return;
  }

#if defined(ESP32)
#if defined(HAS_TDECK_GT911)
  if (s_tiles_from_sd) {
    lv_label_set_text(s_map_status_lbl,
        "Map tiles: microSD\n\n"
        "/maps/osm/z/x/y.png\n"
        "(or /tiles/z/x/y.jpg)\n\n"
        "Map appears when a tile\nfor this area is found.");
  } else
#endif
  if (!s_tiles_fs_ready) {
    lv_label_set_text(s_map_status_lbl,
        "Map storage error.\nReflash the tiles partition.");
  } else if (wifi_up) {
    // Wi-Fi up: the missing tiles were just queued for download. Reassure
    // the user it's working — the screen repaints (s_tile_fetch_dirty) as
    // tiles land.
    lv_label_set_text(s_map_status_lbl,
        "Downloading map tiles\xe2\x80\xa6\n\n"
        "Keep Wi-Fi connected.\nTiles appear as they arrive\n"
        "and are saved for offline use.");
  } else {
    // No Wi-Fi and no saved tiles here — be explicit about the fix.
    lv_label_set_text(s_map_status_lbl,
        "No saved map tiles here.\n\n"
        "Connect to Wi-Fi (Settings \xe2\x86\x92 Wi-Fi)\n"
        "to download this area.\n"
        "Saved tiles stay available offline.");
  }
#else
  lv_label_set_text(s_map_status_lbl, "No tiles (non-ESP32)");
#endif
  lv_obj_clear_flag(s_map_status_lbl, LV_OBJ_FLAG_HIDDEN);
}

// ----- Markers -----
//
// Self marker = small white crosshair; contact markers = colored dots
// sized for finger taps. Plotted as children of s_map_canvas so they
// stack on top of the tile layer. The cache below is flat (linear scan
// when handling marker taps) — fine for at-most-32 visible markers.
struct MapMarker {
  int       mesh_idx;   // ContactInfo index for real contacts; -1 for self
  lv_obj_t* obj;
};
constexpr int k_map_markers_max = 32;
static MapMarker s_map_markers[k_map_markers_max] = {};

// Dotted self->contact link lines (toggled by the on-map button). Each line
// keeps its own persistent 2-point array — LVGL does not copy the points.
static bool       s_map_show_links = true;
static lv_obj_t*  s_map_link_objs[k_map_markers_max] = {};
static lv_point_t s_map_link_pts[k_map_markers_max][2];

static void freeMapMarkers() {
  for (auto& m : s_map_markers) {
    if (m.obj) { lv_obj_del(m.obj); m.obj = nullptr; }
    m.mesh_idx = -2;   // "slot empty" sentinel
  }
  for (auto& ln : s_map_link_objs) {
    if (ln) { lv_obj_del(ln); ln = nullptr; }
  }
}

static void openMarkerPopupForContact(int mesh_idx);
static void openContactActionSheet(uint32_t mesh_idx, bool is_repeater, const char* name);
static void openMapPicker(const int* idxs, int n);
// (onMapMarkerClickedCb removed — marker taps are now dispatched centrally
// from the canvas's RELEASED handler in mapCanvasEventCb. See the marker
// scan in the tap branch below.)

// Plot self + contacts onto the canvas at their lat/lon. Called every time
// tiles are re-rendered (pan, zoom, recenter, tab open) so markers stay
// pinned to the right pixel for the current center.
static void renderMapMarkers() {
  freeMapMarkers();
  if (!s_map_canvas) return;
  if (s_map_center_lat == 0.0 && s_map_center_lon == 0.0) return;
  // Markers share the pan layer with tiles so live-pan slides everything
  // together with a single set_pos.
  lv_obj_t* parent = s_map_pan_layer ? s_map_pan_layer : s_map_canvas;

  double cwx, cwy;
  latLonToWorldPx(s_map_center_lat, s_map_center_lon, s_map_zoom, &cwx, &cwy);

  // Self screen position (computed even if off-canvas, so link lines from an
  // off-screen self still point the right way; clipped to the canvas by LVGL).
  bool self_has = false;
  int  self_sx = 0, self_sy = 0;
  if (g_lv.task) {
    const double self_lat = g_lv.task->getNodeLat();
    const double self_lon = g_lv.task->getNodeLon();
    if (self_lat != 0.0 || self_lon != 0.0) {
      double swx, swy;
      latLonToWorldPx(self_lat, self_lon, s_map_zoom, &swx, &swy);
      self_sx = (int)(swx - cwx + k_map_canvas_w / 2);
      self_sy = (int)(swy - cwy + k_map_canvas_h / 2);
      self_has = true;
    }
  }

  // ---- Dotted links from self to each on-screen contact (drawn first so the
  //      markers sit on top). Toggled by the on-map links button.
  if (s_map_show_links && self_has) {
    auto clampc = [](int v) -> lv_coord_t {
      if (v < -2000) v = -2000;
      if (v >  2000) v =  2000;
      return (lv_coord_t)v;
    };
    int link_n = 0;
    for (uint32_t i = 0; i < the_mesh.getNumContacts() && link_n < k_map_markers_max; ++i) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(i, c)) continue;
      if (c.gps_lat == 0 && c.gps_lon == 0) continue;
      double mwx, mwy;
      latLonToWorldPx((double)c.gps_lat / 1.0e6, (double)c.gps_lon / 1.0e6,
                      s_map_zoom, &mwx, &mwy);
      const int sx = (int)(mwx - cwx + k_map_canvas_w / 2);
      const int sy = (int)(mwy - cwy + k_map_canvas_h / 2);
      if (sx < -8 || sx >= k_map_canvas_w + 8 || sy < -8 || sy >= k_map_canvas_h + 8) continue;
      s_map_link_pts[link_n][0].x = clampc(self_sx);
      s_map_link_pts[link_n][0].y = clampc(self_sy);
      s_map_link_pts[link_n][1].x = clampc(sx);
      s_map_link_pts[link_n][1].y = clampc(sy);
      lv_obj_t* ln = lv_line_create(parent);
      lv_line_set_points(ln, s_map_link_pts[link_n], 2);
      // Solid (not dashed): LVGL 8.3's SW renderer doesn't reliably dash
      // diagonal lines — a thin, semi-transparent line reads as a "link".
      lv_obj_set_style_line_width(ln, 2, LV_PART_MAIN);
      lv_obj_set_style_line_color(ln, lv_color_hex(0x6FA8DA), LV_PART_MAIN);
      lv_obj_set_style_line_opa(ln, LV_OPA_70, LV_PART_MAIN);
      lv_obj_set_style_line_rounded(ln, true, LV_PART_MAIN);
      lv_obj_clear_flag(ln, LV_OBJ_FLAG_CLICKABLE);
      s_map_link_objs[link_n] = ln;
      ++link_n;
    }
  }

  // ---- Self marker — crosshair (on top of the links).
  if (self_has &&
      self_sx >= -10 && self_sx < k_map_canvas_w + 10 &&
      self_sy >= -10 && self_sy < k_map_canvas_h + 10) {
    MapMarker& m = s_map_markers[0];
    m.mesh_idx = -1;
    m.obj = lv_label_create(parent);
    lv_label_set_text(m.obj, LV_SYMBOL_GPS);
    lv_obj_set_style_text_font(m.obj, &g_font_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(m.obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    // The glyph's optical center isn't at its bbox center — fudge the
    // align slightly so the crosshair lines up with the tile pixel.
    lv_obj_set_pos(m.obj, self_sx - 8, self_sy - 11);
  }

  // ---- Contact markers — colored circles sized for taps (14×14 with a
  //      2-px black border so they read on any tile background).
  int slot = 1;   // slot 0 reserved for self
  for (uint32_t i = 0; i < the_mesh.getNumContacts() && slot < k_map_markers_max; ++i) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(i, c)) continue;
    if (c.gps_lat == 0 && c.gps_lon == 0) continue;

    const double lat = (double)c.gps_lat / 1.0e6;
    const double lon = (double)c.gps_lon / 1.0e6;
    double mwx, mwy;
    latLonToWorldPx(lat, lon, s_map_zoom, &mwx, &mwy);
    const int sx = (int)(mwx - cwx + k_map_canvas_w / 2);
    const int sy = (int)(mwy - cwy + k_map_canvas_h / 2);
    // Cull off-screen markers — they'd just allocate widgets we never see.
    if (sx < -8 || sx >= k_map_canvas_w + 8 ||
        sy < -8 || sy >= k_map_canvas_h + 8) continue;

    // Color by contact type so a glance at the map maps to the chip
    // colors on the Contacts tab. Defaults to chat-peer orange.
    uint32_t color = 0xFF6F4D;
    if (c.type == ADV_TYPE_REPEATER) color = 0x4DA8FF;
    else if (c.type == ADV_TYPE_ROOM) color = 0xC9A24A;

    MapMarker& m = s_map_markers[slot];
    m.mesh_idx = (int)i;
    m.obj = lv_obj_create(parent);
    lv_obj_remove_style_all(m.obj);
    lv_obj_set_size(m.obj, 14, 14);
    lv_obj_set_pos(m.obj, sx - 7, sy - 7);
    lv_obj_set_style_bg_color(m.obj, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(m.obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(m.obj, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(m.obj, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(m.obj, 7, LV_PART_MAIN);
    // Markers are NOT clickable themselves — taps go to the canvas, whose
    // RELEASED handler scans all markers within ~16 px of the tap point.
    // This makes overlapping markers selectable (disambiguation sheet) and
    // also lets panning that starts on a marker fall through to the canvas
    // pan handler instead of being swallowed by the marker.
    lv_obj_clear_flag(m.obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(m.obj, LV_OBJ_FLAG_SCROLLABLE);
    ++slot;
  }
}

// (The on-map links toggle moved into the Map options popup below; the dotted
// self->contact link lines are now controlled by the "Show link lines" switch.)

// ===== Map options popup (gear button, top-right of the map) =================
// Holds the per-map settings that used to be one-off overlay buttons: the
// self->contact link lines, a "reload tiles" repair action, and an info/credits
// sheet. Centralising them frees the map's right edge and gives room to grow.
static lv_obj_t* s_map_opts_root = nullptr;

static void closeMapOptions() {
  if (s_map_opts_root) { lv_obj_del_async(s_map_opts_root); s_map_opts_root = nullptr; }
}
static void mapOptionsDismissCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  // Only dismiss on a tap of the dim backdrop itself, not a child (card) tap.
  if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;
  lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
  closeMapOptions();
}

// "Lines" switch inside the options popup.
static void mapOptLinesCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t* sw = lv_event_get_target(e);
  s_map_show_links = lv_obj_has_state(sw, LV_STATE_CHECKED);
  renderMapMarkers();   // rebuild links to reflect the new state
}

#if defined(HAS_TDECK_GT911)
// Map tile source toggle (in the map options popup): ON = read tiles off the microSD
// (fully offline, no server fetch); OFF = tile server + on-device cache.
static void mapOptTilesSdCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  const bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  touchPrefsSetTilesFromSd(on);
  s_tiles_from_sd = on;
  if (on) fmSdTryMount();        // mount now so the reload below can read from the card
  freeMapTiles();                // drop stale tile widgets → reload from the new source
  renderMapTiles();
  if (g_lv.task) g_lv.task->showAlert(on ? "Map tiles: microSD" : "Map tiles: server", 1400);
}
#endif

// "Reload tiles" — delete the currently-visible tiles from the LittleFS cache
// and re-queue them for download, so a corrupted/partial tile in view can be
// repaired without wiping the whole pack. Bounded to the 9 on-screen tiles at
// the current zoom (NOT a bulk area download — stays OSM-policy-friendly).
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
static void mapReloadVisibleTiles() {
  if (s_map_center_lat == 0.0 && s_map_center_lon == 0.0) {
    if (g_lv.task) g_lv.task->showAlert("Set your location first", 1600);
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (g_lv.task) g_lv.task->showAlert("Connect Wi-Fi to reload tiles", 2000);
    return;
  }
  double cwx, cwy;
  latLonToWorldPx(s_map_center_lat, s_map_center_lon, s_map_zoom, &cwx, &cwy);
  const int32_t ctx = (int32_t)floor(cwx / 256.0);
  const int32_t cty = (int32_t)floor(cwy / 256.0);
  // Drop the in-RAM tile widgets so the next render re-reads from disk (and
  // misses, since we delete the files below) → re-queues the download.
  freeMapTiles();
  int n = 0;
  for (int dy = -k_map_grid_radius; dy <= k_map_grid_radius; ++dy) {
    for (int dx = -k_map_grid_radius; dx <= k_map_grid_radius; ++dx) {
      const int32_t tx = ctx + dx, ty = cty + dy;
      char path[48];
      snprintf(path, sizeof(path), "/tiles/%u/%ld/%ld.jpg",
               (unsigned)s_map_zoom, (long)tx, (long)ty);
      if (s_tiles_fs_ready && s_tiles_fs.exists(path)) s_tiles_fs.remove(path);
      // Clear the dedup ring entry so queueTileForFetch doesn't skip it as
      // "seen recently".
      const uint32_t k = tileFetchDedupKey(s_map_zoom, tx, ty);
      for (int i = 0; i < k_tile_fetch_dedup_size; ++i)
        if (s_tile_fetch_dedup[i] == k) s_tile_fetch_dedup[i] = 0;
      ++n;
    }
  }
  char msg[40];
  snprintf(msg, sizeof(msg), "Reloading %d tiles\xE2\x80\xA6", n);
  if (g_lv.task) g_lv.task->showAlert(msg, 1600);
  renderMapTiles();      // misses on the just-deleted files → queues fresh fetches
  renderMapMarkers();
}
#endif

static void mapOptReloadCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeMapOptions();
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  mapReloadVisibleTiles();
#else
  if (g_lv.task) g_lv.task->showAlert("Tile reload needs Wi-Fi build", 1800);
#endif
}

// Map info / credits sheet — OSM attribution (policy requirement) plus a short
// explanation of how the tile fetching/caching works.
static void mapOptInfoCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeMapOptions();
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_map_opts_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_map_opts_root);
  lv_obj_set_size(s_map_opts_root, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_map_opts_root, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_map_opts_root, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_map_opts_root, LV_OPA_70, LV_PART_MAIN);
  lv_obj_clear_flag(s_map_opts_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_map_opts_root, mapOptionsDismissCb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* card = lv_obj_create(s_map_opts_root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, sw - 24, (sh - STATUSBAR_H) - 24);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  styleSurface(card, COLOR_PANEL, 8);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
  lv_obj_set_scroll_dir(card, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);
  addCloseXBadge(card, mapOptionsDismissCb);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "About the map");
  lv_obj_set_style_text_font(title, &g_font_14, LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_pos(title, 0, 2);

  lv_obj_t* lbl = lv_label_create(card);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, sw - 24 - 20);
  lv_obj_set_pos(lbl, 0, 28);
  lv_obj_set_style_text_font(lbl, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_label_set_text(lbl,
    "Map data \xC2\xA9 OpenStreetMap contributors.\n"
    "openstreetmap.org/copyright\n"
    "Licensed under the Open Database License (ODbL).\n\n"
    "How tiles work:\n"
    "The map is built from 256\xC3\x97256 \"slippy\" tiles. Only the tiles for the "
    "area you're viewing are fetched \xE2\x80\x94 there is no bulk pre-download.\n\n"
    "Because this device can't do HTTPS (not enough heap after Wi-Fi starts) "
    "and decodes JPEG far more cheaply than PNG, tiles come from the meshcomod "
    "proxy: it fetches the PNG from OpenStreetMap over HTTPS with an identifying "
    "User-Agent, re-encodes it as JPEG, and caches it. Your device then caches "
    "each tile to its own flash, so a tile is only downloaded once.\n\n"
    "Use Options \xE2\x86\x92 Reload tiles to re-download the tiles currently in "
    "view if one looks corrupted.");
}

// Open the options popup: a compact bottom-anchored card with the Lines switch
// + Reload + Info rows.
static void openMapOptions() {
  closeMapOptions();
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_map_opts_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_map_opts_root);
  lv_obj_set_size(s_map_opts_root, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_map_opts_root, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_map_opts_root, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_map_opts_root, LV_OPA_50, LV_PART_MAIN);
  lv_obj_clear_flag(s_map_opts_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_map_opts_root, mapOptionsDismissCb, LV_EVENT_CLICKED, nullptr);

  const lv_coord_t cardw = sw - 24;
  lv_obj_t* card = lv_obj_create(s_map_opts_root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, cardw, 210);   // tall enough for the (T-Deck) SD-tiles row + About
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 8);
  styleSurface(card, COLOR_PANEL, 8);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 12, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "Map options");
  lv_obj_set_style_text_font(title, &g_font_14, LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_pos(title, 0, 0);
  int y = 26;

  // Row: Lines toggle.
  lv_obj_t* ll = lv_label_create(card);
  lv_label_set_text(ll, "Show link lines");
  lv_obj_set_style_text_color(ll, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(ll, &g_font_14, LV_PART_MAIN);
  lv_obj_set_pos(ll, 2, y + 4);
  lv_obj_t* sw_lines = lv_switch_create(card);
  lv_obj_align(sw_lines, LV_ALIGN_TOP_RIGHT, 0, y);
  if (s_map_show_links) lv_obj_add_state(sw_lines, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw_lines, mapOptLinesCb, LV_EVENT_VALUE_CHANGED, nullptr);
  y += 40;

#if defined(HAS_TDECK_GT911)
  // Row: tile source — microSD (offline) vs the tile server.
  {
    lv_obj_t* tl = lv_label_create(card);
    lv_label_set_text(tl, "Tiles from SD card");
    lv_obj_set_style_text_color(tl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(tl, &g_font_14, LV_PART_MAIN);
    lv_obj_set_pos(tl, 2, y + 4);
    lv_obj_t* sw_sd = lv_switch_create(card);
    lv_obj_align(sw_sd, LV_ALIGN_TOP_RIGHT, 0, y);
    if (s_tiles_from_sd) lv_obj_add_state(sw_sd, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw_sd, mapOptTilesSdCb, LV_EVENT_VALUE_CHANGED, nullptr);
    y += 40;
  }
#endif

  auto mk_row_btn = [&](const char* txt, lv_event_cb_t cb) {
    lv_obj_t* b = lv_btn_create(card);
    lv_obj_set_size(b, cardw - 24, 38);
    lv_obj_set_pos(b, 0, y);
    styleButton(b);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &g_font_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
    y += 40;
  };
  mk_row_btn(LV_SYMBOL_REFRESH "  Reload tiles in view", mapOptReloadCb);
  mk_row_btn(LV_SYMBOL_EYE_OPEN "  About / credits",     mapOptInfoCb);

  // Close X (top-right of the card). Added last so move_foreground() keeps it
  // above the title/switch/rows and reliably tappable. Same dismiss path as the
  // backdrop tap. The 32×32 hit area only grazes the link-lines switch (y=30)
  // by ~2px, which is imperceptible.
  addCloseXBadge(card, mapOptionsDismissCb);
}

static void mapOpenOptionsCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  openMapOptions();
}

// ----- Marker tap popup -----
//
// Reuses openContactActionSheet (the same sheet you get tapping a row in
// the Contacts tab) so the user gets the full action set — send DM,
// telemetry, range test, etc. — without us duplicating UI.
static void openMarkerPopupForContact(int mesh_idx) {
  if (mesh_idx < 0) {
    // Self marker — show a small toast with our coords. Cheaper than a
    // full popup; the user already has their own profile screen.
    if (g_lv.task) {
      char buf[40];
      snprintf(buf, sizeof(buf), "Self  %.4f, %.4f",
               g_lv.task->getNodeLat(), g_lv.task->getNodeLon());
      g_lv.task->showAlert(buf, 1500);
    }
    return;
  }
  ContactInfo c;
  if (!the_mesh.getContactByIdx((uint32_t)mesh_idx, c)) return;
  const bool is_repeater = (c.type == ADV_TYPE_REPEATER);
  openContactActionSheet((uint32_t)mesh_idx, is_repeater, c.name);
}

// ----- Overlapping-markers picker -----
//
// When 2+ markers fall within the tap-forgiveness radius, the canvas
// dispatcher calls this with the list of hits. We show a vertical list of
// contact rows; tapping one closes the picker and routes through the
// normal openMarkerPopupForContact path. Self (mesh_idx == -1) is shown
// as "(you)" so it's pickable even when other markers sit on top of it.
static lv_obj_t* s_map_picker_root = nullptr;

static void closeMapPicker() {
  if (s_map_picker_root) {
    lv_obj_del_async(s_map_picker_root);
    s_map_picker_root = nullptr;
  }
}
static void mapPickerBackdropCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_indev_t* a = lv_indev_get_act();
  if (a) lv_indev_wait_release(a);
  closeMapPicker();
}
static void mapPickerRowCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const int idx = (int)(intptr_t)lv_event_get_user_data(e);
  lv_indev_t* a = lv_indev_get_act();
  if (a) lv_indev_wait_release(a);
  closeMapPicker();
  openMarkerPopupForContact(idx);
}

static void openMapPicker(const int* idxs, int n) {
  closeMapPicker();
  if (n <= 0) return;
  if (n > 6) n = 6;   // cap card height; in practice rare to overlap > a few

  lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_map_picker_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_map_picker_root);
  lv_obj_set_size(s_map_picker_root, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_map_picker_root, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_map_picker_root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_map_picker_root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_map_picker_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_map_picker_root, mapPickerBackdropCb, LV_EVENT_CLICKED, nullptr);

  const int card_w = 220;
  const int btn_h  = 34;
  const int gap    = 4;
  const int hdr_h  = 30;
  const int pad    = 10;
  const int card_h = hdr_h + n * (btn_h + gap) + pad;

  lv_obj_t* card = lv_obj_create(s_map_picker_root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, pad, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  addCloseXBadge(card, mapPickerBackdropCb);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "Nearby on map");
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &g_font_14, LV_PART_MAIN);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, card_w - 2 * pad - 32);   // leave room for X
  lv_obj_set_pos(title, 0, 0);

  int y = hdr_h;
  for (int i = 0; i < n; ++i) {
    const int midx = idxs[i];
    char row_label[40];
    if (midx < 0) {
      snprintf(row_label, sizeof(row_label), LV_SYMBOL_GPS "  (you)");
    } else {
      ContactInfo c;
      if (!the_mesh.getContactByIdx((uint32_t)midx, c)) continue;
      const char* icon = (c.type == ADV_TYPE_REPEATER) ? LV_SYMBOL_CHARGE :
                         (c.type == ADV_TYPE_ROOM)     ? LV_SYMBOL_LOOP   :
                                                          LV_SYMBOL_ENVELOPE;
      char nm[24];
      copyUtf8ReplacingMissingGlyphs(&g_font_14, nm, sizeof(nm), c.name);
      snprintf(row_label, sizeof(row_label), "%s  %s",
               icon, nm[0] ? nm : "(unnamed)");
    }
    lv_obj_t* b = lv_btn_create(card);
    lv_obj_set_size(b, card_w - 2 * pad, btn_h);
    lv_obj_set_pos(b, 0, y);
    styleButton(b);
    lv_obj_add_event_cb(b, mapPickerRowCb, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>((intptr_t)midx));
    lv_obj_t* lbl = lv_label_create(b);
    lv_label_set_text(lbl, row_label);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, card_w - 2 * pad - 16);
    lv_obj_set_style_text_font(lbl, &g_font_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
    y += btn_h + gap;
  }
}

// ===== "Show contact on map" — list every contact that has GPS coords, tap one
// to recenter the map on it. =================================================
static lv_obj_t* s_map_contacts_root = nullptr;
static lv_obj_t* s_map_contacts_list = nullptr;   // the scrollable rows container (rebuilt on sort)
// Sort order for the "contacts on map" list. Persists across opens this session.
enum MapContactsSort : uint8_t { MC_SORT_NAME = 0, MC_SORT_DIST, MC_SORT_HEARD, MC_SORT_COUNT };
static uint8_t s_map_contacts_sort = MC_SORT_DIST;   // distance is the most useful default
static const char* mapContactsSortName(uint8_t s) {
  switch (s) { case MC_SORT_NAME: return "Name"; case MC_SORT_DIST: return "Distance";
               case MC_SORT_HEARD: return "Heard"; default: return "?"; }
}

static void closeMapContacts() {
  if (s_map_contacts_root) { lv_obj_del_async(s_map_contacts_root); s_map_contacts_root = nullptr; }
  s_map_contacts_list = nullptr;
}
static void mapContactsBackdropCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;   // backdrop only
  lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
  closeMapContacts();
}
// Tap a row → recenter the map on that contact + zoom in a touch + close.
static void mapContactsRowCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const int midx = (int)(intptr_t)lv_event_get_user_data(e);
  lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
  closeMapContacts();
  ContactInfo c;
  if (!the_mesh.getContactByIdx((uint32_t)midx, c)) return;
  if (c.gps_lat == 0 && c.gps_lon == 0) return;
  s_map_center_lat = (double)c.gps_lat / 1.0e6;
  s_map_center_lon = (double)c.gps_lon / 1.0e6;
  // Zoom in a step (capped) so "show on map" frames the contact closely.
  if (s_map_zoom < k_map_zoom_max) s_map_zoom = (uint8_t)(s_map_zoom + 1);
  renderMapTiles();
  renderMapMarkers();
  refreshMapInfoLabel();
  if (g_lv.task) {
    char nm[24];
    copyUtf8ReplacingMissingGlyphs(&g_font_14, nm, sizeof(nm), c.name);
    char msg[40];
    snprintf(msg, sizeof(msg), "Centered on %s", nm[0] ? nm : "contact");
    g_lv.task->showAlert(msg, 1200);
  }
}

// One GPS-bearing contact, with the derived sort keys precomputed.
struct MapContactEntry {
  int      midx;
  char     name[24];
  uint8_t  type;
  int32_t  lat_e6, lon_e6;
  double   dist_km;       // -1 if self has no fix
  uint32_t age_secs;      // 0 = unknown
};

// (Re)build the scrollable row list from the current sort. Separated from the
// popup shell so the Sort button can re-list without rebuilding the card.
static void mapContactsFillList() {
  if (!s_map_contacts_list) return;
  lv_obj_clean(s_map_contacts_list);

  // Collect GPS-bearing contacts + their distance/age keys.
  static MapContactEntry* ents = (MapContactEntry*)psAlloc(sizeof(MapContactEntry) * 64);
  int n = 0;
  const double self_lat = g_lv.task ? g_lv.task->getNodeLat() : 0.0;
  const double self_lon = g_lv.task ? g_lv.task->getNodeLon() : 0.0;
  uint32_t now_secs = 0;
  { mesh::RTCClock* rtc = the_mesh.getRTCClock(); if (rtc) now_secs = rtc->getCurrentTime(); }
  const uint32_t total = the_mesh.getNumContacts();
  for (uint32_t i = 0; i < total && n < 64; ++i) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(i, c)) continue;
    if (c.gps_lat == 0 && c.gps_lon == 0) continue;
    MapContactEntry& e = ents[n];
    e.midx = (int)i;
    e.type = c.type;
    e.lat_e6 = c.gps_lat; e.lon_e6 = c.gps_lon;
    copyUtf8ReplacingMissingGlyphs(&g_font_14, e.name, sizeof(e.name), c.name);
    e.dist_km = (self_lat == 0.0 && self_lon == 0.0) ? -1.0
                : contactDistanceKm(self_lat, self_lon,
                                    (double)c.gps_lat / 1.0e6, (double)c.gps_lon / 1.0e6);
    e.age_secs = (now_secs > c.last_advert_timestamp && c.last_advert_timestamp != 0)
                 ? (now_secs - c.last_advert_timestamp) : 0;
    ++n;
  }

  // Sort by the chosen key. (Stable enough for this small list.)
  qsort(ents, n, sizeof(MapContactEntry), [](const void* a, const void* b) -> int {
    const MapContactEntry* ea = static_cast<const MapContactEntry*>(a);
    const MapContactEntry* eb = static_cast<const MapContactEntry*>(b);
    switch (s_map_contacts_sort) {
      case MC_SORT_DIST: {
        // Unknown distance (-1) sorts last.
        const double da = ea->dist_km < 0 ? 1e12 : ea->dist_km;
        const double db = eb->dist_km < 0 ? 1e12 : eb->dist_km;
        if (da < db) return -1; if (da > db) return 1; break;
      }
      case MC_SORT_HEARD: {
        // Most recently heard first; unknown (0) last.
        const uint32_t aa = ea->age_secs ? ea->age_secs : 0xFFFFFFFFu;
        const uint32_t ab = eb->age_secs ? eb->age_secs : 0xFFFFFFFFu;
        if (aa < ab) return -1; if (aa > ab) return 1; break;
      }
      default: break;   // MC_SORT_NAME → fall through to name compare
    }
    return strcasecmp(ea->name, eb->name);
  });

  if (n == 0) {
    lv_obj_t* empty = lv_label_create(s_map_contacts_list);
    lv_label_set_text(empty, "No contacts have shared\na GPS location yet.");
    lv_obj_set_style_text_color(empty, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(empty, &g_font_14, LV_PART_MAIN);
    return;
  }

  // Row width from the display (the list's own width may not be resolved yet on
  // the first fill, right after creation).
  const lv_coord_t rw = (lv_disp_get_hor_res(nullptr) - 24) - 2 * 10 - 8;
  for (int i = 0; i < n; ++i) {
    const MapContactEntry& e = ents[i];
    const char* icon = (e.type == ADV_TYPE_REPEATER) ? LV_SYMBOL_CHARGE :
                       (e.type == ADV_TYPE_ROOM)     ? LV_SYMBOL_LOOP   : LV_SYMBOL_GPS;
    lv_obj_t* b = lv_btn_create(s_map_contacts_list);
    lv_obj_set_size(b, rw, 46);
    styleButton(b);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1A1B1C), LV_PART_MAIN);
    lv_obj_set_style_pad_all(b, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(b, mapContactsRowCb, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>((intptr_t)e.midx));
    // Line 1 (left): icon + name. Line 1 (right): distance · age. Line 2: coords.
    char l1[40];
    snprintf(l1, sizeof(l1), "%s  %s", icon, e.name[0] ? e.name : "(unnamed)");
    lv_obj_t* nl = lv_label_create(b);
    lv_label_set_text(nl, l1);
    lv_label_set_long_mode(nl, LV_LABEL_LONG_DOT);
    // Constrain BOTH width and height to one line: LONG_DOT otherwise wraps to a
    // second line (dotting only the overflow), which a long name then pushed
    // down onto the coords row. A 1-line height forces single-line + ellipsis.
    lv_obj_set_width(nl, rw - 96);            // leave room for the dist/age badge on the right
    lv_obj_set_height(nl, lv_font_get_line_height(&g_font_14));
    lv_obj_set_style_text_font(nl, &g_font_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(nl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_align(nl, LV_ALIGN_TOP_LEFT, 8, 5);

    char dist[16] = "";
    formatDistanceBadge(dist, sizeof(dist), self_lat, self_lon, e.lat_e6, e.lon_e6);
    char age[12]; formatAgeBadge(age, sizeof(age), e.age_secs);
    char meta[28];
    if (dist[0]) snprintf(meta, sizeof(meta), "%s \xC2\xB7 %s", dist, age);
    else         snprintf(meta, sizeof(meta), "%s", age);
    lv_obj_t* ml = lv_label_create(b);
    lv_label_set_text(ml, meta);
    lv_obj_set_style_text_font(ml, &g_font_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(ml, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_align(ml, LV_ALIGN_TOP_RIGHT, -8, 6);

    char co[32];
    snprintf(co, sizeof(co), "%.5f, %.5f",
             (double)e.lat_e6 / 1.0e6, (double)e.lon_e6 / 1.0e6);
    lv_obj_t* cl = lv_label_create(b);
    lv_label_set_text(cl, co);
    lv_obj_set_style_text_font(cl, &g_font_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(cl, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_align(cl, LV_ALIGN_BOTTOM_LEFT, 8, -4);
  }
}

// Header Sort button label (shows the CURRENT key) — updated on cycle.
static lv_obj_t* s_map_contacts_sort_lbl = nullptr;
static void mapContactsSortCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  s_map_contacts_sort = (uint8_t)((s_map_contacts_sort + 1) % MC_SORT_COUNT);
  if (s_map_contacts_sort_lbl)
    lv_label_set_text_fmt(s_map_contacts_sort_lbl, LV_SYMBOL_SHUFFLE " %s",
                          mapContactsSortName(s_map_contacts_sort));
  mapContactsFillList();
}

static void openMapContactsList() {
  closeMapContacts();
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_map_contacts_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_map_contacts_root);
  lv_obj_set_size(s_map_contacts_root, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_map_contacts_root, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_map_contacts_root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_map_contacts_root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_map_contacts_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_map_contacts_root, mapContactsBackdropCb, LV_EVENT_CLICKED, nullptr);

  const int pad = 10;
  const lv_coord_t card_w = sw - 24;
  const lv_coord_t card_h = sh - STATUSBAR_H - 20;
  lv_obj_t* card = lv_obj_create(s_map_contacts_root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  styleSurface(card, COLOR_PANEL, 8);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, pad, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  addCloseXBadge(card, mapContactsBackdropCb);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, LV_SYMBOL_GPS "  On map");
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &g_font_14, LV_PART_MAIN);
  lv_obj_set_pos(title, 0, 2);

  // Sort button (header, right of the title; left of the close X).
  lv_obj_t* sort_b = lv_btn_create(card);
  lv_obj_set_size(sort_b, 108, 26);
  lv_obj_align(sort_b, LV_ALIGN_TOP_RIGHT, -28, 0);
  styleButton(sort_b);
  lv_obj_set_style_bg_color(sort_b, lv_color_hex(0x1A1B1C), LV_PART_MAIN);
  lv_obj_add_event_cb(sort_b, mapContactsSortCb, LV_EVENT_CLICKED, nullptr);
  s_map_contacts_sort_lbl = lv_label_create(sort_b);
  lv_label_set_text_fmt(s_map_contacts_sort_lbl, LV_SYMBOL_SHUFFLE " %s",
                        mapContactsSortName(s_map_contacts_sort));
  lv_obj_set_style_text_font(s_map_contacts_sort_lbl, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_map_contacts_sort_lbl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_center(s_map_contacts_sort_lbl);

  // Scrollable list (filled + re-filled by mapContactsFillList).
  s_map_contacts_list = lv_obj_create(card);
  lv_obj_remove_style_all(s_map_contacts_list);
  lv_obj_set_size(s_map_contacts_list, card_w - 2 * pad, card_h - 2 * pad - 34);
  lv_obj_set_pos(s_map_contacts_list, 0, 34);
  lv_obj_set_flex_flow(s_map_contacts_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_map_contacts_list, 6, LV_PART_MAIN);
  lv_obj_set_scroll_dir(s_map_contacts_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_map_contacts_list, LV_SCROLLBAR_MODE_AUTO);
  mapContactsFillList();
}

static void mapOpenContactsCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  openMapContactsList();
}

// ----- Pan -----
//
// Touch-and-drag on the canvas with LIVE preview. PRESSING fires every
// 15 ms (LV_INDEV_DEF_READ_PERIOD); we just translate every existing tile
// and marker by the incremental touch delta. lv_obj_set_pos is cheap
// (style change + invalidation, no decode), so the map slides under the
// finger in real time.
//
// On RELEASED we compute the total delta, convert to lat/lon, and call
// renderMapTiles — which now reuses tile slots whose (z,x,y) is still
// wanted, so only newly-visible tiles get loaded from SPIFFS.
//
// Movement under 6 px on release is treated as a tap; we re-snap any
// pixels of jitter back to the proper grid.
static bool       s_map_panning           = false;
static lv_point_t s_map_pan_anchor        = {0, 0};
static lv_point_t s_map_pan_last          = {0, 0};
static double     s_map_pan_start_lat     = 0.0;
static double     s_map_pan_start_lon     = 0.0;

// Translate the pan layer by (dx, dy). All tiles + markers ride on the
// layer, so this is a SINGLE set_pos regardless of how many children sit
// on top. Compare to the previous per-child loop which made LVGL emit ~38
// dirty rects per PRESSING tick and merge them into a whole-canvas redraw.
static void shiftMapChildren(int dx, int dy) {
  if (!s_map_pan_layer || (dx == 0 && dy == 0)) return;
  lv_obj_set_pos(s_map_pan_layer,
                 lv_obj_get_x(s_map_pan_layer) + dx,
                 lv_obj_get_y(s_map_pan_layer) + dy);
}

static void mapCanvasEventCb(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_PRESSED  && code != LV_EVENT_PRESSING &&
      code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) return;
  lv_indev_t* indev = lv_indev_get_act();
  if (!indev) return;
  lv_point_t p;
  lv_indev_get_point(indev, &p);

  if (code == LV_EVENT_PRESSED) {
    s_map_pan_anchor    = p;
    s_map_pan_last      = p;
    s_map_pan_start_lat = s_map_center_lat;
    s_map_pan_start_lon = s_map_center_lon;
    s_map_panning       = true;
    return;
  }
  if (!s_map_panning) return;

  if (code == LV_EVENT_PRESSING) {
    // Incremental delta since the last PRESSING tick — slide children.
    const int idx = p.x - s_map_pan_last.x;
    const int idy = p.y - s_map_pan_last.y;
    if (idx == 0 && idy == 0) return;
    shiftMapChildren(idx, idy);
    s_map_pan_last = p;
    return;
  }
  // RELEASED or PRESS_LOST — finalize.
  s_map_panning = false;
  const int tdx = p.x - s_map_pan_anchor.x;
  const int tdy = p.y - s_map_pan_anchor.y;
  // 6-px deadzone: treat as a tap. Snap children back so any sub-pixel
  // jitter we introduced during PRESSING goes away.
  if (tdx > -6 && tdx < 6 && tdy > -6 && tdy < 6) {
    if (tdx != 0 || tdy != 0) shiftMapChildren(-tdx, -tdy);
    // Centralized marker hit-test: find every marker whose center is
    // within 16 px of the tap. One hit → open that contact. Multiple
    // hits (overlapping markers) → open a picker so the user can choose.
    // 16 px is a finger-forgiveness radius, larger than the 14-px marker
    // diameter so an off-center tap still scores.
    int hits[k_map_markers_max];
    int n_hits = 0;
    const int R2 = 16 * 16;
    for (auto& m : s_map_markers) {
      if (!m.obj) continue;
      lv_area_t a;
      lv_obj_get_coords(m.obj, &a);
      const int mx = (a.x1 + a.x2) / 2;
      const int my = (a.y1 + a.y2) / 2;
      const int ddx = mx - p.x;
      const int ddy = my - p.y;
      if (ddx * ddx + ddy * ddy <= R2) {
        if (n_hits < k_map_markers_max) hits[n_hits++] = m.mesh_idx;
      }
    }
    if (n_hits == 1) {
      openMarkerPopupForContact(hits[0]);
    } else if (n_hits > 1) {
      openMapPicker(hits, n_hits);
    }
    return;
  }
  double start_wx, start_wy;
  latLonToWorldPx(s_map_pan_start_lat, s_map_pan_start_lon, s_map_zoom,
                  &start_wx, &start_wy);
  double new_lat, new_lon;
  // Finger right = content shifts right under finger = center shifts LEFT,
  // so we subtract the touch delta from the start world-px center.
  worldPxToLatLon(start_wx - tdx, start_wy - tdy, s_map_zoom, &new_lat, &new_lon);
  s_map_center_lat = new_lat;
  s_map_center_lon = new_lon;
  // Slot reuse means existing tiles get repositioned (not re-decoded);
  // only the newly-visible 1-3 tiles trigger a SPIFFS read.
  renderMapTiles();
  renderMapMarkers();
  refreshMapInfoLabel();
}

// ----- Zoom + recenter -----
//
// Zoom PROBES whether the requested level is usable at the current center
// before committing: the center tile must already be cached (offline .jpg
// pack OR Wi-Fi-fetched .png), or Wi-Fi must be up so renderMapTiles can
// download it on the fly. With "zoom packs" we only cache the min + max levels
// per location, so the buttons JUMP to the nearest usable level (skipping the
// uncached levels in between) rather than dead-ending — e.g. one zoom-out from
// z14 lands on the cached z12 overview instead of refusing at the empty z13.
#if defined(ESP32)
static bool mapZoomReachable(uint8_t z) {
  if (!s_tiles_fs_ready) return true;     // can't probe — don't block the user
  double wx, wy;
  latLonToWorldPx(s_map_center_lat, s_map_center_lon, z, &wx, &wy);
  const long tx = (long)floor(wx / 256.0);
  const long ty = (long)floor(wy / 256.0);
  char path[48];
  snprintf(path, sizeof(path), "/tiles/%u/%ld/%ld.jpg", (unsigned)z, tx, ty);
  if (s_tiles_fs.exists(path)) return true;
  snprintf(path, sizeof(path), "/tiles/%u/%ld/%ld.png", (unsigned)z, tx, ty);
  if (s_tiles_fs.exists(path)) return true;
#if defined(MULTI_TRANSPORT_COMPANION)
  if (WiFi.status() == WL_CONNECTED) return true;   // renderMapTiles will fetch it
#endif
  return false;
}
#endif
static void mapZoomInCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (s_map_zoom >= k_map_zoom_max) return;
#if defined(ESP32)
  uint8_t want = 0;
  for (uint8_t z = s_map_zoom + 1; z <= k_map_zoom_max; ++z) {
    if (mapZoomReachable(z)) { want = z; break; }
  }
  if (!want) {
    if (g_lv.task) g_lv.task->showAlert("Max zoom for this pack", 1200);
    return;
  }
#else
  const uint8_t want = s_map_zoom + 1;
#endif
  s_map_zoom = want;
  renderMapTiles();
  renderMapMarkers();
  refreshMapInfoLabel();
}
static void mapZoomOutCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (s_map_zoom <= k_map_zoom_min) return;
#if defined(ESP32)
  uint8_t want = 0;
  for (uint8_t z = s_map_zoom - 1; z >= k_map_zoom_min; --z) {
    if (mapZoomReachable(z)) { want = z; break; }
    if (z == k_map_zoom_min) break;   // guard: z is unsigned, don't wrap below min
  }
  if (!want) {
    if (g_lv.task) g_lv.task->showAlert("Min zoom for this pack", 1200);
    return;
  }
#else
  const uint8_t want = s_map_zoom - 1;
#endif
  s_map_zoom = want;
  renderMapTiles();
  renderMapMarkers();
  refreshMapInfoLabel();
}
static void mapRecenterCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (!g_lv.task) return;
  s_map_center_lat = g_lv.task->getNodeLat();
  s_map_center_lon = g_lv.task->getNodeLon();
  renderMapTiles();
  renderMapMarkers();
  refreshMapInfoLabel();
}

// Recenters on self GPS and rebuilds the tile grid. Called from tabChangedCb
// every time the user switches TO the Map tab.
static void onMapTabActivated() {
  // The entire UI is now clocked at 160 MHz from UITask::begin, so there's
  // no per-tab boost dance here anymore. (Tried 240 MHz briefly — the
  // PSRAM bus tightens enough at that clock for the SJPG decoder to
  // emit occasional RGB565 noise. 160 MHz is the sweet spot.)
  if (g_lv.task) {
    s_map_center_lat = g_lv.task->getNodeLat();
    s_map_center_lon = g_lv.task->getNodeLon();
  }
  // One-time auto-snap to the highest zoom level the pack actually
  // contains around this center. After this, the user's zoom-in/out
  // taps are honoured verbatim — they can pop above the pack and see
  // "no tile pack" (a clear cue to tap − to go back).
  {
    const uint8_t best = bestAvailableZoom(s_map_center_lat, s_map_center_lon);
    if (best != 0) s_map_zoom = best;
  }
  // 9 SPIFFS reads + JPEG decodes take ~1-3 seconds on first paint. Show a
  // visible "loading" hint and force an LVGL render pass BEFORE we block on
  // the actual tile reads, so the user doesn't think the device froze.
  if (s_map_status_lbl) {
    lv_label_set_text(s_map_status_lbl, "Loading map\xe2\x80\xa6");
    lv_obj_clear_flag(s_map_status_lbl, LV_OBJ_FLAG_HIDDEN);
  }
  lv_refr_now(NULL);   // drain one frame so the label paints before we block
  renderMapTiles();
  renderMapMarkers();
  refreshMapInfoLabel();
}

// Immersive map chrome: on the Map tab the status bar, bottom info strip and
// tab bar all go transparent so the (full-screen) map shows through behind them.
// Because OSM tiles are LIGHT, the status-bar text/icons are switched to BLACK
// for legibility, and reverted to the normal off-white when leaving the tab.
static void applyMapChrome(bool on) {
  // ---- Background tile canvas: show it + make the tabview see-through so it
  //      shows behind the (transparent) chrome. Hidden + opaque off-map. ----
  if (s_map_canvas) {
    if (on) { lv_obj_clear_flag(s_map_canvas, LV_OBJ_FLAG_HIDDEN); lv_obj_move_background(s_map_canvas); }
    else    { lv_obj_add_flag(s_map_canvas, LV_OBJ_FLAG_HIDDEN); }
  }
  if (g_lv.tabview) {
    // The tabview + its content + the Map page must be transparent on the map
    // tab so the background canvas shows through; opaque otherwise.
    lv_obj_set_style_bg_opa(g_lv.tabview, on ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t* tvc = lv_tabview_get_content(g_lv.tabview);
    if (tvc) lv_obj_set_style_bg_opa(tvc, on ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_MAIN);
  }
  if (s_map_page)
    lv_obj_set_style_bg_opa(s_map_page, on ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_MAIN);
  // ---- Status bar (lv_layer_sys, floats above the map) ----
  if (g_statusbar.root) {
    lv_obj_set_style_bg_opa(g_statusbar.root, on ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(g_statusbar.root, on ? LV_OPA_TRANSP : LV_OPA_30, LV_PART_MAIN);
    const lv_color_t fg     = lv_color_hex(on ? 0x000000 : COLOR_TEXT);
    const lv_color_t fg_sub = lv_color_hex(on ? 0x000000 : COLOR_SUB);
    if (g_statusbar.left_label) lv_obj_set_style_text_color(g_statusbar.left_label, fg, LV_PART_MAIN);
    if (g_statusbar.batt_icon)  lv_obj_set_style_text_color(g_statusbar.batt_icon, fg, LV_PART_MAIN);
    if (g_statusbar.batt_pct)   lv_obj_set_style_text_color(g_statusbar.batt_pct, fg_sub, LV_PART_MAIN);
    if (g_statusbar.clock)      lv_obj_set_style_text_color(g_statusbar.clock, fg_sub, LV_PART_MAIN);
    if (g_statusbar.conn_icon)  lv_obj_set_style_text_color(g_statusbar.conn_icon, fg_sub, LV_PART_MAIN);
  }
  // ---- Tab bar (bottom menu) — translucent so the map shows through ----
  if (g_lv.tabview) {
    lv_obj_t* btns = lv_tabview_get_tab_btns(g_lv.tabview);
    if (btns) {
      // Fully transparent on the map — the black icons read directly over the
      // map tiles, no grey bar behind them.
      lv_obj_set_style_bg_opa(btns, on ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_text_color(btns, lv_color_hex(on ? 0x101010 : COLOR_SUB), LV_PART_ITEMS);
      lv_obj_set_style_text_color(btns, lv_color_hex(on ? 0x000000 : COLOR_TEXT),
                                  LV_PART_ITEMS | LV_STATE_CHECKED);
    }
  }
}

static void makeMapTab(lv_obj_t* tab) {
  lv_obj_set_scroll_dir(tab, LV_DIR_NONE);
  lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(tab, COLOR_BG, 0);
  lv_obj_set_style_pad_all(tab, 0, LV_PART_MAIN);
  s_map_page = tab;

  // Full-screen map. The TILES live on s_map_canvas — a full-screen surface
  // parented to the SCREEN ROOT and pushed to the background, so it sits BEHIND
  // the tabview (and thus paints behind the transparent status bar + tab bar on
  // the Map tab). Touch can't reach it there (the tabview's transparent map page
  // is on top), so panning is driven by a transparent touch-catcher in this
  // page (below). All tile/marker projection reads k_map_canvas_w/h → a
  // full-screen canvas projects to the full screen.
  k_map_canvas_w = lv_disp_get_hor_res(nullptr);
  k_map_canvas_h = lv_disp_get_ver_res(nullptr);
  constexpr int kMapInfoH = 34;   // bottom info strip height (floats over the map)

  s_map_canvas = lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(s_map_canvas);
  lv_obj_set_size(s_map_canvas, k_map_canvas_w, k_map_canvas_h);
  lv_obj_set_pos(s_map_canvas, 0, 0);
  lv_obj_set_style_bg_color(s_map_canvas, lv_color_hex(0x0A0B0C), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_map_canvas, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_map_canvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(s_map_canvas, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_move_background(s_map_canvas);              // behind the tabview
  lv_obj_add_flag(s_map_canvas, LV_OBJ_FLAG_HIDDEN); // shown only on the Map tab
  // Pan layer — a transparent container that holds tile widgets + marker
  // widgets. Sliding the pan layer's position during a finger drag moves
  // everything as a unit (one invalidation per frame instead of 19).
  s_map_pan_layer = lv_obj_create(s_map_canvas);
  lv_obj_remove_style_all(s_map_pan_layer);
  lv_obj_set_size(s_map_pan_layer, k_map_canvas_w, k_map_canvas_h);
  lv_obj_set_pos(s_map_pan_layer, 0, 0);
  lv_obj_set_style_bg_opa(s_map_pan_layer, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_clear_flag(s_map_pan_layer, LV_OBJ_FLAG_SCROLLABLE);
  // Touch events must still reach the canvas — make the pan layer's tap
  // events bubble (LV_OBJ_FLAG_EVENT_BUBBLE) but also let the canvas catch
  // its own PRESSED/PRESSING directly by NOT marking the layer clickable.
  lv_obj_clear_flag(s_map_pan_layer, LV_OBJ_FLAG_CLICKABLE);
  // Placeholder text until tiles render here.
  s_map_status_lbl = lv_label_create(s_map_canvas);
  lv_label_set_long_mode(s_map_status_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_map_status_lbl, k_map_canvas_w - 20);
  lv_obj_set_style_text_color(s_map_status_lbl, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_map_status_lbl, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_align(s_map_status_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_text(s_map_status_lbl,
      "Map — no tile pack on SPIFFS yet.\n\n"
      "Upload a tile pack to /tiles/<z>/<x>/<y>.jpg "
      "with the host-side generator.");
  lv_obj_center(s_map_status_lbl);

  // Bottom corner read-outs — transparent black text over the map, tucked into
  // the bottom corners UNDER the tab-bar icons: coords bottom-LEFT, marker /
  // download count bottom-RIGHT. A subtle light halo keeps them legible over
  // dark map patches.
  auto style_corner = [&](lv_obj_t* l) {
    lv_obj_set_style_text_color(l, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, &g_font_12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(l, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(l, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(l, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(l, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(l, 3, LV_PART_MAIN);
  };
  s_map_info_lbl = lv_label_create(tab);   // coords (bottom-left)
  style_corner(s_map_info_lbl);
  lv_label_set_text(s_map_info_lbl, "—");
  lv_obj_align(s_map_info_lbl, LV_ALIGN_BOTTOM_LEFT, 2, -2);
  s_map_count_lbl = lv_label_create(tab);  // marker / status count (bottom-right)
  style_corner(s_map_count_lbl);
  lv_label_set_text(s_map_count_lbl, "");
  lv_obj_align(s_map_count_lbl, LV_ALIGN_BOTTOM_RIGHT, -2, -2);
  (void)kMapInfoH;

  // Touch catcher: the tiles live on the background canvas (behind the tabview)
  // and can't receive touch, so a full-page transparent layer in THIS tab page
  // drives pan/drag. It's the FIRST child (created before the overlay buttons +
  // info text below) so those sit on top and still catch their own taps. The
  // catcher and the canvas are both anchored at screen (0,0), so the touch
  // coordinates mapCanvasEventCb reads line up with the tile projection.
  s_map_touch = lv_obj_create(tab);
  lv_obj_remove_style_all(s_map_touch);
  lv_obj_set_size(s_map_touch, lv_pct(100), lv_pct(100));
  lv_obj_set_pos(s_map_touch, 0, 0);
  lv_obj_set_style_bg_opa(s_map_touch, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_clear_flag(s_map_touch, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_map_touch, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_map_touch, mapCanvasEventCb, LV_EVENT_PRESSED,    nullptr);
  lv_obj_add_event_cb(s_map_touch, mapCanvasEventCb, LV_EVENT_PRESSING,   nullptr);
  lv_obj_add_event_cb(s_map_touch, mapCanvasEventCb, LV_EVENT_RELEASED,   nullptr);
  lv_obj_add_event_cb(s_map_touch, mapCanvasEventCb, LV_EVENT_PRESS_LOST, nullptr);
  lv_obj_move_to_index(s_map_touch, 0);   // keep it beneath the buttons/labels

  // Overlay controls — siblings of the canvas inside the tab, so they
  // float on top of the tile grid and don't get freed when tiles refresh.
  // Layout: right-edge column [zoom+ / zoom- / recenter], 28-px buttons
  // with 4-px gutters.
  auto make_overlay_btn = [&](const char* sym, int y, lv_event_cb_t cb) -> lv_obj_t* {
    lv_obj_t* b = lv_btn_create(tab);
    lv_obj_set_size(b, 32, 28);
    lv_obj_set_pos(b, k_map_canvas_w - 32 - 4, y);
    styleButton(b);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_PART_MAIN);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, &g_font_16, LV_PART_MAIN);
    lv_obj_center(l);
    return b;
  };
  // Options gear sits at the TOP of the right-edge column; zoom/recenter below,
  // then a "contacts on map" picker (list of GPS-bearing contacts → recenter).
  make_overlay_btn(LV_SYMBOL_SETTINGS, 4,         mapOpenOptionsCb);
  make_overlay_btn(LV_SYMBOL_PLUS,     4 + 32,    mapZoomInCb);
  make_overlay_btn(LV_SYMBOL_MINUS,    4 + 32*2,  mapZoomOutCb);
  make_overlay_btn(LV_SYMBOL_GPS,      4 + 32*3,  mapRecenterCb);
  make_overlay_btn(LV_SYMBOL_LIST,     4 + 32*4,  mapOpenContactsCb);

  // (OSM attribution now lives in the status bar's left zone on the map tab —
  // see updateGlobalStatusBar.)
}

// Refresh the bottom info strip — called from the periodic refresh tick
// once the Map tab is active. Cheap (one snprintf + label set).
static void refreshMapInfoLabel() {
  if (!s_map_info_lbl || !g_lv.task) return;
  const double lat = g_lv.task->getNodeLat();
  const double lon = g_lv.task->getNodeLon();
  // Count contacts with non-zero GPS — the actual map will plot these as
  // markers once tile rendering is in.
  int with_gps = 0;
  for (uint32_t i = 0; i < the_mesh.getNumContacts(); ++i) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(i, c)) continue;
    if (c.gps_lat != 0 || c.gps_lon != 0) ++with_gps;
  }
  // Compact map-coverage hint. When the current view has gaps (tiles not
  // yet on disk), say what's happening instead of just the marker count:
  //   • tiles queued/downloading  -> "downloading N"
  //   • gaps but Wi-Fi is down     -> "Wi-Fi off"
  // Otherwise show the usual marker count.
  char tail[28];
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  const bool wifi_up = (WiFi.status() == WL_CONNECTED);
  if (s_tile_fetch_pending > 0) {
    snprintf(tail, sizeof(tail), "\xe2\x86\x93 %u downloading",
             (unsigned)s_tile_fetch_pending);
  } else if (s_map_last_missing > 0 && !wifi_up) {
    snprintf(tail, sizeof(tail), "Wi-Fi off \xe2\x80\x94 gaps");
  } else {
    snprintf(tail, sizeof(tail), "%d on map", with_gps);
  }
#else
  snprintf(tail, sizeof(tail), "%d on map", with_gps);
#endif
  // Coords → bottom-left corner; count/status → bottom-right corner.
  char buf[40];
  if (lat == 0.0 && lon == 0.0) snprintf(buf, sizeof(buf), "GPS unset");
  else                          snprintf(buf, sizeof(buf), "%.4f, %.4f", lat, lon);
  lv_label_set_text(s_map_info_lbl, buf);
  if (s_map_count_lbl) lv_label_set_text(s_map_count_lbl, tail);
}

// ----- Chat detail (full-screen overlay on lv_scr_act) -------
static void makeChatDetail(LvChatPanel& p) {
  p.overlay = lv_obj_create(lv_scr_act());
  lv_obj_set_size(p.overlay, chatScreenW(), chatScreenH());
  // Overlay starts below the global status bar so the bar stays visible.
  lv_obj_set_pos(p.overlay, 0, STATUSBAR_H);
  styleSurface(p.overlay, COLOR_BG, 0);
  lv_obj_set_style_pad_all(p.overlay, 0, LV_PART_MAIN);   // prevent child-position offset
  lv_obj_clear_flag(p.overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(p.overlay, LV_OBJ_FLAG_HIDDEN);

  // ---- No header bar ----
  // The old back-button + thread-name bar ate ~44 px of every conversation. The
  // thread name now lives in the global status bar (set on open via
  // setChatStatusTitle), and a small floating HOME button (created last, at the
  // end of this fn so it sits on top) handles exit — mirroring the Terminal/Files
  // fullscreen views. Gives the conversation the full height.
  p.header_name = nullptr;

  // ---- Message area (scrollable container of speech-bubble children) ----
  // Each message gets its own lv_obj bubble inside p.msgs so we can style
  // outgoing/incoming differently and align left/right WhatsApp-style.
  p.msgs = lv_obj_create(p.overlay);
  lv_obj_set_size(p.msgs, chatScreenW(), chatMsgHOpen());
  lv_obj_set_pos(p.msgs, 0, CHAT_HDR_H);
  styleSurface(p.msgs, COLOR_BG, 0);
  lv_obj_set_style_border_width(p.msgs, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(p.msgs, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(p.msgs, 6, LV_PART_MAIN);
  lv_obj_set_style_text_color(p.msgs, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(p.msgs, &g_font_12, LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(p.msgs, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_scroll_dir(p.msgs, LV_DIR_VER);
  lv_obj_add_event_cb(p.msgs, scrollClampOnEndCb, LV_EVENT_SCROLL_END, nullptr);

  // ---- Composer row ----
  p.composer_row = lv_obj_create(p.overlay);
  lv_obj_set_size(p.composer_row, chatScreenW(), CHAT_COMP_H);
  lv_obj_set_pos(p.composer_row, 0, chatCompYOpen());
  styleSurface(p.composer_row, COLOR_PANEL, 0);
  lv_obj_clear_flag(p.composer_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_side(p.composer_row, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
  lv_obj_set_style_border_width(p.composer_row, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(p.composer_row, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_pad_hor(p.composer_row, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(p.composer_row, 2, LV_PART_MAIN);

  // Layout math (row = 240 px, pad_hor = 4 → content area = 232):
  //   QR(30) + gap(6) + Emoji(30) + gap(6) + TA + gap(6) + Send(34)
  // The QR + Emoji chips share the left edge; the textarea fills the middle.
  lv_obj_set_style_pad_hor(p.composer_row, 4, LV_PART_MAIN);

  // Macro picker button: a compact chip taps to a popup grid of user-
  // defined quick-reply presets. Saves typing for stock phrases ("ok",
  // "on the way", ...) — same idea as MCterm's preset macros. Editable
  // from Settings → Quick replies.
  lv_obj_t* qr_btn = lv_btn_create(p.composer_row);
  lv_obj_set_size(qr_btn, 30, 30);
  lv_obj_align(qr_btn, LV_ALIGN_LEFT_MID, 0, 0);
  styleButton(qr_btn);
  lv_obj_set_style_radius(qr_btn, 15, LV_PART_MAIN);
  lv_obj_set_style_bg_color(qr_btn, lv_color_hex(0x1A1B1C), LV_PART_MAIN);
  lv_obj_add_event_cb(qr_btn, openQuickReplyPickerCb, LV_EVENT_CLICKED, &p);
  lv_obj_t* ql = lv_label_create(qr_btn);
  lv_label_set_text(ql, LV_SYMBOL_LIST);
  lv_obj_set_style_text_font(ql, &g_font_14, LV_PART_MAIN);
  lv_obj_center(ql);

  // Emoji / special-character picker button (smiley). Opens the insert grid.
  lv_obj_t* emoji_btn = lv_btn_create(p.composer_row);
  lv_obj_set_size(emoji_btn, 30, 30);
  lv_obj_align(emoji_btn, LV_ALIGN_LEFT_MID, 36, 0);   // 30 (QR) + 6 gap
  styleButton(emoji_btn);
  lv_obj_set_style_radius(emoji_btn, 15, LV_PART_MAIN);
  lv_obj_set_style_bg_color(emoji_btn, lv_color_hex(0x1A1B1C), LV_PART_MAIN);
  lv_obj_add_event_cb(emoji_btn, openEmojiPickerCb, LV_EVENT_CLICKED, &p);
  lv_obj_t* el = lv_label_create(emoji_btn);
  lv_label_set_text(el, "\xF0\x9F\x98\x8A");   // 😊
  lv_obj_set_style_text_font(el, &g_font_16, LV_PART_MAIN);
  lv_obj_center(el);

  p.composer_ta = lv_textarea_create(p.composer_row);
  // Fill the row between the two left chips and Send (right): content width is
  // screen - 8 (pad), minus QR(30)+gap(6)+Emoji(30)+gap(6) on the left and
  // gap(6)+Send(34) on the right => screen - 120. Widens with the screen.
  lv_obj_set_size(p.composer_ta, chatScreenW() - 120, 30);
  // 30 (QR) + 6 + 30 (Emoji) + 6 = 72 offset from content-left.
  lv_obj_align(p.composer_ta, LV_ALIGN_LEFT_MID, 72, 0);
  styleCard(p.composer_ta);
  lv_obj_set_style_radius(p.composer_ta, 15, LV_PART_MAIN);   // pill shape
  lv_obj_set_style_pad_ver(p.composer_ta, 3, LV_PART_MAIN);   // tighter so the slim row fits the text
  lv_textarea_set_one_line(p.composer_ta, true);
  lv_textarea_set_placeholder_text(p.composer_ta, "Type a message...");
  lv_obj_set_style_text_color(p.composer_ta, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(p.composer_ta, &g_font_14, LV_PART_MAIN);
  // Tap on composer → show keyboard
  lv_obj_add_event_cb(p.composer_ta, composerFocusCb, LV_EVENT_FOCUSED, &p);
  lv_obj_add_event_cb(p.composer_ta, kbActivityPressCb, LV_EVENT_PRESSED, nullptr);

  lv_obj_t* send = lv_btn_create(p.composer_row);
  lv_obj_set_size(send, 34, 30);
  lv_obj_align(send, LV_ALIGN_RIGHT_MID, 0, 0);
  styleButton(send);
  lv_obj_set_style_radius(send, 15, LV_PART_MAIN);
  lv_obj_add_event_cb(send, sendFromPanelCb, LV_EVENT_CLICKED, &p);
  lv_obj_t* sl = lv_label_create(send);
  lv_label_set_text(sl, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_font(sl, &g_font_16, LV_PART_MAIN);
  lv_obj_center(sl);

  // Floating HOME button (created last so it sits above the message area) —
  // exits the conversation, mirroring the Terminal/Files fullscreen views. The
  // thread name shows in the status bar; this replaces the old back-button bar.
  lv_obj_t* home = lv_btn_create(p.overlay);
  lv_obj_set_size(home, 40, 28);
  lv_obj_align(home, LV_ALIGN_TOP_RIGHT, -6, 4);
  styleButton(home);
  // Reuse backBtnCb (closes the chat → thread list). PRESSED + CLICKED matches
  // the old back button so the cap-touch swipe-detector race can't drop the tap.
  lv_obj_add_event_cb(home, backBtnCb, LV_EVENT_PRESSED, &p);
  lv_obj_add_event_cb(home, backBtnCb, LV_EVENT_CLICKED, &p);
  lv_obj_t* hl = lv_label_create(home);
  lv_label_set_text(hl, LV_SYMBOL_HOME);
  lv_obj_center(hl);
}

static int append_settings_section(lv_obj_t* tab, int y, const char* title, lv_event_cb_t cb, int sub_idx) {
  // Row width tracks the screen (settings tab has 8-px padding each side) so
  // sections fill the width in both portrait and landscape.
  const lv_coord_t row_w = tabContentW() - 16;
  lv_obj_t* row = lv_btn_create(tab);
  /* `styleSettingsRow` calls `remove_style_all` — must run *before* align/size or position is lost
   * and every row stacks at (0,0), leaving only the last sibling visible. */
  styleSettingsRow(row);
  lv_obj_set_size(row, row_w, k_settings_row_h);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
  lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* tit = lv_label_create(row);
  lv_label_set_text(tit, title);
  lv_obj_set_style_text_font(tit, &g_font_14, LV_PART_MAIN);
  lv_obj_set_style_text_color(tit, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_opa(tit, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_align(tit, LV_ALIGN_TOP_LEFT, 14, 8);

  lv_obj_t* sub = lv_label_create(row);
  lv_label_set_long_mode(sub, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(sub, row_w - 48);
  lv_obj_set_height(sub, 18);
  lv_obj_set_style_text_font(sub, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(sub, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_anim_speed(sub, 28, LV_PART_MAIN);
  lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 14, 29);
  lv_label_set_text(sub, "…");

  lv_obj_t* chev = lv_label_create(row);
  lv_label_set_text(chev, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_font(chev, &g_font_14, LV_PART_MAIN);
  lv_obj_set_style_text_color(chev, lv_color_hex(0x4A5D70), LV_PART_MAIN);
  lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -12, 0);

  if (sub_idx >= 0 && sub_idx < SEC_COUNT) g_set_sec_sub[sub_idx] = sub;
  return y + k_settings_row_h + k_settings_row_gap;
}

// Configure one sub-tab page: small padding, vertical flex (sections stack),
// vertical scroll for overflow.
static void prepSettingsPage(lv_obj_t* p) {
  styleSurface(p, COLOR_BG, 0);
  lv_obj_set_style_pad_all(p, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_row(p, 2, LV_PART_MAIN);
  lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(p, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);
}

// Build one sub-tab's controls on demand, tearing down the page we left, so only
// the visible sub-tab's widgets are allocated at once. The inline settings
// otherwise keep ALL sections' widgets alive — many small LVGL allocations that
// fall back to scarce internal DRAM (the regression that bloated DRAM usage).
static void settingsBuildPage(int idx) {
  if (idx < 0 || idx >= kSettingsSubtabCount || !s_settings_pages[idx]) return;
  if (idx == s_settings_built_page) return;
  if (s_settings_built_page >= 0 && s_settings_pages[s_settings_built_page]) {
    if (s_settings_built_page == 4) {   // About: drop the live-label pointers first
      s_sysinfo_lbl = nullptr; s_update_about_lbl = nullptr; s_ota_status_lbl = nullptr;
      s_ota_btn = nullptr; s_ota_btn_lbl = nullptr;
      g_lv.settings_status = nullptr; g_lv.diag_id_label = nullptr; g_lv.diag_label = nullptr;
    }
    lv_obj_clean(s_settings_pages[s_settings_built_page]);
  }
  s_settings_built_page = idx;
  lv_obj_t* page = s_settings_pages[idx];
  const lv_coord_t lblw = lv_disp_get_hor_res(nullptr) - 16;
  resetSettingsModalState();
  s_settings_inline_parent = page;
  switch (idx) {
    case 0: buildProfileSettings(); break;
    case 1: buildRadioSettings(); buildAutoAddSettings(); buildExperimentalSettings(); break;
    case 2: buildWifiSettings(); buildBluetoothSettings(); break;
    case 3: buildDeviceSettings(); buildQuickReplySettings(); break;
    case 4: {
      s_settings_inline_parent = nullptr;
      s_update_about_lbl = lv_label_create(page);   // update/firmware status (top)
      lv_label_set_long_mode(s_update_about_lbl, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(s_update_about_lbl, lblw);
      lv_obj_set_style_text_font(s_update_about_lbl, &g_font_12, LV_PART_MAIN);
      lv_obj_set_style_text_color(s_update_about_lbl, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
      lv_label_set_text(s_update_about_lbl, "");

      // "Install update" button — over-the-air update to the latest release.
      // Only meaningful on a tagged build with OTA support; the callback
      // re-validates (Wi-Fi up, update known) and reports errors inline.
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
      if (FIRMWARE_OTA_ENV[0] && firmwareReleaseN() >= 0) {
        s_ota_btn = lv_btn_create(page);
        lv_obj_set_size(s_ota_btn, lblw, 38);
        styleButton(s_ota_btn);
        lv_obj_set_style_bg_color(s_ota_btn, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_ota_btn, lv_color_hex(0x3B7039), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_add_event_cb(s_ota_btn, otaInstallLatestCb, LV_EVENT_CLICKED, nullptr);
        s_ota_btn_lbl = lv_label_create(s_ota_btn);
        lv_label_set_text(s_ota_btn_lbl, LV_SYMBOL_DOWNLOAD "  Install update over Wi-Fi");
        lv_obj_set_style_text_font(s_ota_btn_lbl, &g_font_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_ota_btn_lbl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
        lv_obj_center(s_ota_btn_lbl);
        otaButtonRefreshState();   // grey immediately if we already know we're current

        s_ota_status_lbl = lv_label_create(page);
        lv_label_set_long_mode(s_ota_status_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(s_ota_status_lbl, lblw);
        lv_obj_set_style_text_font(s_ota_status_lbl, &g_font_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_ota_status_lbl, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
        lv_label_set_text(s_ota_status_lbl, "");
      }
#endif

      s_settings_inline_parent = page;  buildSystemInfoSettings();
      s_settings_inline_parent = nullptr;
      g_lv.settings_status = lv_label_create(page);
      lv_label_set_long_mode(g_lv.settings_status, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(g_lv.settings_status, lblw);
      lv_obj_set_style_text_color(g_lv.settings_status, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
      lv_obj_set_style_text_font(g_lv.settings_status, &g_font_12, LV_PART_MAIN);
      lv_label_set_text(g_lv.settings_status, "");
      g_lv.diag_id_label = lv_label_create(page);
      lv_label_set_long_mode(g_lv.diag_id_label, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(g_lv.diag_id_label, lblw);
      lv_obj_set_style_text_color(g_lv.diag_id_label, lv_color_hex(0xA8C8FF), LV_PART_MAIN);
      lv_obj_set_style_text_font(g_lv.diag_id_label, &g_font_12, LV_PART_MAIN);
      lv_label_set_text(g_lv.diag_id_label, s_diag_id_pinned[0] ? s_diag_id_pinned : "ID …");
      g_lv.diag_label = lv_label_create(page);
      lv_label_set_long_mode(g_lv.diag_label, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(g_lv.diag_label, lblw);
      lv_obj_set_style_text_color(g_lv.diag_label, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
      lv_obj_set_style_text_font(g_lv.diag_label, &g_font_12, LV_PART_MAIN);
      lv_label_set_text(g_lv.diag_label, "Diagnostics");
      versionCheckUpdateUi();
      break;
    }
  }
  s_settings_inline_parent = nullptr;
}

static void settingsSubtabChangedCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || !s_settings_subtab) return;
  settingsBuildPage((int)lv_tabview_get_tab_act(s_settings_subtab));
}

static void makeSettings(lv_obj_t* tab) {
  styleSurface(tab, COLOR_BG);
  lv_obj_set_style_pad_all(tab, 0, LV_PART_MAIN);
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < SEC_COUNT; ++i) g_set_sec_sub[i] = nullptr;   // legacy subtitles retired
  // Pages SHARE the g_set_modal widget-pointer struct: reset once, then each
  // inline builder writes its own (distinct) fields. The save callbacks read
  // those same pointers, so they keep working with no modal open.
  resetSettingsModalState();

  // A top sub-tab bar groups the settings into categories. The Set page holds
  // just this one tabview (far under the bottom-tabview direct-child limit);
  // each category page is a scrollable flex column of inline control sections.
  lv_obj_t* sub = lv_tabview_create(tab, LV_DIR_TOP, 32);
  lv_obj_set_size(sub, lv_pct(100), lv_pct(100));
  styleSurface(sub, COLOR_BG, 0);
  // The tabview's content container defaults to a light theme bg — force dark.
  lv_obj_t* sub_content = lv_tabview_get_content(sub);
  if (sub_content) styleSurface(sub_content, COLOR_BG, 0);
  lv_obj_t* sub_btns = lv_tabview_get_tab_btns(sub);
  // Dark button bar (the btnmatrix MAIN bg is light by default = the "white").
  lv_obj_set_style_bg_color(sub_btns, lv_color_hex(COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(sub_btns, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_side(sub_btns, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_style_border_color(sub_btns, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(sub_btns, 1, LV_PART_MAIN);
  lv_obj_set_style_text_font(sub_btns, &g_font_12, LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(sub_btns, LV_OPA_TRANSP, LV_PART_ITEMS);
  lv_obj_set_style_border_width(sub_btns, 0, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(sub_btns, lv_color_hex(COLOR_ACCENT), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_bg_opa(sub_btns, LV_OPA_40, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(sub_btns, lv_color_hex(COLOR_SUB), LV_PART_ITEMS);
  lv_obj_set_style_text_color(sub_btns, lv_color_hex(COLOR_TEXT), LV_PART_ITEMS | LV_STATE_CHECKED);
  // Gap between the 5 tab cells + a touch of inner padding so adjacent labels
  // ("Profile"/"Radio"/"Network"…) don't run edge-to-edge and visually touch in
  // the narrow portrait (240 px) width. pad_column spaces the cells; the small
  // text padding keeps each label centered with breathing room.
  lv_obj_set_style_pad_column(sub_btns, 2, LV_PART_MAIN);
  lv_obj_set_style_pad_left(sub_btns, 1, LV_PART_ITEMS);
  lv_obj_set_style_pad_right(sub_btns, 1, LV_PART_ITEMS);
  s_settings_subtab = sub;
  // Red dot over the "About" sub-tab button (rightmost) — the update info lives
  // there, so flag it when the gear badge is showing. Child of the Set page so
  // it floats above the sub-tab bar; only visible while on the Settings tab.
  s_update_subtab_badge = lv_obj_create(tab);
  lv_obj_remove_style_all(s_update_subtab_badge);
  lv_obj_set_size(s_update_subtab_badge, 9, 9);
  lv_obj_set_style_radius(s_update_subtab_badge, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_update_subtab_badge, lv_color_hex(0xE2403A), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_update_subtab_badge, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_align(s_update_subtab_badge, LV_ALIGN_TOP_RIGHT, -6, 4);
  lv_obj_add_flag(s_update_subtab_badge, LV_OBJ_FLAG_HIDDEN);

  // Short labels: the 5 cells split the (narrow, 240 px in V4 portrait) bar
  // evenly, so "Network"/"Profile" would fill a cell edge-to-edge and the last
  // letter touched the next tab's first letter ("…k" / "D…"). Trimmed to fit.
  s_settings_pages[0] = lv_tabview_add_tab(sub, "Profile");
  s_settings_pages[1] = lv_tabview_add_tab(sub, "Radio");
  s_settings_pages[2] = lv_tabview_add_tab(sub, "Net");
  s_settings_pages[3] = lv_tabview_add_tab(sub, "Device");
  s_settings_pages[4] = lv_tabview_add_tab(sub, "About");
  for (int i = 0; i < kSettingsSubtabCount; ++i) prepSettingsPage(s_settings_pages[i]);

  // Lazy: build only the visible sub-tab's controls; switching pages tears down
  // the old one and builds the new (keeps DRAM low — see settingsBuildPage).
  lv_obj_add_event_cb(sub, settingsSubtabChangedCb, LV_EVENT_VALUE_CHANGED, nullptr);
  s_settings_built_page = -1;
  settingsBuildPage((int)lv_tabview_get_tab_act(sub));   // populate the initially-active page
  versionCheckUpdateUi();   // reflect any completed check in the gear/About badges
}

// ============================================================
// Refresh functions
// ============================================================

// Rebuild message history in the open chat detail.
// ---- Bubble timestamp + long-press menu (Copy/Info) ---------------------
//
// Timestamp helpers: m.ts is stored as RTC epoch seconds when available
// (set in appendMessage), else uptime seconds. The Info popup wants the
// full date+time; the bubble itself only shows HH:MM, with "—" when the
// RTC has never been bootstrapped on this device.
static constexpr uint32_t k_ts_epoch_min = 1700000000u; // Nov 2023 — sanity floor

static void formatBubbleHhMm(uint32_t ts, char* out, int cap) {
  if (cap <= 0 || !out) return;
  if (ts < k_ts_epoch_min) { out[0] = '\0'; return; }
  time_t t = (time_t)ts;
  struct tm tm_loc{};
  localtime_r(&t, &tm_loc);
  snprintf(out, cap, "%02d:%02d", tm_loc.tm_hour, tm_loc.tm_min);
}

static void formatFullTimestamp(uint32_t ts, char* out, int cap) {
  if (cap <= 0 || !out) return;
  if (ts < k_ts_epoch_min) {
    snprintf(out, cap, "(RTC unset)");
    return;
  }
  time_t t = (time_t)ts;
  struct tm tm_loc{};
  localtime_r(&t, &tm_loc);
  snprintf(out, cap, "%04d-%02d-%02d %02d:%02d:%02d",
           tm_loc.tm_year + 1900, tm_loc.tm_mon + 1, tm_loc.tm_mday,
           tm_loc.tm_hour, tm_loc.tm_min, tm_loc.tm_sec);
}

// Module state for the per-message action menu + Info popup. Only one of
// each can be open at a time; opening a new one closes the old.
// True if `text` contains "@<my node name>" (case-insensitive, with a word
// boundary after) — i.e. this message @mentions me.
static bool textMentionsMe(const char* text) {
  if (!text || !text[0]) return false;
  NodePrefs* pr = the_mesh.getNodePrefs();
  const char* me = pr ? pr->node_name : nullptr;
  if (!me || !me[0]) return false;
  const size_t nl = strlen(me);
  for (const char* p = text; *p; ++p) {
    if (*p != '@') continue;
    // Accept both "@name" (bare) and "@[name]" (bracketed) mention syntax.
    const bool bracket = (p[1] == '[');
    const char* q = bracket ? p + 2 : p + 1;
    if (strncasecmp(q, me, nl) != 0) continue;
    const char a = q[nl];
    if (bracket) {
      if (a == ']') return true;
    } else if (a == '\0' || a == ' ' || a == '\n' || a == ',' || a == '.' ||
               a == '!' || a == '?' || a == ':' || a == ';') {
      return true;
    }
  }
  return false;
}

static lv_obj_t* s_msg_menu_root = nullptr;
static lv_obj_t* s_msg_info_root = nullptr;
static int       s_msg_menu_idx  = -1;   // absolute index into _ui_msgs
static char      s_msg_menu_sender[UITask::MAX_SENDER_NAME + 1] = {0};
// Buffer the body text so the Copy button doesn't reach back into the ring
// (the bubble might get redrawn between menu-open and copy-tap).
static char      s_msg_menu_text[UITask::MAX_MSG_TEXT + 1] = {0};

static void closeMsgActionMenu() {
  if (s_msg_menu_root) {
    lv_obj_del_async(s_msg_menu_root);
    s_msg_menu_root = nullptr;
  }
}
static void closeMsgInfoPopup() {
  if (s_msg_info_root) {
    lv_obj_del_async(s_msg_info_root);
    s_msg_info_root = nullptr;
  }
}

// Full-route trace, triggered from the message Info popup. s_trace_route_pending
// tells onTracePingResult to render the multi-hop result in this dismissable
// card (vs the compact "Trace SNR" toast used by the repeater action-sheet probe).
static lv_obj_t* s_trace_result_root   = nullptr;
static bool      s_trace_route_pending = false;

static void closeTraceResultPopup() {
  if (s_trace_result_root) {
    lv_obj_del_async(s_trace_result_root);
    s_trace_result_root = nullptr;
  }
}
static void traceResultBackdropCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeTraceResultPopup();
}
static void openTraceResultPopup(const char* title, const char* body) {
  closeTraceResultPopup();
  lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_trace_result_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_trace_result_root);
  lv_obj_set_size(s_trace_result_root, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_trace_result_root, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_trace_result_root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_trace_result_root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_trace_result_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_trace_result_root, traceResultBackdropCb, LV_EVENT_CLICKED, nullptr);

  const int card_w = 220;
  const int card_h = 236;
  lv_obj_t* card = lv_obj_create(s_trace_result_root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
  lv_obj_set_scroll_dir(card, LV_DIR_VER);
  addCloseXBadge(card, traceResultBackdropCb);

  lv_obj_t* ttl = lv_label_create(card);
  lv_label_set_text(ttl, title);
  lv_obj_set_style_text_color(ttl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(ttl, &g_font_14, LV_PART_MAIN);
  lv_label_set_long_mode(ttl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(ttl, card_w - 20 - 32);
  lv_obj_set_pos(ttl, 0, 4);

  lv_obj_t* lbl = lv_label_create(card);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, card_w - 20);
  lv_label_set_text(lbl, body);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl, &g_font_12, LV_PART_MAIN);
  lv_obj_set_pos(lbl, 0, 32);
  lv_obj_add_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(lbl, copyLabelLongPressCb, LV_EVENT_LONG_PRESSED,
                      const_cast<char*>("info"));
}

static void msgMenuCopyCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_indev_t* a = lv_indev_get_act();
  if (a) lv_indev_wait_release(a);
  clipboardSet(s_msg_menu_text, "message");
  closeMsgActionMenu();
}

// Forward decl so the menu's Info button can fire the popup.
static void openMessageInfoPopup(int msg_idx);

static void msgMenuInfoCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_indev_t* a = lv_indev_get_act();
  if (a) lv_indev_wait_release(a);
  const int idx = s_msg_menu_idx;
  closeMsgActionMenu();
  openMessageInfoPopup(idx);
}

// Insert "@[<sender>] " into the channel composer so you can @mention them. The
// bracketed form is the mention syntax the other clients use (textMentionsMe
// also accepts a bare "@name" for backward-compat).
static void msgMenuMentionCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_indev_t* a = lv_indev_get_act();
  if (a) lv_indev_wait_release(a);
  closeMsgActionMenu();
  LvChatPanel& p = g_lv.ch;                 // mentions are channel-only
  if (!p.composer_ta || !s_msg_menu_sender[0]) return;
  char ins[UITask::MAX_SENDER_NAME + 8];
  snprintf(ins, sizeof ins, "@[%s] ", s_msg_menu_sender);
  lv_textarea_add_text(p.composer_ta, ins);
  lv_textarea_set_cursor_pos(p.composer_ta, LV_TEXTAREA_CURSOR_LAST);
  showKb(&p);                               // focus composer so typing continues there
}

static void msgMenuBackdropCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_indev_t* a = lv_indev_get_act();
  if (a) lv_indev_wait_release(a);
  closeMsgActionMenu();
}
static void msgInfoBackdropCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_indev_t* a = lv_indev_get_act();
  if (a) lv_indev_wait_release(a);
  closeMsgInfoPopup();
}

static void openMessageActionMenu(int msg_idx) {
  if (!g_lv.task) return;
  UITask::UIMessage m;
  if (!g_lv.task->getMessageByIndex(msg_idx, m)) return;
  closeMsgActionMenu();
  closeMsgInfoPopup();
  s_msg_menu_idx = msg_idx;
  // Snapshot the body text + sender — the ring may rotate between now and tap.
  strncpy(s_msg_menu_text, m.text, sizeof(s_msg_menu_text) - 1);
  s_msg_menu_text[sizeof(s_msg_menu_text) - 1] = '\0';
  strncpy(s_msg_menu_sender, m.sender, sizeof(s_msg_menu_sender) - 1);
  s_msg_menu_sender[sizeof(s_msg_menu_sender) - 1] = '\0';
  // Offer "Mention" only for an incoming channel message (mention its sender).
  const bool can_mention = m.channel && !m.outgoing && m.sender[0];

  lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_msg_menu_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_msg_menu_root);
  // Sit below the global status bar so the centered card lines up with the
  // visible area (matches every other modal in the UI).
  lv_obj_set_size(s_msg_menu_root, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_msg_menu_root, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_msg_menu_root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_msg_menu_root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_msg_menu_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_msg_menu_root, msgMenuBackdropCb, LV_EVENT_CLICKED, nullptr);

  const int card_w = 180;
  const int btn_h  = 38;
  const int gap    = 6;
  const int pad    = 10;
  // 28-px header row at the top reserves space for the close-X badge so it
  // doesn't sit on top of the Copy / Info buttons.
  const int hdr_h  = 28;
  const int nbtn   = can_mention ? 3 : 2;
  const int card_h = hdr_h + nbtn * btn_h + (nbtn - 1) * gap + 2 * pad;

  lv_obj_t* card = lv_obj_create(s_msg_menu_root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, pad, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  addCloseXBadge(card, msgMenuBackdropCb);

  auto mk_btn = [&](const char* text, lv_event_cb_t cb, int y_off) {
    lv_obj_t* b = lv_btn_create(card);
    lv_obj_set_size(b, card_w - 2 * pad, btn_h);
    lv_obj_set_pos(b, 0, y_off);
    styleButton(b);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lbl = lv_label_create(b);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &g_font_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_center(lbl);
  };
  int by = hdr_h;
  if (can_mention) {
    char ml[UITask::MAX_SENDER_NAME + 16];
    snprintf(ml, sizeof ml, "@%.16s", m.sender);   // "Mention" is implied by the @
    mk_btn(ml, msgMenuMentionCb, by);
    by += btn_h + gap;
  }
  mk_btn(LV_SYMBOL_COPY "  Copy", msgMenuCopyCb, by);  by += btn_h + gap;
  mk_btn(LV_SYMBOL_LIST "  Info", msgMenuInfoCb, by);
}

// "Trace route" from the message Info popup: run a full multi-hop trace to the
// conversation peer (each repeater on the path reports its SNR). Result lands
// asynchronously in onTracePingResult, which renders the hop list.
static void msgInfoTraceCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  ContactInfo c;
  if (!g_lv.task->lookupActiveContact(c)) {
    g_lv.task->showAlert("No contact to trace", 1500);
    return;
  }
  closeMsgInfoPopup();
  s_trace_route_pending = true;
  uint32_t tag = the_mesh.uiSendTraceRoute(c);
  if (tag == 0) {
    s_trace_route_pending = false;
    g_lv.task->showAlert("Trace failed", 1400);
  } else {
    g_lv.task->showAlert("Tracing route\xe2\x80\xa6", 1600);
  }
}

static void openMessageInfoPopup(int msg_idx) {
  if (!g_lv.task) return;
  UITask::UIMessage m;
  if (!g_lv.task->getMessageByIndex(msg_idx, m)) return;
  closeMsgInfoPopup();

  lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_msg_info_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_msg_info_root);
  lv_obj_set_size(s_msg_info_root, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_msg_info_root, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_msg_info_root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_msg_info_root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_msg_info_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_msg_info_root, msgInfoBackdropCb, LV_EVENT_CLICKED, nullptr);

  // DM messages get a taller card with a "Trace route" button at the bottom;
  // channel messages have no single peer to trace, so they keep the compact card.
  const bool show_trace = !m.channel;
  const int card_w = 220;
  const int card_h = show_trace ? 290 : 250;
  lv_obj_t* card = lv_obj_create(s_msg_info_root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  addCloseXBadge(card, msgInfoBackdropCb);

  // Build a single multi-line label — keeps layout dead simple, and the
  // user can long-press it (clipboardSet) to copy the whole metadata
  // dump if they ever need it. Body text:
  //   Message info
  //   ────────────
  //   Time   2026-05-27 22:34:01
  //   From   <sender>            (only when incoming)
  //   Path   flood, 2 hops       (or "direct (routed)" or "—")
  //   SNR    -3.5 dB             (or "—")
  //   RSSI   -101 dBm            (or "—")
  //   Status sent / delivered / failed   (only when outgoing DM)
  char body[420];
  int blen = 0;
  // Title is rendered as a separate label below — don't repeat it in body.
  // Time
  char tbuf[40];
  formatFullTimestamp(m.ts, tbuf, sizeof(tbuf));
  blen += snprintf(body + blen, sizeof(body) - blen, "Time   %s", tbuf);
  // From (incoming only — outgoing is obviously us)
  if (!m.outgoing && m.sender[0]) {
    blen += snprintf(body + blen, sizeof(body) - blen, "\nFrom   %s", m.sender);
  }
  // Direction / channel/dm
  blen += snprintf(body + blen, sizeof(body) - blen, "\nDir    %s%s",
                    m.outgoing ? "outgoing" : "incoming",
                    m.channel  ? " (channel)" : " (DM)");
  // RX metadata: only meaningful for incoming messages received post-v4.
  const bool has_rx = (m.meta_flags & UITask::MSG_META_HAS_RX) != 0;
  if (has_rx) {
    const bool is_flood = (m.meta_flags & UITask::MSG_META_IS_FLOOD) != 0;
    if (is_flood) {
      blen += snprintf(body + blen, sizeof(body) - blen,
                       "\nPath   flood, %u hops, %u-byte hash",
                       (unsigned)(m.path_len & 0x3F), (unsigned)((m.path_len >> 6) + 1));
    } else {
      blen += snprintf(body + blen, sizeof(body) - blen,
                       "\nPath   direct (routed)");
    }
    blen += snprintf(body + blen, sizeof(body) - blen,
                     "\nSNR    %.2f dB", (double)m.snr_q4 / 4.0);
    blen += snprintf(body + blen, sizeof(body) - blen,
                     "\nRSSI   %d dBm", (int)m.rssi);
    if (m.meta_flags & UITask::MSG_META_HAS_SCOPE) {
      blen += snprintf(body + blen, sizeof(body) - blen, "\nScope  %04X", (unsigned)m.in_scope);
    }
    // Full inbound route as consistent hex hops (e.g. 24E0>6401>B303) — the
    // repeaters this flood traversed. No name resolution (kept it uniform hex).
    if (is_flood && m.in_path_n > 0) {
      const uint8_t hsz = (uint8_t)((m.path_len >> 6) + 1);
      const uint8_t cnt = (uint8_t)(m.path_len & 0x3F);
      blen += snprintf(body + blen, sizeof(body) - blen, "\nRoute  ");
      int off = 0;
      for (uint8_t h = 0; h < cnt && off + hsz <= m.in_path_n; ++h, off += hsz) {
        if (h) blen += snprintf(body + blen, sizeof(body) - blen, ">");
        for (uint8_t b = 0; b < hsz; ++b)
          blen += snprintf(body + blen, sizeof(body) - blen, "%02X", m.in_path[off + b]);
      }
    }
  } else if (!m.outgoing) {
    blen += snprintf(body + blen, sizeof(body) - blen,
                     "\nPath   \xe2\x80\x94"
                     "\nSNR    \xe2\x80\x94"
                     "\nRSSI   \xe2\x80\x94");
  }
  // Delivery state (outgoing DMs only)
  if (m.outgoing && !m.channel) {
    const char* ds = "—";
    switch (m.deliv_state) {
      case UITask::DELIV_NONE:      ds = "queued";    break;
      case UITask::DELIV_SENT:      ds = "sent";      break;
      case UITask::DELIV_DELIVERED: ds = "delivered"; break;
      case UITask::DELIV_FAILED:    ds = "failed";    break;
    }
    blen += snprintf(body + blen, sizeof(body) - blen, "\nStatus %s", ds);
  }
  // "Repeats heard": echoes of our sent flood re-broadcast by nearby repeaters,
  // counted live this session. Shown for any tracked outgoing flood.
  if (m.outgoing && m.sent_fp) {
    blen += snprintf(body + blen, sizeof(body) - blen,
                     "\nRepeats heard: %u", (unsigned)the_mesh.uiRepeatsForFp(m.sent_fp));
  }
  if (blen >= (int)sizeof(body)) blen = sizeof(body) - 1;
  body[blen] = '\0';

  // Title row at top-left, sized to clear the close-X (top-right 32 px).
  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "Message info");
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &g_font_14, LV_PART_MAIN);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, card_w - 20 - 32);
  lv_obj_set_pos(title, 0, 4);

  // Body sits BELOW the close-X zone so it can use the full card width
  // without any overlap risk.
  lv_obj_t* lbl = lv_label_create(card);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, card_w - 20);
  lv_label_set_text(lbl, body);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl, &g_font_12, LV_PART_MAIN);
  lv_obj_set_pos(lbl, 0, 32);
  // Long-press the body to copy the full dump — useful for paste-into-bug-report.
  lv_obj_add_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(lbl, copyLabelLongPressCb, LV_EVENT_LONG_PRESSED,
                      const_cast<char*>("info"));

  // "Trace route" — full multi-hop trace to the conversation peer, with each
  // repeater's SNR. DM threads only (a channel has no single peer to trace).
  if (show_trace) {
    lv_obj_t* tb = lv_btn_create(card);
    lv_obj_set_size(tb, card_w - 20, 32);
    lv_obj_set_pos(tb, 0, card_h - 20 - 32);
    styleButton(tb);
    lv_obj_add_event_cb(tb, msgInfoTraceCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* tl = lv_label_create(tb);
    lv_label_set_text(tl, LV_SYMBOL_GPS "  Trace route");
    lv_obj_set_style_text_font(tl, &g_font_12, LV_PART_MAIN);
    lv_obj_center(tl);
  }

  // Bottom-right Close button removed — the X badge at the top-right is
  // the standard dismiss affordance now (same as every other popup).
}

// Long-press handler attached to each chat bubble. User-data is the
// absolute msg index (as an intptr_t).
static void bubbleLongPressMenuCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_LONG_PRESSED) return;
  lv_indev_t* act = lv_indev_get_act();
  if (act) lv_indev_wait_release(act);
  const int idx = (int)(intptr_t)lv_event_get_user_data(e);
  openMessageActionMenu(idx);
}

// Helper: render a centered placeholder label into the (cleaned) msgs panel.
static void chatDetailShowPlaceholder(LvChatPanel& p, const char* msg) {
  lv_obj_t* lbl = lv_label_create(p.msgs);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, 200);
  lv_label_set_text(lbl, msg);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_center(lbl);
}

static void refreshChatDetail(LvChatPanel& p) {
  if (!g_lv.task || !p.msgs) return;
  lv_obj_clean(p.msgs);

  if (!g_lv.task->hasActiveThread() ||
      g_lv.task->activeThreadIsChannel() != p.channel_mode) {
    chatDetailShowPlaceholder(p, "No thread selected.\n\nTap a chat to open it.");
    return;
  }
  int msg_idx[UITask::MAX_UI_MESSAGES];
  int n = g_lv.task->getActiveThreadMessageCount(msg_idx, UITask::MAX_UI_MESSAGES, false);
  if (n <= 0) {
    chatDetailShowPlaceholder(p, "No messages yet.\nSay hello!");
    return;
  }
  // Only render the most recent messages. Each bubble is several LVGL objects,
  // and small allocations fall back to scarce internal DRAM — rendering a full
  // 96-message history exhausts it and aborts (OOM). Newest-first is false, so
  // the last kRenderCap entries are the most recent.
  static const int kRenderCap = 30;
  const int render_start = (n > kRenderCap) ? (n - kRenderCap) : 0;

  // WhatsApp-style bubble layout:
  // - Each bubble auto-sizes to its content.
  // - Soft maximum width caps long messages around 75% of the screen so a
  //   single-line "ok" doesn't take a full row and a paragraph still wraps.
  // - Outgoing bubbles right-align (and use SENT color), incoming bubbles
  //   left-align (and use RECV color).
  // Size the bubble field to the actual message area (p.msgs is full width with
  // 6-px padding each side) instead of a hardcoded 240-px value — otherwise on
  // the wider T-Deck (320 px) bubbles bunch up in the middle and right-aligned
  // bubbles never reach the right edge.
  const lv_coord_t kContentW   = chatScreenW() - 12;
  const lv_coord_t kBubbleMaxW = (kContentW * 80) / 100;   // ~80% width cap
  constexpr lv_coord_t kBubblePadH = 8;
  constexpr lv_coord_t kBubblePadV = 5;
  constexpr lv_coord_t kSideGutter = 2;
  constexpr lv_coord_t kRowGap     = 4;

  lv_coord_t y_pos = 0;
  for (int i = render_start; i < n; ++i) {
    UITask::UIMessage m;
    if (!g_lv.task->getMessageByIndex(msg_idx[i], m)) continue;

    // Backwards compat: messages saved before the sender-parser landed have
    // sender="rx" with the actual "Name: body" still glued onto m.text. If
    // we spot that shape, split it on the fly so old history shows nicely.
    const char* show_sender = m.sender;
    const char* show_text   = m.text;
    char retro_sender[UITask::MAX_SENDER_NAME + 1] = "";
    if (p.channel_mode && !m.outgoing &&
        (m.sender[0] == '\0' || (m.sender[0] == 'r' && m.sender[1] == 'x' && m.sender[2] == '\0'))) {
      const char* colon = strstr(m.text, ": ");
      if (colon) {
        int slen = static_cast<int>(colon - m.text);
        if (slen > 0 && slen <= UITask::MAX_SENDER_NAME) {
          strncpy(retro_sender, m.text, slen);
          retro_sender[slen] = '\0';
          show_sender = retro_sender;
          show_text   = colon + 2;
        }
      }
    }

    char san_sender[UITask::MAX_SENDER_NAME + 8];
    char san_text[UITask::MAX_MSG_TEXT + 8];
    copyUtf8ReplacingMissingGlyphs(&g_font_12, san_sender, sizeof(san_sender), show_sender);
    copyUtf8ReplacingMissingGlyphs(&g_font_12, san_text, sizeof(san_text), show_text);

    lv_obj_t* bubble = lv_obj_create(p.msgs);
    lv_obj_remove_style_all(bubble);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(bubble, 10, LV_PART_MAIN);
    const bool mentions_me = p.channel_mode && !m.outgoing && textMentionsMe(show_text);
    lv_obj_set_style_bg_color(bubble,
        lv_color_hex(mentions_me ? COLOR_MENTION_BG
                                 : (m.outgoing ? COLOR_SENT_BG : COLOR_RECV_BG)), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(bubble, kBubblePadH, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(bubble, kBubblePadV, LV_PART_MAIN);
    // Content-driven width AND height. Inner label wrap below caps the
    // effective width to kBubbleMaxW, so short messages shrink and long
    // messages wrap inside the cap.
    lv_obj_set_size(bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    int inner_y = 0;
    // Group/channel rooms show the sender name as a small accent line above
    // the message text (mirrors WhatsApp group chats). DMs skip it — you
    // already know who you're talking to.
    if (p.channel_mode && !m.outgoing && san_sender[0]) {
      lv_obj_t* slbl = lv_label_create(bubble);
      lv_label_set_text(slbl, san_sender);
      lv_obj_set_style_text_font(slbl, &g_font_12, LV_PART_MAIN);
      lv_obj_set_style_text_color(slbl, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
      lv_obj_set_pos(slbl, 0, inner_y);
      inner_y += 14;
    }

    lv_obj_t* tlbl = lv_label_create(bubble);
    lv_obj_set_style_text_font(tlbl, &g_font_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(tlbl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(tlbl, san_text);
    // WhatsApp-style auto-shrink: measure the natural width of the text
    // first; if it fits within the soft cap, use that natural width so the
    // bubble hugs short messages (e.g. "ok" → ~24 px). Otherwise clamp the
    // label to the cap and turn on LONG_WRAP so long messages wrap. LVGL's
    // LONG_WRAP mode requires an explicit width — it doesn't combine with
    // LV_SIZE_CONTENT — hence the manual measure.
    const lv_coord_t kInnerMaxW = kBubbleMaxW - 2 * kBubblePadH;
    lv_point_t txt_size;
    lv_txt_get_size(&txt_size, san_text,
                    &g_font_12,
                    0 /*letter_space*/, 0 /*line_space*/,
                    LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (txt_size.x <= kInnerMaxW) {
      lv_obj_set_width(tlbl, txt_size.x);
    } else {
      lv_label_set_long_mode(tlbl, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(tlbl, kInnerMaxW);
    }
    lv_obj_set_pos(tlbl, 0, inner_y);
    // Long-press the bubble text → open the per-message action menu
    // (Copy / Info). The absolute msg index is packed into user_data so the
    // menu can pull the right entry out of the ring even after a refresh.
    lv_obj_add_flag(tlbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tlbl, bubbleLongPressMenuCb, LV_EVENT_LONG_PRESSED,
                        reinterpret_cast<void*>((intptr_t)msg_idx[i]));
    // ALSO attach to the bubble itself so the long-press still works if the
    // user taps the bubble's padding rather than precisely on the text.
    lv_obj_add_flag(bubble, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bubble, bubbleLongPressMenuCb, LV_EVENT_LONG_PRESSED,
                        reinterpret_cast<void*>((intptr_t)msg_idx[i]));

    // Footer row: HH:MM timestamp + (for outgoing DMs) the delivery glyph.
    // Both sit in one label so they share the bottom-right anchor — keeps
    // the bubble content-driven without an extra flex container.
    char ts_buf[8];
    formatBubbleHhMm(m.ts, ts_buf, sizeof(ts_buf));
    const char* deliv_glyph = "";
    uint32_t deliv_fg = COLOR_SUB;
    if (m.outgoing && !p.channel_mode && m.deliv_state != UITask::DELIV_NONE) {
      switch (m.deliv_state) {
        case UITask::DELIV_SENT:      deliv_glyph = " " LV_SYMBOL_OK; deliv_fg = COLOR_SUB;    break;
        case UITask::DELIV_DELIVERED: deliv_glyph = " " LV_SYMBOL_OK LV_SYMBOL_OK; deliv_fg = COLOR_ACCENT; break;
        case UITask::DELIV_FAILED:    deliv_glyph = " " LV_SYMBOL_CLOSE; deliv_fg = 0xE08080;  break;
      }
    }
    // Repeats-heard tag for outgoing floods (DM + channel): how many nearby
    // repeaters re-broadcast this message. Only shown once ≥1.
    char rep_buf[12] = "";
    if (m.outgoing && m.sent_fp) {
      const uint8_t reps = the_mesh.uiRepeatsForFp(m.sent_fp);
      if (reps > 0) snprintf(rep_buf, sizeof(rep_buf), " " LV_SYMBOL_REFRESH "%u", (unsigned)reps);
    }
    if (ts_buf[0] || deliv_glyph[0] || rep_buf[0]) {
      char footer[40];
      snprintf(footer, sizeof(footer), "%s%s%s", ts_buf, deliv_glyph, rep_buf);
      lv_obj_t* foot = lv_label_create(bubble);
      lv_label_set_text(foot, footer);
      lv_obj_set_style_text_font(foot, &g_font_12, LV_PART_MAIN);
      // Tint matches delivery state when there's a glyph; otherwise the
      // timestamp uses the muted SUB color so it doesn't compete with text.
      lv_obj_set_style_text_color(foot, lv_color_hex(deliv_glyph[0] ? deliv_fg : COLOR_SUB), LV_PART_MAIN);
      // Manual placement under the text — LV_ALIGN_BOTTOM_RIGHT inside a
      // LV_SIZE_CONTENT parent fights the auto-size pass and the footer
      // ends up overlapping the last line. Measure the actual text + footer
      // sizes and stack them.
      const lv_coord_t txt_w_used = (txt_size.x <= kInnerMaxW) ? txt_size.x : kInnerMaxW;
      lv_point_t wrapped_size;
      lv_txt_get_size(&wrapped_size, san_text, &g_font_12,
                      0, 0, txt_w_used > 0 ? txt_w_used : LV_COORD_MAX,
                      LV_TEXT_FLAG_NONE);
      lv_point_t foot_size;
      lv_txt_get_size(&foot_size, footer, &g_font_12, 0, 0,
                      LV_COORD_MAX, LV_TEXT_FLAG_NONE);
      const int foot_x = (txt_w_used > foot_size.x) ? (txt_w_used - foot_size.x) : 0;
      const int foot_y = inner_y + wrapped_size.y + 1;
      lv_obj_set_pos(foot, foot_x, foot_y);
    }

    // Force layout so the bubble's content-driven size is final, then bias
    // outgoing bubbles to the right gutter and incoming to the left.
    lv_obj_update_layout(bubble);
    lv_coord_t bw = lv_obj_get_width(bubble);
    lv_coord_t bh = lv_obj_get_height(bubble);
    if (bw > kBubbleMaxW) bw = kBubbleMaxW;
    lv_coord_t x_pos = m.outgoing
        ? (kContentW - bw - kSideGutter)
        : kSideGutter;
    lv_obj_set_pos(bubble, x_pos, y_pos);
    y_pos += bh + kRowGap;
  }

  // Scroll to the bottom so the newest message is in view.
  lv_obj_update_layout(p.msgs);
  lv_obj_scroll_to_y(p.msgs, LV_COORD_MAX, LV_ANIM_OFF);
}

// Rebuild the thread list inside a tab.
static void refreshChatList(LvChatPanel& p) {
  if (!g_lv.task || !p.list_cont) return;
  lv_obj_clean(p.list_cont);

  int idxs[UITask::MAX_UI_THREADS];
  int count = 0;
  if (p.inbox_combined) {
    count = g_lv.task->getCombinedInboxCount(idxs, UITask::MAX_UI_THREADS);
  } else {
    count = g_lv.task->getThreadCount(p.channel_mode, idxs, UITask::MAX_UI_THREADS);
  }

  if (count <= 0) {
    const char* empty = p.inbox_combined ? "No channels or chats yet" : (p.channel_mode ? "No channels yet" : "No chats yet");
    lv_obj_t* l = lv_list_add_text(p.list_cont, empty);
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_pad_all(l, 20, LV_PART_MAIN);
    return;
  }

  for (int i = 0; i < count; ++i) {
    bool ch = false; uint16_t unread = 0; uint32_t ts = 0;
    char name[UITask::MAX_THREAD_NAME + 1];
    if (!g_lv.task->getThreadInfo(idxs[i], ch, unread, ts, name, sizeof(name))) continue;

    char san_name[UITask::MAX_THREAD_NAME + 8];
    copyUtf8ReplacingMissingGlyphs(&g_font_14, san_name, sizeof(san_name), name);

    // Name only — the unread count is shown as a right-aligned badge below.
    const char* icon = ch ? LV_SYMBOL_LOOP : LV_SYMBOL_ENVELOPE;
    lv_obj_t* btn = lv_list_add_btn(p.list_cont, icon, san_name);

    // WhatsApp-like row style
    lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x141516), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x141516), LV_PART_MAIN);
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(btn,
        lv_color_hex(unread > 0 ? COLOR_ACCENT : COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(btn, &g_font_14, LV_PART_MAIN);
    // Single-line rows — tighten height/padding so more threads fit per
    // screen (was 56/16, mostly empty padding around one line of text).
    lv_obj_set_style_min_height(btn, 36, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(btn, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_left(btn, 12, LV_PART_MAIN);

    // Make long names scroll horizontally instead of being clipped.
    // lv_list_add_btn creates: child[0]=icon label, child[1]=text label.
    lv_obj_t* text_lbl = lv_obj_get_child(btn, 1);
    if (text_lbl) {
      lv_label_set_long_mode(text_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
      // Leave room on the right for the unread badge.
      lv_obj_set_width(text_lbl, lv_disp_get_hor_res(nullptr) - 116);
    }

    // Right-aligned unread badge (pill with the count).
    if (unread > 0) {
      lv_obj_t* badge = lv_label_create(btn);
      lv_obj_add_flag(badge, LV_OBJ_FLAG_IGNORE_LAYOUT);   // float, not in the row flex
      char cnt[8];
      snprintf(cnt, sizeof cnt, "%u", (unsigned)unread);
      lv_label_set_text(badge, cnt);
      lv_obj_set_style_text_font(badge, &g_font_12, LV_PART_MAIN);
      lv_obj_set_style_text_color(badge, lv_color_hex(0x0A0B0C), LV_PART_MAIN);
      lv_obj_set_style_bg_color(badge, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_radius(badge, 9, LV_PART_MAIN);
      lv_obj_set_style_pad_hor(badge, 6, LV_PART_MAIN);
      lv_obj_set_style_pad_ver(badge, 1, LV_PART_MAIN);
      lv_obj_align(badge, LV_ALIGN_RIGHT_MID, -10, 0);
    }

    // Blue "@" to the left of the count when an unread message here @mentions me.
    if (g_lv.task->threadHasMention(idxs[i])) {
      lv_obj_t* at = lv_label_create(btn);
      lv_obj_add_flag(at, LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_label_set_text(at, "@");
      lv_obj_set_style_text_font(at, &g_font_14, LV_PART_MAIN);
      lv_obj_set_style_text_color(at, lv_color_hex(COLOR_MENTION), LV_PART_MAIN);
      lv_obj_align(at, LV_ALIGN_RIGHT_MID, unread > 0 ? -48 : -10, 0);
    }

    p.ctx_store[i].idx     = idxs[i];
    p.ctx_store[i].channel = ch;
    lv_obj_add_event_cb(btn, threadSelectCb,    LV_EVENT_CLICKED,      &p.ctx_store[i]);
    lv_obj_add_event_cb(btn, threadLongPressCb, LV_EVENT_LONG_PRESSED, &p.ctx_store[i]);
  }
}

// Format a duration in seconds as a short badge like "12s" / "13m" / "2h" /
// "5d" / ">1y" / "?". Returns "?" for zero or absurd ages so we don't show
// 50-year-old timestamps from a never-synced RTC.
static void formatAgeBadge(char* buf, size_t cap, uint32_t age_secs) {
  if (cap == 0) return;
  if (age_secs == 0 || age_secs > (uint32_t)400 * 24u * 3600u) {
    snprintf(buf, cap, "?");
    return;
  }
  if (age_secs < 60u)             snprintf(buf, cap, "%us", (unsigned)age_secs);
  else if (age_secs < 3600u)      snprintf(buf, cap, "%um", (unsigned)(age_secs / 60u));
  else if (age_secs < 24u*3600u)  snprintf(buf, cap, "%uh", (unsigned)(age_secs / 3600u));
  else                            snprintf(buf, cap, "%ud", (unsigned)(age_secs / (24u*3600u)));
}

// Append a tiny pill-shaped status chip to the row. Used for RPT/USR/SRV/SNS
// type tags + DIR / GPS state.
static lv_obj_t* makeContactChip(lv_obj_t* parent, const char* text,
                                 uint32_t bg, uint32_t fg, lv_coord_t w) {
  lv_obj_t* chip = lv_obj_create(parent);
  lv_obj_remove_style_all(chip);
  lv_obj_set_size(chip, w, 16);
  lv_obj_set_style_bg_color(chip, lv_color_hex(bg), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(chip, 3, LV_PART_MAIN);
  lv_obj_set_style_pad_all(chip, 0, LV_PART_MAIN);
  lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* lbl = lv_label_create(chip);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_color(lbl, lv_color_hex(fg), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl, &g_font_12, LV_PART_MAIN);
  lv_obj_center(lbl);
  return chip;
}

// Two-byte chip labels for advert types. Colors echo MCterm's palette.
static void contactTypeChipStyle(uint8_t type, const char*& label,
                                 uint32_t& bg, uint32_t& fg) {
  switch (type) {
    case ADV_TYPE_REPEATER:
      label = "RPT"; bg = 0xC59F00; fg = 0x171a0e; break;   // yellow
    case ADV_TYPE_ROOM:
      label = "SRV"; bg = 0x6A4FC8; fg = 0xF1ECFF; break;   // purple
    case ADV_TYPE_SENSOR:
      label = "SNS"; bg = 0x2A6BD0; fg = 0xE6F2FF; break;   // blue
    case ADV_TYPE_CHAT:
    default:
      label = "USR"; bg = 0x2A3848; fg = 0xCFE0F4; break;   // muted
  }
}

// Great-circle distance in km between two lat/lon points (haversine).
static double contactDistanceKm(double lat1, double lon1,
                                double lat2, double lon2) {
  const double R = 6371.0;  // mean Earth radius, km
  const double dlat = (lat2 - lat1) * M_PI / 180.0;
  const double dlon = (lon2 - lon1) * M_PI / 180.0;
  const double a = sin(dlat / 2) * sin(dlat / 2) +
                   cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
                   sin(dlon / 2) * sin(dlon / 2);
  return R * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}

// Format a compact distance badge, honouring the km/miles preference:
//   metric:    "120m", "1.4km", "37km"
//   imperial:  "390ft", "0.9mi", "23mi"
// `out` is left empty when either fix is unknown (caller falls back to a
// plain "GPS" tag).
static void formatDistanceBadge(char* out, size_t out_cap,
                                double self_lat, double self_lon,
                                int32_t c_lat_e6, int32_t c_lon_e6) {
  out[0] = '\0';
  if (self_lat == 0.0 && self_lon == 0.0) return;        // no self fix
  if (c_lat_e6 == 0 && c_lon_e6 == 0) return;            // no contact fix
  const double km = contactDistanceKm(self_lat, self_lon,
                                      (double)c_lat_e6 / 1.0e6,
                                      (double)c_lon_e6 / 1.0e6);
#if defined(ESP32)
  const bool miles = touchPrefsGetUseMiles();
#else
  const bool miles = false;
#endif
  if (miles) {
    const double mi = km * 0.621371;
    if (mi < 0.19) {        // under ~1000 ft → show feet
      snprintf(out, out_cap, "%dft", (int)(mi * 5280.0 + 0.5));
    } else if (mi < 10.0) {
      snprintf(out, out_cap, "%.1fmi", mi);
    } else {
      snprintf(out, out_cap, "%dmi", (int)(mi + 0.5));
    }
  } else {
    if (km < 1.0) {
      snprintf(out, out_cap, "%dm", (int)(km * 1000.0 + 0.5));
    } else if (km < 10.0) {
      snprintf(out, out_cap, "%.1fkm", km);
    } else {
      snprintf(out, out_cap, "%dkm", (int)(km + 0.5));
    }
  }
}

static void refreshContactsList() {
  if (!g_lv.contacts_list || !g_lv.task) return;
  // Cache: skip the rebuild unless the count / filter / sort / search
  // changed, or 30 seconds have elapsed (so age labels re-render).
  static int     s_last_count  = -1;
  static uint8_t s_last_filter = 0xFF;
  static uint8_t s_last_sort   = 0xFF;
  static char    s_last_search[24] = {0};
  static unsigned long s_last_age_refresh_ms = 0;
  static bool    s_last_use_miles = false;
  const int     curr_count  = the_mesh.getNumContacts();
  const uint8_t curr_filter = g_lv.contacts_filter;
  const unsigned long now_ms = millis();
#if defined(ESP32)
  const bool curr_use_miles = touchPrefsGetUseMiles();
#else
  const bool curr_use_miles = false;
#endif
  // 60 s between forced rebuilds — only matters for the age labels
  // (e.g. "2h" → "3h"). Earlier 30 s wasn't necessary and doubled the
  // rebuild rate on the contacts tab.
  const bool age_refresh_due = (now_ms - s_last_age_refresh_ms) > 60000UL;
  const bool search_changed = strncmp(s_last_search, g_lv.contacts_search, sizeof(s_last_search)) != 0;
  if (curr_count == s_last_count && curr_filter == s_last_filter &&
      g_contacts_sort == s_last_sort && !age_refresh_due && !search_changed &&
      curr_use_miles == s_last_use_miles &&
      lv_obj_get_child_cnt(g_lv.contacts_list) > 0) {
    return;
  }
  s_last_count  = curr_count;
  s_last_filter = curr_filter;
  s_last_sort   = g_contacts_sort;
  s_last_use_miles = curr_use_miles;
  strncpy(s_last_search, g_lv.contacts_search, sizeof(s_last_search) - 1);
  s_last_search[sizeof(s_last_search) - 1] = '\0';
  s_last_age_refresh_ms = now_ms;
  lv_obj_clean(g_lv.contacts_list);
  // Sync the search-active indicator chip with the current search state.
  if (g_lv.contacts_search_indicator) {
    if (g_lv.contacts_search[0]) lv_obj_clear_flag(g_lv.contacts_search_indicator, LV_OBJ_FLAG_HIDDEN);
    else                          lv_obj_add_flag(g_lv.contacts_search_indicator, LV_OBJ_FLAG_HIDDEN);
  }

  // Snapshot matching contacts so we can sort them. File-static array on
  // .bss — no heap allocation per refresh.
  struct Entry {
    int      mesh_idx;
    uint8_t  type;
    bool     has_dir;
    bool     has_gps;
    bool     is_fav;
    uint32_t last_heard;
    int32_t  gps_lat;   // microdegrees (0 = unknown)
    int32_t  gps_lon;
    char     name[40];
  };
  static Entry* s_entries = (Entry*)psAlloc(sizeof(Entry) * 128);
  int n_entries = 0;
  // Snapshot the favorites blob once per refresh. touchPrefsIsFavorite
  // hits NVS on every call, and with N contacts the contact-list rebuild
  // was doing N NVS reads — noticeably laggy past ~30 contacts. One read
  // here + an in-memory memcmp scan per contact is several ms faster.
#if defined(ESP32)
  uint8_t fav_buf[TOUCH_FAVORITES_MAX * TOUCH_FAVORITE_KEY_BYTES];
  const int fav_count = touchPrefsCopyFavorites(fav_buf);
#else
  const int fav_count = 0;
  uint8_t* const fav_buf = nullptr;
#endif
  // Lower-cased search needle, computed once per refresh, so the inner
  // strcasestr-style search doesn't re-lower-case the operator's input N
  // times per character.
  char search_lc[24] = {0};
  bool have_search = (g_lv.contacts_search[0] != '\0');
  if (have_search) {
    int n = (int)strlen(g_lv.contacts_search);
    if (n > (int)sizeof(search_lc) - 1) n = (int)sizeof(search_lc) - 1;
    for (int i = 0; i < n; ++i) {
      char ch = g_lv.contacts_search[i];
      if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
      search_lc[i] = ch;
    }
    search_lc[n] = '\0';
  }
  for (int i = 0; i < curr_count && n_entries < 128; ++i) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(static_cast<uint32_t>(i), c) || !c.name[0]) continue;
    const bool is_rep = (c.type == ADV_TYPE_REPEATER);
    if (curr_filter == 1u && !is_rep) continue;
    if (curr_filter == 2u && is_rep) continue;
#if defined(ESP32)
    const bool is_fav = touchPrefsFavoritesSnapshotContains(fav_buf, fav_count, c.id.pub_key);
#else
    const bool is_fav = false;
#endif
    // Favorites filter only narrows the list when the operator actually
    // has favorites. With none set, "favorites" falls back to showing
    // every contact rather than an empty list (this is also the default
    // view on first open, so an operator who hasn't starred anyone still
    // sees their contacts).
    if (curr_filter == 3u && fav_count > 0 && !is_fav) continue;
    // Text-search filter: case-insensitive substring match on the contact
    // name. Cheap because contact names are <40 chars.
    if (have_search) {
      char name_lc[40];
      int nlen = (int)strlen(c.name);
      if (nlen > (int)sizeof(name_lc) - 1) nlen = (int)sizeof(name_lc) - 1;
      for (int j = 0; j < nlen; ++j) {
        char ch = c.name[j];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        name_lc[j] = ch;
      }
      name_lc[nlen] = '\0';
      if (strstr(name_lc, search_lc) == nullptr) continue;
    }
    Entry& e = s_entries[n_entries++];
    e.mesh_idx   = i;
    e.type       = c.type;
    e.has_dir    = (c.out_path_len != OUT_PATH_UNKNOWN && c.out_path_len > 0);
    e.has_gps    = (c.gps_lat != 0 || c.gps_lon != 0);
    e.gps_lat    = c.gps_lat;
    e.gps_lon    = c.gps_lon;
    e.is_fav     = is_fav;
    e.last_heard = c.last_advert_timestamp;
    strncpy(e.name, c.name, sizeof(e.name) - 1);
    e.name[sizeof(e.name) - 1] = '\0';
  }

  qsort(s_entries, n_entries, sizeof(Entry),
        [](const void* a, const void* b) -> int {
    const Entry* ea = static_cast<const Entry*>(a);
    const Entry* eb = static_cast<const Entry*>(b);
    // Favorites always sort above non-favorites regardless of the
    // category filter. Operator's starred contacts should be the first
    // thing visible whether they're filtering by RPT, Peer, or anything.
    if (ea->is_fav != eb->is_fav) {
      return ea->is_fav ? -1 : 1;
    }
    if (g_contacts_sort == CONTACTS_SORT_LAST_HEARD ||
        g_contacts_sort == CONTACTS_SORT_LAST_MSG) {
      if (eb->last_heard != ea->last_heard) {
        return (eb->last_heard > ea->last_heard) ? 1 : -1;
      }
    }
    return strcasecmp(ea->name, eb->name);
  });

  const uint32_t now_secs = the_mesh.getRTCClock()->getCurrentTime();
  // Self GPS for the per-row distance badge — fetched once per refresh.
  const double self_lat = g_lv.task->getNodeLat();
  const double self_lon = g_lv.task->getNodeLon();

  for (int k = 0; k < n_entries; ++k) {
    if (k >= (int)(sizeof(s_contacts_ctx)/sizeof(s_contacts_ctx[0]))) break;
    const Entry& e = s_entries[k];
    const bool is_rep = (e.type == ADV_TYPE_REPEATER);

    char san[40];
    copyUtf8ReplacingMissingGlyphs(&g_font_14, san, sizeof(san), e.name);

    // Compact ASCII metadata in the label itself (no nested widgets). This
    // is plain `lv_list_add_btn` so the layout stays whatever lv_list does
    // by default — that's the variant that booted reliably on pre-alpha_3.
    const char* ttag = "USR";
    switch (e.type) {
      case ADV_TYPE_REPEATER: ttag = "RPT"; break;
      case ADV_TYPE_ROOM:     ttag = "SRV"; break;
      case ADV_TYPE_SENSOR:   ttag = "SNS"; break;
      case ADV_TYPE_CHAT:
      default:                ttag = "USR"; break;
    }
    char age_buf[12];
    uint32_t age_secs = 0;
    if (now_secs > e.last_heard && e.last_heard != 0) age_secs = now_secs - e.last_heard;
    formatAgeBadge(age_buf, sizeof(age_buf), age_secs);
    // Two-line row: name on line 1, metadata on line 2. Embedded "\n" +
    // LV_LABEL_LONG_WRAP avoids appending a sibling label inside
    // lv_list_add_btn's flex layout — earlier attempts to do that
    // bootlooped on pre-alpha because the list-button's internal flex
    // rearranged on first scroll. Single-label wrap is the safe variant.
    // The favorite indicator is rendered separately below as a floating
    // overlay so the * actually looks like a marker, not a typo on the
    // first character.
    // GPS column: show the live distance from our position to theirs when
    // both fixes are known (e.g. "1.4km"); fall back to a plain "GPS" tag
    // when they have a fix but we don't.
    char gps_badge[16] = "";
    if (e.has_gps) {
      char dist[12];
      formatDistanceBadge(dist, sizeof(dist), self_lat, self_lon,
                          e.gps_lat, e.gps_lon);
      if (dist[0]) snprintf(gps_badge, sizeof(gps_badge), "  %s", dist);
      else         snprintf(gps_badge, sizeof(gps_badge), "  GPS");
    }
    // Landscape is wide enough to fit name + metadata on ONE line, so the row
    // can be ~1/3 shorter; portrait keeps the two-line layout (name on top,
    // metadata below).
    const bool ct_land = chatLandscape();
    char label[110];
    snprintf(label, sizeof(label), "%s%s[%s]  %s%s%s",
             san, ct_land ? "   " : "\n", ttag, age_buf,
             e.has_dir ? "  DIR" : "",
             gps_badge);

    const char* icon = is_rep ? LV_SYMBOL_CHARGE : LV_SYMBOL_ENVELOPE;
    lv_obj_t* btn = lv_list_add_btn(g_lv.contacts_list, icon, label);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x141516), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x141516), LV_PART_MAIN);
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(btn, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(btn, &g_font_14, LV_PART_MAIN);
    // Two-line portrait row needs ~56; single-line landscape row fits in ~34.
    lv_obj_set_style_min_height(btn, ct_land ? 34 : 56, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(btn, ct_land ? 4 : 6, LV_PART_MAIN);
    lv_obj_set_style_pad_left(btn, 10, LV_PART_MAIN);
    lv_obj_t* text_lbl = lv_obj_get_child(btn, 1);
    if (text_lbl) {
      // WRAP (not SCROLL) so the embedded \n produces a real second line
      // without marquee-ing either line. recolor() lets us paint the
      // metadata line in COLOR_SUB while keeping the name line in COLOR_TEXT.
      // Landscape: single line, so give it the extra width to stay one row.
      lv_label_set_long_mode(text_lbl, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(text_lbl, ct_land ? (tabContentW() - 80) : 195);
    }
    // Favorite marker: a large yellow asterisk pinned to the right edge of
    // the row. Floating + IGNORE_LAYOUT keeps it out of the lv_list_add_btn
    // flex chain (sibling children inside the flex was what bootlooped
    // pre-alpha_4). LVGL/Montserrat doesn't ship a star glyph at all — '*'
    // at font 28 yellow is the closest we get without a custom font subset.
    if (e.is_fav) {
      lv_obj_t* star = lv_label_create(btn);
      lv_label_set_text(star, TOUCH_SYM_STAR_BIG);
      // 14 px star — the 28 px one was a billboard pinned to the row edge.
      // The smaller glyph reads as a discreet "favorite" marker without
      // dominating the row layout.
      lv_obj_set_style_text_font(star, &star_font_14, LV_PART_MAIN);
      lv_obj_set_style_text_color(star, lv_color_hex(0xC9A24A), LV_PART_MAIN);
      lv_obj_add_flag(star, LV_OBJ_FLAG_FLOATING);
      lv_obj_add_flag(star, LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_obj_align(star, LV_ALIGN_RIGHT_MID, -8, 0);
    }

    s_contacts_ctx[k].mesh_idx    = static_cast<uint32_t>(e.mesh_idx);
    s_contacts_ctx[k].is_repeater = is_rep;
    lv_obj_add_event_cb(btn, contactSelectCb, LV_EVENT_CLICKED, &s_contacts_ctx[k]);
  }

  if (n_entries == 0) {
    lv_obj_t* l = lv_list_add_text(g_lv.contacts_list, "No matching contacts");
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_pad_all(l, 20, LV_PART_MAIN);
  }
}

static void refreshThreadLists() {
  refreshChatList(g_lv.dm);
}

// Rolling diagnostic log
static void pushDiagLine(const char* message) {
  if (!message || !message[0]) return;
  // Identity / radio boot line: pin to a dedicated label so it cannot scroll
  // out from under the operator. Without this, the most important diagnostic
  // (does our pubkey prefix stay stable across flashes?) is the FIRST thing
  // to disappear once the diag ring fills with RX/TX chatter.
  if (message[0] == 'I' && message[1] == 'D' && message[2] == ' ') {
    strncpy(s_diag_id_pinned, message, sizeof(s_diag_id_pinned) - 1);
    s_diag_id_pinned[sizeof(s_diag_id_pinned) - 1] = '\0';
    if (g_lv.diag_id_label) {
      lv_label_set_text(g_lv.diag_id_label, s_diag_id_pinned);
    }
    return;
  }
  strncpy(s_diag_ring[s_diag_line % DIAG_LINES], message, DIAG_COLS - 1);
  s_diag_ring[s_diag_line % DIAG_LINES][DIAG_COLS - 1] = '\0';
  s_diag_line++;
  if (!g_lv.diag_label) return;
  char block[DIAG_LINES * DIAG_COLS + 32];
  int p = 0;
  int first = (s_diag_line > DIAG_LINES) ? (s_diag_line - DIAG_LINES) : 0;
  for (int i = first; i < s_diag_line && p < (int)sizeof(block) - 2; i++) {
    p += snprintf(block + p, sizeof(block) - static_cast<size_t>(p),
                  "%s\n", s_diag_ring[i % DIAG_LINES]);
  }
  lv_label_set_text(g_lv.diag_label, p > 0 ? block : "—");
}

static void pushLogLine(char ring[][RXLOG_COLS], int& line_counter, const char* message) {
  if (!message || !message[0]) return;
  strncpy(ring[line_counter % RXLOG_LINES], message, RXLOG_COLS - 1);
  ring[line_counter % RXLOG_LINES][RXLOG_COLS - 1] = '\0';
  ++line_counter;
}

static void collectLogText(const char ring[][RXLOG_COLS], int line_counter, char* out, size_t out_cap) {
  if (!out || out_cap == 0) return;
  out[0] = '\0';
  if (line_counter <= 0) return;
  int p = 0;
  int first = (line_counter > RXLOG_LINES) ? (line_counter - RXLOG_LINES) : 0;
  for (int i = first; i < line_counter && p < static_cast<int>(out_cap) - 2; ++i) {
    p += snprintf(out + p, out_cap - static_cast<size_t>(p), "%s\n", ring[i % RXLOG_LINES]);
  }
  if (p < 0 || p >= static_cast<int>(out_cap)) out[out_cap - 1] = '\0';
}

static void setLogModeButtonStyles() {
  if (!g_set_modal.log_rx_btn || !g_set_modal.log_raw_btn) return;
  const bool rx_selected = (g_set_modal.log_mode == 0);
  lv_obj_set_style_bg_color(
      g_set_modal.log_rx_btn,
      lv_color_hex(rx_selected ? COLOR_ACCENT : 0x2A3848),
      LV_PART_MAIN);
  lv_obj_set_style_text_color(
      g_set_modal.log_rx_btn,
      lv_color_hex(rx_selected ? 0x031315 : COLOR_TEXT),
      LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      g_set_modal.log_raw_btn,
      lv_color_hex(rx_selected ? 0x2A3848 : COLOR_ACCENT),
      LV_PART_MAIN);
  lv_obj_set_style_text_color(
      g_set_modal.log_raw_btn,
      lv_color_hex(rx_selected ? COLOR_TEXT : 0x031315),
      LV_PART_MAIN);
}

static void refreshLogModalView() {
  if (!g_set_modal.log_view_ta || g_set_modal.kind != SettingsModalKind::Log) return;
  static char text[(RXLOG_LINES * RXLOG_COLS) + 64];
  if (g_set_modal.log_mode == 0) {
    collectLogText(s_rxlog_ring, s_rxlog_line, text, sizeof(text));
  } else {
    collectLogText(s_rawlog_ring, s_rawlog_line, text, sizeof(text));
  }
  lv_textarea_set_text(g_set_modal.log_view_ta, text[0] ? text : "No entries yet.");
  lv_textarea_set_cursor_pos(g_set_modal.log_view_ta, LV_TEXTAREA_CURSOR_LAST);
  setLogModeButtonStyles();
}

static void logModeRxCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  g_set_modal.log_mode = 0;
  refreshLogModalView();
}

static void logModeRawCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  g_set_modal.log_mode = 1;
  refreshLogModalView();
}

#if defined(HAS_TDECK_TRACKBALL)
// Poll the trackball each UI tick: move the cursor by the accumulated motion and
// show/hide it on activity. The centre-click -> touch handling lives in the
// PIN_USER_BTN block of UITask::loop() (it needs screen on/off context).
// ---- Trackball map panning (Map tab only) ----
// Each motion tick slides the pan layer live (cheap); when motion stops the
// accumulated shift is committed to a new center + a tile/marker re-render.
static int           s_map_tb_tx = 0, s_map_tb_ty = 0;   // accumulated screen-px shift
static bool          s_map_tb_pan_pending = false;
static unsigned long s_map_tb_last_motion = 0;

static void mapTrackballFinalizePan() {
  s_map_tb_pan_pending = false;
  if (s_map_tb_tx == 0 && s_map_tb_ty == 0) return;
  double cwx, cwy;
  latLonToWorldPx(s_map_center_lat, s_map_center_lon, s_map_zoom, &cwx, &cwy);
  double nlat, nlon;
  worldPxToLatLon(cwx + s_map_tb_tx, cwy + s_map_tb_ty, s_map_zoom, &nlat, &nlon);
  s_map_center_lat = nlat;
  s_map_center_lon = nlon;
  s_map_tb_tx = 0;
  s_map_tb_ty = 0;
  renderMapTiles();    // also snaps the pan layer back to (0,0)
  renderMapMarkers();
  refreshMapInfoLabel();
}

static void updateTrackball(unsigned long now) {
  if (!s_tb_cursor) return;
  const int W = lv_disp_get_hor_res(nullptr);
  const int H = lv_disp_get_ver_res(nullptr);

  int dx = 0, dy = 0;
  const bool moved = tdeckTrackballReadMotion(&dx, &dy);

  // ---- Emoji picker selector mode ----
  // While the emoji sheet is open the trackball drives a grid highlight instead
  // of the soft cursor: each motion step moves one cell, the centre click
  // inserts (handled in the PIN_USER_BTN block). The cursor is hidden so it
  // can't also click through to the grid.
  if (s_emoji_sheet) {
    s_tb_click_press = false;   // selector consumes clicks; don't inject a tap
    if (!lv_obj_has_flag(s_tb_cursor, LV_OBJ_FLAG_HIDDEN))
      lv_obj_add_flag(s_tb_cursor, LV_OBJ_FLAG_HIDDEN);
    if (moved && (dx != 0 || dy != 0)) {
      emojiSelectorMove(dx, dy);
      if (g_lv.task) g_lv.task->noteUserInput();
    }
    return;
  }

  // Flush a pending map pan if the user navigated away from the Map tab.
  const bool on_map = (getActiveTab() == MAP_TAB_INDEX);
  if (!on_map && s_map_tb_pan_pending) mapTrackballFinalizePan();

  if (on_map) {
    // On the Map tab the trackball pans the map instead of driving the cursor.
    // The cursor is hidden and its click does nothing here (tap markers with a
    // finger); the click flag is consumed so lvglTouchRead won't inject it.
    s_tb_click_press = false;
    if (!lv_obj_has_flag(s_tb_cursor, LV_OBJ_FLAG_HIDDEN))
      lv_obj_add_flag(s_tb_cursor, LV_OBJ_FLAG_HIDDEN);
    if (moved && (dx != 0 || dy != 0)) {
      const int sdx = dx * kTbCursorStepPx;
      const int sdy = dy * kTbCursorStepPx;
      shiftMapChildren(-sdx, -sdy);   // roll right -> view pans east
      s_map_tb_tx += sdx;
      s_map_tb_ty += sdy;
      s_map_tb_pan_pending = true;
      s_map_tb_last_motion = now;
      if (g_lv.task) g_lv.task->noteUserInput();
    } else if (s_map_tb_pan_pending && (now - s_map_tb_last_motion) > 220) {
      mapTrackballFinalizePan();
    }
    return;
  }

  // ---- Cursor mode (every other tab) ----
  // Accumulate raw motion into the TARGET at full sensitivity (so total travel
  // per roll is unchanged); the rendered cursor eases toward it below.
  if (moved) {
    s_tb_target_x += (float)(dx * kTbCursorStepPx);
    s_tb_target_y += (float)(dy * kTbCursorStepPx);
    if (s_tb_target_x < 0) s_tb_target_x = 0;
    if (s_tb_target_y < 0) s_tb_target_y = 0;
    if (s_tb_target_x > W - 1) s_tb_target_x = (float)(W - 1);
    if (s_tb_target_y > H - 1) s_tb_target_y = (float)(H - 1);
    s_tb_last_active_ms = now;
    if (g_lv.task) g_lv.task->noteUserInput();
  }
  if (s_tb_click_press) s_tb_last_active_ms = now;  // stay visible while clicking

  // Framerate-independent low-pass toward the target. k = dt/tau (capped at 1),
  // so a diagonal roll whose two axes pulse at different ticks renders as one
  // smooth glide instead of a horizontal/vertical staircase.
  unsigned long dt = now - s_tb_prev_ms;
  s_tb_prev_ms = now;
  if (dt > 100) dt = 100;
  float k = (float)dt / kTbCursorSmoothMs;
  if (k > 1.0f) k = 1.0f;
  s_tb_render_x += (s_tb_target_x - s_tb_render_x) * k;
  s_tb_render_y += (s_tb_target_y - s_tb_render_y) * k;
  s_tb_cursor_x = (int)(s_tb_render_x + 0.5f);
  s_tb_cursor_y = (int)(s_tb_render_y + 0.5f);

  const bool visible = (now - s_tb_last_active_ms) < kTbCursorHideMs;
  if (visible) {
    lv_obj_set_pos(s_tb_cursor, s_tb_cursor_x - kTbCursorDiameter / 2,
                                s_tb_cursor_y - kTbCursorDiameter / 2);
    lv_obj_clear_flag(s_tb_cursor, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_tb_cursor);
  } else if (!lv_obj_has_flag(s_tb_cursor, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_add_flag(s_tb_cursor, LV_OBJ_FLAG_HIDDEN);
  }
}
#endif

#if defined(HAS_TDECK_KEYBOARD)
// Close the topmost popup / modal (front-to-back priority), like tapping its
// X / close button. Returns true if one was dismissed.
static bool hwKeyDismissTopPopup() {
  if (s_meminfo_root)     { closeMemInfo();            return true; }   // topmost diagnostic popup
#if defined(HAS_TDECK_GT911)
  if (s_fm_img_root)      { fmImageClose();           return true; }   // image viewer (top FM overlay)
  if (s_editor_root)      { fmEditorClose();          return true; }   // file-manager modals first
  if (s_fm_prompt)        { fmPromptClose();          return true; }
  if (s_fm_actions)       { fmCloseActions();         return true; }
  if (s_term_picker_root) { closeTermCmdPicker();     return true; }   // above the fullscreen view
  if (s_fullscreen_view) {
    closeFullscreenView();
    if (g_lv.tabview) lv_tabview_set_act(g_lv.tabview, 0, LV_ANIM_OFF);
    return true;
  }
#endif
  if (s_confirm_modal)          { confirmDismiss();             return true; }
  if (s_map_contacts_root)      { closeMapContacts();           return true; }
  if (s_map_picker_root)        { closeMapPicker();             return true; }
  if (s_trace_result_root)      { closeTraceResultPopup();      return true; }
  if (s_msg_menu_root)          { closeMsgActionMenu();         return true; }
  if (s_msg_info_root)          { closeMsgInfoPopup();          return true; }
  if (s_admin_picker_root)      { closeAdminCmdPicker();        return true; }
  if (s_admin_pw_root)          { closeAdminPwPrompt();         return true; }
  if (s_addch_sheet)            { closeAddChannelSheet();       return true; }
  if (s_qr_sheet)               { closeQuickReplySheet();       return true; }
  if (s_channel_long_sheet)     { closeChannelLongSheet();      return true; }
  if (s_action_sheet_root)      { closeActionSheet();           return true; }
  if (s_contacts_search_sheet)  { closeContactsSearchSheet();   return true; }
  if (s_contacts_overflow_root) { closeContactsOverflowSheet(); return true; }
  if (s_share_my_root)          { closeShareMyContact();        return true; }
  if (s_los_root)               { closeLosModal();              return true; }
  if (s_admin_root)             { closeAdminConsole();          return true; }
  if (settingsModalIsOpen())    { closeSettingsModal();         return true; }
  if (s_power_menu)             { closePowerMenu();             return true; }
  if (s_cc_root)                { closeControlCenter();         return true; }
  return false;
}

// Keys that act as "close the popup" when no text field is focused. The user
// picked the easy-to-find corner keys; there's no dedicated Esc on this keyboard.
static bool isDismissKey(int key) {
  switch (key) {
    case 0x08:                       // backspace
    case 0x0D: case 0x0A:            // enter
    case 'p': case 'P':
    case 'q': case 'Q':
    case 'a': case 'A':
      return true;
    default: return false;
  }
}

// True if any popup/modal is currently up (mirror of hwKeyDismissTopPopup's set).
static bool anyPopupOpen() {
  return s_confirm_modal || s_map_picker_root || s_msg_menu_root || s_msg_info_root ||
         s_admin_picker_root || s_admin_pw_root || s_addch_sheet || s_qr_sheet ||
         s_channel_long_sheet || s_action_sheet_root || s_contacts_search_sheet ||
         s_contacts_overflow_root || s_share_my_root || s_los_root || s_admin_root ||
         s_meminfo_root || settingsModalIsOpen() || s_cc_root
#if defined(HAS_TDECK_GT911)
         || s_fullscreen_view || s_term_picker_root || s_fm_prompt || s_fm_actions || s_editor_root || s_fm_img_root
#endif
         ;
}

// Bottom-tab a key jumps to (no popup, no field focused), or -1. Space/H Home,
// M Chats, C Contacts, L Map, S Settings.
static int tabForKey(int key) {
  switch (key) {
    case ' ':
    case 'h': case 'H': return 0;
    case 'm': case 'M': return CHAT_INBOX_TAB_INDEX;
    case 'c': case 'C': return CONTACTS_TAB_INDEX;
    case 'l': case 'L': return MAP_TAB_INDEX;
    case 's': case 'S': return SETTINGS_TAB_INDEX;
    default: return -1;
  }
}

#if defined(HAS_TDECK_GT911)
// ---- Lock screen -------------------------------------------------------------
// A full-screen overlay (wallpaper + live clock + lock-state text) shown while
// the T-Deck is hard-locked. The wallpaper is a JPEG decoded to RGB565; the
// clock and the "Screen locked" / "hold to unlock" text are drawn live here so
// the image stays a plain wallpaper the user can swap. Lifecycle: lockScreen()
// builds it (dark); a key/trackball press reveals it (backlight on) without
// unlocking; holding the trackball ~1 s unlocks (see the loop's button block).
static lv_obj_t*    s_lock_root      = nullptr;
static lv_obj_t*    s_lock_clock     = nullptr;
static uint8_t*     s_lock_wall      = nullptr;   // decoded RGB565 (lvglPsramAlloc)
static lv_img_dsc_t s_lock_wall_dsc;
static int          s_lock_clock_min = -1;        // last minute drawn (redraw guard)

// How long the trackball must be held to unlock, in ms.
static const unsigned long kLockUnlockHoldMs = 1000;
// "Unlocking…" countdown popup (mirrors the spacebar lock countdown) shown
// while the trackball is held on the lock screen.
static lv_obj_t* s_unlock_popup = nullptr;
static lv_obj_t* s_unlock_count = nullptr;

// Decode the configured wallpaper (internal SPIFFS path, or an "sd:"-prefixed SD
// path) into a fresh RGB565 buffer; fall back to the embedded placeholder.
// Caller owns the buffer (lvglPsramFree). Returns nullptr only if even the
// placeholder fails to decode.
static uint8_t* lockscreenDecodeWallpaper(int* out_w, int* out_h) {
  char path[TOUCH_LOCK_WALLPAPER_MAXLEN];
  touchPrefsGetLockWallpaper(path, sizeof path);
  // (The built-in default is handled by lockscreenShow() via the RGB565 embed;
  // this path only runs for user-selected wallpapers.)
  fs::FS* fsp = &SPIFFS;
  const char* fp = path;
  if (!strncmp(path, "sd:", 3)) { fsp = &SD; fp = path + 3; }

  uint8_t* rgb = nullptr;
  if (fp && fp[0]) {
    File f = fsp->open(fp, "r");
    if (f) {
      size_t sz = f.size();
      if (sz > 0 && sz <= FM_IMG_MAX) {
        uint8_t* enc = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
        if (!enc) enc = (uint8_t*)malloc(sz);
        if (enc) {
          size_t rd = f.readBytes((char*)enc, sz);
          const bool is_png = (rd >= 4 && enc[0] == 0x89 && enc[1] == 'P');
          if (!is_png) rgb = decodeJpegToRgb565(enc, rd, out_w, out_h);
          free(enc);
        }
      }
      f.close();
    }
  }
  if (!rgb) rgb = decodeJpegToRgb565(lockscreen_placeholder_jpg,
                                     lockscreen_placeholder_jpg_len, out_w, out_h);
  return rgb;
}

static void lockscreenUpdateClock() {
  if (!s_lock_clock) return;
  mesh::RTCClock* rtc = the_mesh.getRTCClock();
  uint32_t t = rtc ? rtc->getCurrentTime() : 0;
  int hh = 0, mm = 0;
  if (t > 0) { time_t tt = (time_t)t; struct tm v; localtime_r(&tt, &v); hh = v.tm_hour; mm = v.tm_min; }
  char b[8]; snprintf(b, sizeof b, "%02d:%02d", hh, mm);
  lv_label_set_text(s_lock_clock, b);
  s_lock_clock_min = mm;
}

static void lockscreenUnlockPopupHide() {
  if (s_unlock_popup) { lv_obj_del(s_unlock_popup); s_unlock_popup = nullptr; s_unlock_count = nullptr; }
}
// Show / update the "Unlocking…" countdown while the trackball is held.
static void lockscreenUnlockProgress(unsigned long remaining_ms) {
  if (!s_unlock_popup) {
    s_unlock_popup = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_unlock_popup);
    lv_obj_set_size(s_unlock_popup, 180, 104);
    lv_obj_center(s_unlock_popup);
    lv_obj_set_style_bg_color(s_unlock_popup, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_unlock_popup, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_unlock_popup, 12, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_unlock_popup, lv_color_hex(0x18191A), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_unlock_popup, 1, LV_PART_MAIN);
    lv_obj_clear_flag(s_unlock_popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* t = lv_label_create(s_unlock_popup);
    lv_label_set_text(t, "Unlocking\xE2\x80\xA6");
    lv_obj_set_style_text_color(t, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(t, &g_font_16, LV_PART_MAIN);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 8);

    s_unlock_count = lv_label_create(s_unlock_popup);
    lv_obj_set_style_text_color(s_unlock_count, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_unlock_count, &g_font_16, LV_PART_MAIN);
    lv_obj_align(s_unlock_count, LV_ALIGN_CENTER, 0, 6);

    lv_obj_t* h = lv_label_create(s_unlock_popup);
    lv_label_set_text(h, "keep holding");
    lv_obj_set_style_text_color(h, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(h, &g_font_12, LV_PART_MAIN);
    lv_obj_align(h, LV_ALIGN_BOTTOM_MID, 0, -8);
  }
  lv_obj_move_foreground(s_unlock_popup);
  if (s_unlock_count) {
    char b[4]; snprintf(b, sizeof b, "%u", (unsigned)((remaining_ms + 999) / 1000));
    lv_label_set_text(s_unlock_count, b);
  }
}

// Build the overlay if needed and bring it to the front. Idempotent: a second
// call just re-foregrounds it (no wallpaper re-decode).
static void lockscreenShow() {
  // The status bar sits on lv_layer_sys (above this overlay); drop its opaque
  // background + accent border so the wallpaper shows through behind the icons.
  if (g_statusbar.root) {
    lv_obj_set_style_bg_opa(g_statusbar.root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(g_statusbar.root, LV_OPA_TRANSP, LV_PART_MAIN);
  }
  if (s_lock_root) { lv_obj_move_foreground(s_lock_root); return; }
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);

  s_lock_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_lock_root);
  lv_obj_set_size(s_lock_root, sw, sh);
  lv_obj_set_pos(s_lock_root, 0, 0);
  lv_obj_set_style_bg_color(s_lock_root, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_lock_root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_lock_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_lock_root, LV_OBJ_FLAG_CLICKABLE);   // absorb taps (no UI leak)

  // Wallpaper, scaled to cover the screen (crop overflow, never letterbox).
  int ww = 0, wh = 0;
  const uint8_t* wall_data = nullptr;
  if (s_lock_wall) { lvglPsramFree(s_lock_wall); s_lock_wall = nullptr; }
  char wpath[TOUCH_LOCK_WALLPAPER_MAXLEN];
  touchPrefsGetLockWallpaper(wpath, sizeof wpath);
  if (!strcmp(wpath, "/lock/placeholder.jpg")) {
    // Default: the pre-dithered RGB565 embed, drawn straight from flash — crisp,
    // with no JPEG round-trip to re-introduce gradient banding.
    wall_data = (const uint8_t*)lockscreen_wallpaper_rgb565;
    ww = LOCKSCREEN_WALLPAPER_W;
    wh = LOCKSCREEN_WALLPAPER_H;
  } else {
    s_lock_wall = lockscreenDecodeWallpaper(&ww, &wh);   // custom wallpaper (JPEG)
    wall_data = s_lock_wall;
  }
  if (wall_data && ww > 0 && wh > 0) {
    memset(&s_lock_wall_dsc, 0, sizeof s_lock_wall_dsc);
    s_lock_wall_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    s_lock_wall_dsc.header.w  = (uint32_t)ww;
    s_lock_wall_dsc.header.h  = (uint32_t)wh;
    s_lock_wall_dsc.data      = wall_data;
    s_lock_wall_dsc.data_size = (uint32_t)ww * (uint32_t)wh * sizeof(lv_color_t);
    lv_obj_t* img = lv_img_create(s_lock_root);
    lv_img_set_src(img, &s_lock_wall_dsc);
    lv_img_set_antialias(img, true);
    lv_img_set_pivot(img, ww / 2, wh / 2);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    uint32_t zx = (uint32_t)sw * 256u / (uint32_t)ww;
    uint32_t zy = (uint32_t)sh * 256u / (uint32_t)wh;
    uint32_t zoom = (zx > zy) ? zx : zy;             // cover
    if (zoom < 1) zoom = 1; if (zoom > 2048) zoom = 2048;
    lv_img_set_zoom(img, (uint16_t)zoom);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
  }

  const lv_color_t col = lv_color_hex(touchPrefsGetLockTextColor());

  s_lock_clock = lv_label_create(s_lock_root);
  lv_label_set_text(s_lock_clock, "--:--");
  lv_obj_set_style_text_font(s_lock_clock, &lv_font_montserrat_28, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_lock_clock, col, LV_PART_MAIN);
  lv_obj_align(s_lock_clock, LV_ALIGN_TOP_MID, 0, 30);   // below the 22 px status bar
  s_lock_clock_min = -1;
  lockscreenUpdateClock();

  lv_obj_t* st = lv_label_create(s_lock_root);
  lv_label_set_text(st, "Screen locked");
  lv_obj_set_style_text_font(st, &g_font_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(st, col, LV_PART_MAIN);
  lv_obj_align(st, LV_ALIGN_TOP_MID, 0, 190);

  lv_obj_t* hint = lv_label_create(s_lock_root);
  lv_label_set_text(hint, "hold the trackball to unlock");
  lv_obj_set_style_text_font(hint, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(hint, col, LV_PART_MAIN);
  lv_obj_set_style_text_opa(hint, LV_OPA_70, LV_PART_MAIN);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
}

static void lockscreenHide() {
  lockscreenUnlockPopupHide();
  if (s_lock_root) { lv_obj_del(s_lock_root); s_lock_root = nullptr; s_lock_clock = nullptr; }
  if (s_lock_wall) { lvglPsramFree(s_lock_wall); s_lock_wall = nullptr; }
  s_lock_clock_min = -1;
  // Restore the status bar's normal opaque background + accent border.
  if (g_statusbar.root) {
    lv_obj_set_style_bg_opa(g_statusbar.root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(g_statusbar.root, LV_OPA_30, LV_PART_MAIN);
  }
}

// Refresh the clock when the minute rolls over (called each loop tick).
static void serviceLockscreen() {
  if (!s_lock_root || !s_lock_clock) return;
  mesh::RTCClock* rtc = the_mesh.getRTCClock();
  uint32_t t = rtc ? rtc->getCurrentTime() : 0;
  int mm = 0;
  if (t > 0) { time_t tt = (time_t)t; struct tm v; localtime_r(&tt, &v); mm = v.tm_min; }
  if (mm != s_lock_clock_min) lockscreenUpdateClock();
}

// ---- Lock-screen wallpaper picker (lists JPEGs in internal /lock/ + SD) ----
static lv_obj_t* s_lockwall_picker = nullptr;
static char s_lockwall_paths[24][TOUCH_LOCK_WALLPAPER_MAXLEN];
static int  s_lockwall_count = 0;

static void lockwallPickerClose() {
  if (s_lockwall_picker) { lv_obj_del(s_lockwall_picker); s_lockwall_picker = nullptr; }
}
static void lockwallPickerCloseCb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) lockwallPickerClose();
}
static bool lockwallIsJpg(const char* name) {
  if (!name) return false;
  const char* d = strrchr(name, '.');
  return d && (!strcasecmp(d, ".jpg") || !strcasecmp(d, ".jpeg"));
}
static void lockwallAddPath(const char* prefpath) {
  const int cap = (int)(sizeof s_lockwall_paths / sizeof s_lockwall_paths[0]);
  if (s_lockwall_count >= cap) return;
  for (int i = 0; i < s_lockwall_count; ++i)
    if (!strcmp(s_lockwall_paths[i], prefpath)) return;          // dedup
  strncpy(s_lockwall_paths[s_lockwall_count], prefpath, TOUCH_LOCK_WALLPAPER_MAXLEN - 1);
  s_lockwall_paths[s_lockwall_count][TOUCH_LOCK_WALLPAPER_MAXLEN - 1] = '\0';
  s_lockwall_count++;
}
static void lockwallScan() {
  s_lockwall_count = 0;
  // Internal SPIFFS is flat — match files whose full path is under /lock/.
  File root = SPIFFS.open("/");
  if (root) {
    File e = root.openNextFile();
    while (e) {
      const char* full = e.path();
      if (full && !strncmp(full, "/lock/", 6) && lockwallIsJpg(full)) lockwallAddPath(full);
      e.close();
      e = root.openNextFile();
    }
    root.close();
  }
  // SD: real directories. Look in /lock and the card root.
  if (s_sd_mounted) {
    const char* dirs[2] = { "/lock", "/" };
    for (int di = 0; di < 2; ++di) {
      File d = SD.open(dirs[di]);
      if (d && d.isDirectory()) {
        File e = d.openNextFile();
        while (e) {
          if (!e.isDirectory()) {
            const char* nm = e.name();
            const char* base = strrchr(nm, '/'); base = base ? base + 1 : nm;
            if (lockwallIsJpg(base)) {
              char pref[TOUCH_LOCK_WALLPAPER_MAXLEN];
              if (!strcmp(dirs[di], "/")) snprintf(pref, sizeof pref, "sd:/%s", base);
              else                        snprintf(pref, sizeof pref, "sd:%s/%s", dirs[di], base);
              lockwallAddPath(pref);
            }
          }
          e.close();
          e = d.openNextFile();
        }
      }
      if (d) d.close();
    }
  }
}
static void lockwallChosenCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const char* path = (const char*)lv_event_get_user_data(e);
  if (!path) return;
  touchPrefsSetLockWallpaper(path);
  if (s_lockwall_btn_lbl) {
    char disp[64]; lockwallDisplayName(path, disp, sizeof disp);
    lv_label_set_text(s_lockwall_btn_lbl, disp);
  }
  lockwallPickerClose();
  if (g_lv.task) g_lv.task->showAlert("Lock wallpaper set", 1100);
}
static void openLockWallPicker() {
  lockwallScan();
  lockwallPickerClose();
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_lockwall_picker = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_lockwall_picker);
  lv_obj_set_size(s_lockwall_picker, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_lockwall_picker, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_lockwall_picker, lv_color_hex(COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_lockwall_picker, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_lockwall_picker, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(s_lockwall_picker);
  lv_label_set_text(title, "Lock wallpaper");
  lv_obj_set_style_text_font(title, &g_font_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_pos(title, 8, 8);

  lv_obj_t* close = lv_btn_create(s_lockwall_picker);
  lv_obj_set_size(close, 30, 26);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -6, 4);
  styleButton(close);
  lv_obj_add_event_cb(close, lockwallPickerCloseCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* cl = lv_label_create(close); lv_label_set_text(cl, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_font(cl, &g_font_12, LV_PART_MAIN); lv_obj_center(cl);

  lv_obj_t* list = lv_obj_create(s_lockwall_picker);
  lv_obj_remove_style_all(list);
  lv_obj_set_size(list, sw - 12, sh - STATUSBAR_H - 42);
  lv_obj_set_pos(list, 6, 36);
  lv_obj_set_style_pad_row(list, 6, LV_PART_MAIN);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);

  if (s_lockwall_count == 0) {
    lv_obj_t* empty = lv_label_create(list);
    lv_label_set_text(empty, "No JPEGs found in /lock/.\nAdd images via the file\nmanager or an SD card.");
    lv_obj_set_style_text_color(empty, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(empty, &g_font_12, LV_PART_MAIN);
  }
  for (int i = 0; i < s_lockwall_count; ++i) {
    lv_obj_t* b = lv_btn_create(list);
    lv_obj_set_width(b, lv_pct(100));
    lv_obj_set_height(b, 36);
    styleButton(b);
    lv_obj_add_event_cb(b, lockwallChosenCb, LV_EVENT_CLICKED, s_lockwall_paths[i]);
    lv_obj_t* l = lv_label_create(b);
    char disp[64]; lockwallDisplayName(s_lockwall_paths[i], disp, sizeof disp);
    lv_label_set_text(l, disp);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, lv_pct(94));
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 4, 0);
  }
}
static void openLockWallPickerCb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) openLockWallPicker();
}
static void lockColorChosenCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const uint32_t rgb = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
  touchPrefsSetLockTextColor(rgb);
  if (g_lv.task) g_lv.task->showAlert("Lock text colour set", 1000);
}
#endif  // HAS_TDECK_GT911
// The settings-backup import picker below must link on BOTH touch boards (its
// Import button is not keyboard-gated), so briefly close the enclosing
// HAS_TDECK_KEYBOARD region here and reopen it right after openBackupPicker().
#endif  // HAS_TDECK_KEYBOARD — paused for the board-agnostic backup picker

// ---- Settings backup: import file picker ------------------------------------
// Mirrors the lock-wallpaper picker UX: a full-screen list of *.json files on
// internal flash + (T-Deck) the SD card root. Picking one shows a short confirm
// then streams it through MyMesh::uiImportBackup and reboots so the imported
// radio/identity settings take effect. Lives OUTSIDE the HAS_TDECK_GT911 guard
// above because the Import button exists on both touch boards (the Heltec has
// no SD slot, so it lists internal flash only).
static lv_obj_t* s_backup_picker = nullptr;
static char      s_backup_paths[24][160];   // "int:/foo.json" | "sd:/foo.json"
static char      s_backup_disp [24][48];    // shown label, e.g. "SD: foo.json"
static int       s_backup_count = 0;
static char      s_backup_chosen[160] = {0};

static bool backupIsJson(const char* name) {
  if (!name) return false;
  const char* base = strrchr(name, '/'); base = base ? base + 1 : name;
  // Skip hidden / macOS AppleDouble sidecars (".DS_Store", and the "._<name>"
  // resource-fork file Finder writes next to every file it copies to a FAT SD
  // card) — otherwise "._meshcore-backup.json" shows up as a bogus duplicate.
  if (base[0] == '.') return false;
  const char* d = strrchr(base, '.');
  return d && !strcasecmp(d, ".json");
}
static void backupAddPath(const char* stored, const char* disp) {
  const int cap = (int)(sizeof s_backup_paths / sizeof s_backup_paths[0]);
  if (s_backup_count >= cap) return;
  for (int i = 0; i < s_backup_count; ++i)
    if (!strcmp(s_backup_paths[i], stored)) return;             // dedup
  strncpy(s_backup_paths[s_backup_count], stored, 159); s_backup_paths[s_backup_count][159] = '\0';
  strncpy(s_backup_disp [s_backup_count], disp,   47);  s_backup_disp [s_backup_count][47]  = '\0';
  s_backup_count++;
}
static void backupScan() {
  s_backup_count = 0;
  // Internal SPIFFS is flat — list any *.json at the root.
  SPIFFS.begin(false);
  File root = SPIFFS.open("/");
  if (root) {
    File e = root.openNextFile();
    while (e) {
      const char* full = e.path();
      if (full && backupIsJson(full)) {
        const char* base = strrchr(full, '/'); base = base ? base + 1 : full;
        char stored[160]; snprintf(stored, sizeof stored, "int:%s", full);
        char disp[48];    snprintf(disp,   sizeof disp,   "Internal: %s", base);
        backupAddPath(stored, disp);
      }
      e.close();
      e = root.openNextFile();
    }
    root.close();
  }
#if defined(HAS_TDECK_GT911)
  // SD card root (a real directory on the T-Deck). Actively (re)mount here: the
  // SD is otherwise only probed while the file manager is open, so without this
  // the picker would miss the card until the user had opened the file manager
  // once. fmSdTryMount() no-ops if already mounted, else walks the mount ladder.
  if (fmSdTryMount()) {
    File d = SD.open("/");
    if (d && d.isDirectory()) {
      File e = d.openNextFile();
      while (e) {
        if (!e.isDirectory()) {
          const char* nm = e.name();
          const char* base = strrchr(nm, '/'); base = base ? base + 1 : nm;
          if (backupIsJson(base)) {
            char stored[160]; snprintf(stored, sizeof stored, "sd:/%s", base);
            char disp[48];    snprintf(disp,   sizeof disp,   "SD: %s", base);
            backupAddPath(stored, disp);
          }
        }
        e.close();
        e = d.openNextFile();
      }
    }
    if (d) d.close();
  }
#endif
}
static void backupPickerClose() {
  if (s_backup_picker) { lv_obj_del(s_backup_picker); s_backup_picker = nullptr; }
}
static void backupPickerCloseCb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) backupPickerClose();
}
// Apply the file stashed in s_backup_chosen, then reboot.
static void doBackupImportChosen() {
  if (!g_lv.task || !s_backup_chosen[0]) return;
  fs::FS* fsp = nullptr;
  const char* path = s_backup_chosen;
  if (!strncmp(path, "int:", 4)) { SPIFFS.begin(false); fsp = &SPIFFS; path += 4; }
#if defined(HAS_TDECK_GT911)
  else if (!strncmp(path, "sd:", 3)) { fsp = &SD; path += 3; }
#endif
  if (!fsp) { g_lv.task->showAlert("Import: storage unavailable", 2000); return; }
  // "Importing…" overlay, painted before the blocking parse + apply.
  lv_obj_t* ov = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(ov);
  lv_obj_set_size(ov, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
  lv_obj_set_style_bg_color(ov, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(ov, LV_OPA_70, LV_PART_MAIN);
  lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
  { lv_obj_t* ol = lv_label_create(ov); lv_label_set_text(ol, "Importing settings\xe2\x80\xa6");
    lv_obj_set_style_text_color(ol, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(ol, &g_font_16, LV_PART_MAIN); lv_obj_center(ol); }
  lv_refr_now(nullptr);
  File f = fsp->open(path, FILE_READ);
  // uiImportBackup is a long synchronous burst of SPIFFS writes — identity,
  // channels, and DataStore::saveContacts issues ~12 writes per contact (2000+
  // for a full address book). That keeps the flash cache disabled long enough to
  // starve the idle task and trip the TASK watchdog; the panic surfaces as an
  // 'ipc0' abort inside spi_flash_op_block_func (a flash/cache stall, NOT a
  // partition fault). A successful import reboots immediately, so suspend the
  // per-core idle watchdogs for the duration and only restore them if we bail.
  wdtHeavyBegin();
  int nch = 0, nco = 0;
  bool ok = f && the_mesh.uiImportBackup(f, 0x1F, true, true, &nch, &nco);
  if (f) f.close();
  if (!ok) {
    wdtHeavyEnd();
    lv_obj_del(ov);
    g_lv.task->showAlert("Import failed (bad/unreadable JSON)", 2400);
    return;
  }
  g_lv.task->persistHistoryNow();   // nests under the guard above (ref-counted)
  delay(1200);
  ESP.restart();
}
static void backupChosenCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const char* stored = (const char*)lv_event_get_user_data(e);
  if (!stored) return;
  strncpy(s_backup_chosen, stored, sizeof(s_backup_chosen) - 1);
  s_backup_chosen[sizeof(s_backup_chosen) - 1] = '\0';
  backupPickerClose();
  showConfirm("Import this backup?\nReplaces identity,\nchannels & contacts,\nthen reboots.",
              "Import", doBackupImportChosen);
}
static void openBackupPicker() {
  backupScan();
  backupPickerClose();
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_backup_picker = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_backup_picker);
  lv_obj_set_size(s_backup_picker, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_backup_picker, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_backup_picker, lv_color_hex(COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_backup_picker, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_backup_picker, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(s_backup_picker);
  lv_label_set_text(title, "Import settings");
  lv_obj_set_style_text_font(title, &g_font_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_pos(title, 8, 8);

  lv_obj_t* close = lv_btn_create(s_backup_picker);
  lv_obj_set_size(close, 30, 26);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -6, 4);
  styleButton(close);
  lv_obj_add_event_cb(close, backupPickerCloseCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* cl = lv_label_create(close); lv_label_set_text(cl, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_font(cl, &g_font_12, LV_PART_MAIN); lv_obj_center(cl);

  lv_obj_t* list = lv_obj_create(s_backup_picker);
  lv_obj_remove_style_all(list);
  lv_obj_set_size(list, sw - 12, sh - STATUSBAR_H - 42);
  lv_obj_set_pos(list, 6, 36);
  lv_obj_set_style_pad_row(list, 6, LV_PART_MAIN);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);

  if (s_backup_count == 0) {
    lv_obj_t* empty = lv_label_create(list);
    lv_label_set_text(empty, "No .json backups found.\nExport one first, or copy a\nmeshcore-backup.json to the\nSD card or internal flash.");
    lv_obj_set_style_text_color(empty, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(empty, &g_font_12, LV_PART_MAIN);
  }
  for (int i = 0; i < s_backup_count; ++i) {
    lv_obj_t* b = lv_btn_create(list);
    lv_obj_set_width(b, lv_pct(100));
    lv_obj_set_height(b, 36);
    styleButton(b);
    lv_obj_add_event_cb(b, backupChosenCb, LV_EVENT_CLICKED, s_backup_paths[i]);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, s_backup_disp[i]);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, lv_pct(94));
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 4, 0);
  }
}

// Reopen the HAS_TDECK_KEYBOARD region paused above for the backup picker; it
// closes at that region's original #endif further below. (The next #if is the
// original spacebar-countdown guard, now nested one level deeper — harmless.)
#if defined(HAS_TDECK_KEYBOARD)

#if defined(HAS_TDECK_KEYBOARD)
// ---- Spacebar lock countdown -------------------------------------------------
// The spacebar locks the screen, but to avoid accidental locks it first runs a
// 1 s "Locking…" countdown (tap anywhere, or press any other key, to cancel).
// The T-Deck keyboard reports one byte per press with no reliable key-up, so a
// literal "key held for 3 s" can't be measured — this is a press-to-start,
// cancellable 3 s countdown that serves the same purpose.
static lv_obj_t*     s_locking_popup    = nullptr;
static lv_obj_t*     s_locking_count    = nullptr;
static unsigned long s_locking_deadline = 0;

static void cancelLockingCountdown() {
  s_locking_deadline = 0;
  if (s_locking_popup) { lv_obj_del_async(s_locking_popup); s_locking_popup = nullptr; s_locking_count = nullptr; }
}
static void lockingCountdownTapCb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) cancelLockingCountdown();
}
static void startLockingCountdown() {
  if (s_locking_deadline) return;                       // already counting
  s_locking_deadline = millis() + 1000;
  s_locking_popup = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_locking_popup);
  lv_obj_set_size(s_locking_popup, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr) - STATUSBAR_H);
  lv_obj_set_pos(s_locking_popup, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_locking_popup, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_locking_popup, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_locking_popup, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_locking_popup, lockingCountdownTapCb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* card = lv_obj_create(s_locking_popup);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, 180, 104);
  lv_obj_center(card);
  lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* t = lv_label_create(card);
  lv_label_set_text(t, "Locking\xE2\x80\xA6");          // Locking…
  lv_obj_set_style_text_color(t, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(t, &g_font_16, LV_PART_MAIN);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 8);

  s_locking_count = lv_label_create(card);
  lv_label_set_text(s_locking_count, "1");
  lv_obj_set_style_text_color(s_locking_count, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_locking_count, &g_font_16, LV_PART_MAIN);
  lv_obj_align(s_locking_count, LV_ALIGN_CENTER, 0, 6);

  lv_obj_t* hint = lv_label_create(card);
  lv_label_set_text(hint, "tap to cancel");
  lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(hint, &g_font_12, LV_PART_MAIN);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
}
// Advance / fire the countdown; called every loop tick from UITask::loop.
static void serviceLockingCountdown(unsigned long now) {
  if (!s_locking_deadline) return;
  if ((long)(now - s_locking_deadline) >= 0) {
    cancelLockingCountdown();
    if (g_lv.task) g_lv.task->lockScreen();
    return;
  }
  if (s_locking_count) {
    const unsigned long remain = s_locking_deadline - now;
    char b[4]; snprintf(b, sizeof b, "%u", (unsigned)((remain + 999) / 1000));   // 3,2,1
    lv_label_set_text(s_locking_count, b);
  }
}
#endif

// Route a physical-keyboard character into the textarea currently being edited
// — the on-screen keyboard's target, which is the chat composer directly or the
// settings mirror textarea. No field open -> the key is ignored.
static void handleHwKey(int key) {
  if (!g_lv.keyboard) return;
  // Hard-locked: any key just reveals the lock screen (lights the panel). It
  // never types or switches tabs — only a trackball *hold* unlocks.
  if (g_lv.task && g_lv.task->isManualLock()) { g_lv.task->lockscreenReveal(); return; }
  // Idle-dimmed (not hard-locked): ignore keys; a touch/click wakes into the UI.
  if (g_lv.task && g_lv.task->isScreenOff()) return;
#if defined(HAS_TDECK_KEYBOARD)
  // Any key other than the lock key (space) aborts a pending lock countdown.
  if (s_locking_deadline && key != ' ') cancelLockingCountdown();
#endif
  // The on-screen keyboard is never shown on the T-Deck, but a textarea is still
  // bound to it on focus — that binding is our target.
  lv_obj_t* ta = lv_keyboard_get_textarea(g_lv.keyboard);
  if (!ta) {
    // First-boot setup owns the screen: with no field focused, swallow the key
    // so it can't switch the (hidden) tabs behind the wizard or arm the lock.
    if (s_setup_root) return;
#if defined(HAS_TDECK_KEYBOARD)
    // Spacebar (while NOT editing a text field) locks the screen: backlight off
    // + manual lock, so touch and trackball-scroll are ignored — only a
    // trackball CLICK unlocks it. The T-Deck's only side button is a hardware
    // reset (unremappable), so the spacebar is the keyboard lock key. NB this
    // overrides the old "space = Home tab" shortcut — use 'h' for Home.
    if (key == ' ') {
      startLockingCountdown();   // 1 s "Locking…" countdown; tap / any other key cancels
      return;
    }
#endif
    // Not editing a field. If a popup is up, the dismiss keys close it; on a
    // bare tab, the navigation keys jump between the bottom tabs.
    if (anyPopupOpen()) {
      if (isDismissKey(key)) {
        hwKeyDismissTopPopup();
        if (g_lv.task) g_lv.task->noteUserInput();
      }
    } else {
      const int tab = tabForKey(key);
      if (tab >= 0 && g_lv.tabview) {
        lv_tabview_set_act(g_lv.tabview, (uint32_t)tab, LV_ANIM_OFF);
        if (g_lv.task) g_lv.task->noteUserInput();
      }
    }
    return;
  }
  if (key == 0x08 || key == 0x7F) {            // backspace / delete
    lv_textarea_del_char(ta);
  } else if (key == ' ') {
    // Double-tap SPACE within 250 ms toggles between English and the
    // configured secondary keyboard. If no secondary is set, it behaves
    // as a normal space.
    static unsigned long s_last_space_ms = 0;
    unsigned long now = millis();
    bool toggle = (now - s_last_space_ms) < 250;
    s_last_space_ms = now;
    if (toggle && keyboardLayoutsAnySecondary()) {
      // Remove the just-inserted first space before cycling.
      lv_textarea_del_char(ta);
      KeyboardLayoutId next = keyboardLayoutsCycle(g_lv.keyboard);
#if defined(ESP32)
      touchPrefsSetKeyboardLayout(static_cast<uint8_t>(next));
#endif
      if (g_lv.task) {
        g_lv.task->showAlert(keyboardLayoutName(next), 800);
      }
    } else {
      lv_textarea_add_char(ta, ' ');
    }
  } else if (key == 0x0D) {                   // regular ENTER (CR)
#if defined(HAS_TDECK_GT911)
    if (s_editor_ta && ta == s_editor_ta) {
      lv_textarea_add_char(ta, '\n');   // multiline editor: Enter inserts a newline
    } else if (s_term_input_ta && s_kb_bind_ta == s_term_input_ta) {
      terminalSubmit();   // terminal: run the command, keep the field focused
    } else
#endif
    if (s_kb_panel) {
      // Chat composer: send and keep the composer focused for the next message.
      LvChatPanel* p = s_kb_panel;
      if (g_lv.task && p->composer_ta) {
        const char* text = lv_textarea_get_text(p->composer_ta);
        if (text && text[0]) {
          g_lv.task->setComposerMode(true);
          g_lv.task->composerReset();
          for (const char* cp = text; *cp; ++cp) g_lv.task->composerAppendChar(*cp);
          if (g_lv.task->composerSend()) {
            lv_textarea_set_text(p->composer_ta, "");
            refreshChatDetail(*p);
            g_lv.dirty_threads = true;
          }
          lv_keyboard_set_textarea(g_lv.keyboard, p->composer_ta);  // re-bind
        }
      }
    } else {
      hideKb();   // settings/other field: confirm (syncs into the real field) + unfocus
    }
  } else if (key >= 0x20 && key < 0x7F) {      // printable ASCII
    bool shifted = (key >= 'A' && key <= 'Z');
    const char* mapped = keyboardLayoutMapHwKey(keyboardLayoutsGetCurrent(), key, shifted);
    if (mapped) {
      lv_textarea_add_text(ta, mapped);
    } else {
      lv_textarea_add_char(ta, (uint32_t)key);
    }
  }
  if (g_lv.task) g_lv.task->noteUserInput();
}
#endif

static void refreshLiveDiag(unsigned long now) {
  if (!s_live_diag_label) return;
  if (now < s_live_diag_next_ms) return;
  s_live_diag_next_ms = now + 250;
  uint16_t _rawx = 0, _rawy = 0;
  heltecV4CapTouchGetRaw(&_rawx, &_rawy);
  char line[256];
  snprintf(line, sizeof(line), "L%lu R%lu P%lu T%lu %s\nraw %u,%u\n%s",
           static_cast<unsigned long>(s_live_diag_loops),
           static_cast<unsigned long>(s_live_diag_reads),
           static_cast<unsigned long>(s_live_diag_pressed),
           static_cast<unsigned long>(s_live_diag_tap_edges),
           g_lv.touch_inited ? "touch:ok" : "touch:wait",
           static_cast<unsigned>(_rawx), static_cast<unsigned>(_rawy),
           heltecV4CapTouchDebug());
  // Dev-only overlay (k_show_live_diag_overlay): the label is created hidden in
  // normal builds, so this just keeps it current for when a developer flips the
  // flag on for touch bring-up. No effect on the shipped UI.
  lv_label_set_text(s_live_diag_label, line);
}

/** Brief banner on `lv_layer_top()` so feedback is visible over settings modals. */
static lv_obj_t*   s_alert_toast       = nullptr;
static lv_timer_t* s_alert_toast_timer = nullptr;

static void alertToastTimerCb(lv_timer_t* t) {
  s_alert_toast_timer = nullptr;
  if (s_alert_toast) lv_obj_add_flag(s_alert_toast, LV_OBJ_FLAG_HIDDEN);
  if (t) lv_timer_del(t);
}

static void showAlertToastLvgl(const char* text, uint32_t duration_ms) {
  if (!text || !text[0]) return;
  if (!lv_disp_get_default() || !g_lv.ready) return;
  if (duration_ms < 400u) duration_ms = 400u;
  if (duration_ms > 8000u) duration_ms = 8000u;

  if (s_alert_toast_timer) {
    lv_timer_del(s_alert_toast_timer);
    s_alert_toast_timer = nullptr;
  }

  if (!s_alert_toast) {
    s_alert_toast = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_alert_toast);
    lv_obj_set_width(s_alert_toast, 224);
    lv_obj_set_style_bg_opa(s_alert_toast, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_alert_toast, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_radius(s_alert_toast, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_alert_toast, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_alert_toast, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_pad_hor(s_alert_toast, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(s_alert_toast, 10, LV_PART_MAIN);
    lv_obj_clear_flag(s_alert_toast, LV_OBJ_FLAG_SCROLLABLE);
    // The toast is informational, not interactive — make it non-clickable
    // so taps pass through to whatever's behind (most importantly the map
    // canvas's pan handler). lv_obj_create defaults to CLICKABLE on, which
    // meant any tap inside the toast's 224×~44 strip was being swallowed
    // for the full 0.9-1.5 s display duration — UI felt "frozen" until
    // auto-hide.
    lv_obj_clear_flag(s_alert_toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_alert_toast, LV_OBJ_FLAG_FLOATING);

    lv_obj_t* l = lv_label_create(s_alert_toast);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, 200);
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, &g_font_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
  }

  char san_toast[96];
  copyUtf8ReplacingMissingGlyphs(&g_font_14, san_toast, sizeof(san_toast), text);
  lv_obj_t* lbl = lv_obj_get_child(s_alert_toast, 0);
  if (lbl) lv_label_set_text(lbl, san_toast);
  lv_obj_update_layout(s_alert_toast);
  {
    lv_obj_t* lbl2 = lv_obj_get_child(s_alert_toast, 0);
    lv_coord_t inner = lbl2 ? (lv_obj_get_height(lbl2) + 20) : 44;
    if (inner < 44) inner = 44;
    if (inner > 96) inner = 96;
    lv_obj_set_height(s_alert_toast, inner);
  }
  // Drop below the global status bar so the toast doesn't clip into the
  // time/battery row. 6 px breathing room below the bar's bottom border.
  lv_obj_align(s_alert_toast, LV_ALIGN_TOP_MID, 0, STATUSBAR_H + 6);
  lv_obj_clear_flag(s_alert_toast, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_alert_toast);

  s_alert_toast_timer = lv_timer_create(alertToastTimerCb, duration_ms, nullptr);
  lv_timer_set_repeat_count(s_alert_toast_timer, 1);
}

#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
static const char* wifiStaStatusBrief(int s) {
  switch (s) {
    case WL_CONNECTED: return "connected";
    case WL_IDLE_STATUS: return "idle";
    case WL_NO_SHIELD:
      /* Often stale while STA is coming up after BLE; firmware re-issues begin in main. */
      if (WiFi.getMode() == WIFI_STA) return "joining…";
      return "off";
    case WL_NO_SSID_AVAIL: return "AP not found";
    case WL_SCAN_COMPLETED: return "scan done";
    case WL_CONNECT_FAILED: return "auth failed";
    case WL_CONNECTION_LOST: return "link lost";
    case WL_DISCONNECTED: return "disconnected";
    default: return "connecting…";
  }
}
#endif

static void refreshSettingsSectionSubtitles() {
  if (!g_lv.task) return;
  NodePrefs* prefs = the_mesh.getNodePrefs();

  if (g_set_sec_sub[SEC_PROFILE]) {
    const char* nm = g_lv.task->getNodeNameCstr();
    if (!nm) nm = "";
    char nm_vis[48];
    copyUtf8ReplacingMissingGlyphs(&g_font_12, nm_vis, sizeof(nm_vis), nm);
    char sub[72];
    if (prefs) {
      snprintf(sub, sizeof(sub), "%.22s · share %s · path m%u", nm_vis,
               prefs->advert_loc_policy ? "on" : "off",
               static_cast<unsigned>(prefs->path_hash_mode > 2 ? 2 : prefs->path_hash_mode));
    } else {
      snprintf(sub, sizeof(sub), "%.40s", nm_vis);
    }
    lv_label_set_text(g_set_sec_sub[SEC_PROFILE], sub);
  }

  if (g_set_sec_sub[SEC_RADIO] && prefs) {
    lv_label_set_text_fmt(g_set_sec_sub[SEC_RADIO], "%.3f MHz · SF%u · TX %+ddBm",
                          static_cast<double>(prefs->freq), static_cast<unsigned>(prefs->sf),
                          static_cast<int>(prefs->tx_power_dbm));
  } else if (g_set_sec_sub[SEC_RADIO]) {
    lv_label_set_text(g_set_sec_sub[SEC_RADIO], "—");
  }

  if (g_set_sec_sub[SEC_AUTOADD] && prefs) {
    unsigned bits = 0;
    if (prefs->autoadd_config & AUTO_ADD_CHAT) ++bits;
    if (prefs->autoadd_config & AUTO_ADD_REPEATER) ++bits;
    if (prefs->autoadd_config & AUTO_ADD_ROOM_SERVER) ++bits;
    if (prefs->autoadd_config & AUTO_ADD_SENSOR) ++bits;
    lv_label_set_text_fmt(g_set_sec_sub[SEC_AUTOADD], "%u types · hops %u · manual %s",
                          bits, static_cast<unsigned>(prefs->autoadd_max_hops),
                          prefs->manual_add_contacts ? "on" : "off");
  } else if (g_set_sec_sub[SEC_AUTOADD]) {
    lv_label_set_text(g_set_sec_sub[SEC_AUTOADD], "—");
  }

  if (g_set_sec_sub[SEC_BLUETOOTH]) {
    // BLE is active only when the radio was initialised at boot (mutex with
    // Wi-Fi). Otherwise the device is in Wi-Fi mode and BLE is inactive.
    if (g_lv.task->hasBleCapability() && g_lv.task->isBleEnabled()) {
      lv_label_set_text(g_set_sec_sub[SEC_BLUETOOTH], "Active");
    } else {
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
      lv_label_set_text(g_set_sec_sub[SEC_BLUETOOTH],
                        wifiConfigGetRadioEnabled() ? "Inactive (Wi-Fi mode)" : "Inactive");
#else
      lv_label_set_text(g_set_sec_sub[SEC_BLUETOOTH], "Inactive");
#endif
    }
  }

  // Wi-Fi row subtitle: brief connection state (handled on its own dedicated page).
  if (g_set_sec_sub[SEC_WIFI]) {
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
    char ssid[WIFI_CONFIG_SSID_MAX];
    wifiConfigGetSsid(ssid, sizeof(ssid));
    const int re = wifiConfigGetRadioEnabled() ? 1 : 0;
    if (!re) {
      lv_label_set_text(g_set_sec_sub[SEC_WIFI], "Radio off");
    } else if (WiFi.status() == WL_CONNECTED) {
      IPAddress ip = WiFi.localIP();
      lv_label_set_text_fmt(g_set_sec_sub[SEC_WIFI], "%s · %d.%d.%d.%d",
                            ssid[0] ? ssid : "(none)", ip[0], ip[1], ip[2], ip[3]);
    } else {
      lv_label_set_text_fmt(g_set_sec_sub[SEC_WIFI], "%s · %s",
                            ssid[0] ? ssid : "(none)",
                            wifiStaStatusBrief(static_cast<int>(WiFi.status())));
    }
#else
    lv_label_set_text(g_set_sec_sub[SEC_WIFI], "n/a in this build");
#endif
  }

#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  // wifi_sta_status_l is the dedicated Wi-Fi page's status line — created ONLY by
  // buildWifiSettings, for the old modal AND the inline Net sub-tab. The old gate
  // also required g_set_modal.root + kind==Wifi (both modal-only), so the inline
  // sub-tab was stuck on "Loading…". resetSettingsModalState() zeroes the struct on
  // every sub-tab switch, so a non-null pointer reliably means the page is live.
  if (g_set_modal.wifi_sta_status_l) {
    const int re2 = wifiConfigGetRadioEnabled() ? 1 : 0;
    if (!re2) {
      lv_label_set_text(g_set_modal.wifi_sta_status_l, "Radio: off");
    } else {
      char line[160];
      const int st = static_cast<int>(WiFi.status());
      char ssid_buf[WIFI_CONFIG_SSID_MAX];
      wifiConfigGetSsid(ssid_buf, sizeof(ssid_buf));
      if (st == WL_CONNECTED) {
        IPAddress ip = WiFi.localIP();
        const int rssi = WiFi.RSSI();
        // 2 lines: ssid on top, then IP + RSSI + WS on one compact line (the IP
        // used to be on its own line, pushing the block to 3 lines → overlap).
        snprintf(line, sizeof(line),
                 "Connected: %s\nIP %u.%u.%u.%u  ·  %d dBm  ·  WS %d",
                 ssid_buf[0] ? ssid_buf : "(unnamed)",
                 (unsigned)ip[0], (unsigned)ip[1], (unsigned)ip[2], (unsigned)ip[3],
                 rssi, g_lv.task->getWsConnectedCount());
      } else {
        snprintf(line, sizeof(line), "%s%s\n%s",
                 ssid_buf[0] ? "Target: " : "",
                 ssid_buf[0] ? ssid_buf : "(no SSID set)",
                 wifiStaStatusBrief(st));
      }
      lv_label_set_text(g_set_modal.wifi_sta_status_l, line);
    }
  }
#endif

  if (g_set_sec_sub[SEC_DEVICE]) {
    lv_label_set_text_fmt(g_set_sec_sub[SEC_DEVICE], "GPS %s · Buzzer %s",
                          onOff(g_lv.task->getGPSState()),
                          g_lv.task->isBuzzerQuiet() ? "quiet" : "on");
  }

  // Live GPS fix status: refresh the Device-settings line and the control-center
  // line while either is open, so a fix appearing/dropping shows without reopening.
  if (g_set_modal.root && g_set_modal.gps_status &&
      g_set_modal.kind == SettingsModalKind::Device) {
    lv_label_set_text(g_set_modal.gps_status, gpsStatusStr());
  }
  if (s_cc_root && s_cc_gps_label) {
    lv_label_set_text(s_cc_gps_label, gpsStatusStr());
  }

  if (g_set_sec_sub[SEC_EXPERIMENTAL] && prefs) {
    lv_label_set_text_fmt(g_set_sec_sub[SEC_EXPERIMENTAL],
                          "Multi-ack %s · Repeat %s · RX boost %s",
                          onOff(prefs->multi_acks != 0), onOff(prefs->client_repeat != 0),
                          onOff(prefs->rx_boosted_gain != 0));
  } else if (g_set_sec_sub[SEC_EXPERIMENTAL]) {
    lv_label_set_text(g_set_sec_sub[SEC_EXPERIMENTAL], "—");
  }

  if (g_set_sec_sub[SEC_LOG]) {
    lv_label_set_text_fmt(g_set_sec_sub[SEC_LOG], "RX %d entries · Raw %d entries",
                          s_rxlog_line < RXLOG_LINES ? s_rxlog_line : RXLOG_LINES,
                          s_rawlog_line < RXLOG_LINES ? s_rawlog_line : RXLOG_LINES);
  }
}

// ============================================================
// Status-bar control center (tap the top bar)
// A small drop-down panel with date/time + battery/Wi-Fi info and quick
// Wi-Fi / Bluetooth toggles, iPhone-control-center style. Bluetooth toggles
// instantly; Wi-Fi is reboot-based on this radio (mutually exclusive with BLE)
// so it confirms first.
// ============================================================
// Which transport was initialized at boot. Only the boot transport can be
// toggled live; bringing up the OTHER one at runtime would OOM (Bluedroid and
// esp_wifi can't both hold the internal heap), so that path must reboot.
// Captured once in UITask::begin. true = Wi-Fi was the boot transport.
static bool      s_boot_wifi_transport = false;

static void closeControlCenter() {
  if (s_cc_root) { lv_obj_del_async(s_cc_root); s_cc_root = nullptr; }
  s_cc_gps_label = nullptr;
}
static void ccBackdropCb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) closeControlCenter();
}
static void openControlCenter();   // fwd — toggle cbs rebuild the panel

// Wi-Fi and Bluetooth are mutually exclusive on this radio (Bluedroid and
// esp_wifi can't both hold the internal heap), so SWITCHING from one to the
// other reboots to re-init the chosen transport cleanly. Turning the active
// transport OFF, or turning one ON when neither is active, is a clean runtime
// operation with no reboot.
#if defined(ESP32)
static void ccWifiOnApply() {                 // reboot into Wi-Fi (from BT)
  wifiConfigSetRadioEnabled(true);            // boot picks Wi-Fi as transport
  if (g_lv.task) g_lv.task->rebootDevice();   // saves chat history first
}
static void ccBleOnApply() {                  // reboot into Bluetooth (from Wi-Fi)
  wifiConfigSetRadioEnabled(false);           // boot then picks Bluetooth
  if (g_lv.task) g_lv.task->rebootDevice();
}
#endif
static void ccWifiCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
#if defined(ESP32)
  if (s_boot_wifi_transport) {
    // Wi-Fi is the transport inited at boot → toggle its radio live (off/on)
    // with no reboot (esp_wifi stays inited; WIFI_OFF / WiFi.begin in the loop).
    const bool on = wifiConfigGetRadioEnabled();
    wifiConfigSetRadioEnabled(!on);
    if (g_lv.task) g_lv.task->showAlert(on ? "Wi-Fi off" : "Wi-Fi on", 800);
    openControlCenter();
  } else {
    // Booted into Bluetooth: esp_wifi was never inited and Bluedroid holds the
    // heap, so bringing Wi-Fi up at runtime would OOM-freeze. Reboot to switch.
    closeControlCenter();
    showConfirm("Switch to Wi-Fi?\nBluetooth will turn off (reboot).",
                "Reboot", ccWifiOnApply);
  }
#endif
}
static void ccBleCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task ||
      !g_lv.task->hasBleCapability())
    return;
#if defined(ESP32)
  if (s_boot_wifi_transport) {
    // Booted into Wi-Fi: bringing Bluetooth up at runtime would fight esp_wifi
    // for the heap. Reboot to switch transports.
    closeControlCenter();
    showConfirm("Switch to Bluetooth?\nWi-Fi will turn off (reboot).",
                "Reboot", ccBleOnApply);
    return;
  }
#endif
  // Bluetooth is the boot transport → toggle it live.
  const bool on = g_lv.task->isBleEnabled();
  on ? g_lv.task->disableBle() : g_lv.task->enableBle();
  g_lv.task->showAlert(on ? "Bluetooth off" : "Bluetooth on", 800);
  openControlCenter();
}
static void ccGpsCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  g_lv.task->toggleGPS();
  g_lv.task->showAlert(g_lv.task->getGPSState() ? "GPS on" : "GPS off", 800);
  openControlCenter();   // rebuild so the toggle reflects the new state
}

#if defined(HAS_TDECK_KEYBOARD)
static void ccKbBacklightCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  s_kb_bl_mode = (uint8_t)((s_kb_bl_mode + 1) % 3);   // off -> on -> auto -> off
  touchPrefsSetKbBacklight(s_kb_bl_mode);
  openControlCenter();   // rebuild so the chip label reflects the new mode
}
#endif

#if defined(HAS_TDECK_GT911)
// Lock the screen from the control center (T-Deck only; the V4 has its physical
// lock button). Dismiss the panel first, then turn the screen off + manual-lock.
// Unlock with a trackball/BOOT press.
static void ccLockCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  closeControlCenter();
  g_lv.task->lockScreen();
}
// Sound on/off chip (T-Deck speaker). Plays a confirmation chime when enabling.
static void ccSoundCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  g_lv.task->toggleBuzzer();
  const bool quiet = g_lv.task->isBuzzerQuiet();
  if (!quiet) tdeckPlayNotify();
  openControlCenter();   // rebuild so the chip's active state updates
}
#endif

// ---- Power menu (power off / reboot) ----
// The T-Deck has no power-management IC to cut rail power, so "power off" is a
// deep sleep: the lowest-power state the chip can hold. It wakes on the
// trackball centre button (GPIO0). Chat history is flushed first.
static void closePowerMenu() {
  if (s_power_menu) { lv_obj_del_async(s_power_menu); s_power_menu = nullptr; }
}
static void powerMenuBackdropCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;   // backdrop only
  closePowerMenu();
}
static void powerCancelCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closePowerMenu();
}
static void powerRebootCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !g_lv.task) return;
  closePowerMenu();
  g_lv.task->showAlert("Rebooting\xE2\x80\xA6", 600);
  g_lv.task->rebootDevice();   // flushes chat history, then reboots
}
static void powerOffCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closePowerMenu();
#if defined(ESP32)
  if (g_lv.task) {
    g_lv.task->persistHistoryNow();        // flush chat before we go down
    g_lv.task->showAlert("Powering off\xE2\x80\xA6 click trackball to wake", 1500);
  }
  // Let the toast paint, then enter deep sleep.
  lv_refr_now(NULL);
  delay(900);
#if defined(PIN_USER_BTN)
  const gpio_num_t wake = (gpio_num_t)PIN_USER_BTN;   // GPIO0, trackball click, active-low
  // CRITICAL: the trackball button is held HIGH by a pull-up while idle and
  // pulled LOW only when pressed. Across deep sleep the normal GPIO pull is
  // powered down, so without holding the pull-up via the RTC domain the pad
  // floats LOW the instant we sleep — ext0 wake-on-LOW then fires immediately
  // and the device "powers off then reboots". Hold the pull-up in RTC so the
  // pad stays HIGH until a real press, and clear any stale wake config first.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  rtc_gpio_init(wake);
  rtc_gpio_set_direction(wake, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en(wake);
  rtc_gpio_pulldown_dis(wake);
  esp_sleep_enable_ext0_wakeup(wake, 0);   // wake when the button is pressed (LOW)
#endif
  esp_deep_sleep_start();   // never returns; a press wakes via full reboot
#endif
}

// Open the power menu: a small centred card with Power off / Reboot / Cancel.
static void openPowerMenu() {
  closeControlCenter();
  closePowerMenu();
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_power_menu = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_power_menu);
  lv_obj_set_size(s_power_menu, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_power_menu, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_power_menu, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_power_menu, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_power_menu, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_power_menu, powerMenuBackdropCb, LV_EVENT_CLICKED, nullptr);

  const int card_w = (sw - 40 > 240) ? 240 : (sw - 40);
  lv_obj_t* card = lv_obj_create(s_power_menu);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, 188);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, -10);
  styleSurface(card, COLOR_PANEL, 10);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 12, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, LV_SYMBOL_POWER "  Power");
  lv_obj_set_style_text_font(title, &g_font_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

  auto mk = [&](const char* txt, lv_event_cb_t cb, uint32_t bg, int y) {
    lv_obj_t* b = lv_btn_create(card);
    lv_obj_set_size(b, card_w - 24, 40);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, y);
    styleButton(b);
    if (bg) {
      lv_obj_set_style_bg_color(b, lv_color_hex(bg), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    }
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &g_font_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_center(l);
    return b;
  };
  mk(LV_SYMBOL_POWER "  Power off", powerOffCb,    0xC44B55, 30);
  mk(LV_SYMBOL_REFRESH "  Reboot",  powerRebootCb, 0,        78);
  mk("Cancel",                      powerCancelCb, 0,        126);
}

static void ccPowerCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  openPowerMenu();
}

static void ccToggle(lv_obj_t* parent, const char* sym, const char* label,
                     bool active, lv_event_cb_t cb, int width = 66, int height = 54) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_size(b, width, height);
  styleButton(b);
  lv_obj_set_style_radius(b, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_all(b, 0, LV_PART_MAIN);   // reclaim the chip's inner area for the icon+label stack
  lv_obj_set_style_bg_color(b, lv_color_hex(active ? COLOR_STATUS_OK : COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(b, active ? LV_OPA_COVER : LV_OPA_20, LV_PART_MAIN);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  // Icon over label, both pinned with a fixed gap so they never collide even on
  // the shorter (44 px) T-Deck chips. Icon ~4 px from the top, label ~3 px from
  // the bottom.
  lv_obj_t* ic = lv_label_create(b);
  lv_label_set_text(ic, sym);
  lv_obj_set_style_text_font(ic, &g_font_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(ic, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 4);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, label);
  lv_obj_set_style_text_font(l, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(l, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -3);
}

// ---- Display backlight brightness (PWM on PIN_TFT_LEDA_CTL, active-high) ----
// Both touch boards expose PIN_TFT_LEDA_CTL (T-Deck + Heltec V4 TFT), so the
// brightness slider is available on both.
#if defined(PIN_TFT_LEDA_CTL) && (PIN_TFT_LEDA_CTL >= 0)
#define HAS_BACKLIGHT_PWM 1
static uint8_t s_brightness_pct = 100;
static bool    s_bl_pwm_ready   = false;
constexpr int  kBlPwmChannel    = 6;

static void applyBrightness(uint8_t pct) {
  if (pct < 5)   pct = 5;
  if (pct > 100) pct = 100;
  s_brightness_pct = pct;
  if (!s_bl_pwm_ready) {
    ledcSetup(kBlPwmChannel, 20000, 8);              // 20 kHz, 8-bit
    ledcAttachPin(PIN_TFT_LEDA_CTL, kBlPwmChannel);  // takes the pin over from the display's digitalWrite
    s_bl_pwm_ready = true;
  }
  ledcWrite(kBlPwmChannel, (uint32_t)pct * 255u / 100u);
}

static void ccBrightnessCb(lv_event_t* e) {
  applyBrightness((uint8_t)lv_slider_get_value(lv_event_get_target(e)));   // live
}
static void ccBrightnessReleaseCb(lv_event_t* e) {
  touchPrefsSetBrightness((uint8_t)lv_slider_get_value(lv_event_get_target(e)));  // persist
}
#endif

static void openControlCenter() {
  closeControlCenter();
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_cc_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_cc_root);
  lv_obj_set_size(s_cc_root, sw, sh - STATUSBAR_H);
  lv_obj_set_pos(s_cc_root, 0, STATUSBAR_H);
  lv_obj_set_style_bg_color(s_cc_root, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_cc_root, LV_OPA_50, LV_PART_MAIN);
  lv_obj_clear_flag(s_cc_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_foreground(s_cc_root);
  lv_obj_add_event_cb(s_cc_root, ccBackdropCb, LV_EVENT_CLICKED, nullptr);

  // Portrait (V4 TFT, 240 wide) vs landscape (T-Deck, 320 wide): the narrow
  // screen can't fit the battery/IP beside the clock, so portrait stacks the
  // header vertically and uses a taller card.
  const bool portrait = (sw < sh);
  const int card_w = (sw - 12 > 300) ? 300 : (sw - 12);
  lv_obj_t* card = lv_obj_create(s_cc_root);
  lv_obj_remove_style_all(card);
#if defined(HAS_TDECK_GT911)
  lv_obj_set_size(card, card_w, 200);   // sysinfo + thin brightness slider + 2-row toggle grid (fits 240−22 screen)
#else
  // Portrait has headroom on the 320-tall screen; make the card taller so the
  // brightness slider + toggles + sysinfo all get their own rows.
  lv_obj_set_size(card, card_w, portrait ? 236 : 150);
#endif
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 4);
  lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x18191A), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  // ---- Clock + date (left) ----
  char clock_s[8] = "--:--", date_s[28] = "RTC unset";
#if defined(ESP32)
  time_t now_t = time(nullptr);
  if (now_t > 1700000000) {
    struct tm tm_loc; localtime_r(&now_t, &tm_loc);
    strftime(clock_s, sizeof clock_s, "%H:%M", &tm_loc);
    strftime(date_s, sizeof date_s, "%a %d %b %Y", &tm_loc);
  }
#endif
  lv_obj_t* clk = lv_label_create(card);
  lv_label_set_text(clk, clock_s);
  lv_obj_set_style_text_font(clk, &g_font_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(clk, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_align(clk, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_t* dt = lv_label_create(card);
  lv_label_set_text(dt, date_s);
  lv_obj_set_style_text_font(dt, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(dt, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_align(dt, LV_ALIGN_TOP_LEFT, 0, 21);

  // ---- Battery + Wi-Fi info (right) ----
  const uint16_t mv = batteryMvSmoothed();
  const int pct = batteryPercentFromMv(mv);
  char batt_s[28];
  if (batteryIsCharging(mv))
    snprintf(batt_s, sizeof batt_s, LV_SYMBOL_CHARGE " Charging  %u.%02uV",
             (unsigned)(mv / 1000), (unsigned)((mv % 1000) / 10));
  else
    snprintf(batt_s, sizeof batt_s, "%d%%  %u.%02uV",
             pct < 0 ? 0 : pct, (unsigned)(mv / 1000), (unsigned)((mv % 1000) / 10));
  char winfo[40] = "Wi-Fi off";
#if defined(ESP32)
  if (WiFi.status() == WL_CONNECTED)
    snprintf(winfo, sizeof winfo, "%s", WiFi.localIP().toString().c_str());
#endif
  // Power icon — just the white glyph in the top-right corner (no button chrome,
  // iPhone-style). Opens the power off / reboot menu. Battery nudged left (-30)
  // so it clears the glyph.
  {
    lv_obj_t* pb = lv_btn_create(card);
    lv_obj_remove_style_all(pb);                     // strip bg + border — icon only
    lv_obj_set_size(pb, 28, 24);
    lv_obj_align(pb, LV_ALIGN_TOP_RIGHT, 2, -1);
    lv_obj_add_flag(pb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pb, ccPowerCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* pl = lv_label_create(pb);
    lv_label_set_text(pl, LV_SYMBOL_POWER);
    lv_obj_set_style_text_font(pl, &g_font_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(pl, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_center(pl);
  }

  // Battery: landscape puts it top-right beside the clock; portrait stacks it on
  // its own line under the date (no room beside the clock at 240 px wide).
  lv_obj_t* bi = lv_label_create(card);
  lv_label_set_text(bi, batt_s);
  lv_obj_set_style_text_font(bi, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(bi, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  int gps_y = 42;
  if (portrait) {
    lv_obj_align(bi, LV_ALIGN_TOP_LEFT, 0, 40);   // under the date
    gps_y = 58;
  } else {
    lv_obj_align(bi, LV_ALIGN_TOP_RIGHT, -30, 2);
  }

  // ---- GPS fix status (live; refreshed while the panel is open) ----
  s_cc_gps_label = lv_label_create(card);
  lv_label_set_long_mode(s_cc_gps_label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_cc_gps_label, card_w - 20);
  lv_label_set_text(s_cc_gps_label, gpsStatusStr());
  lv_obj_set_style_text_font(s_cc_gps_label, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_cc_gps_label, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_align(s_cc_gps_label, LV_ALIGN_TOP_LEFT, 0, gps_y);

#if defined(HAS_BACKLIGHT_PWM)
  // ---- Brightness slider (thin; a sun/gear glyph anchors it on the left so it
  //      doesn't cost a separate label row). On the T-Deck it sits just under
  //      the header; in portrait (V4) it sits below the GPS line.
  const int bl_y = portrait ? (gps_y + 18) : 64;
  lv_obj_t* bl_ic = lv_label_create(card);
  lv_label_set_text(bl_ic, LV_SYMBOL_SETTINGS);   // small left anchor for the slider
  lv_obj_set_style_text_font(bl_ic, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(bl_ic, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_align(bl_ic, LV_ALIGN_TOP_LEFT, 0, bl_y - 2);
  lv_obj_t* bl = lv_slider_create(card);
  lv_obj_set_size(bl, card_w - 20 - 22, 6);        // thin 6-px bar, room for the glyph on the left
  lv_obj_align(bl, LV_ALIGN_TOP_RIGHT, 0, bl_y);
  lv_slider_set_range(bl, 5, 100);
  lv_slider_set_value(bl, s_brightness_pct, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bl, lv_color_hex(COLOR_STATUS_OK), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(bl, lv_color_hex(COLOR_STATUS_OK), LV_PART_KNOB);
  lv_obj_set_style_pad_all(bl, 4, LV_PART_KNOB);   // smaller knob to match the thin bar
  lv_obj_add_event_cb(bl, ccBrightnessCb,        LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(bl, ccBrightnessReleaseCb, LV_EVENT_RELEASED,      nullptr);
#endif

  // ---- System info line (CPU + RAM% + PSRAM% + IP) — one thin line, pinned to
  //      the very bottom of the card. Memory is shown as % used so it stays
  //      compact and all four facts fit on one row.
  {
    char sys_s[72];
#if defined(ESP32)
    const unsigned cpu = (unsigned)ESP.getCpuFreqMHz();
    const size_t dram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t dram_tot  = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t ps_free   = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t ps_tot    = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const unsigned dram_pct = dram_tot ? (unsigned)(100 - (dram_free * 100 / dram_tot)) : 0;
    const unsigned ps_pct   = ps_tot   ? (unsigned)(100 - (ps_free   * 100 / ps_tot))   : 0;
    char ipbuf[20] = "no IP";
    if (WiFi.status() == WL_CONNECTED)
      snprintf(ipbuf, sizeof ipbuf, "%s", WiFi.localIP().toString().c_str());
    snprintf(sys_s, sizeof sys_s, "%uMHz \xC2\xB7 RAM %u%% \xC2\xB7 PSRAM %u%% \xC2\xB7 %s",
             cpu, dram_pct, ps_pct, ipbuf);
#else
    snprintf(sys_s, sizeof sys_s, "system info n/a");
#endif
    lv_obj_t* sysl = lv_label_create(card);
    // ONE line only: LONG_DOT still wraps to a 2nd line (dotting only the
    // overflow) unless the height is clamped — that wrap was bleeding the line
    // through the toggle chips. Clamp to a single line height.
    lv_label_set_long_mode(sysl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(sysl, card_w - 20);
    lv_obj_set_height(sysl, lv_font_get_line_height(&g_font_12));
    lv_label_set_text(sysl, sys_s);
    lv_obj_set_style_text_font(sysl, &g_font_12, LV_PART_MAIN);   // smallest available font
    lv_obj_set_style_text_color(sysl, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_align(sysl, LV_ALIGN_BOTTOM_MID, 0, 0);
  }

  // ---- Quick toggles (bottom row) ----
  bool wifi_on = false, ble_on = false;
#if defined(ESP32)
  wifi_on = wifiConfigGetRadioEnabled();
#endif
  if (g_lv.task) ble_on = g_lv.task->isBleEnabled();
  lv_obj_t* row = lv_obj_create(card);
  lv_obj_remove_style_all(row);
#if defined(HAS_TDECK_GT911)
  // 2-row grid: 4 chips per row, so chip 5 (Lock) wraps onto a 2nd row. Sits
  // ABOVE the bottom system-info line (-16 offset leaves room for it).
  lv_obj_set_size(row, card_w - 20, 88);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(row, 4, LV_PART_MAIN);
#else
  // V4: 3 chips. WRAP so they never overflow the (narrow, in portrait) card.
  lv_obj_set_size(row, card_w - 20, 54);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(row, 6, LV_PART_MAIN);
#endif
  // Bottom-anchored but lifted clear of the 1-line sysinfo (~16 px) plus a gap.
  lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  const bool gps_on = g_lv.task && g_lv.task->getGPSState();
  // T-Deck: 5 chips in a 2-row grid. V4: 3 chips; size them to fit the card
  // width with even gaps (portrait 240 px → ~68 px each fits 3 across).
  int tw = 66, th = 54;
#if defined(HAS_TDECK_GT911)
  tw = 58; th = 40;
#else
  tw = (card_w - 20 - 2 * 6) / 3;   // 3 chips + 2 gaps across the content width
  if (tw > 76) tw = 76;
#endif
  ccToggle(row, LV_SYMBOL_WIFI, "Wi-Fi", wifi_on, ccWifiCb, tw, th);
  if (!g_lv.task || g_lv.task->hasBleCapability())
    ccToggle(row, LV_SYMBOL_BLUETOOTH, "BT", ble_on, ccBleCb, tw, th);
  ccToggle(row, LV_SYMBOL_GPS, "GPS", gps_on, ccGpsCb, tw, th);
#if defined(HAS_TDECK_KEYBOARD)
  ccToggle(row, LV_SYMBOL_KEYBOARD,
           s_kb_bl_mode == 0 ? "off" : (s_kb_bl_mode == 1 ? "on" : "auto"),
           s_kb_bl_mode != 0, ccKbBacklightCb, tw, th);
#endif
#if defined(HAS_TDECK_GT911)
  ccToggle(row, LV_SYMBOL_EYE_OPEN, "Lock", false, ccLockCb, tw, th);
  // Sound on/off (notification tones over the T-Deck speaker).
  const bool sound_on = g_lv.task && !g_lv.task->isBuzzerQuiet();
  ccToggle(row, LV_SYMBOL_AUDIO, "Sound", sound_on, ccSoundCb, tw, th);
#endif
  // (Power is the round icon in the card's top-right corner, not a grid chip.)
}

static void statusBarTapCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (s_cc_root) closeControlCenter(); else openControlCenter();
}

// Build the always-on status bar. Called once from UITask::begin after
// lv_init. Lives on lv_layer_sys so it sits above lv_layer_top (modals,
// keyboard mirror) — guarantees the time/battery are visible everywhere.
static void buildGlobalStatusBar() {
  g_statusbar.root = lv_obj_create(lv_layer_sys());
  lv_obj_remove_style_all(g_statusbar.root);
  // Full screen width (responsive to rotation — 240 portrait / 320 landscape).
  lv_obj_set_size(g_statusbar.root, lv_disp_get_hor_res(nullptr), STATUSBAR_H);
  lv_obj_set_pos(g_statusbar.root, 0, 0);
  lv_obj_set_style_bg_color(g_statusbar.root, lv_color_hex(COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(g_statusbar.root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(g_statusbar.root, LV_OBJ_FLAG_SCROLLABLE);
  // Tap the bar to open the control center (quick Wi-Fi/Bluetooth toggles +
  // clock/battery). Child labels are non-clickable, so taps reach the root.
  lv_obj_add_flag(g_statusbar.root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_statusbar.root, statusBarTapCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_set_style_border_side(g_statusbar.root, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_style_border_color(g_statusbar.root, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_set_style_border_opa(g_statusbar.root, LV_OPA_30, LV_PART_MAIN);
  lv_obj_set_style_border_width(g_statusbar.root, 1, LV_PART_MAIN);

  // Left zone — dynamic per-tab. Default to "MESHCOMOD"; updateGlobal-
  // StatusBar() swaps to the envelope+count on non-home tabs.
  g_statusbar.left_label = lv_label_create(g_statusbar.root);
  lv_label_set_text(g_statusbar.left_label, "MESHCOMOD");
  lv_obj_set_style_text_color(g_statusbar.left_label, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(g_statusbar.left_label, &g_font_14, LV_PART_MAIN);
  lv_obj_align(g_statusbar.left_label, LV_ALIGN_LEFT_MID, 6, 0);

  // Right zone, anchored to the right edge (from rightmost to leftmost):
  // battery icon, battery %, clock, conn icon. Compact spacings.
  g_statusbar.batt_icon = lv_label_create(g_statusbar.root);
  lv_label_set_text(g_statusbar.batt_icon, LV_SYMBOL_BATTERY_2);
  lv_obj_set_style_text_color(g_statusbar.batt_icon, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(g_statusbar.batt_icon, &g_font_14, LV_PART_MAIN);
  lv_obj_align(g_statusbar.batt_icon, LV_ALIGN_RIGHT_MID, -2, 0);

  g_statusbar.batt_pct = lv_label_create(g_statusbar.root);
  lv_label_set_text(g_statusbar.batt_pct, "?");
  lv_obj_set_style_text_color(g_statusbar.batt_pct, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(g_statusbar.batt_pct, &g_font_12, LV_PART_MAIN);
  lv_obj_align(g_statusbar.batt_pct, LV_ALIGN_RIGHT_MID, -22, 0);

  g_statusbar.clock = lv_label_create(g_statusbar.root);
  lv_label_set_text(g_statusbar.clock, "--:--");
  lv_obj_set_style_text_color(g_statusbar.clock, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(g_statusbar.clock, &g_font_12, LV_PART_MAIN);
  lv_obj_align(g_statusbar.clock, LV_ALIGN_RIGHT_MID, -64, 0);

  g_statusbar.conn_icon = lv_label_create(g_statusbar.root);
  lv_label_set_text(g_statusbar.conn_icon, "");
  lv_obj_set_style_text_color(g_statusbar.conn_icon, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(g_statusbar.conn_icon, &g_font_12, LV_PART_MAIN);
  lv_obj_align(g_statusbar.conn_icon, LV_ALIGN_RIGHT_MID, -104, 0);

  // Keyboard layout indicator — sits between conn_icon and clock.
  g_statusbar.layout_label = lv_label_create(g_statusbar.root);
  lv_label_set_text(g_statusbar.layout_label, "");
  lv_obj_set_style_text_color(g_statusbar.layout_label, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(g_statusbar.layout_label, &g_font_12, LV_PART_MAIN);
  lv_obj_align(g_statusbar.layout_label, LV_ALIGN_RIGHT_MID, -130, 0);
  lv_obj_add_flag(g_statusbar.layout_label, LV_OBJ_FLAG_HIDDEN);
}

static void updateGlobalStatusBar() {
  if (!g_statusbar.root || !g_lv.task) return;

  // ---- Left zone ----
  if (s_chat_title[0]) {
    // An open conversation surfaces its thread name here (the in-chat header bar
    // was removed to reclaim height). Keep the global unread badge as a prefix so
    // you still see mail waiting in OTHER threads while reading this one. Smaller
    // font so the name + badge fit.
    lv_obj_set_style_text_font(g_statusbar.left_label, &g_font_12, LV_PART_MAIN);
    int total_unread = g_lv.task->getUnreadTotal();
    if (total_unread > 0) {
      char buf[64];
      snprintf(buf, sizeof(buf), LV_SYMBOL_ENVELOPE " %d  %s", total_unread, s_chat_title);
      lv_label_set_text(g_statusbar.left_label, buf);
    } else {
      lv_label_set_text(g_statusbar.left_label, s_chat_title);
    }
  } else
#if defined(HAS_TDECK_GT911)
  if (s_fullscreen_view && s_fullscreen_title[0]) {
    // A fullscreen tool view (Terminal / Files) borrows the left zone for its
    // title, so it can drop its own header row and use the full height.
    lv_label_set_text(g_statusbar.left_label, s_fullscreen_title);
  } else
#endif
  {
    int tab = -1;
    if (g_lv.tabview) tab = (int)lv_tabview_get_tab_act(g_lv.tabview);
    // The OSM credit is longer than the usual left-zone text, so render it in the
    // smaller font — otherwise on the narrow V4 portrait bar its end ("…Map")
    // runs into the Wi-Fi icon. Restore the normal font on other tabs.
    lv_obj_set_style_text_font(g_statusbar.left_label,
                               tab == MAP_TAB_INDEX ? &g_font_12 : &g_font_14, LV_PART_MAIN);
    if (tab == MAP_TAB_INDEX) {
      // On the immersive map the left zone carries the required OSM attribution.
      lv_label_set_text(g_statusbar.left_label, "\xC2\xA9 OpenStreetMap");
    } else if (tab == 0) {
      lv_label_set_text(g_statusbar.left_label, "MESHCOMOD");
    } else {
      int total_unread = g_lv.task->getUnreadTotal();
      if (total_unread > 0) {
        char buf[24];
        snprintf(buf, sizeof(buf), LV_SYMBOL_ENVELOPE "  %d", total_unread);
        lv_label_set_text(g_statusbar.left_label, buf);
      } else {
        // No unread → blank the left zone entirely. Operator complaint was
        // the envelope was always lit even with an empty inbox, which read
        // as "you have mail" 24/7.
        lv_label_set_text(g_statusbar.left_label, "");
      }
    }
  }

  // ---- Connection icon ----
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  const bool wifi_up = (WiFi.status() == WL_CONNECTED);
  const bool ble_up  = g_lv.task->hasBleCapability() && g_lv.task->isBleEnabled();
  if (wifi_up) {
    lv_label_set_text(g_statusbar.conn_icon, LV_SYMBOL_WIFI);
    lv_obj_clear_flag(g_statusbar.conn_icon, LV_OBJ_FLAG_HIDDEN);
  } else if (ble_up) {
    lv_label_set_text(g_statusbar.conn_icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_clear_flag(g_statusbar.conn_icon, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g_statusbar.conn_icon, LV_OBJ_FLAG_HIDDEN);
  }
#endif

  // ---- Battery ----
  const uint16_t mv = batteryMvSmoothed();
  const bool charging = batteryIsCharging(mv);
  const int pct = batteryPercentFromMv(mv);
  static int s_last_pct = -9999;
  static bool s_last_charging = false;
  if (pct != s_last_pct || charging != s_last_charging) {
    char buf[8];
    if (pct < 0)        snprintf(buf, sizeof(buf), "?");
    else if (charging)  snprintf(buf, sizeof(buf), "CHG");   // on USB — cell % isn't observable
    else                snprintf(buf, sizeof(buf), "%d%%", pct);
    lv_label_set_text(g_statusbar.batt_pct, buf);
    s_last_pct = pct;
    s_last_charging = charging;
  }
  static const char* s_last_glyph = nullptr;
  const char* g = batteryGlyphForMv(mv);
  if (g != s_last_glyph) {
    lv_label_set_text(g_statusbar.batt_icon, g);
    s_last_glyph = g;
  }

  // ---- Clock ----
#if defined(ESP32)
  static int s_last_min = -1;
  time_t now_t = time(nullptr);
  if (now_t > 1700000000) {
    struct tm tm_loc;
    localtime_r(&now_t, &tm_loc);
    const int mod = tm_loc.tm_hour * 60 + tm_loc.tm_min;
    if (mod != s_last_min) {
      char buf[8];
      snprintf(buf, sizeof(buf), "%02d:%02d", tm_loc.tm_hour, tm_loc.tm_min);
      lv_label_set_text(g_statusbar.clock, buf);
      s_last_min = mod;
    }
  }
#endif

  // ---- Layout indicator ----
  if (g_statusbar.layout_label) {
    if (keyboardLayoutsAnySecondary()) {
      const char* name = keyboardLayoutName(keyboardLayoutsGetCurrent());
      lv_label_set_text(g_statusbar.layout_label, name);
      lv_obj_clear_flag(g_statusbar.layout_label, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(g_statusbar.layout_label, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static void refreshStatusLabels() {
  if (!g_lv.task) return;
  // Global status bar updates every refresh — visible on every tab, so
  // it's not gated on home_active like the home tab's body widgets.
  updateGlobalStatusBar();
  uint16_t active_tab = 0xFFFF;
  if (g_lv.tabview) active_tab = lv_tabview_get_tab_act(g_lv.tabview);
  const bool home_active = (active_tab == 0);
  if (home_active) refreshHomeBattery();
  // Push a TX/RX sample onto the home chart: delta packets since last tick.
  if (home_active && s_home_chart && s_home_chart_tx && s_home_chart_rx) {
    static uint32_t last_tx = 0;
    static uint32_t last_rx = 0;
    uint32_t cur_tx = the_mesh.getNumSentFlood() + the_mesh.getNumSentDirect();
    uint32_t cur_rx = the_mesh.getNumRecvFlood() + the_mesh.getNumRecvDirect();
    uint32_t dtx = cur_tx >= last_tx ? cur_tx - last_tx : 0;
    uint32_t drx = cur_rx >= last_rx ? cur_rx - last_rx : 0;
    last_tx = cur_tx;
    last_rx = cur_rx;
    lv_chart_set_next_value(s_home_chart, s_home_chart_tx, (lv_coord_t)dtx);
    lv_chart_set_next_value(s_home_chart, s_home_chart_rx, (lv_coord_t)drx);
    // Auto-grow Y range (only grows — keeps the chart visually stable).
    static int s_chart_max_y = 10;
    int peak = (int)((dtx > drx) ? dtx : drx);
    if (peak > s_chart_max_y) {
      s_chart_max_y = peak + 4;
      lv_chart_set_range(s_home_chart, LV_CHART_AXIS_PRIMARY_Y, 0, s_chart_max_y);
    }
    // Legend: total counts since boot, plus the current y-range peak.
    if (s_home_chart_legend) {
      lv_label_set_text_fmt(s_home_chart_legend,
                            "TX %u  /  RX %u   (max %d/tick)",
                            (unsigned)cur_tx, (unsigned)cur_rx, s_chart_max_y);
    }
  }
  const bool settings_active = (active_tab == SETTINGS_TAB_INDEX);
  // Map tab: refresh the bottom info strip (self GPS + on-map count) only
  // when the tab is actually visible. Cheap, but no point doing it on every
  // tick if the user is somewhere else.
  if (active_tab == MAP_TAB_INDEX) refreshMapInfoLabel();
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  // Wi-Fi fetcher arrived a new tile — re-render the map so the freshly
  // downloaded tile appears. Only re-render if we're actually on the
  // map tab; otherwise let the next tab activation pick it up.
  if (s_tile_fetch_dirty && active_tab == MAP_TAB_INDEX) {
    s_tile_fetch_dirty = false;
    renderMapTiles();
    renderMapMarkers();
  }
  // Line-of-sight worker result handoff (non-blocking).
  losPoll();
#endif

  if (g_lv.home_state && home_active) {
    /* Show which transport radio is up. Boot picks WiFi xor BLE based on
     * saved creds (heap can't fit both + LVGL). For WiFi we also show the IP
     * once associated, or a connect-progress hint while it's still trying. */
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
    if (WiFi.status() == WL_CONNECTED) {
      IPAddress ip = WiFi.localIP();
      lv_label_set_text_fmt(g_lv.home_state, LV_SYMBOL_WIFI " %d.%d.%d.%d",
                            ip[0], ip[1], ip[2], ip[3]);
    } else if (WiFi.getMode() == WIFI_STA) {
      const char* hint;
      switch (WiFi.status()) {
        case WL_IDLE_STATUS:     hint = "Starting…";  break;
        case WL_NO_SSID_AVAIL:   hint = "SSID not found"; break;
        case WL_CONNECT_FAILED:  hint = "Auth failed";   break;
        case WL_CONNECTION_LOST: hint = "Link lost";     break;
        case WL_DISCONNECTED:    hint = "Connecting…";   break;
        case WL_NO_SHIELD:       hint = "Init…";         break;
        default:                 hint = "Connecting…";   break;
      }
      lv_label_set_text_fmt(g_lv.home_state, LV_SYMBOL_WIFI " %s", hint);
    } else if (g_lv.task->hasBleCapability() && g_lv.task->isBleEnabled()) {
      lv_label_set_text(g_lv.home_state, LV_SYMBOL_BLUETOOTH);
    } else {
      lv_label_set_text(g_lv.home_state, "—");
    }
#else
    lv_label_set_text(g_lv.home_state,
                      g_lv.task->hasConnection() ? "Connected" : "Disconnected");
#endif
  }
  if (g_lv.home_stats && home_active) {
#if defined(ESP32)
    bool show_dc = touchPrefsGetDutyMeterShown();
#else
    bool show_dc = false;
#endif
    if (show_dc) {
      // Compute used % from the remaining tx budget. max_budget =
      // window_ms * (1 / (1 + factor)). With airtime_factor=1 → 50% cap;
      // factor=9 → 10% cap (typical EU868). Show both "used%" and "cap%"
      // so the user can tell whether they're near the regulatory ceiling.
      NodePrefs* prefs = the_mesh.getNodePrefs();
      float factor = prefs ? prefs->airtime_factor : 1.0f;
      float duty   = 1.0f / (1.0f + factor);
      unsigned long window_ms = 3600000UL;   // matches Dispatcher default
      unsigned long max_budget = (unsigned long)((float)window_ms * duty);
      unsigned long remaining  = the_mesh.getRemainingTxBudget();
      if (remaining > max_budget) remaining = max_budget;
      unsigned used_pct = max_budget > 0
          ? (unsigned)((100UL * (max_budget - remaining)) / max_budget)
          : 0u;
      unsigned cap_pct  = (unsigned)(duty * 100.0f + 0.5f);
      lv_label_set_text_fmt(g_lv.home_stats,
                            "Unread %d  |  Bat %umV\nDC %u%% used / %u%% cap",
                            g_lv.task->getUnreadTotal(),
                            static_cast<unsigned>(g_lv.task->getBattMilliVolts()),
                            used_pct, cap_pct);
    } else {
      lv_label_set_text_fmt(g_lv.home_stats, "Unread %d  |  Bat %umV",
                            g_lv.task->getUnreadTotal(),
                            static_cast<unsigned>(g_lv.task->getBattMilliVolts()));
    }
  }
  if (g_lv.settings_status && settings_active) {
#if defined(ESP32)
    lv_label_set_text_fmt(
        g_lv.settings_status,
        "TCP %s  BLE %s\nWS clients %d  GPS %s  Buzzer %s",
        onOff(g_lv.task->isTcpEnabled()),
        g_lv.task->hasBleCapability() ? onOff(g_lv.task->isBleEnabled()) : "n/a",
        g_lv.task->getWsConnectedCount(),
        onOff(g_lv.task->getGPSState()), g_lv.task->isBuzzerQuiet() ? "quiet" : "on");
#else
    lv_label_set_text_fmt(
        g_lv.settings_status,
        "TCP %s  BLE %s\nWS clients %d  GPS %s  Buzzer %s",
        onOff(g_lv.task->isTcpEnabled()),
        g_lv.task->hasBleCapability() ? onOff(g_lv.task->isBleEnabled()) : "n/a",
        g_lv.task->getWsConnectedCount(), onOff(g_lv.task->getGPSState()),
        g_lv.task->isBuzzerQuiet() ? "quiet" : "on");
#endif
  }
  if (settings_active || g_set_modal.root) refreshSettingsSectionSubtitles();
}

// ============================================================
// Boot splash
// Brief animated "MESHCOMOD" overlay shown on top of the home screen after
// LVGL is up. Replaces the static "Loading..." moment so the device feels
// like it's introducing itself. Auto-removes after ~1.7s; tap to dismiss.
// ============================================================
static lv_obj_t* s_splash_root = nullptr;

static void splashSetOpa(void* var, int32_t v) {
  lv_obj_set_style_opa(static_cast<lv_obj_t*>(var), static_cast<lv_opa_t>(v), LV_PART_MAIN);
}

static void splashSetX(void* var, int32_t v) {
  lv_obj_set_x(static_cast<lv_obj_t*>(var), static_cast<lv_coord_t>(v));
}

static void splashSetWidth(void* var, int32_t v) {
  lv_obj_set_width(static_cast<lv_obj_t*>(var), static_cast<lv_coord_t>(v));
}

static void splashRemove() {
  if (!s_splash_root) return;
  lv_obj_del(s_splash_root);
  s_splash_root = nullptr;
}

static void splashFadeOutReady(lv_anim_t* a) {
  (void)a;
  splashRemove();
}

static void splashHoldThenFadeOut(lv_timer_t* t) {
  lv_timer_del(t);
  if (!s_splash_root) return;
  // Fade the whole splash out together; ready_cb on the last anim removes the overlay.
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_splash_root);
  lv_anim_set_time(&a, 350);
  lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_exec_cb(&a, splashSetOpa);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
  lv_anim_set_ready_cb(&a, splashFadeOutReady);
  lv_anim_start(&a);
}

static void splashTapDismissCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  splashRemove();
}

static void buildBootSplash() {
  // Full-screen opaque card on lv_layer_top so it covers the freshly-rendered
  // home tab. Use the same dark background as the rest of the UI so we don't
  // flash a different palette to the user.
  s_splash_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_splash_root);
  lv_obj_set_size(s_splash_root, lv_disp_get_hor_res(nullptr),
                  lv_disp_get_ver_res(nullptr));
  lv_obj_set_pos(s_splash_root, 0, 0);
  lv_obj_set_style_bg_color(s_splash_root, lv_color_hex(COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_splash_root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_splash_root, 0, LV_PART_MAIN);
  lv_obj_clear_flag(s_splash_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_splash_root, LV_OBJ_FLAG_FLOATING);
  lv_obj_move_foreground(s_splash_root);
  lv_obj_add_event_cb(s_splash_root, splashTapDismissCb, LV_EVENT_CLICKED, nullptr);

  // ---- Title: TOUCH BETA ----
  // The bootloader/early-boot screen already paints the pixelated
  // "MESHCOMOD" wordmark; this second beat names the build channel
  // (TOUCH BETA). Short, white on black, fades up identical to the
  // previous wordmark animation — feels like the next beat of the boot
  // sequence rather than a brand splash.
  lv_obj_t* title = lv_label_create(s_splash_root);
  lv_label_set_text(title, "TOUCH BETA");
  // UNSCII_16 is a 16-pixel-tall bitmap font — same pixelated style as
  // the bootloader's MESHCOMOD wordmark, so the splash reads as a
  // continuation of the boot screen instead of a re-styled "designed"
  // overlay.
  lv_obj_set_style_text_font(title, &lv_font_unscii_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_letter_space(title, 3, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -16);
  lv_obj_set_style_opa(title, LV_OPA_TRANSP, LV_PART_MAIN);

  // ---- Underline ----
  lv_obj_t* rule = lv_obj_create(s_splash_root);
  lv_obj_remove_style_all(rule);
  lv_obj_set_size(rule, 0, 2);
  lv_obj_align(rule, LV_ALIGN_CENTER, 0, 14);
  lv_obj_set_style_bg_color(rule, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(rule, 1, LV_PART_MAIN);

  // ---- Subtitle ----
  // Matches the "ON-NET" title: announces the link is live. Spaced
  // letters keep it visually quiet under the bigger wordmark.
  lv_obj_t* sub = lv_label_create(s_splash_root);
  lv_label_set_text(sub, "mesh  link  armed");
  // UNSCII_8 keeps the pixelated boot-screen feel; Montserrat would
  // break the visual continuity with the title.
  lv_obj_set_style_text_font(sub, &lv_font_unscii_8, LV_PART_MAIN);
  lv_obj_set_style_text_color(sub, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_letter_space(sub, 4, LV_PART_MAIN);
  lv_obj_align(sub, LV_ALIGN_CENTER, 0, 32);
  lv_obj_set_style_opa(sub, LV_OPA_TRANSP, LV_PART_MAIN);

  // Phase 1: title fades in while sliding up from y=-6 to y=-16.
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, title);
  lv_anim_set_time(&a, 550);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_exec_cb(&a, splashSetOpa);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);

  // Phase 2: underline grows outward from the center over 500ms (180px wide).
  lv_anim_t aw;
  lv_anim_init(&aw);
  lv_anim_set_var(&aw, rule);
  lv_anim_set_time(&aw, 500);
  lv_anim_set_delay(&aw, 250);
  lv_anim_set_values(&aw, 0, 180);
  lv_anim_set_exec_cb(&aw, splashSetWidth);
  lv_anim_set_path_cb(&aw, lv_anim_path_ease_out);
  lv_anim_start(&aw);
  // Centering doesn't auto-update with width on lv_obj_create; keep the rule
  // centered by sliding x in lockstep with the width grow.
  lv_anim_t ax;
  lv_anim_init(&ax);
  lv_anim_set_var(&ax, rule);
  lv_anim_set_time(&ax, 500);
  lv_anim_set_delay(&ax, 250);
  lv_anim_set_values(&ax, 120, 120 - 90);  // (240-w)/2 across the grow
  lv_anim_set_exec_cb(&ax, splashSetX);
  lv_anim_set_path_cb(&ax, lv_anim_path_ease_out);
  lv_anim_start(&ax);

  // Phase 3: subtitle fades in slightly delayed.
  lv_anim_t as;
  lv_anim_init(&as);
  lv_anim_set_var(&as, sub);
  lv_anim_set_time(&as, 450);
  lv_anim_set_delay(&as, 500);
  lv_anim_set_values(&as, LV_OPA_TRANSP, LV_OPA_70);
  lv_anim_set_exec_cb(&as, splashSetOpa);
  lv_anim_set_path_cb(&as, lv_anim_path_ease_out);
  lv_anim_start(&as);

  // Hold for ~1.5 s after the last fade-in completes (subtitle ends at
  // t=950 ms, fade-out starts at t=2400), then trigger fade-out. Earlier
  // 1100 ms timer made the splash feel rushed — the operator barely got
  // to read "ON-NET" before it dismissed.
  lv_timer_t* t = lv_timer_create(splashHoldThenFadeOut, 2400, nullptr);
  lv_timer_set_repeat_count(t, 1);
}

// ============================================================
// First-boot setup wizard
// ============================================================
// A one-time, full-screen flow shown on a fresh flash (gated by
// touchPrefsGetSetupDone): Welcome -> Name -> Region -> Wi-Fi. It reuses the
// existing setters (setNodeName, setRadioParams, wifiConfig*) so it's a thin UI
// over machinery that already works. Name + region are committed as the user
// advances; Wi-Fi (optional) is committed on Finish, which then reboots once so
// the radio re-inits with the chosen region and Wi-Fi associates.
static const lv_coord_t kSetupBtnH = 40;

static void setupShowStep(int step);   // fwd: nav callbacks below call back into it
static void setupFillRegionList();      // fwd: built by the region step + recoloured on tap

static lv_obj_t* setupBtn(const char* txt, lv_event_cb_t cb, bool primary,
                          lv_coord_t x, lv_coord_t y, lv_coord_t w) {
  lv_obj_t* b = lv_btn_create(s_setup_root);
  lv_obj_set_size(b, w, kSetupBtnH);
  lv_obj_set_pos(b, x, y);
  styleButton(b);
  if (primary) {
    lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x3B7039), LV_PART_MAIN | LV_STATE_PRESSED);
  }
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, &g_font_14, LV_PART_MAIN);
  lv_obj_set_style_text_color(l, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_center(l);
  return b;
}

// Title + optional "Step N of 3" tag + optional wrapped blurb. Returns the y
// just below the header so the step body can stack under it.
static int setupHeader(const char* title, const char* blurb, const char* step_tag) {
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_obj_t* t = lv_label_create(s_setup_root);
  lv_label_set_text(t, title);
  lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(t, sw - 84);
  lv_obj_set_style_text_font(t, &g_font_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(t, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_pos(t, 12, 12);
  int y = 12 + 24;
  if (step_tag && step_tag[0]) {
    lv_obj_t* s = lv_label_create(s_setup_root);
    lv_label_set_text(s, step_tag);
    lv_obj_set_style_text_font(s, &g_font_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(s, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_align(s, LV_ALIGN_TOP_RIGHT, -12, 16);
  }
  if (blurb && blurb[0]) {
    lv_obj_t* b = lv_label_create(s_setup_root);
    lv_label_set_text(b, blurb);
    lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(b, sw - 24);
    lv_obj_set_style_text_font(b, &g_font_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(b, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_pos(b, 12, y + 2);
    lv_obj_update_layout(b);                 // resolve wrap height (1 or 2 lines)
    y += 2 + lv_obj_get_height(b) + 4;
  }
  return y;
}

// Mark setup complete and tear the overlay down, revealing the live OS beneath.
// No reboot — used for Skip and (implicitly) anything that doesn't change the
// radio/Wi-Fi. The status bar (hidden while the wizard owns the screen) returns.
static void setupWizardClose() {
  g_set_modal.wifi_ssid_ta = nullptr;       // we borrowed these for the scan popup
  g_set_modal.wifi_pwd_ta  = nullptr;
  hideKb();
  if (s_setup_root) { lv_obj_del(s_setup_root); s_setup_root = nullptr; }
  s_setup_name_ta = nullptr;
  s_setup_region_list = nullptr;
  s_setup_ssid_ta = nullptr;
  s_setup_pwd_ta = nullptr;
  if (g_statusbar.root) lv_obj_clear_flag(g_statusbar.root, LV_OBJ_FLAG_HIDDEN);
}

static void setupSkipCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  touchPrefsSetSetupDone(true);             // don't show it again on next boot
  setupWizardClose();
  if (g_lv.task) g_lv.task->showAlert("You can run setup later in Settings \xE2\x86\x92 Device", 2200);
}

static void setupGetStartedCb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) setupShowStep(1);
}

static void setupBackCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (s_setup_step > 0) setupShowStep(s_setup_step - 1);
}

static void setupNameNextCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  kbMirrorSyncToReal();   // flush the on-screen-keyboard mirror into the field
  if (s_setup_name_ta) {
    const char* name = lv_textarea_get_text(s_setup_name_ta);
    if (!name || !name[0]) {
      if (g_lv.task) g_lv.task->showAlert("Enter a name", 1200);
      return;
    }
    if (g_lv.task) g_lv.task->setNodeName(name);
  }
  setupShowStep(2);
}

static void setupRegionNextCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (s_setup_region_sel >= 0 && s_setup_region_sel < (int)k_mesh_radio_preset_count && g_lv.task) {
    const MeshRadioPreset& p = k_mesh_radio_presets[s_setup_region_sel];
    g_lv.task->setRadioParams(p.freq_mhz, p.bw_khz, p.sf, p.cr, p.tx_dbm,
                              static_cast<float>(p.airtime_limit_pct) / 100.0f);
  }
  setupShowStep(3);
}

static void setupFinishCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  kbMirrorSyncToReal();
  touchPrefsSetSetupDone(true);   // mark done so the wizard won't reappear on next boot
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  if (s_setup_ssid_ta) {
    char ssid[WIFI_CONFIG_SSID_MAX];
    char pwd[WIFI_CONFIG_PWD_MAX];
    strncpy(ssid, lv_textarea_get_text(s_setup_ssid_ta), sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0';
    strncpy(pwd, s_setup_pwd_ta ? lv_textarea_get_text(s_setup_pwd_ta) : "", sizeof(pwd) - 1);
    pwd[sizeof(pwd) - 1] = '\0';
    // Trim stray spaces (a leading/trailing space in the SSID = "AP not found"
    // and the device sits forever in "connecting…"). Mirrors saveWifiCb.
    trimWifiField(ssid);
    trimWifiField(pwd);
    if (ssid[0]) {   // empty SSID = user skipped Wi-Fi
      wifiConfigSetSsid(ssid);
      wifiConfigSetPwd(pwd);
      wifiConfigSetRadioEnabled(true);
      wifiConfigRequestApply();
    }
  }
#endif
  // Region was applied live when the user advanced past the region step
  // (setRadioParams -> applyRadioFromPrefs), and Wi-Fi re-associates live via the
  // apply request above — so nothing here needs a reboot. Just close the wizard
  // back to the main UI.
  setupWizardClose();
  if (g_lv.task) g_lv.task->showAlert("Setup complete", 1400);
}

// Tap a region row: select it + recolour in place (keeps the scroll position).
static void setupRegionRowCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const int idx = (int)(intptr_t)lv_event_get_user_data(e);
  s_setup_region_sel = idx;
  if (!s_setup_region_list) return;
  const uint32_t n = lv_obj_get_child_cnt(s_setup_region_list);
  for (uint32_t i = 0; i < n; ++i) {
    lv_obj_t* c = lv_obj_get_child(s_setup_region_list, i);
    if (c) lv_obj_set_style_bg_color(
               c, lv_color_hex((int)i == idx ? COLOR_STATUS_OK : 0x1A1B1C), LV_PART_MAIN);
  }
}

static void setupFillRegionList() {
  if (!s_setup_region_list) return;
  lv_obj_clean(s_setup_region_list);
  const lv_coord_t rw = lv_disp_get_hor_res(nullptr) - 24 - 12;   // list width minus pad/scrollbar
  for (int i = 0; i < (int)k_mesh_radio_preset_count; ++i) {
    lv_obj_t* r = lv_btn_create(s_setup_region_list);
    lv_obj_set_size(r, rw, 34);
    styleButton(r);
    lv_obj_set_style_bg_color(r, lv_color_hex(i == s_setup_region_sel ? COLOR_STATUS_OK : 0x1A1B1C),
                              LV_PART_MAIN);
    lv_obj_add_event_cb(r, setupRegionRowCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_t* l = lv_label_create(r);
    lv_label_set_text(l, k_mesh_radio_presets[i].label);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, rw - 16);
    lv_obj_set_style_text_font(l, &g_font_14, LV_PART_MAIN);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
  }
}

static void setupWifiScanCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  // A Wi-Fi scan needs the Wi-Fi driver up. Touch boots Wi-Fi-first (BLE off),
  // so on a fresh device this is normally live and the scan just works. The
  // guard only trips if this device was switched to BLE — then the radio is
  // down (BLE-vs-Wi-Fi heap mutex) and a scan would panic; tell the user to
  // type the name (it connects after the finishing reboot) or re-enable Wi-Fi.
  if (!wifiConfigWantsWifi()) {
    if (g_lv.task)
      g_lv.task->showAlert("Type your network name — or switch to Wi-Fi in Settings to scan", 3000);
    return;
  }
  wifiScanOpenAndKick();
#endif
}

static void setupShowStep(int step) {
  if (!s_setup_root) return;
  hideKb();
  // The scan popup writes the chosen SSID through these borrowed pointers; drop
  // them before the Wi-Fi fields are freed by the clean below.
  g_set_modal.wifi_ssid_ta = nullptr;
  g_set_modal.wifi_pwd_ta  = nullptr;
  lv_obj_clean(s_setup_root);
  s_setup_name_ta = nullptr;
  s_setup_region_list = nullptr;
  s_setup_ssid_ta = nullptr;
  s_setup_pwd_ta = nullptr;
  s_setup_step = step;

  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  const lv_coord_t btn_y = sh - kSetupBtnH - 10;

  if (step == 0) {
    setupHeader("Welcome to Meshcomod", nullptr, nullptr);
    lv_obj_t* m = lv_label_create(s_setup_root);
    lv_label_set_text(m,
        "Let's set up your device.\n\n"
        "You'll pick a name, choose your LoRa region, and (optionally) join "
        "Wi-Fi for time sync, maps and updates. Takes about a minute.");
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(m, sw - 24);
    lv_obj_set_style_text_font(m, &g_font_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(m, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_pos(m, 12, 46);
    setupBtn("Skip", setupSkipCb, false, 12, btn_y, 96);
    setupBtn("Get Started", setupGetStartedCb, true, 12 + 96 + 8, btn_y, sw - 24 - 96 - 8);
  } else if (step == 1) {
    int y = setupHeader("Choose your name",
                        "How you'll appear to other nodes. You can change this later in Settings.",
                        "Step 1 of 3");
    s_setup_name_ta = lv_textarea_create(s_setup_root);
    lv_obj_set_size(s_setup_name_ta, sw - 24, 36);
    lv_obj_set_pos(s_setup_name_ta, 12, y + 6);
    lv_textarea_set_one_line(s_setup_name_ta, true);
    lv_textarea_set_placeholder_text(s_setup_name_ta, "Your name");
    lv_textarea_set_max_length(s_setup_name_ta, 30);
    if (g_lv.task) {
      const char* cur = g_lv.task->getNodeNameCstr();
      if (cur && cur[0]) lv_textarea_set_text(s_setup_name_ta, cur);
    }
    attachSettingsTaEvents(s_setup_name_ta);
    setupBtn("Back", setupBackCb, false, 12, btn_y, 72);
    setupBtn("Next", setupNameNextCb, true, sw - 12 - 120, btn_y, 120);
  } else if (step == 2) {
    int y = setupHeader("Choose your region",
                        "Every node you talk to must match. You can change this later in Settings.",
                        "Step 2 of 3");
    s_setup_region_list = lv_obj_create(s_setup_root);
    lv_obj_remove_style_all(s_setup_region_list);
    lv_obj_set_size(s_setup_region_list, sw - 24, btn_y - (y + 6) - 8);
    lv_obj_set_pos(s_setup_region_list, 12, y + 6);
    lv_obj_set_flex_flow(s_setup_region_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_setup_region_list, 4, LV_PART_MAIN);
    lv_obj_set_scroll_dir(s_setup_region_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_setup_region_list, LV_SCROLLBAR_MODE_AUTO);
    if (s_setup_region_sel < 0) s_setup_region_sel = findMatchingMeshRadioPreset(the_mesh.getNodePrefs());
    setupFillRegionList();
    setupBtn("Back", setupBackCb, false, 12, btn_y, 72);
    setupBtn("Next", setupRegionNextCb, true, sw - 12 - 120, btn_y, 120);
  } else {
    int y = setupHeader("Connect to Wi-Fi", "Optional — type your network, or leave blank to skip.", "Step 3 of 3");
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
    // Live scanning needs Wi-Fi up. Touch boots Wi-Fi-first (BLE off), so on a
    // fresh device this is true and Scan is shown; it only goes false if this
    // device was switched to BLE (radio down — then we hide Scan and the user
    // types the SSID, which connects after the finishing reboot).
    const bool can_scan = wifiConfigWantsWifi();
    const int scan_w = 76, gap = 6;
    s_setup_ssid_ta = lv_textarea_create(s_setup_root);
    lv_obj_set_size(s_setup_ssid_ta, can_scan ? (sw - 24 - scan_w - gap) : (sw - 24), 34);
    lv_obj_set_pos(s_setup_ssid_ta, 12, y + 6);
    lv_textarea_set_one_line(s_setup_ssid_ta, true);
    lv_textarea_set_placeholder_text(s_setup_ssid_ta, "Network name (SSID)");
    lv_textarea_set_max_length(s_setup_ssid_ta, WIFI_CONFIG_SSID_MAX - 1);
    attachSettingsTaEvents(s_setup_ssid_ta);
    if (can_scan) {
      lv_obj_t* scan = lv_btn_create(s_setup_root);
      lv_obj_set_size(scan, scan_w, 34);
      lv_obj_set_pos(scan, sw - 12 - scan_w, y + 6);
      styleButton(scan);
      lv_obj_add_event_cb(scan, setupWifiScanCb, LV_EVENT_CLICKED, nullptr);
      lv_obj_t* sl = lv_label_create(scan); lv_label_set_text(sl, LV_SYMBOL_WIFI " Scan");
      lv_obj_set_style_text_font(sl, &g_font_12, LV_PART_MAIN); lv_obj_center(sl);
    }
    s_setup_pwd_ta = lv_textarea_create(s_setup_root);
    lv_obj_set_size(s_setup_pwd_ta, sw - 24, 34);
    lv_obj_set_pos(s_setup_pwd_ta, 12, y + 6 + 42);
    lv_textarea_set_one_line(s_setup_pwd_ta, true);
    lv_textarea_set_password_mode(s_setup_pwd_ta, true);
    lv_textarea_set_placeholder_text(s_setup_pwd_ta, "Password (empty = skip Wi-Fi)");
    lv_textarea_set_max_length(s_setup_pwd_ta, WIFI_CONFIG_PWD_MAX - 1);
    attachSettingsTaEvents(s_setup_pwd_ta);
    if (!can_scan) {
      lv_obj_t* hint = lv_label_create(s_setup_root);
      lv_label_set_text(hint, "Tip: you can scan for networks in Settings \xE2\x86\x92 Network after setup.");
      lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(hint, sw - 24);
      lv_obj_set_style_text_font(hint, &g_font_12, LV_PART_MAIN);
      lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
      lv_obj_set_pos(hint, 12, y + 6 + 42 + 40);
    }
    // Let the scan popup drop the chosen SSID straight into our field.
    g_set_modal.wifi_ssid_ta = s_setup_ssid_ta;
    g_set_modal.wifi_pwd_ta  = s_setup_pwd_ta;
    // Auto-present the network picker once on arrival so nearby SSIDs show up
    // immediately (no need to tap Scan first). Once per wizard run; the user can
    // Close it to type a hidden network instead.
    if (can_scan && !s_setup_wifi_autoscan_done) {
      s_setup_wifi_autoscan_done = true;
      wifiScanOpenAndKick();
    }
#else
    lv_obj_t* na = lv_label_create(s_setup_root);
    lv_label_set_text(na, "Wi-Fi isn't available on this build.");
    lv_obj_set_style_text_color(na, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_pos(na, 12, y + 8);
#endif
    setupBtn("Back", setupBackCb, false, 12, btn_y, 72);
    setupBtn("Finish", setupFinishCb, true, sw - 12 - 120, btn_y, 120);
  }
}

static void setupWizardOpen() {
  if (s_setup_root) return;
  if (g_statusbar.root) lv_obj_add_flag(g_statusbar.root, LV_OBJ_FLAG_HIDDEN);  // own the whole screen
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_setup_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_setup_root);
  lv_obj_set_size(s_setup_root, sw, sh);
  lv_obj_set_pos(s_setup_root, 0, 0);
  styleSurface(s_setup_root, COLOR_BG, 0);
  lv_obj_set_style_pad_all(s_setup_root, 0, LV_PART_MAIN);
  lv_obj_clear_flag(s_setup_root, LV_OBJ_FLAG_SCROLLABLE);
  s_setup_region_sel = -1;
  s_setup_wifi_autoscan_done = false;   // re-arm the one-shot auto-scan for this run
  setupShowStep(0);
  lv_obj_move_foreground(s_setup_root);
}

// Called once at the end of UITask::begin: show the wizard on a fresh flash.
static void setupWizardMaybeOpen() {
  if (touchPrefsGetSetupDone()) return;
  setupWizardOpen();
}

// "Run setup again" button in Device settings: re-open the flow over the live
// UI. Finishing reboots (to apply region/Wi-Fi); Skip just closes again.
static void setupRerunCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  setupWizardOpen();
}

// ============================================================
// Build full UI tree
// ============================================================
static void buildUiTree() {
  // Always boot to the Home tab (the last-used tab is no longer restored).
  uint8_t saved_tab = 0;

  lv_obj_t* root = lv_scr_act();
  styleSurface(root, COLOR_BG, 0);
  lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);   // zero default theme padding so overlays sit at (0,0)
  lv_obj_set_style_text_color(root, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);

  // ---- Tabview ----
  g_lv.tabview = lv_tabview_create(root, LV_DIR_BOTTOM, TABBAR_H);
  // Tabview leaves the top STATUSBAR_H pixels free so the global status
  // bar (on lv_layer_sys) doesn't paint over tab content. Sized from the
  // live display resolution so it fills the screen in either orientation
  // (240x320 portrait / 320x240 landscape).
  lv_obj_set_size(g_lv.tabview, lv_disp_get_hor_res(nullptr),
                  lv_disp_get_ver_res(nullptr) - STATUSBAR_H);
  lv_obj_align(g_lv.tabview, LV_ALIGN_TOP_LEFT, 0, STATUSBAR_H);
  styleSurface(g_lv.tabview, COLOR_BG, 0);
  lv_obj_set_style_anim_time(g_lv.tabview, 150, LV_PART_MAIN);
  lv_obj_add_event_cb(g_lv.tabview, tabChangedCb, LV_EVENT_VALUE_CHANGED, nullptr);

  // Disable LVGL's built-in horizontal-drag-to-switch on the tabview content.
  // Without this, a swipe across the screen advances the tab twice — once via
  // LVGL's internal drag handler, then again via our applySwipeGesture() on
  // finger release.
  {
    lv_obj_t* tv_content = lv_tabview_get_content(g_lv.tabview);
    if (tv_content) {
      lv_obj_clear_flag(tv_content, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_scroll_dir(tv_content, LV_DIR_NONE);
      // Explicit black bg + square corners: applyMapChrome toggles this
      // container's bg_opa for the immersive map, and the LVGL theme default is
      // a light, ROUNDED panel — which peeked through as white rounded corners
      // behind the home/other tabs after returning from the map. Pin it black
      // and square so the opaque state is pure COLOR_BG.
      lv_obj_set_style_bg_color(tv_content, lv_color_hex(COLOR_BG), LV_PART_MAIN);
      lv_obj_set_style_radius(tv_content, 0, LV_PART_MAIN);
    }
  }

  lv_obj_t* tab_btns = lv_tabview_get_tab_btns(g_lv.tabview);
  // Tab bar bar itself sits on pure BG, not the panel — keeps the bottom
  // strip indistinguishable from the rest of the screen except for the
  // active-tab highlight.
  styleSurface(tab_btns, COLOR_BG, 0);
  // Per-tab item: dim outline (low-opacity gray) by default; the active
  // tab fills with the neutral accent at low opacity instead of the
  // earlier bright fill. Stops the bottom strip from looking like a row
  // of glowing buttons.
  lv_obj_set_style_bg_opa(tab_btns, LV_OPA_TRANSP, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(tab_btns, lv_color_hex(COLOR_ACCENT),
                             LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_bg_opa(tab_btns, LV_OPA_30, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_width(tab_btns, 0, LV_PART_ITEMS);
  lv_obj_set_style_text_color(tab_btns, lv_color_hex(COLOR_SUB), LV_PART_ITEMS);
  lv_obj_set_style_text_color(tab_btns, lv_color_hex(COLOR_TEXT),
                               LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_font(tab_btns, &g_font_14, LV_PART_MAIN);

  // Tab labels: icons-only (house, envelope, list, GPS pin, gear) — saves
  // space and lets all five tabs sit comfortably in the 240px wide bar
  // (240/5 = 48 px per icon).
  lv_obj_t* tab_home     = lv_tabview_add_tab(g_lv.tabview, LV_SYMBOL_HOME);
  lv_obj_t* tab_chats    = lv_tabview_add_tab(g_lv.tabview, LV_SYMBOL_ENVELOPE);
  lv_obj_t* tab_contacts = lv_tabview_add_tab(g_lv.tabview, LV_SYMBOL_LIST);
  lv_obj_t* tab_map      = lv_tabview_add_tab(g_lv.tabview, LV_SYMBOL_GPS);
  lv_obj_t* tab_settings = lv_tabview_add_tab(g_lv.tabview, LV_SYMBOL_SETTINGS);
  // Slightly larger font for icons so they're easy to tap.
  lv_obj_set_style_text_font(tab_btns, &g_font_16, LV_PART_MAIN);

  // Home and Set tabs scroll vertically at the tab page level
  for (lv_obj_t* t : {tab_home, tab_settings}) {
    styleSurface(t, COLOR_BG, 0);
    lv_obj_set_scroll_dir(t, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(t, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(t, scrollClampOnEndCb, LV_EVENT_SCROLL_END, nullptr);
  }

  // ---- Build tab contents ----
  makeHome(tab_home);

  g_lv.dm.channel_mode = false;
  g_lv.ch.channel_mode = true;
  makeChatList(tab_chats, g_lv.dm, false, true);
  makeContactsTab(tab_contacts);
  makeMapTab(tab_map);

  // Red "!" update badge over the Settings gear (rightmost bottom tab). A child
  // of the screen (so it has no layout overriding its alignment) created BEFORE
  // the chat-detail overlays + above the tabview — it floats over the gear on
  // the main tabs but is covered by full-screen overlays (chat detail, modals,
  // lock screen) that sit above it in the screen z-order.
  s_update_badge = lv_label_create(lv_scr_act());
  lv_label_set_text(s_update_badge, "!");
  lv_obj_set_size(s_update_badge, 15, 15);
  lv_obj_set_style_radius(s_update_badge, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_update_badge, lv_color_hex(0xE2403A), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_update_badge, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_update_badge, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_update_badge, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_align(s_update_badge, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_update_badge, 0, LV_PART_MAIN);
  lv_obj_align(s_update_badge, LV_ALIGN_BOTTOM_RIGHT, -8, -(TABBAR_H - 15));
  lv_obj_add_flag(s_update_badge, LV_OBJ_FLAG_HIDDEN);

  // Create full-screen detail overlays (hidden until a thread is tapped)
  makeChatDetail(g_lv.dm);
  makeChatDetail(g_lv.ch);

  makeSettings(tab_settings);

  // ---- Shared on-screen keyboard (child of root, hidden initially) ----
  /* On `lv_layer_top()` so the keyboard appears above fullscreen settings/chat overlays. */
  g_lv.keyboard = lv_keyboard_create(lv_layer_top());
  lv_obj_set_size(g_lv.keyboard, lv_disp_get_hor_res(nullptr), CHAT_KB_H);
  lv_obj_align(g_lv.keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(g_lv.keyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(g_lv.keyboard, keyboardCb, LV_EVENT_READY,         nullptr);
  lv_obj_add_event_cb(g_lv.keyboard, keyboardCb, LV_EVENT_CANCEL,        nullptr);
  lv_obj_add_event_cb(g_lv.keyboard, keyboardCb, LV_EVENT_VALUE_CHANGED, nullptr);
  // Dark-theme keyboard styling
  lv_obj_set_style_bg_color(g_lv.keyboard, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_color(g_lv.keyboard, lv_color_hex(0x1B2B3A),    LV_PART_ITEMS);
  lv_obj_set_style_border_color(g_lv.keyboard, lv_color_hex(0x18191A), LV_PART_ITEMS);
  lv_obj_set_style_text_color(g_lv.keyboard,  lv_color_hex(COLOR_TEXT), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(g_lv.keyboard, lv_color_hex(COLOR_ACCENT),
                             LV_PART_ITEMS | LV_STATE_PRESSED);
  // Pre-warm the keyboard: force LVGL to compute its full button-matrix
  // layout NOW so the first tap on a textarea doesn't pay the ~30+ key
  // first-render cost. Without this the first keyboard show in the
  // session lagged ~300-500 ms — most noticeable on the map → chat
  // marker-open path because freeMapTiles + tab anim were already
  // competing for the same refresh tick.
  lv_obj_update_layout(g_lv.keyboard);

  kbMirrorEnsureCreated();

  // ---- Rotation arrows (shown only while the keyboard is up) ----
  // Two small chevron buttons that toggle landscape typing in either
  // direction. Tap a side once to rotate, tap the same side again to return
  // to portrait. Choice persists via NVS so the user's preferred landscape
  // direction comes back the next time the keyboard opens.
  auto makeRotBtn = [](const char* sym, lv_event_cb_t cb) {
    lv_obj_t* b = lv_btn_create(lv_layer_top());
    lv_obj_set_size(b, 32, 26);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1B2B3A), LV_PART_MAIN);
    lv_obj_set_style_border_color(b, lv_color_hex(0x2A3D52), LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(b, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(b, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_center(l);
    lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
    return b;
  };
  // Both buttons use the rotation/refresh glyph; left vs. right position
  // tells the user which way the display will turn.
  s_kb_rot_left_btn  = makeRotBtn(LV_SYMBOL_REFRESH, kbRotLeftCb);
  s_kb_rot_right_btn = makeRotBtn(LV_SYMBOL_REFRESH, kbRotRightCb);

  // Restore saved keyboard rotation preference (portrait by default).
#if defined(ESP32)
  {
    Preferences pr;
    if (pr.begin("meshTouch", true)) {
      uint8_t saved = pr.getUChar("kbrot", 0);
      pr.end();
      if (saved == LV_DISP_ROT_90 || saved == LV_DISP_ROT_270) s_kb_rotation = saved;
      else s_kb_rotation = LV_DISP_ROT_NONE;
    }
  }
#endif

  // Apply saved keyboard preferences at boot.
#if defined(ESP32)
  {
    uint16_t en_mask = touchPrefsGetEnabledLayouts();
    keyboardLayoutsSetEnabledMask(en_mask);
    uint8_t saved_layout = touchPrefsGetKeyboardLayout();
    // If the saved active layout isn't one of the enabled set any more, fall
    // back to English.
    if (saved_layout != static_cast<uint8_t>(KeyboardLayoutId::EN) &&
        !(en_mask & (1u << saved_layout))) {
      saved_layout = static_cast<uint8_t>(KeyboardLayoutId::EN);
    }
    keyboardLayoutsApply(g_lv.keyboard, static_cast<KeyboardLayoutId>(saved_layout));
  }
#endif

  // Temporary live diagnostics overlay: loop and touch-read counters.
  s_live_diag_label = lv_label_create(lv_layer_top());
  lv_obj_set_width(s_live_diag_label, 236);
  // Sit just BELOW the global status bar — that bar lives on lv_layer_sys
  // (above lv_layer_top), so a top-aligned overlay at y=2 would be hidden
  // underneath it.
  lv_obj_align(s_live_diag_label, LV_ALIGN_TOP_LEFT, 2, STATUSBAR_H + 2);
  lv_obj_set_style_text_font(s_live_diag_label, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_live_diag_label, lv_color_hex(0xC7D2DE), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_live_diag_label, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_live_diag_label, lv_color_hex(0x0A0B0C), LV_PART_MAIN);
  lv_obj_set_style_pad_hor(s_live_diag_label, 4, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(s_live_diag_label, 2, LV_PART_MAIN);
  lv_obj_set_style_radius(s_live_diag_label, 4, LV_PART_MAIN);
  lv_label_set_text(s_live_diag_label, "diag boot...");
  if (!k_show_live_diag_overlay) lv_obj_add_flag(s_live_diag_label, LV_OBJ_FLAG_HIDDEN);

#if defined(HAS_TDECK_TRACKBALL)
  // Trackball cursor: a small ring on the system layer (above the status bar and
  // modals) so it's always visible. Non-interactive — the click is delivered as
  // a touch via lvglTouchRead. Hidden until the trackball is moved/clicked.
  s_tb_cursor = lv_obj_create(lv_layer_sys());
  lv_obj_remove_style_all(s_tb_cursor);
  lv_obj_set_size(s_tb_cursor, kTbCursorDiameter, kTbCursorDiameter);
  lv_obj_set_style_radius(s_tb_cursor, kTbCursorDiameter / 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_tb_cursor, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_tb_cursor, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_tb_cursor, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(s_tb_cursor, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_border_opa(s_tb_cursor, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_tb_cursor, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(s_tb_cursor, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_tb_cursor, LV_OBJ_FLAG_HIDDEN);
  s_tb_cursor_x = lv_disp_get_hor_res(nullptr) / 2;
  s_tb_cursor_y = lv_disp_get_ver_res(nullptr) / 2;
  s_tb_target_x = s_tb_render_x = (float)s_tb_cursor_x;
  s_tb_target_y = s_tb_render_y = (float)s_tb_cursor_y;
#endif

  // ---- Initial data population ----
  refreshThreadLists();
  // Pre-build the Contacts list at boot so the first tab switch into Contacts
  // doesn't pay the rebuild cost on the foreground; the cache inside
  // refreshContactsList will keep subsequent switches free.
  refreshContactsList();
  refreshStatusLabels();

  if (g_lv.tabview) lv_tabview_set_act(g_lv.tabview, saved_tab, LV_ANIM_OFF);
  s_lv_tab_prev = static_cast<int>(saved_tab);
  if (g_lv.task) g_lv.task->onLvTabChanged(static_cast<int>(saved_tab));
  buildBootSplash();
  pushDiagLine("UI ready");
}
#endif  // HAS_TOUCH_UI

// ============================================================
// UITask method stubs (non-LVGL screen management shims)
// ============================================================
char UITask::checkDisplayOn(char c)    { return c; }
char UITask::handleLongPress(char c)   { return c; }
char UITask::handleDoubleClick(char c) { return c; }
char UITask::handleTripleClick(char c) { return c; }
void UITask::setCurrScreen(UIScreen* c) { curr = c; }
void UITask::stepComposerChar(int delta)   { _composer_char_idx  += delta; }
void UITask::stepComposerAction(int delta) { _composer_action_idx += delta; }
void UITask::userLedHandler() {}

void UITask::discoveredContact(const ContactInfo& contact, bool is_new, uint8_t path_len) {
  /* `is_new=false` → contact is already in contacts[]; nothing to do (the
   * Discovered modal only shows pending nodes that haven't been added yet).
   * On `is_new=true` we either update the existing slot for this pubkey or
   * claim a free / least-recently-used slot. */
  if (!is_new) {
    // Refresh in_contacts flag if we happen to have a stale entry for it.
    for (int i = 0; i < DISCOVERED_MAX; ++i) {
      if (s_discovered[i].used &&
          memcmp(s_discovered[i].ci.id.pub_key, contact.id.pub_key, PUB_KEY_SIZE) == 0) {
        s_discovered[i].in_contacts = true;
        break;
      }
    }
    return;
  }

  int slot = -1;
  // Match by pubkey first so repeated adverts update the same entry.
  for (int i = 0; i < DISCOVERED_MAX; ++i) {
    if (s_discovered[i].used &&
        memcmp(s_discovered[i].ci.id.pub_key, contact.id.pub_key, PUB_KEY_SIZE) == 0) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    // Free slot wins, otherwise overwrite the oldest entry.
    uint32_t oldest_ms = UINT32_MAX;
    int oldest_idx = 0;
    for (int i = 0; i < DISCOVERED_MAX; ++i) {
      if (!s_discovered[i].used) { slot = i; break; }
      if (s_discovered[i].recv_ms < oldest_ms) {
        oldest_ms = s_discovered[i].recv_ms;
        oldest_idx = i;
      }
    }
    if (slot < 0) slot = oldest_idx;
  }
  s_discovered[slot].used = true;
  s_discovered[slot].in_contacts = false;
  s_discovered[slot].path_len = path_len;
  s_discovered[slot].last_advert_ts = contact.last_advert_timestamp;
  s_discovered[slot].recv_ms = (uint32_t)millis();
  s_discovered[slot].ci = contact;
  ++s_discovered_seq;
}

void UITask::onThreadsChanged() {
  /* Force the periodic mesh-thread refresh to run on the next loop iter so
   * channels / contacts added via the companion-serial protocol show up
   * immediately instead of after the 4 s backstop. */
  _next_mesh_thread_refresh = 0;
}

void UITask::onPingReply(const ContactInfo& contact, const uint8_t* data, size_t len) {
  s_ui_ping_deadline_ms = 0;  // reply arrived, cancel timeout

  unsigned batt_mv = 0;
  unsigned uptime_s = 0;
  unsigned queue_len = 0;
  int16_t  last_rssi = 0;
  bool parsed = false;

  /* Repeaters reply with a packed binary RepeaterStats struct:
   *   u16 batt_mv, u16 queue_len, i16 noise_floor, i16 last_rssi,
   *   u32 n_recv, u32 n_sent, u32 air_secs, u32 uptime_secs, ...
   * Room servers and sensors reply with a JSON blob via StatsFormatHelper. */
  if (len >= 20) {
    batt_mv   = (unsigned)(data[0] | (data[1] << 8));
    queue_len = (unsigned)(data[2] | (data[3] << 8));
    last_rssi = (int16_t)(data[6] | (data[7] << 8));
    // uptime_secs is at offset 16 (after 2+2+2+2 + 4+4 = 16)
    uptime_s  = (unsigned)(data[16] | (data[17] << 8) | (data[18] << 16) | (data[19] << 24));
    if (batt_mv >= 2000 && batt_mv <= 5500) parsed = true;
  }
  if (!parsed) {
    // JSON fallback for non-repeater nodes
    char body[160];
    size_t copy_len = len < sizeof(body) - 1 ? len : sizeof(body) - 1;
    memcpy(body, data, copy_len);
    body[copy_len] = '\0';
    const char* p;
    if ((p = strstr(body, "\"battery_mv\":")) != nullptr) {
      sscanf(p + 13, "%u", &batt_mv);
    }
    if ((p = strstr(body, "\"uptime_secs\":")) != nullptr) {
      sscanf(p + 14, "%u", &uptime_s);
    }
    if ((p = strstr(body, "\"queue_len\":")) != nullptr) {
      sscanf(p + 12, "%u", &queue_len);
    }
    if (batt_mv > 0 || uptime_s > 0) parsed = true;
  }

  char nm[24];
  copyUtf8ReplacingMissingGlyphs(&g_font_14, nm, sizeof(nm),
                                 contact.name[0] ? contact.name : "node");
  char msg[120];
  if (parsed) {
    if (uptime_s >= 3600) {
      snprintf(msg, sizeof(msg), "%s: %u.%02uV  up %uh%02um  q=%u  rssi=%d",
               nm, batt_mv / 1000, (batt_mv % 1000) / 10,
               uptime_s / 3600, (uptime_s % 3600) / 60, queue_len, (int)last_rssi);
    } else {
      snprintf(msg, sizeof(msg), "%s: %u.%02uV  up %um  q=%u  rssi=%d",
               nm, batt_mv / 1000, (batt_mv % 1000) / 10,
               uptime_s / 60, queue_len, (int)last_rssi);
    }
  } else {
    snprintf(msg, sizeof(msg), "%s: reply (%u bytes)", nm, (unsigned)len);
  }
  showAlert(msg, 5000);
}

// Decode a CayenneLPP-formatted telemetry response into a human-readable
// toast. Walks the channels with LPPReader and concatenates whatever common
// fields the payload contains (voltage / temperature / humidity / pressure /
// GPS / etc). Unknown channels are skipped via LPPReader::skipData. The
// alert is capped at 5 s because the touch panel only has one toast slot.
void UITask::onTelemetryReply(const ContactInfo& contact, const uint8_t* data, size_t len) {
  s_ui_ping_deadline_ms = 0;  // reply arrived, cancel timeout
  char nm[24];
  copyUtf8ReplacingMissingGlyphs(&g_font_14, nm, sizeof(nm),
                                 contact.name[0] ? contact.name : "node");
  if (!data || len == 0) {
    char msg[64];
    snprintf(msg, sizeof(msg), "%s: empty telemetry", nm);
    showAlert(msg, 4000);
    return;
  }
  // Build a single short summary string. snprintf with running offset keeps
  // it bounded; once the buffer is full subsequent writes are silently
  // truncated.
  char body[160];
  int p = 0;
  const size_t body_cap = sizeof(body);
  auto bodyRoom = [&]() -> size_t {
    return (p >= 0 && (size_t)p < body_cap) ? (body_cap - (size_t)p) : 0;
  };
  bool any = false;
  // Cap len to 0xFF so LPPReader's uint8_t length doesn't truncate weirdly.
  uint8_t rd_len = (len > 250) ? 250 : (uint8_t)len;
  LPPReader rd(data, rd_len);
  uint8_t channel, type;
  while (rd.readHeader(channel, type)) {
    const char* sep = any ? "  " : "";
    switch (type) {
      case LPP_VOLTAGE: {
        float v = 0; if (!rd.readVoltage(v)) goto done;
        if (bodyRoom()) p += snprintf(body + p, bodyRoom(), "%s%.2fV", sep, (double)v);
        any = true; break;
      }
      case LPP_TEMPERATURE: {
        float t = 0; if (!rd.readTemperature(t)) goto done;
        if (bodyRoom()) p += snprintf(body + p, bodyRoom(), "%s%.1f\xc2\xb0\x43", sep, (double)t);
        any = true; break;
      }
      case LPP_RELATIVE_HUMIDITY: {
        float h = 0; if (!rd.readRelativeHumidity(h)) goto done;
        if (bodyRoom()) p += snprintf(body + p, bodyRoom(), "%s%.0f%%RH", sep, (double)h);
        any = true; break;
      }
      case LPP_BAROMETRIC_PRESSURE: {
        float bp = 0; if (!rd.readPressure(bp)) goto done;
        if (bodyRoom()) p += snprintf(body + p, bodyRoom(), "%s%.0fhPa", sep, (double)bp);
        any = true; break;
      }
      case LPP_ALTITUDE: {
        float a = 0; if (!rd.readAltitude(a)) goto done;
        if (bodyRoom()) p += snprintf(body + p, bodyRoom(), "%salt%.0fm", sep, (double)a);
        any = true; break;
      }
      case LPP_GPS: {
        float lat = 0, lon = 0, alt = 0;
        if (!rd.readGPS(lat, lon, alt)) goto done;
        if (bodyRoom()) p += snprintf(body + p, bodyRoom(), "%s%.4f,%.4f", sep, (double)lat, (double)lon);
        any = true; break;
      }
      case LPP_ANALOG_INPUT:
      case LPP_ANALOG_OUTPUT: {
        // 2 bytes signed * 0.01. Render with channel id so the operator can
        // tell apart multiple analog readings on one node.
        float a = rd.readVoltage(a) ? a : 0;  // same wire shape; reuse for parse
        // LPPReader doesn't expose an ANALOG reader, so we used readVoltage
        // (which is also 2 bytes). The math (multiplier=100, unsigned vs
        // signed) is close enough for a toast; flag the channel so users
        // know it's an analog reading.
        if (bodyRoom()) p += snprintf(body + p, bodyRoom(), "%sch%u=%.2f", sep, (unsigned)channel, (double)a);
        any = true; break;
      }
      case LPP_DIGITAL_INPUT:
      case LPP_DIGITAL_OUTPUT:
      case LPP_PRESENCE:
      case LPP_SWITCH: {
        // 1-byte boolean / state. Reads cursor manually because LPPReader
        // doesn't expose a digital reader.
        rd.skipData(type);  // walks _pos past the value (1 byte for digital)
        if (bodyRoom()) p += snprintf(body + p, bodyRoom(), "%sch%u=bin", sep, (unsigned)channel);
        any = true; break;
      }
      case LPP_PERCENTAGE: {
        rd.skipData(type);  // 1 byte
        if (bodyRoom()) p += snprintf(body + p, bodyRoom(), "%sch%u=pct", sep, (unsigned)channel);
        any = true; break;
      }
      case LPP_LUMINOSITY:
      case LPP_CONCENTRATION:
      case LPP_POWER:
      case LPP_DIRECTION:
      case LPP_CURRENT: {
        rd.skipData(type);  // 2 bytes
        if (bodyRoom()) p += snprintf(body + p, bodyRoom(), "%sch%u t=%02X", sep, (unsigned)channel, (unsigned)type);
        any = true; break;
      }
      default:
        // Unknown / unused channel type — skip its bytes so the next header
        // parses correctly. If skipData walks past the buffer, the next
        // readHeader returns false and we exit.
        rd.skipData(type);
        break;
    }
    if (p > 0 && (size_t)p >= body_cap - 1) break;   // body buffer full
  }
done: {
  // Always also include a short hex dump so we can compare what the
  // decoder produced against the raw wire bytes. Helps spot wrap/offset
  // errors and unknown LPP types in one shot.
  char hex[40];
  int hp = 0;
  const int dump = (len < 12) ? (int)len : 12;
  for (int i = 0; i < dump && hp + 3 < (int)sizeof(hex); ++i) {
    hp += snprintf(hex + hp, sizeof(hex) - (size_t)hp, "%02X ", data[i]);
  }
  if (hp > 0 && hex[hp - 1] == ' ') hex[hp - 1] = '\0';
  char msg[240];
  if (any) {
    snprintf(msg, sizeof(msg), "%s: %s\n[%uB] %s",
             nm, body, (unsigned)len, hex);
  } else {
    snprintf(msg, sizeof(msg), "%s: %uB %s",
             nm, (unsigned)len, hex);
  }
  showAlert(msg, 7000);
}
}

void UITask::onAdminLoginResult(const ContactInfo& contact, bool success, uint8_t perms) {
  // Only react if the user opened a login prompt against THIS contact —
  // otherwise this could be a stale login from the companion-serial side
  // (the web app) bleeding into our touch flow.
  if (memcmp(contact.id.pub_key, s_admin_pub32, 32) != 0) return;
#if defined(ESP32)
  // Save (or clear) the remembered password based on the Remember
  // checkbox snapshot. We do this on success only — saving a known-bad
  // password would lock the operator out of the prefill optimization.
  if (success && s_admin_pw_remember_flag) {
    touchPrefsSetRepeaterPassword(contact.id.pub_key, s_admin_pw_attempt);
  } else if (success && !s_admin_pw_remember_flag) {
    // Box unchecked at submit time: ensure any previously-remembered
    // password is cleared, so toggling Remember off has the obvious
    // semantics.
    touchPrefsSetRepeaterPassword(contact.id.pub_key, "");
  }
  // Don't leave the password in RAM longer than needed.
  memset(s_admin_pw_attempt, 0, sizeof(s_admin_pw_attempt));
  s_admin_pw_remember_flag = false;
#endif
  if (success) {
    closeAdminPwPrompt();
    if (s_room_join_idx >= 0 && contact.type == ADV_TYPE_ROOM) {
      // Joined a room server: drop the operator straight into the room chat.
      // The server now syncs history (sync_since) and pushes new room
      // messages, which land in the normal conversation view.
      const int idx = s_room_join_idx;
      s_room_join_idx = -1;
      char msg[80];
      snprintf(msg, sizeof(msg), "Joined %.40s", contact.name);
      showAlert(msg, 1400);
      openMeshContactDm((uint32_t)idx);
    } else {
      // Repeater admin login → open the CLI console.
      s_room_join_idx = -1;
      openAdminConsole(contact);
      char msg[80];
      snprintf(msg, sizeof(msg), "Login OK (perms %u)", (unsigned)perms);
      showAlert(msg, 1200);
    }
  } else {
    s_room_join_idx = -1;
    closeAdminPwPrompt();
    showAlert("Login failed", 2000);
  }
}

void UITask::onAdminCommandReply(const ContactInfo& contact, const char* text) {
  // Only append if the reply is from the contact we're currently logged
  // into. Replies from a different repeater would be confusing — the
  // companion-serial side handles those separately via queueMessage.
  if (memcmp(contact.id.pub_key, s_admin_pub32, 32) != 0) return;
  adminLogAppend("", text ? text : "");
}

void UITask::markHistoryDirty(unsigned long delay_ms) {
  _history_dirty = true;
  _next_history_flush_ms = millis() + delay_ms;
}

void UITask::flushHistoryIfDue(unsigned long now) {
  // Synchronous, prompt save — same as the released build, which persisted
  // reliably (a hardware reset shortly after a message keeps the chat). The
  // save runs ~delay after each change (markHistoryDirty) and writes from
  // internal RAM, so it's quick. (The async-to-worker experiment widened the
  // reset-loss window and starved the LOS worker — reverted.)
  if (!_history_dirty) return;
  if (now < _next_history_flush_ms) return;
  if (saveHistoryToStorage()) _history_dirty = false;
  else _next_history_flush_ms = now + 2000;
}

bool UITask::loadHistoryFromStorage() {
#if defined(ESP32)
  if (!SPIFFS.begin(false)) return false;
  File f = SPIFFS.open(k_ui_history_path, "r");
  if (!f) return false;

  UiHistoryHeader hdr{};
  if (f.readBytes(reinterpret_cast<char*>(&hdr), sizeof(hdr)) != static_cast<int>(sizeof(hdr))) {
    f.close();
    return false;
  }
  if (hdr.magic != k_ui_history_magic ||
      hdr.version < k_ui_history_min_version || hdr.version > k_ui_history_version ||
      hdr.ui_msg_count > MAX_UI_MESSAGES || hdr.ui_msg_head >= MAX_UI_MESSAGES) {
    f.close();
    return false;
  }

  // On-disk record sizes. v5+ stores them so a blob written by an older build
  // (shorter records — fields appended since) still loads: we read the stored
  // size and leave the appended tail zeroed. A v4 blob predates these fields
  // but shares the current record layout, so fall back to sizeof(). Reject
  // absurd sizes from a corrupt blob.
  const size_t disk_thread_sz =
      (hdr.version >= 5 && hdr.thread_rec_size) ? hdr.thread_rec_size : sizeof(UiHistoryThread);
  const size_t disk_msg_sz =
      (hdr.version >= 5 && hdr.msg_rec_size) ? hdr.msg_rec_size : sizeof(UiHistoryMsg);
  if (disk_thread_sz == 0 || disk_thread_sz > 4096 ||
      disk_msg_sz == 0 || disk_msg_sz > 4096) {
    f.close();
    return false;
  }

  // Read one on-disk record into a zero-initialized current-layout struct:
  // keep min(disk_sz, cur_sz) bytes, skip any surplus tail of a newer record.
  // Older blobs leave the appended tail zeroed; newer blobs are truncated to
  // what this build understands.
  auto readRec = [&](void* dst, size_t cur_sz, size_t disk_sz) -> bool {
    memset(dst, 0, cur_sz);
    const size_t take = disk_sz < cur_sz ? disk_sz : cur_sz;
    if (f.readBytes(reinterpret_cast<char*>(dst), take) != static_cast<int>(take)) return false;
    if (disk_sz > cur_sz && !f.seek(f.position() + (disk_sz - cur_sz))) return false;
    return true;
  };

  UiHistoryThread t{};
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    if (!readRec(&t, sizeof(t), disk_thread_sz)) {
      f.close();
      return false;
    }
    _ui_threads[i].used = t.used != 0;
    _ui_threads[i].channel = t.channel != 0;
    _ui_threads[i].unread = t.unread;
    _ui_threads[i].last_ts = t.last_ts;
    _ui_threads[i].mesh_contact_idx = t.mesh_contact_idx;
    memcpy(_ui_threads[i].mesh_contact_pub, t.mesh_contact_pub, sizeof(t.mesh_contact_pub));
    memcpy(_ui_threads[i].mesh_contact_key6, t.mesh_contact_key6, sizeof(t.mesh_contact_key6));
    _ui_threads[i].mesh_channel_slot = t.mesh_channel_slot;
    strncpy(_ui_threads[i].name, t.name, MAX_THREAD_NAME);
    _ui_threads[i].name[MAX_THREAD_NAME] = '\0';
  }

  UiHistoryMsg m{};
  for (int i = 0; i < MAX_UI_MESSAGES; ++i) {
    if (!readRec(&m, sizeof(m), disk_msg_sz)) {
      f.close();
      return false;
    }
    _ui_msgs[i].ts = m.ts;
    _ui_msgs[i].channel = m.channel != 0;
    _ui_msgs[i].outgoing = m.outgoing != 0;
    _ui_msgs[i].meta_flags = m.meta_flags;
    _ui_msgs[i].path_len   = m.path_len;
    _ui_msgs[i].snr_q4     = m.snr_q4;
    _ui_msgs[i].rssi       = m.rssi;
    strncpy(_ui_msgs[i].thread, m.thread, MAX_THREAD_NAME);
    _ui_msgs[i].thread[MAX_THREAD_NAME] = '\0';
    strncpy(_ui_msgs[i].sender, m.sender, MAX_SENDER_NAME);
    _ui_msgs[i].sender[MAX_SENDER_NAME] = '\0';
    strncpy(_ui_msgs[i].text, m.text, MAX_MSG_TEXT);
    _ui_msgs[i].text[MAX_MSG_TEXT] = '\0';
  }
  f.close();

  _ui_msg_count = hdr.ui_msg_count;
  _ui_msg_head = hdr.ui_msg_head;
  _msgcount = static_cast<int>(hdr.msgcount);
  _active_thread_idx = hdr.active_thread_idx;
  _active_thread_is_channel = hdr.active_thread_is_channel != 0;
  if (_active_thread_idx < 0 || _active_thread_idx >= MAX_UI_THREADS ||
      !_ui_threads[_active_thread_idx].used) {
    _active_thread_idx = -1;
    _active_thread_is_channel = false;
  }
  return true;
#else
  return false;
#endif
}

bool UITask::saveHistoryToStorage() {
#if defined(ESP32)
  // Write from internal-RAM stack structs (fast). NOTE: do NOT "optimize"
  // this into a single big write from a PSRAM buffer — the flash driver has
  // to bounce a PSRAM source through internal RAM chunk-by-chunk, which is
  // SLOWER than these per-struct writes and was a real regression. Keep the
  // write synchronous + prompt (called right after each message) so a
  // hardware reset doesn't lose the chat — that's what the released build did
  // and it was reliable.
  if (!SPIFFS.begin(false)) return false;
  WdtHeavyGuard _wg;   // a fragmenting history write can trigger a multi-second SPIFFS GC
  File f = SPIFFS.open(k_ui_history_path, "w");
  if (!f) return false;

  UiHistoryHeader hdr{};
  hdr.magic = k_ui_history_magic;
  hdr.version = k_ui_history_version;
  hdr.thread_rec_size = static_cast<uint16_t>(sizeof(UiHistoryThread));
  hdr.msg_rec_size = static_cast<uint16_t>(sizeof(UiHistoryMsg));
  hdr.ui_msg_count = static_cast<uint16_t>(_ui_msg_count);
  hdr.ui_msg_head = static_cast<uint16_t>(_ui_msg_head);
  hdr.msgcount = static_cast<uint32_t>(_msgcount);
  hdr.active_thread_idx = static_cast<int16_t>(_active_thread_idx);
  hdr.active_thread_is_channel = _active_thread_is_channel ? 1u : 0u;
  if (f.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) {
    f.close();
    return false;
  }

  UiHistoryThread t{};
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    memset(&t, 0, sizeof(t));
    t.used = _ui_threads[i].used ? 1u : 0u;
    t.channel = _ui_threads[i].channel ? 1u : 0u;
    t.unread = _ui_threads[i].unread;
    t.last_ts = _ui_threads[i].last_ts;
    t.mesh_contact_idx = _ui_threads[i].mesh_contact_idx;
    memcpy(t.mesh_contact_pub, _ui_threads[i].mesh_contact_pub, sizeof(t.mesh_contact_pub));
    memcpy(t.mesh_contact_key6, _ui_threads[i].mesh_contact_key6, sizeof(t.mesh_contact_key6));
    t.mesh_channel_slot = _ui_threads[i].mesh_channel_slot;
    strncpy(t.name, _ui_threads[i].name, MAX_THREAD_NAME);
    t.name[MAX_THREAD_NAME] = '\0';
    if (f.write(reinterpret_cast<const uint8_t*>(&t), sizeof(t)) != sizeof(t)) {
      f.close();
      return false;
    }
  }

  UiHistoryMsg m{};
  for (int i = 0; i < MAX_UI_MESSAGES; ++i) {
    memset(&m, 0, sizeof(m));
    m.ts = _ui_msgs[i].ts;
    m.channel = _ui_msgs[i].channel ? 1u : 0u;
    m.outgoing = _ui_msgs[i].outgoing ? 1u : 0u;
    m.meta_flags = _ui_msgs[i].meta_flags;
    m.path_len   = _ui_msgs[i].path_len;
    m.snr_q4     = _ui_msgs[i].snr_q4;
    m.rssi       = _ui_msgs[i].rssi;
    strncpy(m.thread, _ui_msgs[i].thread, MAX_THREAD_NAME);
    m.thread[MAX_THREAD_NAME] = '\0';
    strncpy(m.sender, _ui_msgs[i].sender, MAX_SENDER_NAME);
    m.sender[MAX_SENDER_NAME] = '\0';
    strncpy(m.text, _ui_msgs[i].text, MAX_MSG_TEXT);
    m.text[MAX_MSG_TEXT] = '\0';
    if (f.write(reinterpret_cast<const uint8_t*>(&m), sizeof(m)) != sizeof(m)) {
      f.close();
      return false;
    }
  }
  f.close();
  return true;
#else
  return false;
#endif
}

// ============================================================
// Thread management
// ============================================================
int UITask::findOrCreateThread(const char* name, bool channel) {
  if (!name || !name[0]) return -1;
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    if (_ui_threads[i].used && _ui_threads[i].channel == channel &&
        strncmp(_ui_threads[i].name, name, MAX_THREAD_NAME) == 0) return i;
  }
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    if (_ui_threads[i].used) continue;
    _ui_threads[i].used              = true;
    _ui_threads[i].channel           = channel;
    _ui_threads[i].mesh_contact_idx  = -1;
    memset(_ui_threads[i].mesh_contact_pub, 0, sizeof(_ui_threads[i].mesh_contact_pub));
    memset(_ui_threads[i].mesh_contact_key6, 0, sizeof(_ui_threads[i].mesh_contact_key6));
    _ui_threads[i].mesh_channel_slot = -1;
    _ui_threads[i].unread            = 0;
    _ui_threads[i].has_mention       = false;
    _ui_threads[i].last_ts           = millis();
    strncpy(_ui_threads[i].name, name, MAX_THREAD_NAME);
    _ui_threads[i].name[MAX_THREAD_NAME] = '\0';
    return i;
  }
  return -1;
}

bool UITask::removeThread(int idx) {
  if (idx < 0 || idx >= MAX_UI_THREADS) return false;
  if (!_ui_threads[idx].used) return false;
  // Match the thread by stored name (channel/DM) before wiping the slot so
  // we can drop any ring messages that belonged to it. Without this the
  // entries would still occupy the bounded _ui_msgs ring and confuse a
  // future thread of the same name.
  const bool was_channel = _ui_threads[idx].channel;
  char name_copy[MAX_THREAD_NAME + 1];
  strncpy(name_copy, _ui_threads[idx].name, MAX_THREAD_NAME);
  name_copy[MAX_THREAD_NAME] = '\0';

  // Walk the ring and clear messages keyed to this thread name + kind.
  for (int i = 0; i < MAX_UI_MESSAGES; ++i) {
    if (_ui_msgs[i].channel != (was_channel ? 1u : 0u)) continue;
    if (strncmp(_ui_msgs[i].thread, name_copy, MAX_THREAD_NAME) != 0) continue;
    _ui_msgs[i].thread[0] = '\0';
    _ui_msgs[i].text[0]   = '\0';
    _ui_msgs[i].sender[0] = '\0';
  }

  _ui_threads[idx] = UIThread{};
  _ui_threads[idx].used = false;
  _ui_threads[idx].mesh_contact_idx  = -1;
  _ui_threads[idx].mesh_channel_slot = -1;
  if (_active_thread_idx == idx) _active_thread_idx = -1;
  // Flush immediately. markHistoryDirty(200) would normally be lazy, and
  // flushHistoryIfDue further defers writes by 2 s while the user is on the
  // chat inbox — so if they reboot in that window, the on-disk thread list
  // still contained the deleted thread and the channel/DM came back.
  if (!saveHistoryToStorage()) {
    _history_dirty = true;
    _next_history_flush_ms = millis() + 200;
  } else {
    _history_dirty = false;
  }
  return true;
}

bool UITask::looksLikeKnownChannel(const char* name) const {
  if (!name || !name[0]) return false;
  return name[0] == '#' || strncmp(name, "ch:", 3) == 0 || strncmp(name, "chan:", 5) == 0;
}

void UITask::refreshThreadsFromMesh() {
  // Reclaim DM thread slots that have no message history. These were created
  // by an earlier auto-link pass for every known contact, but they bloat the
  // 48-slot thread table and starve newly-added channels (which are far
  // fewer). DMs with actual history stay; new DM threads are created on
  // demand when openMeshContactDm() runs or a message arrives.
  //
  // EXCEPTION: never wipe the currently-active thread. The user might have
  // it open and be typing into it — wiping mid-compose means the next Send
  // tap sees `_ui_threads[_active_thread_idx].name` empty, appendMessage's
  // findOrCreateThread("") returns -1, and both the bubble and the chat
  // list silently drop the message. This was the "send-from-map-marker
  // doesn't track messages" bug.
  for (int t = 0; t < MAX_UI_THREADS; ++t) {
    if (t == _active_thread_idx) continue;
    if (_ui_threads[t].used && !_ui_threads[t].channel && !threadHasMessageHistory(t)) {
      _ui_threads[t].used = false;
      _ui_threads[t].name[0] = '\0';
      _ui_threads[t].mesh_contact_idx = -1;
      memset(_ui_threads[t].mesh_contact_pub,  0, sizeof(_ui_threads[t].mesh_contact_pub));
      memset(_ui_threads[t].mesh_contact_key6, 0, sizeof(_ui_threads[t].mesh_contact_key6));
    }
  }
  for (int t = 0; t < MAX_UI_THREADS; ++t) {
    if (_ui_threads[t].used && !_ui_threads[t].channel &&
        (hasContactPub(_ui_threads[t].mesh_contact_pub) || hasContactKey6(_ui_threads[t].mesh_contact_key6))) {
      _ui_threads[t].mesh_contact_idx = -1;
    }
  }

  ContactInfo c;
  for (int i = 0; i < the_mesh.getNumContacts(); ++i) {
    if (!the_mesh.getContactByIdx(static_cast<uint32_t>(i), c) || !c.name[0]) continue;
    int t = -1;
    for (int k = 0; k < MAX_UI_THREADS; ++k) {
      if (!_ui_threads[k].used || _ui_threads[k].channel) continue;
      if (hasContactPub(_ui_threads[k].mesh_contact_pub) &&
          memcmp(_ui_threads[k].mesh_contact_pub, c.id.pub_key, PUB_KEY_SIZE) == 0) {
        t = k;
        break;
      }
      if (!hasContactPub(_ui_threads[k].mesh_contact_pub) &&
          hasContactKey6(_ui_threads[k].mesh_contact_key6) &&
          memcmp(_ui_threads[k].mesh_contact_key6, c.id.pub_key, 6) == 0) {
        t = k;
        break;
      }
    }
    if (t < 0) {
      const int by_name = findThreadByName(c.name, false);
      if (by_name >= 0) {
        // If a same-name thread is already pinned to a different key, do not
        // overwrite it here. This avoids DM thread/key drift on duplicate names.
        if (hasContactKey6(_ui_threads[by_name].mesh_contact_key6) &&
            memcmp(_ui_threads[by_name].mesh_contact_key6, c.id.pub_key, 6) != 0) {
          continue;
        }
        t = by_name;
      }
      // No matching thread for this contact — don't pre-allocate one. A
      // thread gets created lazily by openMeshContactDm() / appendMessage()
      // when the user actually starts a conversation. Pre-allocating burns
      // slots that channels can no longer claim.
    }
    if (t >= 0) {
      _ui_threads[t].mesh_contact_idx = static_cast<int16_t>(i);
      memcpy(_ui_threads[t].mesh_contact_pub, c.id.pub_key, sizeof(_ui_threads[t].mesh_contact_pub));
      memcpy(_ui_threads[t].mesh_contact_key6, c.id.pub_key, sizeof(_ui_threads[t].mesh_contact_key6));
    }
  }
#ifdef MAX_GROUP_CHANNELS
  ChannelDetails cd;
  for (int i = 0; i < MAX_GROUP_CHANNELS; ++i) {
    if (!the_mesh.getChannel(i, cd)) continue;
    if (!cd.name[0]) continue;
    int t = findOrCreateThread(cd.name, true);
    if (t >= 0) {
      _ui_threads[t].mesh_channel_slot = static_cast<int16_t>(i);
    }
  }
#endif
}

int UITask::findThreadByName(const char* name, bool channel) const {
  if (!name || !name[0]) return -1;
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    if (_ui_threads[i].used && _ui_threads[i].channel == channel &&
        strncmp(_ui_threads[i].name, name, MAX_THREAD_NAME) == 0) return i;
  }
  return -1;
}

void UITask::syncThreadMeshSlots(const char* thread_name, bool channel) {
  int t = findThreadByName(thread_name, channel);
  if (t < 0) return;
  if (channel) {
    _ui_threads[t].mesh_channel_slot = -1;
#ifdef MAX_GROUP_CHANNELS
    ChannelDetails cd;
    for (int i = 0; i < MAX_GROUP_CHANNELS; ++i) {
      if (the_mesh.getChannel(i, cd) && cd.name[0] &&
          strncmp(cd.name, thread_name, MAX_THREAD_NAME) == 0) {
        _ui_threads[t].mesh_channel_slot = static_cast<int16_t>(i);
        break;
      }
    }
#endif
  } else {
    _ui_threads[t].mesh_contact_idx = -1;
    const bool thread_has_pub = hasContactPub(_ui_threads[t].mesh_contact_pub);
    const bool thread_has_key = hasContactKey6(_ui_threads[t].mesh_contact_key6);
    ContactInfo c2;
    if (thread_has_pub) {
      for (int i = 0; i < the_mesh.getNumContacts(); ++i) {
        if (!the_mesh.getContactByIdx(static_cast<uint32_t>(i), c2)) continue;
        if (memcmp(c2.id.pub_key, _ui_threads[t].mesh_contact_pub, PUB_KEY_SIZE) == 0) {
          _ui_threads[t].mesh_contact_idx = static_cast<int16_t>(i);
          memcpy(_ui_threads[t].mesh_contact_key6, c2.id.pub_key, sizeof(_ui_threads[t].mesh_contact_key6));
          return;
        }
      }
      return;
    }
    if (thread_has_key) {
      for (int i = 0; i < the_mesh.getNumContacts(); ++i) {
        if (!the_mesh.getContactByIdx(static_cast<uint32_t>(i), c2)) continue;
        if (memcmp(c2.id.pub_key, _ui_threads[t].mesh_contact_key6, 6) == 0) {
          _ui_threads[t].mesh_contact_idx = static_cast<int16_t>(i);
          memcpy(_ui_threads[t].mesh_contact_pub, c2.id.pub_key, sizeof(_ui_threads[t].mesh_contact_pub));
          return;
        }
      }
      // Keep keyed binding pinned even if currently unresolved in contact list.
      return;
    }
    memset(_ui_threads[t].mesh_contact_pub, 0, sizeof(_ui_threads[t].mesh_contact_pub));
    for (int i = 0; i < the_mesh.getNumContacts(); ++i) {
      if (the_mesh.getContactByIdx(static_cast<uint32_t>(i), c2) &&
          strncmp(c2.name, thread_name, MAX_THREAD_NAME) == 0) {
        _ui_threads[t].mesh_contact_idx = static_cast<int16_t>(i);
        memcpy(_ui_threads[t].mesh_contact_pub, c2.id.pub_key, sizeof(_ui_threads[t].mesh_contact_pub));
        memcpy(_ui_threads[t].mesh_contact_key6, c2.id.pub_key, sizeof(_ui_threads[t].mesh_contact_key6));
        break;
      }
    }
  }
}

bool UITask::lookupActiveContact(ContactInfo& out) const {
  if (_active_thread_idx < 0 || _active_thread_idx >= MAX_UI_THREADS) return false;
  if (_active_dm_contact_set) {
    for (int i = 0; i < the_mesh.getNumContacts(); ++i) {
      if (the_mesh.getContactByIdx(static_cast<uint32_t>(i), out) &&
          memcmp(out.id.pub_key, _active_dm_contact_pub, PUB_KEY_SIZE) == 0) return true;
    }
  }
  const UIThread& th = _ui_threads[_active_thread_idx];
  const bool has_pub = hasContactPub(th.mesh_contact_pub);
  const bool has_key6 = hasContactKey6(th.mesh_contact_key6);
  if (th.mesh_contact_idx >= 0 &&
      the_mesh.getContactByIdx(static_cast<uint32_t>(th.mesh_contact_idx), out)) {
    if ((has_pub && memcmp(out.id.pub_key, th.mesh_contact_pub, PUB_KEY_SIZE) == 0) ||
        (!has_pub && (!has_key6 || memcmp(out.id.pub_key, th.mesh_contact_key6, 6) == 0))) return true;
  }
  if (has_pub) {
    for (int i = 0; i < the_mesh.getNumContacts(); ++i) {
      if (the_mesh.getContactByIdx(static_cast<uint32_t>(i), out) &&
          memcmp(out.id.pub_key, th.mesh_contact_pub, PUB_KEY_SIZE) == 0) return true;
    }
  }
  if (has_key6) {
    for (int i = 0; i < the_mesh.getNumContacts(); ++i) {
      if (the_mesh.getContactByIdx(static_cast<uint32_t>(i), out) &&
          memcmp(out.id.pub_key, th.mesh_contact_key6, 6) == 0) return true;
    }
    // Fallback for stale keyed threads after contact list churn.
    // Keep this enabled so messaging doesn't hard-fail while diagnostics are active.
  }
  // Do not fall back to name-only matching for DM sends.
  // Name collisions (or stale history-only threads) can encrypt to the wrong peer key,
  // which appears as undecodable gibberish on the receiver.
  return false;
}

int UITask::appendMessage(const char* thread, const char* sender, const char* text,
                          bool channel, bool outgoing, bool mark_unread,
                          uint32_t ack_hash, uint8_t deliv_state,
                          uint8_t meta_flags, uint8_t path_len, int8_t snr_q4, int8_t rssi,
                          const uint8_t* in_path, uint8_t in_path_n, uint32_t sent_fp,
                          uint16_t in_scope) {
  int t_idx = findOrCreateThread(thread, channel);
  if (t_idx < 0) return -1;
  UIMessage& m = _ui_msgs[_ui_msg_head];
  // Prefer wall-clock (RTC) over uptime — bubble timestamps want HH:MM, and
  // last_ts/sort comparisons still work since RTC epoch (~1.7e9) outranks
  // any plausible uptime-seconds value. If RTC is unset, fall back to
  // uptime; the Info popup shows "—" when the value isn't an epoch.
  {
    auto* rtc = the_mesh.getRTCClock();
    uint32_t now_epoch = rtc ? rtc->getCurrentTime() : 0;
    m.ts = (now_epoch > 1700000000) ? now_epoch : (uint32_t)(millis() / 1000);
  }
  m.channel = channel;
  m.outgoing = outgoing;
  m.ack_hash    = ack_hash;
  m.deliv_state = deliv_state;
  m.meta_flags  = meta_flags;
  m.path_len    = path_len;
  m.snr_q4      = snr_q4;
  m.rssi        = rssi;
  m.sent_fp     = sent_fp;
  m.in_scope    = in_scope;
  m.in_path_n   = (in_path && in_path_n) ? (in_path_n > MAX_UI_PATH ? (uint8_t)MAX_UI_PATH : in_path_n) : 0;
  if (m.in_path_n) memcpy(m.in_path, in_path, m.in_path_n);
  strncpy(m.thread, thread ? thread : (channel ? "#general" : "Unknown"), MAX_THREAD_NAME);
  m.thread[MAX_THREAD_NAME] = '\0';
  strncpy(m.sender, sender ? sender : (_node_prefs ? _node_prefs->node_name : "me"), MAX_SENDER_NAME);
  m.sender[MAX_SENDER_NAME] = '\0';
  strncpy(m.text, text ? text : "", MAX_MSG_TEXT);
  m.text[MAX_MSG_TEXT] = '\0';
  if (_ui_msg_count < MAX_UI_MESSAGES) ++_ui_msg_count;
  _ui_msg_head = (_ui_msg_head + 1) % MAX_UI_MESSAGES;
  _ui_threads[t_idx].last_ts = m.ts;
  if (mark_unread) _ui_threads[t_idx].unread++;
  if (mark_unread && channel && !outgoing && textMentionsMe(text)) {
    _ui_threads[t_idx].has_mention = true;   // blue @ on the inbox row
  }
  ++_msgcount;
  markHistoryDirty();
  return t_idx;
}

void UITask::onMessageAcked(uint32_t ack_hash) {
  if (ack_hash == 0) return;
  bool any = false;
  for (int i = 0; i < MAX_UI_MESSAGES; ++i) {
    UIMessage& m = _ui_msgs[i];
    if (!m.outgoing || m.channel) continue;
    if (m.ack_hash == 0) continue;
    if (m.ack_hash == ack_hash && m.deliv_state != DELIV_DELIVERED) {
      m.deliv_state = DELIV_DELIVERED;
      any = true;
    }
  }
#if defined(HAS_TOUCH_UI)
  if (any) g_lv.dirty_timeline = true;
#else
  (void)any;
#endif
}

void UITask::onTracePingResult(uint32_t tag, int8_t their_snr, int8_t our_snr,
                               uint8_t extra_hops, const int8_t* extra_snrs) {
  (void)tag;
  // SNR values come from the wire as int8_t * 4. Convert to floating-point
  // dB just for display so a user reading "-3.25 dB" understands the scale
  // (negative SNR is below the noise floor — typical for chirp-spread LoRa).
  const float their_dB = ((float)their_snr) / 4.0f;
  const float our_dB   = ((float)our_snr)   / 4.0f;
  // Route trace (from the message Info popup): render the FULL hop list in a
  // dismissable popup, one line per hop's RX SNR, instead of the compact toast.
  if (s_trace_route_pending) {
    s_trace_route_pending = false;
    char body[256];
    int p = 0;
    p += snprintf(body + p, sizeof(body) - p, "Per-hop RX SNR\n");
    p += snprintf(body + p, sizeof(body) - p, "Hop 1   %+.1f dB", (double)their_dB);
    for (int i = 0; i < extra_hops && extra_snrs && p < (int)sizeof(body); ++i) {
      const float dB = ((float)extra_snrs[i]) / 4.0f;
      p += snprintf(body + p, sizeof(body) - p, "\nHop %d   %+.1f dB", i + 2, (double)dB);
    }
    snprintf(body + p, sizeof(body) - p, "\nReturn  %+.1f dB  (us)", (double)our_dB);
    openTraceResultPopup("Trace route", body);
    return;
  }
  char msg[96];
  if (extra_hops == 0 || !extra_snrs) {
    snprintf(msg, sizeof(msg),
             "Trace: them %.1fdB \xe2\x86\x90 us %.1fdB",
             (double)their_dB, (double)our_dB);
  } else {
    // Show extra hops compactly. Cap to 2 to fit a single toast line.
    char hops[32] = {0};
    int p = 0;
    int n = (extra_hops < 2) ? extra_hops : 2;
    for (int i = 0; i < n; ++i) {
      const float dB = ((float)extra_snrs[i]) / 4.0f;
      p += snprintf(hops + p, sizeof(hops) - (size_t)p, " %+.1f", (double)dB);
    }
    snprintf(msg, sizeof(msg),
             "Trace: them %.1f us %.1f hops%s",
             (double)their_dB, (double)our_dB, hops);
  }
  showAlert(msg, 5000);
}

void UITask::sortThreadsByRecent(bool channel_mode, int out_indexes[], int& out_count) const {
  out_count = 0;
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    if (_ui_threads[i].used && _ui_threads[i].channel == channel_mode)
      out_indexes[out_count++] = i;
  }
  for (int i = 0; i < out_count; ++i) {
    for (int j = i + 1; j < out_count; ++j) {
      if (_ui_threads[out_indexes[j]].last_ts > _ui_threads[out_indexes[i]].last_ts) {
        int tmp = out_indexes[i]; out_indexes[i] = out_indexes[j]; out_indexes[j] = tmp;
      }
    }
  }
}

int UITask::getThreadMessageIndexes(int thread_idx, int out_indexes[], int max_out, bool newest_first) const {
  if (thread_idx < 0 || thread_idx >= MAX_UI_THREADS || !_ui_threads[thread_idx].used) return 0;
  int n = 0;
  for (int i = 0; i < _ui_msg_count && n < max_out; ++i) {
    int idx = (_ui_msg_head - 1 - i + MAX_UI_MESSAGES) % MAX_UI_MESSAGES;
    const UIMessage& m = _ui_msgs[idx];
    if (strncmp(m.thread, _ui_threads[thread_idx].name, MAX_THREAD_NAME) == 0 &&
        m.channel == _ui_threads[thread_idx].channel)
      out_indexes[n++] = idx;
  }
  if (!newest_first) {
    for (int i = 0; i < n / 2; ++i) {
      int tmp = out_indexes[i];
      out_indexes[i] = out_indexes[n - 1 - i];
      out_indexes[n - 1 - i] = tmp;
    }
  }
  return n;
}

void UITask::setActiveThread(int idx, bool channel_mode) {
  if (idx < 0 || idx >= MAX_UI_THREADS || !_ui_threads[idx].used) {
    _active_thread_idx = -1;
    _active_dm_contact_set = false;
    memset(_active_dm_contact_pub, 0, sizeof(_active_dm_contact_pub));
    return;
  }
  _active_thread_idx         = idx;
  _active_thread_is_channel  = channel_mode;
  const bool was_unread = (_ui_threads[idx].unread != 0) || _ui_threads[idx].has_mention;
  _ui_threads[idx].unread    = 0;
  _ui_threads[idx].has_mention = false;
  if (was_unread) markHistoryDirty(200);   // persist the read state (survives a manual reboot)
  if (channel_mode) {
    _active_dm_contact_set = false;
    memset(_active_dm_contact_pub, 0, sizeof(_active_dm_contact_pub));
  } else {
    const UIThread& th = _ui_threads[idx];
    if (hasContactPub(th.mesh_contact_pub)) {
      _active_dm_contact_set = true;
      memcpy(_active_dm_contact_pub, th.mesh_contact_pub, sizeof(_active_dm_contact_pub));
    } else if (th.mesh_contact_idx >= 0) {
      ContactInfo c{};
      if (the_mesh.getContactByIdx(static_cast<uint32_t>(th.mesh_contact_idx), c)) {
        _active_dm_contact_set = true;
        memcpy(_active_dm_contact_pub, c.id.pub_key, sizeof(_active_dm_contact_pub));
      } else {
        _active_dm_contact_set = false;
        memset(_active_dm_contact_pub, 0, sizeof(_active_dm_contact_pub));
      }
    } else {
      _active_dm_contact_set = false;
      memset(_active_dm_contact_pub, 0, sizeof(_active_dm_contact_pub));
    }
  }
  // Do NOT force a history save here. Opening a chat only clears the unread
  // count + sets the active thread — it changes no message content, so a
  // full SPIFFS write on every chat-open is pure overhead and was the
  // "freeze when opening a chat" hitch. The unread-clear persists on the
  // next message save or on reboot; message content already persists on
  // send/receive (appendMessage -> markHistoryDirty).
}

void UITask::resetComposer() {
  _compose_buf[0]      = '\0';
  _composer_char_idx   = 0;
  _composer_action_idx = 0;
}

void UITask::appendComposerChar(char c) {
  size_t n = strlen(_compose_buf);
  if (n + 1 >= sizeof(_compose_buf)) return;
  _compose_buf[n]     = c;
  _compose_buf[n + 1] = '\0';
}

void UITask::appendComposerText(const char* text) {
  if (!text) return;
  size_t n = strlen(_compose_buf);
  size_t tlen = strlen(text);
  if (n + tlen >= sizeof(_compose_buf)) return;
  memcpy(_compose_buf + n, text, tlen + 1);
}

void UITask::backspaceComposerChar() {
  size_t n = strlen(_compose_buf);
  if (n == 0) return;
  size_t i = n;
  while (i > 0 && ((unsigned char)_compose_buf[i - 1] & 0xC0) == 0x80) --i;
  if (i > 0) --i;
  _compose_buf[i] = '\0';
}

bool UITask::sendComposerToActiveThread() {
  static uint32_t s_last_ui_tx_ts = 0;
  static uint8_t s_ui_tx_attempt = 4;
  if (_active_thread_idx < 0 || !_compose_buf[0]) return false;
  const char* sender = (_node_prefs && _node_prefs->node_name[0]) ? _node_prefs->node_name : "me";
  const char* text   = _compose_buf;
  size_t tlen = strlen(text);
  if (tlen > MAX_TEXT_LEN) tlen = MAX_TEXT_LEN;
  char truncated[MAX_TEXT_LEN + 1];
  memcpy(truncated, text, tlen);
  truncated[tlen] = '\0';

  if (_active_thread_is_channel) {
    int16_t slot = _ui_threads[_active_thread_idx].mesh_channel_slot;
    ChannelDetails cd;
    if (slot < 0 || !the_mesh.getChannel(slot, cd) || !cd.name[0]) {
      syncThreadMeshSlots(_ui_threads[_active_thread_idx].name, true);
      slot = _ui_threads[_active_thread_idx].mesh_channel_slot;
    }
    if (slot < 0 || !the_mesh.getChannel(slot, cd) || !cd.name[0]) {
      showAlert("Channel not found", 1500);
      return false;
    }
    uint32_t ts = the_mesh.getRTCClock()->getCurrentTimeUnique();
    if (ts <= s_last_ui_tx_ts) ts = s_last_ui_tx_ts + 1;
    s_last_ui_tx_ts = ts;
    {
      char tx_line[96];
      snprintf(tx_line, sizeof(tx_line), "TX CH ts=%lu len=%u",
               static_cast<unsigned long>(ts), static_cast<unsigned>(strlen(truncated)));
      pushDiagLine(tx_line);
    }
    if (!the_mesh.sendGroupMessage(ts, cd.channel, sender, truncated, static_cast<int>(strlen(truncated)))) {
      showAlert("Send failed", 1200);
      return false;
    }
    appendMessage(_ui_threads[_active_thread_idx].name, sender, truncated, true, true, false,
                  0, DELIV_NONE, 0, 0, 0, 0, nullptr, 0, the_mesh.uiLastSentFp());
    resetComposer();
    showAlert("Sent", 900);
    return true;
  }

  UIThread& th = _ui_threads[_active_thread_idx];
  syncThreadMeshSlots(th.name, false);
  ContactInfo recipient;
  uint8_t target_pub[PUB_KEY_SIZE];
  bool have_target = false;
  if (_active_dm_contact_set) {
    memcpy(target_pub, _active_dm_contact_pub, sizeof(target_pub));
    have_target = true;
  } else if (hasContactPub(th.mesh_contact_pub)) {
    memcpy(target_pub, th.mesh_contact_pub, sizeof(target_pub));
    have_target = true;
  } else {
    // Fallback: if the thread has no pub key (e.g., created from an incoming message
    // before the contact was indexed, or wiped during a save/load cycle), find a contact
    // by matching thread name. This avoids the "No DM key" dead-end after a reboot.
    for (int i = 0; i < the_mesh.getNumContacts(); ++i) {
      ContactInfo c{};
      if (the_mesh.getContactByIdx(static_cast<uint32_t>(i), c) &&
          strncmp(c.name, th.name, MAX_THREAD_NAME) == 0) {
        memcpy(target_pub, c.id.pub_key, sizeof(target_pub));
        // Latch this discovery onto the thread so subsequent sends don't re-scan.
        memcpy(_ui_threads[_active_thread_idx].mesh_contact_pub, c.id.pub_key,
               sizeof(_ui_threads[_active_thread_idx].mesh_contact_pub));
        _ui_threads[_active_thread_idx].mesh_contact_idx = static_cast<int16_t>(i);
        _active_dm_contact_set = true;
        memcpy(_active_dm_contact_pub, c.id.pub_key, sizeof(_active_dm_contact_pub));
        have_target = true;
        break;
      }
    }
  }
  if (!have_target) {
    pushDiagLine("TX DM blocked: no locked pubkey");
    showAlert("No DM key", 1500);
    return false;
  }
  ContactInfo* by_pub = the_mesh.lookupContactByPubKey(target_pub, PUB_KEY_SIZE);
  if (!by_pub) {
    pushDiagLine("TX DM blocked: locked key missing");
    showAlert("Contact missing", 1500);
    return false;
  }
  recipient = *by_pub;
  ContactInfo tx_recipient = recipient;
  // Deterministic touch send behavior: avoid stale direct-path state by forcing
  // per-message flood routing for DM sends.
  tx_recipient.out_path_len = OUT_PATH_UNKNOWN;
  {
    char tx_line[96];
    snprintf(tx_line, sizeof(tx_line), "TX DM to %02X%02X%02X%02X%02X%02X p=%d->flood len=%u",
             tx_recipient.id.pub_key[0], tx_recipient.id.pub_key[1], tx_recipient.id.pub_key[2],
             tx_recipient.id.pub_key[3], tx_recipient.id.pub_key[4], tx_recipient.id.pub_key[5],
             static_cast<int>(recipient.out_path_len), static_cast<unsigned>(strlen(truncated)));
    pushDiagLine(tx_line);
  }
  uint32_t expected_ack = 0, est_timeout = 0;
  uint32_t ts = the_mesh.getRTCClock()->getCurrentTimeUnique();
  if (ts <= s_last_ui_tx_ts) ts = s_last_ui_tx_ts + 1;
  s_last_ui_tx_ts = ts;
  uint8_t attempt = s_ui_tx_attempt++;
  if (attempt < 4) {
    attempt = 4;
    s_ui_tx_attempt = 5;
  }
  int r = the_mesh.sendMessage(tx_recipient, ts, attempt, truncated, expected_ack, est_timeout);
  {
    uint32_t hash4 = 0;
    bool has_hash = the_mesh.getLastTxtTxHash4(hash4);
    char tx_line[132];
    snprintf(tx_line, sizeof(tx_line), "TX DM ts=%lu att=%u r=%d ack=%lu h=%08lX",
             static_cast<unsigned long>(ts), static_cast<unsigned>(attempt), r,
             static_cast<unsigned long>(expected_ack),
             static_cast<unsigned long>(has_hash ? hash4 : 0UL));
    pushDiagLine(tx_line);
  }
  if (r == MSG_SEND_FAILED) {
    showAlert("Send failed", 1200);
    appendMessage(_ui_threads[_active_thread_idx].name, sender, truncated, false, true, false,
                  expected_ack, DELIV_FAILED);
    return false;
  }
  // Register the ACK hash so when the recipient acks the DM, processAck
  // matches it and fires UITask::onMessageAcked → bubble flips to ✓✓.
  the_mesh.uiRegisterExpectedAck(expected_ack, recipient.id.pub_key);
  appendMessage(_ui_threads[_active_thread_idx].name, sender, truncated, false, true, false,
                expected_ack, DELIV_SENT, 0, 0, 0, 0, nullptr, 0, the_mesh.uiLastSentFp());
  resetComposer();
  showAlert("Sent", 900);
  return true;
}

// ============================================================
// UITask::begin
// ============================================================
void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display    = display;
  _sensors    = sensors;
  _node_prefs = node_prefs;

  // GPS resume: initBasicGPS() always leaves the module stopped at boot, so a
  // saved "GPS on" pref never actually starts the hardware until the user
  // toggles it — and since the pref already reads "on", the first tap turns it
  // OFF, so it takes an off/on dance to get a fix. Re-apply the saved pref here
  // (runs after sensors.begin()) so GPS resumes automatically after a reboot.
  // setSettingValue("gps") is a no-op on boards without GPS; on the T-Deck the
  // flaky boot-time gps_detected race is disabled (ENV_SKIP_GPS_DETECT), so this
  // reliably starts the NMEA reader.
  if (_sensors && _node_prefs && _node_prefs->gps_enabled) {
    _sensors->setSettingValue("gps", "1");
  }
  s_tiles_from_sd = touchPrefsGetTilesFromSd();   // map tile source: server (default) vs microSD
#if defined(ESP32)
  // Bump the CPU above the 80 MHz base-config default. The base was
  // chosen for power on a headless companion build; on a touch device
  // the user is actively interacting and 80 MHz is visibly sluggish on
  // popups + tab switches.
  // Stair-step: 160 MHz was known-good (single change). 200 MHz +
  // double-buffer bootlooped after the top bar painted — unclear if it
  // was the clock or the BLE-DRAM pressure. Trying 180 MHz with single
  // buffer is the next conservative step. 240 MHz had RGB565 noise on
  // the map's SJPG decode, so the ceiling is somewhere south of that.
  setCpuFrequencyMhz(180);

  // Local-time zone = CET/CEST base + the user's manual hour offset (Settings ->
  // Device -> Time offset), so every clock reads correctly even offline. The
  // Wi-Fi NTP path applies the same string. The RTC stays UTC — this only
  // shifts localtime() display.
  {
    char tz[48];
    touchPrefsBuildLocalTz(tz, sizeof tz);
    setenv("TZ", tz, 1);
    tzset();
  }

  // Mount the dedicated tiles LittleFS partition. formatOnFail=true so a
  // pristine partition (all-0xFF after the partition-table upgrade) gets
  // initialised to an empty FS instead of failing to mount. partition
  // label "tiles" matches the CSV; base path "/tiles_lfs" is for VFS
  // access (we use the FS object directly, so it's mostly a label).
  s_tiles_fs_ready = s_tiles_fs.begin(true /*formatOnFail*/, "/tiles_lfs",
                                       10 /*maxOpenFiles*/, "tiles");
#if defined(ESP32)
  // Mount failed outright: the Arduino LittleFS wrapper only auto-formats when
  // esp_vfs_littlefs_register returns exactly ESP_FAIL — but grow_on_mount=true
  // means a grow mismatch (or other error) returns a DIFFERENT code, so the
  // partition is left unmounted and the map showed "Reflash the tiles partition"
  // forever even though the partition is present. format() targets the stored
  // "tiles" label, so force a wipe + remount. Tiles are a re-downloadable cache.
  if (!s_tiles_fs_ready) {
    Serial.println("[TILE] tiles begin() failed — forcing format() + remount");
    s_tiles_fs.format();
    s_tiles_fs_ready = s_tiles_fs.begin(true, "/tiles_lfs", 10, "tiles");
    Serial.printf("[TILE] forced reformat -> ready=%d totalBytes=%u\n",
                  (int)s_tiles_fs_ready, (unsigned)s_tiles_fs.totalBytes());
  }
  // Corruption guard: the partition-table change (pre-alpha_12 grew the OTA
  // slots) left some devices with a tiles FS that MOUNTS but has a broken
  // superblock — block_count reads as 0, so the first mkdir on a fresh zoom dir
  // hit a divide-by-zero deep in lfs_alloc and rebooted the tile worker (caught
  // via coredump: exccause 6 in lfs_alloc ← ensureTilesDirPath). formatOnFail
  // doesn't catch this (mount "succeeded"). Detect the bad state via a zero/absurd
  // totalBytes and force a clean reformat. Tiles are a re-downloadable cache, so
  // wiping them is harmless.
  if (s_tiles_fs_ready) {
    const size_t tb = s_tiles_fs.totalBytes();
    if (tb == 0 || tb > 8u * 1024u * 1024u) {   // tiles partition is ~4.75 MB
      Serial.printf("[TILE] tiles FS bad (totalBytes=%u) — reformatting\n", (unsigned)tb);
      s_tiles_fs.end();
      s_tiles_fs.format();
      s_tiles_fs_ready = s_tiles_fs.begin(true, "/tiles_lfs", 10, "tiles");
      Serial.printf("[TILE] reformat -> ready=%d totalBytes=%u\n",
                    (int)s_tiles_fs_ready, (unsigned)s_tiles_fs.totalBytes());
    }
  }
#endif

#if defined(ESP32)
  // One-time SPIFFS cleanup + audit. Earlier dev builds wrote map tiles to
  // SPIFFS (/tiles/<z>/<x>/<y>.jpg) before the dedicated "tiles" LittleFS
  // partition existed; the spiffs partition keeps its old offset across the
  // partition-table upgrade, so that junk survives. A near-full SPIFFS turns
  // every chat-history write into a multi-second GC storm — the "open a DM /
  // receive a message and everything freezes" symptom (the history blob is
  // only ~21 KB, so a slow write means the partition is thrashing, not big).
  //
  // arduino-esp32 core 3.x: File::name() is the BASENAME; full path is
  // path(), and SPIFFS.remove() needs the full path. The old purge matched
  // name() against "/tiles" and so freed nothing. Walk the tree via a dir
  // queue (descends into a synthesized "tiles/" dir), list every file so we
  // can see ground truth in the Settings diag, and delete any image file
  // (*.jpg / *.png) — no legitimate SPIFFS file is an image, so identity /
  // contacts3 / channels2 / new_prefs / chat history are never touched.
  {
    // Big work buffers from INTERNAL heap (NOT PSRAM: SPIFFS.remove() disables
    // the flash cache, making PSRAM unreadable mid-op). Freed right after.
    const int   PATHLEN = 72;
    const int   QCAP    = 256;   // dir paths to visit (dirs < files, so ample)
    const int   VCAP    = 64;    // image victims collected per dir
    char (*dirq)[PATHLEN]    = (char (*)[PATHLEN])malloc((size_t)QCAP * PATHLEN);
    char (*victims)[PATHLEN] = (char (*)[PATHLEN])malloc((size_t)VCAP * PATHLEN);
    if (dirq && victims) {
      int total_purged = 0, listed = 0; uint32_t total_kb = 0;
      // Multi-pass: a single pass with QCAP=256 cleans the whole /tiles tree,
      // but loop a few times as insurance (rmdir empties shrink the tree, and
      // a dir with >VCAP images is finished on a later pass). Stop when a pass
      // deletes nothing.
      for (int pass = 0; pass < 6; ++pass) {
        int qh = 0, qt = 0;
        strncpy(dirq[qt], "/", PATHLEN - 1); dirq[qt][PATHLEN - 1] = '\0'; ++qt;
        int pass_purged = 0;
        while (qh < qt) {
          char dpath[PATHLEN];
          strncpy(dpath, dirq[qh++], PATHLEN - 1); dpath[PATHLEN - 1] = '\0';
          File dir = SPIFFS.open(dpath);
          if (!dir) continue;
          if (!dir.isDirectory()) { dir.close(); continue; }
          int nv = 0;
          File e = dir.openNextFile();
          while (e) {
            const char* full = e.path();   // full path incl. leading '/'
            char fp[PATHLEN];
            strncpy(fp, full ? full : "", PATHLEN - 1); fp[PATHLEN - 1] = '\0';
            if (e.isDirectory()) {
              if (qt < QCAP) { strncpy(dirq[qt], fp, PATHLEN - 1); dirq[qt][PATHLEN - 1] = '\0'; ++qt; }
            } else {
              uint32_t sz = (uint32_t)e.size();
              if (pass == 0 && listed < 22) {
                char ln[80];
                snprintf(ln, sizeof(ln), "%s %uKB", fp, (unsigned)(sz / 1024));
                pushDiagLine(ln);
                ++listed;
              }
              size_t L = strlen(fp);
              bool is_img = (L > 4) && (strcasecmp(fp + L - 4, ".jpg") == 0 ||
                                        strcasecmp(fp + L - 4, ".png") == 0);
              if (is_img && nv < VCAP) {
                strncpy(victims[nv], fp, PATHLEN - 1); victims[nv][PATHLEN - 1] = '\0';
                ++nv;
                total_kb += sz / 1024;
              }
            }
            e.close();
            e = dir.openNextFile();
          }
          dir.close();
          for (int i = 0; i < nv; ++i) {
            if (SPIFFS.remove(victims[i])) { ++pass_purged; ++total_purged; }
          }
          // Drop now-empty tile dirs so later passes reach overflow subtrees.
          if (strncmp(dpath, "/tiles", 6) == 0) SPIFFS.rmdir(dpath);
        }
        if (pass_purged == 0) break;
      }
      if (total_purged > 0) {
        char m[56];
        snprintf(m, sizeof(m), "Purged %d SPIFFS images (%uKB)", total_purged, (unsigned)total_kb);
        pushDiagLine(m);
      }
    }
    if (dirq) free(dirq);
    if (victims) free(victims);
    char su[56];
    snprintf(su, sizeof(su), "SPIFFS %u/%u KB used",
             (unsigned)(SPIFFS.usedBytes() / 1024),
             (unsigned)(SPIFFS.totalBytes() / 1024));
    pushDiagLine(su);
  }
#endif

  touchPrefsBegin();
  uint16_t to_s = touchPrefsGetScreenTimeoutSecs();
  _screen_timeout_ms = static_cast<uint32_t>(to_s) * 1000u;
#endif
  _last_input_ms = millis();
  _screen_off    = false;
#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#endif
#ifdef PIN_AUX_BTN
  pinMode(PIN_AUX_BTN, INPUT_PULLUP);
#endif
  _next_refresh  = millis() + UI_REFRESH_MS;
  _alert[0]      = '\0';
  _alert_expiry  = 0;
  _msgcount      = 0;
  _ui_msg_count  = 0;
  _ui_msg_head   = 0;
  _next_thread_seed       = 0;
  _active_thread_idx      = -1;
  _active_thread_is_channel = false;
  _thread_scroll          = 0;
  _composer_mode          = false;
  _composer_char_idx      = 0;
  _composer_action_idx    = 0;
  _compose_buf[0]         = '\0';
  _active_dm_contact_set  = false;
  memset(_active_dm_contact_pub, 0, sizeof(_active_dm_contact_pub));
  // Message ring + thread table live in PSRAM (≈20 KB), not internal DRAM —
  // internal RAM is tight and the WiFi stack aborts if it can't allocate its
  // connection timers there. Falls back to internal only if PSRAM is somehow full.
  const size_t msgs_bytes    = sizeof(UIMessage) * MAX_UI_MESSAGES;
  const size_t threads_bytes = sizeof(UIThread)  * MAX_UI_THREADS;
  if (!_ui_msgs) {
    _ui_msgs = (UIMessage*)heap_caps_malloc(msgs_bytes, MALLOC_CAP_SPIRAM);
    if (!_ui_msgs) _ui_msgs = (UIMessage*)heap_caps_malloc(msgs_bytes, MALLOC_CAP_8BIT);
  }
  if (!_ui_threads) {
    _ui_threads = (UIThread*)heap_caps_malloc(threads_bytes, MALLOC_CAP_SPIRAM);
    if (!_ui_threads) _ui_threads = (UIThread*)heap_caps_malloc(threads_bytes, MALLOC_CAP_8BIT);
  }
  if (_ui_msgs)    memset(_ui_msgs,    0, msgs_bytes);
  if (_ui_threads) memset(_ui_threads, 0, threads_bytes);
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    _ui_threads[i].mesh_contact_idx  = -1;
    memset(_ui_threads[i].mesh_contact_pub, 0, sizeof(_ui_threads[i].mesh_contact_pub));
    memset(_ui_threads[i].mesh_contact_key6, 0, sizeof(_ui_threads[i].mesh_contact_key6));
    _ui_threads[i].mesh_channel_slot = -1;
  }
  // NOTE: a dev-era "interop safety" block used to live here that, on EVERY
  // boot, force-reset manual_add_contacts=0 and OR'd AUTO_ADD_CHAT|REPEATER into
  // autoadd_config, then savePrefs()'d. That silently clobbered the user's saved
  // Auto-add settings on every reboot (and didn't actually affect advert/message
  // decoding — auto-add is purely contact-list behaviour). Removed: the user's
  // saved Auto-add configuration is now respected as-is across reboots. Fresh
  // installs still get sensible defaults from the firmware's pref initialisation.
  _history_dirty = false;
  _next_history_flush_ms = 0;
  loadHistoryFromStorage();
  _next_mesh_thread_refresh = millis() + 2000;
  refreshThreadsFromMesh();
  _touch_screen = TouchUiScreen::Home;

#if defined(HAS_TOUCH_UI)
  g_lv.task = this;
  if (!g_lv.ready) {
    lv_init();
    initTouchFontFallbacks();
    // Allocate the draw buffer in PSRAM so the ~12 KB it costs comes out of
    // the 8 MB external RAM instead of the 320 KB internal DRAM that WiFi
    // DMA buffers also need. Falls back to DRAM if PSRAM allocation fails.
    if (!g_draw_buffer) {
      const size_t buf_bytes = sizeof(lv_color_t) * 240 * LV_DRAW_BUF_LINES;
      // Internal DMA-capable DRAM — this is the hot loop's read source
      // during SPI flush. PSRAM (~80 MHz QSPI) is ~3× slower than
      // internal SRAM. INTERNAL|DMA also makes it eligible for SPI DMA
      // transfers if the display driver ever grows them. Fall back to
      // PSRAM if internal DRAM is too fragmented at boot.
      g_draw_buffer = (lv_color_t*)heap_caps_malloc(
          buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
      if (!g_draw_buffer) {
        g_draw_buffer = (lv_color_t*)heap_caps_malloc(
            buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      }
      if (!g_draw_buffer) g_draw_buffer = (lv_color_t*)malloc(buf_bytes);
    }
    lv_disp_draw_buf_init(&g_lv.draw_buf, g_draw_buffer, nullptr,
                          240 * LV_DRAW_BUF_LINES);
    g_cap_touch_hw_started = false;
    g_lv.touch_inited      = false;

    // Load the persistent global UI orientation NOW — before the display
    // resolution and screens are set up — so every layout query
    // (lv_disp_get_hor/ver_res) returns the rotated size and the whole UI is
    // built for the chosen orientation. Changing the setting reboots, so this
    // is the single point where orientation is established for the session.
#if defined(ESP32)
    s_ui_rotation = touchPrefsGetUiRotation();
#if defined(HAS_TDECK_GT911)
    // The T-Deck panel is landscape-native (320x240) — the early boot wordmark
    // already renders upright at panel rotation 3. Always run the UI in
    // landscape so it fills the screen (the portrait default left it narrow and
    // clipped). ROT_270 maps to panel rotation 3, matching the boot wordmark.
    s_ui_rotation = LV_DISP_ROT_270;
#endif
    // Apply the saved backlight brightness (takes the LEDA pin over from the
    // display's digitalWrite via LEDC PWM). Both touch boards have the LEDA pin.
#if defined(HAS_BACKLIGHT_PWM)
    applyBrightness(touchPrefsGetBrightness());
#endif
#if defined(HAS_TDECK_KEYBOARD)
    s_kb_bl_mode = touchPrefsGetKbBacklight();
#endif
    // Record the boot transport: Wi-Fi is inited at boot only when its radio is
    // enabled AND creds are saved (mirrors `want_wifi` in main.cpp setup()).
    // The control center uses this to know which transport can be toggled live
    // vs. needs a reboot to switch to.
    s_boot_wifi_transport = wifiConfigWantsWifi();
#endif
    const bool ui_landscape = (s_ui_rotation == LV_DISP_ROT_90 ||
                               s_ui_rotation == LV_DISP_ROT_270);

    lv_disp_drv_init(&g_lv.disp_drv);
    // Landscape rotates the panel in HARDWARE (smooth — no per-pixel software
    // rotation each flush), so tell LVGL the already-rotated resolution and let
    // it render/flush natively in 320x240.
    g_lv.disp_drv.hor_res  = ui_landscape ? 320 : 240;
    g_lv.disp_drv.ver_res  = ui_landscape ? 240 : 320;
    g_lv.disp_drv.flush_cb = lvglFlush;
    g_lv.disp_drv.draw_buf = &g_lv.draw_buf;
    // sw_rotate stays enabled ONLY for the transient keyboard-landscape trick
    // when the base orientation is portrait. In hardware landscape the LVGL
    // rotation is left at NONE so it never software-rotates on top of the panel.
    g_lv.disp_drv.sw_rotate = 1;
    lv_disp_drv_register(&g_lv.disp_drv);

    if (ui_landscape) {
      applyHardwarePanelRotation(s_ui_rotation);       // rotate the ST7789
      heltecV4CapTouchSetPointRotation(s_ui_rotation); // LVGL won't, so driver does
    }
    // Swipe-axis transform always matches the visible orientation.
    heltecV4CapTouchSetRotation(s_ui_rotation);

#if defined(HAS_TDECK_TRACKBALL)
    // Trackball: set up the direction GPIOs/ISRs and orient motion to the UI.
    tdeckTrackballBegin();
    tdeckTrackballSetRotation(s_ui_rotation);
#endif
    // (Audio: the I2S speaker amp is installed on demand per tone — see
    // tdeckPlayNotify — so nothing to set up at boot.)

    // Build the always-on top status bar AFTER the display driver is
    // registered — lv_layer_sys() needs an active disp or it returns
    // nullptr, and creating a child of nullptr was the boot-loop cause.
    buildGlobalStatusBar();

    lv_indev_drv_init(&g_lv.indev_drv);
    g_lv.indev_drv.type    = LV_INDEV_TYPE_POINTER;
    g_lv.indev_drv.read_cb = lvglTouchRead;
    g_lv.indev_drv.disp    = lv_disp_get_default();
    if (!lv_indev_drv_register(&g_lv.indev_drv)) pushDiagLine("LVGL indev failed");

    buildUiTree();

    // Force immediate full repaint to replace the TFT "Loading..." banner.
    lv_obj_invalidate(lv_scr_act());
    lv_disp_t* lv_disp = lv_disp_get_default();
    if (lv_disp) lv_refr_now(lv_disp);

    g_lv.lvgl_tick_prev_us = micros();
    g_lv.ready = true;
    // Wire BaseChatMesh / Dispatcher trace lines (mesh_touch_tx_tracef → touchDiagTraceLine)
    // into the on-device Diag panel. Without this, every TX_COMPOSE / CMP / TX_AIR line
    // emitted from BaseChatMesh.cpp goes to /dev/null because s_touch_diag_cb was never set.
    touchDiagTraceRegister(&pushDiagLine);
    pushDiagLine("diag trace bound");

    // First-boot setup wizard (welcome -> name -> region -> Wi-Fi). Overlays the
    // freshly-built UI; gated by touchPrefsGetSetupDone() so it's shown once.
    setupWizardMaybeOpen();
  }
  g_lv.dirty_threads      = true;
  g_lv.dirty_timeline     = true;
  g_lv.defer_heavy_refresh = false;
  g_lv.heavy_refresh_at_ms = 0;
#endif
}

// ============================================================
// UITask methods
// ============================================================
void UITask::showAlert(const char* text, int duration_millis) {
  if (!text) return;
  strncpy(_alert, text, sizeof(_alert) - 1);
  _alert[sizeof(_alert) - 1] = '\0';
  _alert_expiry = millis() + static_cast<unsigned long>(duration_millis);
#if defined(HAS_TOUCH_UI)
  pushDiagLine(text);
  showAlertToastLvgl(text, static_cast<uint32_t>(duration_millis > 0 ? duration_millis : 900));
#endif
}

int UITask::getUnreadTotal() const {
  // Throttle the (slightly non-trivial) recompute to ~1 Hz and cache the
  // result. The status bar polls this every 250 ms on every non-Home tab;
  // recomputing the filtered sum each time was needless per-tick work.
  // 1 s staleness on an unread badge is imperceptible.
  static uint32_t s_last_ms    = 0;
  static int      s_cached      = 0;
  static bool     s_have_cached = false;
  const uint32_t now = millis();
  if (s_have_cached && (uint32_t)(now - s_last_ms) < 1000u) return s_cached;

  int total = 0;
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    if (!_ui_threads[i].used) continue;
    // Count only what the Chats inbox actually shows (see
    // getCombinedInboxCount): channels always, DM threads only when they
    // have displayable message history. A DM that bumped its unread counter
    // but stored no readable message — e.g. from a peer whose advert we
    // haven't received yet, so the body can't be decrypted — would
    // otherwise show a phantom unread badge with nothing to open.
    if (!_ui_threads[i].channel && !threadHasMessageHistory(i)) continue;
    total += _ui_threads[i].unread;
  }
  s_last_ms = now;
  s_cached = total;
  s_have_cached = true;
  return total;
}

void UITask::markThreadRead(int idx) {
  if (idx < 0 || idx >= MAX_UI_THREADS || !_ui_threads[idx].used) return;
  if (_ui_threads[idx].unread == 0 && !_ui_threads[idx].has_mention) return;
  _ui_threads[idx].unread = 0;
  _ui_threads[idx].has_mention = false;
  markHistoryDirty(200);   // persist soon (survives a manual reboot)
}

void UITask::markAllThreadsRead() {
  bool any = false;
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    if (!_ui_threads[i].used) continue;
    if (_ui_threads[i].unread || _ui_threads[i].has_mention) {
      _ui_threads[i].unread = 0;
      _ui_threads[i].has_mention = false;
      any = true;
    }
  }
  if (any) markHistoryDirty(200);   // persist soon (survives a manual reboot)
}

bool UITask::threadHasMention(int idx) const {
  return idx >= 0 && idx < MAX_UI_THREADS && _ui_threads[idx].used && _ui_threads[idx].has_mention;
}

int UITask::getThreadCount(bool channel_mode, int out_indexes[], int max_out) const {
  int scratch[UI_SORT_SCRATCH];
  int n = 0;
  sortThreadsByRecent(channel_mode, scratch, n);
  int copy_n = (n < max_out) ? n : max_out;
  for (int i = 0; i < copy_n; ++i) out_indexes[i] = scratch[i];
  return copy_n;
}

bool UITask::threadHasMessageHistory(int thread_idx) const {
  if (thread_idx < 0 || thread_idx >= MAX_UI_THREADS || !_ui_threads[thread_idx].used) return false;
  int tmp[8];
  return getThreadMessageIndexes(thread_idx, tmp, 8, false) > 0;
}

int UITask::getCombinedInboxCount(int out_indexes[], int max_out) const {
  int scratch[UI_SORT_SCRATCH];
  int n = 0;
  for (int i = 0; i < MAX_UI_THREADS; ++i) {
    if (!_ui_threads[i].used) continue;
    if (!_ui_threads[i].channel && !threadHasMessageHistory(i)) continue;
    scratch[n++] = i;
  }
  for (int a = 0; a < n; ++a) {
    for (int b = a + 1; b < n; ++b) {
      if (_ui_threads[scratch[b]].last_ts > _ui_threads[scratch[a]].last_ts) {
        int t     = scratch[a];
        scratch[a] = scratch[b];
        scratch[b] = t;
      }
    }
  }
  const int copy_n = (n < max_out) ? n : max_out;
  for (int i = 0; i < copy_n; ++i) out_indexes[i] = scratch[i];
  return copy_n;
}

bool UITask::getThreadInfo(int idx, bool& channel, uint16_t& unread, uint32_t& ts,
                           char* name, size_t name_len) const {
  if (idx < 0 || idx >= MAX_UI_THREADS || !_ui_threads[idx].used) return false;
  channel = _ui_threads[idx].channel;
  unread  = _ui_threads[idx].unread;
  ts      = _ui_threads[idx].last_ts;
  if (name && name_len > 0) {
    strncpy(name, _ui_threads[idx].name, name_len - 1);
    name[name_len - 1] = '\0';
  }
  return true;
}

int UITask::getActiveThreadMessageCount(int out_indexes[], int max_out, bool newest_first) const {
  return getThreadMessageIndexes(_active_thread_idx, out_indexes, max_out, newest_first);
}

bool UITask::getMessageByIndex(int msg_idx, UIMessage& out) const {
  if (msg_idx < 0 || msg_idx >= MAX_UI_MESSAGES) return false;
  out = _ui_msgs[msg_idx];
  return true;
}

void UITask::enterThread(bool channel_mode, int idx) { setActiveThread(idx, channel_mode); }

void UITask::openMeshContactDm(uint32_t mesh_contact_index) {
  ContactInfo c;
  if (!the_mesh.getContactByIdx(mesh_contact_index, c) || !c.name[0]) return;
  int t = findOrCreateThread(c.name, false);
  if (t < 0) return;
  _ui_threads[t].mesh_contact_idx = static_cast<int16_t>(mesh_contact_index);
  memcpy(_ui_threads[t].mesh_contact_pub, c.id.pub_key, sizeof(_ui_threads[t].mesh_contact_pub));
  memcpy(_ui_threads[t].mesh_contact_key6, c.id.pub_key, sizeof(_ui_threads[t].mesh_contact_key6));
  _active_dm_contact_set = true;
  memcpy(_active_dm_contact_pub, c.id.pub_key, sizeof(_active_dm_contact_pub));
  markHistoryDirty(200);
  setActiveThread(t, false);
#if defined(HAS_TOUCH_UI)
  if (!g_lv.ready) {
    g_lv.dirty_threads = true;
    return;
  }
  hideKb();
  closeSettingsModal();
  if (g_lv.ch.detail_open && g_lv.ch.overlay) {
    lv_obj_add_flag(g_lv.ch.overlay, LV_OBJ_FLAG_HIDDEN);
    g_lv.ch.detail_open = false;
  }
  {
    char san[UITask::MAX_THREAD_NAME + 8];
    copyUtf8ReplacingMissingGlyphs(&g_font_12, san, sizeof(san), c.name);
    setChatStatusTitle(san);   // contact name → status bar (no in-chat header bar)
  }
  refreshChatDetail(g_lv.dm);
  g_lv.dm.detail_open = true;
  if (g_lv.dm.overlay) {
    lv_obj_clear_flag(g_lv.dm.overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_lv.dm.overlay);
  }
#if defined(HAS_TDECK_KEYBOARD)
  // Physical keyboard: focus the composer on open so typing goes straight in.
  showKb(&g_lv.dm);
#endif
  // Skip the LV_ANIM_ON tab transition (150 ms slide) — the chat detail
  // overlay covers the tabview so the slide is invisible anyway, and the
  // animation was stealing refresh ticks from the first composer tap +
  // keyboard show, causing a ~300-500 ms perceived lag on the map →
  // marker → Send msg path.
  if (g_lv.tabview) {
    lv_tabview_set_act(g_lv.tabview, CHAT_INBOX_TAB_INDEX, LV_ANIM_OFF);
  }
  g_lv.dirty_threads = true;
#endif
}

void UITask::resetActiveDmPath() {
  ContactInfo c;
  if (!hasActiveThread() || activeThreadIsChannel()) {
    showAlert("Select a DM thread", 1200);
    return;
  }
  if (!lookupActiveContact(c)) {
    showAlert("No contact for thread", 1200);
    return;
  }
  the_mesh.resetPathTo(c);
  showAlert("Path reset", 900);
}

void UITask::onLvTabChanged(int tab_index) {
  if (tab_index < 0) tab_index = 0;
  {
    const int k_last = static_cast<int>(TouchUiScreen::Settings);
    if (tab_index > k_last) tab_index = k_last;
  }
  _touch_screen = static_cast<TouchUiScreen>(static_cast<uint8_t>(tab_index));
#if defined(ESP32_PLATFORM) && defined(HAS_TOUCH_UI)
  Preferences prefs;
  if (prefs.begin("meshTouch", false)) {
    prefs.putUChar("tab", static_cast<uint8_t>(tab_index));
    prefs.end();
  }
#endif
}

void UITask::appendDiag(const char* message) {
#if defined(HAS_TOUCH_UI)
  pushDiagLine(message);
#else
  (void)message;
#endif
}

void UITask::logRxFrame(float snr, float rssi, const uint8_t* raw, int len) {
#if defined(HAS_TOUCH_UI)
  char rx_line[RXLOG_COLS];
  snprintf(rx_line, sizeof(rx_line), "RX %dB len=%d snr=%.2f rssi=%.0f", len, len, snr, rssi);
  pushLogLine(s_rxlog_ring, s_rxlog_line, rx_line);

  char raw_line[RXLOG_COLS];
  int p = snprintf(raw_line, sizeof(raw_line), "%dB ", len);
  if (p < 0) p = 0;
  static const char k_hex[] = "0123456789ABCDEF";
  const int cap = static_cast<int>(sizeof(raw_line));
  const int dump_len = (len > 18) ? 18 : len;
  for (int i = 0; raw && i < dump_len && p + 3 < cap; ++i) {
    const uint8_t b = raw[i];
    raw_line[p++] = k_hex[(b >> 4) & 0x0F];
    raw_line[p++] = k_hex[b & 0x0F];
    raw_line[p++] = (i + 1 < dump_len) ? ' ' : '\0';
  }
  if (dump_len == 0) {
    raw_line[p++] = '-';
    raw_line[p] = '\0';
  } else if (len > dump_len && p + 4 < cap) {
    raw_line[p++] = ' ';
    raw_line[p++] = '.';
    raw_line[p++] = '.';
    raw_line[p++] = '.';
    raw_line[p] = '\0';
  } else {
    raw_line[cap - 1] = '\0';
  }
  pushLogLine(s_rawlog_ring, s_rawlog_line, raw_line);

  if (g_set_modal.kind == SettingsModalKind::Log && g_set_modal.root) {
    refreshLogModalView();
  }
#else
  (void)snr;
  (void)rssi;
  (void)raw;
  (void)len;
#endif
}

bool UITask::isButtonPressed() const { return false; }

void UITask::toggleBuzzer() {
  if (!_node_prefs) return;
  _node_prefs->buzzer_quiet = _node_prefs->buzzer_quiet ? 0 : 1;
  the_mesh.savePrefs();
}

bool UITask::getGPSState() {
  if (!_node_prefs) return false;
  return _node_prefs->gps_enabled != 0;
}

void UITask::toggleGPS() {
  if (!_node_prefs) return;
  // Critical: just flipping the pref bit does NOT power the GPS hardware.
  // The companion-serial CMD_SET_CUSTOM_VAR("gps") path delegates to
  // SensorManager::setSettingValue, which is what calls start_gps() /
  // stop_gps() to enable the L76K (or similar) module and the NMEA reader.
  // The touch toggle was skipping that step — pref was updated but the GPS
  // never actually started, so the user saw "GPS on" but no fix arrived.
  // Mirror the CMD_SET_CUSTOM_VAR sequence: hardware first, pref + save
  // after, so a partial failure (e.g. no GPS detected) doesn't leave the
  // pref claiming the GPS is enabled.
  const bool target_on = !(_node_prefs->gps_enabled);
  const char* value = target_on ? "1" : "0";
  bool hw_ok = false;
  if (_sensors) hw_ok = _sensors->setSettingValue("gps", value);
  // If the sensor manager doesn't recognise the setting (built without
  // ENV_INCLUDE_GPS), fall back to just flipping the pref so the UI is
  // still consistent — same behaviour as before.
  (void)hw_ok;
  _node_prefs->gps_enabled = target_on ? 1 : 0;
  the_mesh.savePrefs();
}

bool UITask::setNodeName(const char* s) {
  if (!_node_prefs) return false;
  if (!s) s = "";
  strncpy(_node_prefs->node_name, s, sizeof(_node_prefs->node_name) - 1);
  _node_prefs->node_name[sizeof(_node_prefs->node_name) - 1] = '\0';
  the_mesh.savePrefs();
  return true;
}

bool UITask::setPosition(double lat, double lon) {
  if (!_sensors) return false;
  _sensors->node_lat = lat;
  _sensors->node_lon = lon;
  // A manual edit is authoritative: keep the GPS auto-persist baseline in sync so
  // it doesn't immediately overwrite what the user just typed.
  _gps_saved_lat = lat;
  _gps_saved_lon = lon;
  the_mesh.savePrefs();
  return true;
}

bool UITask::getGpsFix() {
  if (!_sensors) return false;
  LocationProvider* lp = _sensors->getLocationProvider();
  return lp && lp->isValid();
}

int UITask::getGpsSats() {
  if (!_sensors) return -1;
  LocationProvider* lp = _sensors->getLocationProvider();
  return lp ? (int)lp->satellitesCount() : -1;
}

// Keep the node's advertised location synced to GPS once we have a fix, and
// persist it occasionally so a fix survives reboot ("had a fix at least once").
// Called every UITask::loop(). No-op on boards without GPS (provider == NULL).
void UITask::updateGpsLocation(unsigned long now) {
  if (!_sensors || !getGPSState()) return;
  LocationProvider* lp = _sensors->getLocationProvider();
  if (!lp || !lp->isValid()) return;
  const double la = (double)lp->getLatitude() / 1000000.0;
  const double lo = (double)lp->getLongitude() / 1000000.0;
  if (la == 0.0 && lo == 0.0) return;   // ignore a not-yet-populated fix
  _gps_had_fix = true;
  // Live update — the profile modal and the next advert both read node_lat/lon.
  _sensors->node_lat = la;
  _sensors->node_lon = lo;
  // Persist only when moved meaningfully (~11 m) and at most once / 2 min, so we
  // don't churn SPIFFS on the shared worker (flash writes stall both cores).
  double dlat = la - _gps_saved_lat; if (dlat < 0) dlat = -dlat;
  double dlon = lo - _gps_saved_lon; if (dlon < 0) dlon = -dlon;
  if (now >= _gps_next_persist_ms && (dlat > 1e-4 || dlon > 1e-4)) {
    _gps_saved_lat = la;
    _gps_saved_lon = lo;
    _gps_next_persist_ms = now + 120000;
    the_mesh.savePrefs();
  }
}

bool UITask::setRadioParams(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr, int8_t tx_dbm, float airtime_factor) {
  if (!_node_prefs) return false;
  _node_prefs->freq = freq_mhz;
  _node_prefs->bw = bw_khz;
  _node_prefs->sf = sf;
  _node_prefs->cr = cr;
  _node_prefs->tx_power_dbm = tx_dbm;
  _node_prefs->airtime_factor = airtime_factor;
  the_mesh.savePrefs();
  the_mesh.applyRadioFromPrefs();   // take effect immediately — no reboot needed
  return true;
}

void UITask::setAutoAddConfig(uint8_t mask, uint8_t max_hops, uint8_t manual_add) {
  if (!_node_prefs) return;
  _node_prefs->autoadd_config = mask;
  _node_prefs->autoadd_max_hops = max_hops;
  _node_prefs->manual_add_contacts = manual_add ? 1u : 0u;
  the_mesh.savePrefs();
}

void UITask::setAdvertLocationPolicy(uint8_t policy) {
  if (!_node_prefs) return;
  _node_prefs->advert_loc_policy = policy;
  the_mesh.savePrefs();
}

void UITask::setPathHashMode(uint8_t mode) {
  if (!_node_prefs) return;
  if (mode > 2) mode = 2;
  _node_prefs->path_hash_mode = mode;
  the_mesh.savePrefs();
}

void UITask::setExperimentalFlags(uint8_t multi_acks, uint8_t client_repeat, uint8_t rx_boosted) {
  if (!_node_prefs) return;
  _node_prefs->multi_acks = multi_acks ? 1u : 0u;
  _node_prefs->client_repeat = client_repeat ? 1u : 0u;
  _node_prefs->rx_boosted_gain = rx_boosted ? 1u : 0u;
  the_mesh.savePrefs();
  // Base meshcomod branch does not expose applyRadioFromPrefs(); keep persisted only.
}

bool UITask::setWifiRadio(bool on) {
  (void)on;
  return false;
}

void UITask::setDeviceTimeFromSystemClock() {
#if defined(ESP32)
  time_t t = time(nullptr);
  if (t < 100000) return;
  the_mesh.getRTCClock()->setCurrentTime((uint32_t)t);
#else
  the_mesh.getRTCClock()->setCurrentTime((uint32_t)(millis() / 1000));
#endif
}

/* Screen sleep = backlight off (not full panel reset). Keeps the LVGL frame
 * buffer + panel RAM intact, so wake is instant and the previous image is
 * still on the glass when the LED lights back up — no partial re-render. */
static inline void touchScreenBacklight(bool on) {
#if defined(HAS_BACKLIGHT_PWM)
  // Both touch boards drive the backlight via LEDC PWM on PIN_TFT_LEDA_CTL once
  // applyBrightness() has claimed the pin at boot. A plain digitalWrite would
  // then be a no-op (on the V4, TFT_BL == PIN_TFT_LEDA_CTL == GPIO21), so drive
  // the PWM duty directly: 0 = off, saved brightness = on.
  if (!s_bl_pwm_ready) applyBrightness(s_brightness_pct);   // ensure LEDC is attached
  ledcWrite(kBlPwmChannel, on ? ((uint32_t)s_brightness_pct * 255u / 100u) : 0u);
#elif defined(TFT_BL)
  pinMode(TFT_BL, OUTPUT);
  #ifdef TFT_BACKLIGHT_ON
    digitalWrite(TFT_BL, on ? TFT_BACKLIGHT_ON : !TFT_BACKLIGHT_ON);
  #else
    digitalWrite(TFT_BL, on ? HIGH : LOW);
  #endif
#else
  (void)on;
#endif
}

void UITask::noteUserInput() {
  /* Called from touch input. If the screen was explicitly locked via the
   * BOOT button, ignore touches entirely — only a BOOT button press can
   * release that. Idle-timeout locks still unlock on touch. */
  if (_screen_off && _manual_lock) return;
  _last_input_ms = millis();
  if (_screen_off) wakeScreen();
}

void UITask::wakeScreen() {
  if (!_screen_off) return;
  touchScreenBacklight(true);
  _screen_off    = false;
  _manual_lock   = false;
  _last_input_ms = millis();
}

void UITask::lockScreen() {
  // Backlight off + manual lock so touch is ignored (noteUserInput()
  // early-returns) until a deliberate unlock.
  touchScreenBacklight(false);
  _screen_off  = true;
  _manual_lock = true;
#if defined(HAS_TDECK_GT911)
  // T-Deck: build the lock screen overlay now (dark). A key/trackball press
  // reveals it; holding the trackball unlocks. (V4 keeps the plain off+wake.)
  lockscreenShow();
#endif
}

void UITask::lockscreenReveal() {
#if defined(HAS_TDECK_GT911)
  if (!_manual_lock) return;                 // only meaningful while hard-locked
  if (_screen_off) { touchScreenBacklight(true); _screen_off = false; }
  lockscreenShow();                          // ensure built + on top
  _last_input_ms = millis();                 // restart the re-dim timer
#endif
}

void UITask::unlockScreen() {
  _manual_lock = false;
  _screen_off  = false;
  touchScreenBacklight(true);
#if defined(HAS_TDECK_GT911)
  lockscreenHide();
#endif
  _last_input_ms = millis();
}

uint16_t UITask::getScreenTimeoutSecs() const {
#if defined(ESP32)
  return touchPrefsGetScreenTimeoutSecs();
#else
  return _screen_timeout_ms / 1000;
#endif
}

bool UITask::setScreenTimeoutSecs(uint16_t seconds) {
#if defined(ESP32)
  if (!touchPrefsSetScreenTimeoutSecs(seconds)) return false;
#endif
  _screen_timeout_ms = static_cast<uint32_t>(seconds) * 1000u;
  _last_input_ms = millis();
  return true;
}

bool UITask::sendAdvertNow() { return the_mesh.advert(); }
bool UITask::sendAdvertFlood() { return the_mesh.sendAdvert(true); }
bool UITask::sendAdvertZeroHop() { return the_mesh.sendAdvert(false); }

void UITask::persistHistoryNow() {
  saveHistoryToStorage();
}

void UITask::rebootDevice() {
  // Persist chat history synchronously before we reboot — the periodic
  // flush is off-thread and rate-capped, so without this a reboot could
  // drop the most recent (or, if it never flushed, all) chat history.
  if (_history_dirty) saveHistoryToStorage();
  if (_board) _board->reboot();
}

void UITask::msgRead(int msgcount) { _msgcount = msgcount; }

// Shared core for newMsg / newMsgFromPubWithMeta. Bundles the channel-vs-DM
// sender parsing + thread routing in one place so the meta-aware path doesn't
// drift from the plain path. `meta_flags` is 0 (no RX metadata) when called
// from base newMsg; the touch UI ignores zero-meta entries in the Info popup.
void UITask::newMsgImpl(uint8_t path_len, const char* from_name, const char* text, int msgcount,
                        uint8_t meta_flags, int8_t snr_q4, int8_t rssi) {
  (void)path_len;
  _msgcount = msgcount;
#if defined(HAS_TDECK_GT911)
  // Notification chime on the T-Deck speaker (gated on the buzzer/sound pref).
  if (!isBuzzerQuiet()) tdeckPlayNotify();
#endif
  bool channel = (g_last_event == UIEventType::channelMessage);
  const char* thread = channel
      ? (from_name && from_name[0] ? from_name : "#unknown")
      : (from_name && from_name[0] ? from_name : "Unknown");

  // For channel/group messages the on-wire `text` is "SenderName: body" —
  // split that so the bubble can show the sender as a small label and the
  // body as the message. DMs already arrive with the sender as from_name.
  char parsed_sender[MAX_SENDER_NAME + 1];
  parsed_sender[0] = '\0';
  const char* body = text ? text : "";
  if (channel && text) {
    const char* colon = strstr(text, ": ");
    if (colon) {
      int slen = static_cast<int>(colon - text);
      if (slen > 0 && slen <= MAX_SENDER_NAME) {
        strncpy(parsed_sender, text, slen);
        parsed_sender[slen] = '\0';
        body = colon + 2;
      }
    }
  }
  const char* sender = channel
      ? (parsed_sender[0] ? parsed_sender : (from_name && from_name[0] ? from_name : "node"))
      : (from_name && from_name[0] ? from_name : "node");
  // Inbound route (repeater hashes) for the Info popup — flood RX only; read
  // synchronously from MyMesh, which stashed it just before this call.
  uint8_t in_path[MAX_UI_PATH];
  uint8_t in_path_n = 0;
  uint16_t in_scope = 0;
  if ((meta_flags & MSG_META_HAS_RX) && (meta_flags & MSG_META_IS_FLOOD)) {
    in_path_n = the_mesh.lastRxPath(in_path, sizeof(in_path));
    bool has_scope = false;
    in_scope = the_mesh.lastRxScope(&has_scope);
    if (has_scope) meta_flags |= MSG_META_HAS_SCOPE;
  }
  appendMessage(thread, sender, body, channel, false, true,
                0 /*ack_hash*/, DELIV_NONE,
                meta_flags, path_len, snr_q4, rssi,
                in_path_n ? in_path : nullptr, in_path_n, 0 /*sent_fp*/, in_scope);
  syncThreadMeshSlots(thread, channel);
#if defined(HAS_TDECK_GT911)
  // Mirror incoming traffic into the terminal live feed (only while it's open).
  // Runs on the mesh thread (core 1, same as the UI loop) so the append is safe.
  if (s_term_log_box) {
    char line[200];
    const bool  has_snr = (meta_flags & MSG_META_HAS_RX);
    const float snr     = snr_q4 / 4.0f;
    if (channel) {
      if (has_snr) snprintf(line, sizeof line, "RX [%s] %s: %s  (snr %.1f)", thread, sender, body, snr);
      else         snprintf(line, sizeof line, "RX [%s] %s: %s", thread, sender, body);
      termLogAppendC(TERM_C_RX_CH, nullptr, line);
    } else {
      if (has_snr) snprintf(line, sizeof line, "RX %s: %s  (snr %.1f)", sender, body, snr);
      else         snprintf(line, sizeof line, "RX %s: %s", sender, body);
      termLogAppendC(TERM_C_RX_DM, nullptr, line);
    }
  }
#endif
#if defined(HAS_TOUCH_UI)
  g_lv.dirty_threads  = true;
  g_lv.dirty_timeline = true;
#endif
}

void UITask::newMsgFromPub(uint8_t path_len, const uint8_t* from_pub, const char* from_name, const char* text, int msgcount) {
  newMsg(path_len, from_name, text, msgcount);
  if (!from_pub || !from_name || !from_name[0]) return;
  const int t = findThreadByName(from_name, false);
  if (t < 0) return;
  _ui_threads[t].mesh_contact_idx = -1;
  memcpy(_ui_threads[t].mesh_contact_pub, from_pub, PUB_KEY_SIZE);
  memcpy(_ui_threads[t].mesh_contact_key6, from_pub, 6);
  if (!_ui_threads[t].channel && t == _active_thread_idx) {
    _active_dm_contact_set = true;
    memcpy(_active_dm_contact_pub, from_pub, sizeof(_active_dm_contact_pub));
  }
  syncThreadMeshSlots(from_name, false);
}

void UITask::newMsgFromPubWithMeta(uint8_t path_len, bool is_flood,
                                   const uint8_t* from_pub, const char* from_name,
                                   const char* text, int msgcount,
                                   int8_t snr_q4, int8_t rssi) {
  const uint8_t meta_flags = MSG_META_HAS_RX | (is_flood ? MSG_META_IS_FLOOD : 0);
  newMsgImpl(path_len, from_name, text, msgcount, meta_flags, snr_q4, rssi);
  // Mirror newMsgFromPub's contact-pub plumbing so the newly-arrived DM
  // resolves back to its contact entry for replies.
  if (!from_pub || !from_name || !from_name[0]) return;
  const int t = findThreadByName(from_name, false);
  if (t < 0) return;
  _ui_threads[t].mesh_contact_idx = -1;
  memcpy(_ui_threads[t].mesh_contact_pub, from_pub, PUB_KEY_SIZE);
  memcpy(_ui_threads[t].mesh_contact_key6, from_pub, 6);
  if (!_ui_threads[t].channel && t == _active_thread_idx) {
    _active_dm_contact_set = true;
    memcpy(_active_dm_contact_pub, from_pub, sizeof(_active_dm_contact_pub));
  }
  syncThreadMeshSlots(from_name, false);
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  newMsgImpl(path_len, from_name, text, msgcount, 0 /*meta_flags*/, 0 /*snr*/, 0 /*rssi*/);
}

void UITask::notify(UIEventType t) {
  g_last_event = t;
  switch (t) {
    case UIEventType::contactMessage:    showAlert("New DM",          1200); break;
    case UIEventType::channelMessage:    showAlert("New channel msg",  1200); break;
    case UIEventType::roomMessage:       showAlert("New room msg",     1200); break;
    case UIEventType::newContactMessage: showAlert("New contact",      1200); break;
    case UIEventType::ack:               showAlert("Delivered",         900); break;
    default: break;
  }
}

// ============================================================
// UITask::loop
// ============================================================
void UITask::loop() {
  unsigned long now = millis();
  flushHistoryIfDue(now);
  updateGpsLocation(now);   // sync + persist node location from GPS once fixed
  ++s_live_diag_loops;
  if (_alert_expiry != 0 && now >= _alert_expiry) {
    _alert[0]     = '\0';
    _alert_expiry = 0;
  }
  /* UI ping timeout: if no reply arrived in UI_PING_TIMEOUT_MS, show a
   * "No reply" toast and clear the pending state in MyMesh so a stale match
   * doesn't fire later. */
  if (s_ui_ping_deadline_ms != 0 && now >= s_ui_ping_deadline_ms) {
    s_ui_ping_deadline_ms = 0;
    the_mesh.cancelUIPingPending();
    char msg[64];
    snprintf(msg, sizeof(msg), "No reply from %s", s_ui_ping_target_name);
    showAlert(msg, 3500);
  }

  /* User button (BOOT / PIN_USER_BTN): press toggles the screen. When the
   * panel is off it wakes + resets the idle timer; when on it locks
   * immediately. Active-low with internal pullup.
   *
   * IMPORTANT: GPIO0 is the BOOT strapping pin — if the user held it during
   * a manual flash, it's still LOW for ~hundreds of ms after reset, and a
   * naive edge detect (`prev=HIGH` default) would fire a spurious "press"
   * on the first loop iter and manual-lock the screen. We sample the real
   * state once on first entry so a held BOOT doesn't trigger a fake edge. */
#ifdef PIN_USER_BTN
  {
    static bool s_user_btn_inited = false;
    static uint8_t s_user_btn_prev = HIGH;
    if (!s_user_btn_inited) {
      s_user_btn_prev = digitalRead(PIN_USER_BTN);
      s_user_btn_inited = true;
    }
    uint8_t v = digitalRead(PIN_USER_BTN);
#if defined(HAS_TDECK_TRACKBALL)
    /* On the T-Deck, PIN_USER_BTN (GPIO0) is the trackball centre click — make
     * it act as a touch at the cursor: a held click = a held press (so taps,
     * long-press and drag all work), released = released. While the screen is
     * off a click only wakes it, and is NOT delivered as a tap. */
    const bool tb_pressed = (v == LOW);
    // Swallow the click that unlocks the screen: without this, the SAME held
    // click is re-injected as a cursor tap on the next iteration (once the
    // screen is back on), which clicks the UI underneath — the "flash" on
    // unlock. Consume it until the button is released.
    static bool s_tb_wake_consume = false;
    static unsigned long s_tb_hold_start = 0;   // press timestamp for hold-to-unlock
    if (_manual_lock) {
      // Hard-locked: a press reveals the lock screen (lights it); HOLDING the
      // trackball for kLockUnlockHoldMs unlocks. A short tap just lights it.
      if (tb_pressed && s_user_btn_prev == HIGH) {
        lockscreenReveal();          // light the lock screen, don't unlock
        s_tb_hold_start = now;
      }
      if (tb_pressed && s_tb_hold_start && !_screen_off) {
        const unsigned long held = now - s_tb_hold_start;
        if (held >= kLockUnlockHoldMs) {
          unlockScreen();            // hides the "Unlocking…" popup too
          s_tb_hold_start = 0;
          s_tb_wake_consume = true;  // swallow the release so it isn't a UI tap
        } else if (held >= 200) {    // ignore a quick wake-tap; show on a real hold
          lockscreenUnlockProgress(kLockUnlockHoldMs - held);
        }
      }
      if (!tb_pressed) { s_tb_hold_start = 0; lockscreenUnlockPopupHide(); }  // released early -> cancel
      s_tb_click_press = false;      // never deliver clicks as taps while locked
    } else if (_screen_off) {
      if (tb_pressed && s_user_btn_prev == HIGH) { wakeScreen(); s_tb_wake_consume = true; }
      s_tb_click_press = false;
    } else if (s_emoji_sheet) {
      // Emoji picker open: a fresh press inserts the highlighted glyph via the
      // selector instead of injecting a cursor tap. Edge-triggered so a held
      // click doesn't repeat-insert.
      if (tb_pressed && s_user_btn_prev == HIGH) {
        emojiSelectorClick();
        s_tb_last_active_ms = now; noteUserInput();
      }
      s_tb_click_press = false;
    } else {
      if (!tb_pressed) s_tb_wake_consume = false;   // released after unlock -> clicks re-armed
      s_tb_click_press = tb_pressed && !s_tb_wake_consume;
      if (s_tb_click_press) { s_tb_last_active_ms = now; noteUserInput(); }
    }
#else
    if (v == LOW && s_user_btn_prev == HIGH) {
      if (_screen_off) {
        /* wakeScreen() clears _manual_lock so subsequent touches work. */
        wakeScreen();
      } else {
        touchScreenBacklight(false);
        _screen_off  = true;
        _manual_lock = true;  // touch can't unlock until BOOT pressed again
      }
    }
#endif
    s_user_btn_prev = v;
  }
#endif
#ifdef PIN_AUX_BTN
  {
    static bool s_aux_btn_inited = false;
    static uint8_t s_aux_btn_prev = HIGH;
    if (!s_aux_btn_inited) {
      s_aux_btn_prev = digitalRead(PIN_AUX_BTN);
      s_aux_btn_inited = true;
    }
    uint8_t v = digitalRead(PIN_AUX_BTN);
    if (v == LOW && s_aux_btn_prev == HIGH) {
      noteUserInput();
      /* For now the aux button just wakes the screen — separate action
       * (e.g. send advert, cycle tab) can be wired here later. */
    }
    s_aux_btn_prev = v;
  }
#endif

  /* Screen timeout: toggle just the backlight (not the TFT RST/SPI) when idle
   * for `_screen_timeout_ms`. 0 disables auto-off. */
  // Signed compare: input handlers earlier in THIS loop iteration (the trackball
  // click block, aux button) call noteUserInput() with a fresh millis() that can
  // be a few ms AFTER `now` (sampled at the top of loop()). An unsigned compare
  // then made (now - _last_input_ms) underflow to ~4.29e9 and fire the timeout on
  // every click. Signed keeps a slightly-ahead stamp negative (= "just had input").
  if (_screen_timeout_ms > 0 && !_screen_off &&
      (int32_t)(now - _last_input_ms) >= (int32_t)_screen_timeout_ms) {
    touchScreenBacklight(false);
    _screen_off = true;
  }

#if defined(HAS_TOUCH_UI)
  if (!g_lv.ready) return;

  if (!g_lv.touch_inited) {
    g_lv.touch_inited = heltecV4CapTouchBegin();
    // Surface the one-shot I2C bus-scan result to the Set-tab diag ring exactly
    // once (Begin runs the scan on its first call; the string is stable after).
    static bool s_touch_scan_logged = false;
    if (!s_touch_scan_logged) {
      s_touch_scan_logged = true;
      pushDiagLine(heltecV4CapTouchDebug());
    }
    if (g_lv.touch_inited) {
      pushDiagLine("touch init ok");
      // Once hardware is up, hand the chsc6x poll off to a pinned task on
      // core 0 so it runs at a fixed ~125 Hz independent of LVGL render and
      // the_mesh.loop() — that's the only way touch stays snappy when the
      // mesh stack bursts. Falls back to inline polling in lvglTouchRead if
      // task creation fails.
      if (heltecV4CapTouchStartBackgroundPoll(8)) {
        pushDiagLine("touch async @ 125 Hz");
      } else {
        pushDiagLine("touch async start failed");
      }
    } else if (!g_cap_touch_hw_started) {
      g_cap_touch_hw_started = true;
      pushDiagLine("touch init retrying");
    }
  }

  // Swipe → tab change (blocked while a chat detail overlay is open)
  int8_t swipe_x = 0, swipe_y = 0;
  if (heltecV4CapTouchPopSwipe(&swipe_x, &swipe_y)) {
    applySwipeGesture(swipe_x, swipe_y);
  }

  // Advance LVGL tick using micros so timers still run when millis() does not advance.
  // With LV_TICK_CUSTOM=1 (lv_conf.h) LVGL reads esp_timer_get_time() itself and
  // lv_tick_inc isn't declared — keep the manual path compiled out so the build
  // works either way.
#if !LV_TICK_CUSTOM
  {
    const uint32_t t_us = micros();
    if (t_us < g_lv.lvgl_tick_prev_us) {
      g_lv.lvgl_tick_prev_us = t_us;
    } else {
      uint32_t add_ms = (t_us - g_lv.lvgl_tick_prev_us) / 1000U;
      if (add_ms > 500U) add_ms = 500U;
      if (add_ms > 0) {
        lv_tick_inc(add_ms);
        g_lv.lvgl_tick_prev_us += add_ms * 1000U;
      }
    }
  }
#endif

  // A repeater echo of one of our sent floods was just counted — repaint the
  // open chat so the sent bubble's repeat tag ticks up live.
  if (the_mesh.takeEchoDirty()) g_lv.dirty_timeline = true;

  bool heavy_ok = !g_lv.defer_heavy_refresh || now >= g_lv.heavy_refresh_at_ms;
  if (g_lv.defer_heavy_refresh && heavy_ok) g_lv.defer_heavy_refresh = false;

  if (g_lv.dirty_threads && heavy_ok) {
    refreshThreadLists();
    g_lv.dirty_threads = false;
  }
  if (g_lv.dirty_timeline && heavy_ok) {
    // Only repaint the detail that is currently open.
    if (g_lv.dm.detail_open) refreshChatDetail(g_lv.dm);
    if (g_lv.ch.detail_open) refreshChatDetail(g_lv.ch);
    g_lv.dirty_timeline = false;
  }
#if defined(HAS_TDECK_GT911)
  // microSD insert/remove detection — only while the file manager is open, so
  // there's no idle SPI traffic. SD.begin runs on this loop task (never
  // concurrent with the radio's SPI), and reuses the radio's already-begun bus.
  if (s_fm_list) {
    static unsigned long s_sd_poll_next = 0;
    if (now >= s_sd_poll_next) {
      s_sd_poll_next = now + 2000;
      if (!s_sd_mounted) {
        // Only re-probe after the backoff window — repeatedly re-initing an
        // unmountable card spikes current / churns the bus and can reset the board.
        if (now >= s_sd_retry_after_ms && fmSdTryMount()) {
          showAlert("SD card inserted", 1500);
          if (!s_fm_fs) fmShowRoots();          // refresh roots so it appears
        }
      } else if (SD.cardType() == CARD_NONE) {
        fmSdUnmount();
        showAlert("SD card removed", 1500);
        if (s_fm_fs == &SD || !s_fm_fs) fmShowRoots();
      }
    }
  }
#endif

  if (now >= _next_mesh_thread_refresh) {
    /* Pick up contact / channel additions that came in from the companion
     * serial client (CMD_SET_CHANNEL etc.). 4 s is the backstop — the app
     * also explicitly pings the UI via onThreadsChanged() for instant pickup. */
    _next_mesh_thread_refresh = now + 4000;
    refreshThreadsFromMesh();
    g_lv.dirty_threads  = true;
  }
  if (now >= _next_refresh) {
    refreshStatusLabels();
    _next_refresh = now + UI_REFRESH_MS;
  }
#if defined(HAS_TDECK_TRACKBALL)
  updateTrackball(now);
#endif
#if defined(HAS_TDECK_KEYBOARD)
  // Drain physical-keyboard presses buffered by the touch task into the field.
  for (int kbi = 0; kbi < 12; ++kbi) {
    int key = tdeckKeyboardReadKey();
    if (key <= 0) break;
    if (!_screen_off) s_kb_last_key_ms = now;   // a keypress while locked must not light the kb
    handleHwKey(key);
  }
  // Focusing a text field (cursor starts blinking — tapped or auto-focused)
  // counts as activity, exactly like a keypress: it bumps the timer so the auto
  // backlight lights up and then fades on the same idle window.
  {
    static lv_obj_t* s_kb_prev_ta = nullptr;
    lv_obj_t* cur_ta = g_lv.keyboard ? lv_keyboard_get_textarea(g_lv.keyboard) : nullptr;
    if (cur_ta && cur_ta != s_kb_prev_ta) s_kb_last_key_ms = now;
    s_kb_prev_ta = cur_ta;
  }
  // Keyboard backlight: off / on / auto (lit after activity, off after idle).
  uint8_t kb_bl = 0;
  if (s_kb_bl_mode == 1) kb_bl = 0xFF;
  else if (s_kb_bl_mode == 2 && (now - s_kb_last_key_ms) < kKbBacklightIdleMs) kb_bl = 0xFF;
  if (_screen_off || _manual_lock) kb_bl = 0;   // dark/locked screen -> keep the keyboard dark too
  tdeckKeyboardSetBacklight(kb_bl);
  serviceLockscreen();            // refresh the lock-screen clock on minute roll-over
  serviceLockingCountdown(now);   // advance / fire the spacebar "Locking…" countdown
#endif
  refreshLiveDiag(now);
  versionCheckService(now);   // firmware update check (gear badge + About line)
  refreshSysInfo(now);        // live uptime / heap on the About sub-tab
  wifiScanService();          // draw Wi-Fi scan results when the worker finishes

  // Accidental tab-switch guard: while content is actively being scrolled (and
  // for a short grace period after the scroll ends), make the bottom tab bar
  // ignore clicks so a stray finger landing on it mid-scroll can't change tabs.
  // Deliberate tab taps when not scrolling are unaffected.
  {
    static unsigned long s_tabbar_lock_until = 0;
    static bool s_tabbar_locked = false;
    lv_indev_t* indev = lv_indev_get_next(nullptr);
    if (indev && lv_indev_get_scroll_obj(indev) != nullptr) {
      s_tabbar_lock_until = now + 350;
    }
    const bool want_lock = (now < s_tabbar_lock_until);
    if (want_lock != s_tabbar_locked && g_lv.tabview) {
      lv_obj_t* tab_btns = lv_tabview_get_tab_btns(g_lv.tabview);
      if (tab_btns) {
        if (want_lock) lv_obj_clear_flag(tab_btns, LV_OBJ_FLAG_CLICKABLE);
        else           lv_obj_add_flag(tab_btns, LV_OBJ_FLAG_CLICKABLE);
        s_tabbar_locked = want_lock;
      }
    }
  }

  lv_timer_handler();
#if defined(HAS_TDECK_GT911)
  // Deferred microSD FAT32 format (runs a couple ticks after the notice paints).
  // f_mkfs blocks the loop for tens of seconds on a big card, so drop the loop
  // watchdog around it (CPU0 idle keeps running, so no reset) to avoid a
  // mid-format reset that would corrupt the card.
  if (s_sd_format_pending && --s_sd_format_pending == 0) {
    disableLoopWDT();
    SPIClass* spi = tdeckSharedSPI();
    bool ok = false;
    if (spi) {
      // Explicit reformat (not SD.begin's format_if_empty, which only formats an
      // already-unreadable card): init the card to register the FatFs diskio at
      // pdrv (no mount), f_mkfs to FAT32, write the MESHCOMOD label by hand while
      // unmounted, release, then remount the fresh FS via SD.begin. Works on any
      // card (FAT32, exFAT, blank) since f_mkfs hardware-inits + overwrites it.
      SD.end();                                          // drop any existing mount
      uint8_t pdrv = sdcard_init(PIN_SD_CS, spi, 4000000);
      if (pdrv != 0xFF) {
        char drv[3] = { (char)('0' + pdrv), ':', 0 };
        void* work = malloc(4096);                       // f_mkfs scratch (>= FF_MAX_SS)
        if (work) {
          if (f_mkfs(drv, MC_FM_FAT32, 0, work, 4096) == 0 /*FR_OK*/) {
            sdWriteFatLabel(pdrv, "MESHCOMOD");
            ok = true;
          }
          free(work);
        }
        sdcard_uninit(pdrv);
      }
      if (ok) ok = SD.begin(PIN_SD_CS, *spi, 4000000, "/sd", 3);
    }
    enableLoopWDT();
    fmHideFormatOverlay();
    if (ok && SD.cardType() != CARD_NONE) {
      s_sd_mounted = true;
      s_sd_size = SD.cardSize();
      sdEnsureMeshcomodFolders();                        // BINS / RECBCK / SETTINGS / MAPS / LOGS
      char done[56], cs[16];
      fmFmtSize64(s_sd_size, cs, sizeof cs);
      snprintf(done, sizeof done, "SD formatted - %s (MESHCOMOD)", cs);
      showAlert(done, 3500);
    } else {
      s_sd_mounted = false;
      showAlert("Format failed (no card / SD error)", 3500);
    }
    if (s_fm_list) fmShowRoots();
  }
  // Deferred copy/move (paste). WDT off — a big tree can take a while.
  if (s_fm_paste_pending && --s_fm_paste_pending == 0) {
    disableLoopWDT();
    bool ok = fmDoPaste();
    enableLoopWDT();
    fmHideFormatOverlay();
    showAlert(ok ? "Pasted" : "Paste failed", ok ? 1500 : 2400);
    if (s_fm_list) fmRefresh();
  }
#endif
#endif
}

void UITask::setBootPhase(const char* label) {
  (void)label;
}

void UITask::shutdown(bool restart) {
  (void)restart;
  // Flush chat history before we go down.
  if (_history_dirty) saveHistoryToStorage();
  if (_display) {
    _display->startFrame();
    _display->drawTextCentered(_display->width() / 2, _display->height() / 2, "Shutting down...");
    _display->endFrame();
  }
}
