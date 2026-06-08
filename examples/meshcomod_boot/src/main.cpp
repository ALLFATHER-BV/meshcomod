// meshcomod_boot — recovery LAUNCHER (Arduino). Works with the custom 2nd-stage
// bootloader (bootloader_launcher/): the bootloader defaults to recovery (factory)
// and boots the ota_0 firmware slot ONCE when recovery writes LAUNCH_MAGIC to the
// `bootsel` partition. Every reset/power-cycle returns here -- no firmware hook.
//
// Model (SD as the firmware library, single full-size slot):
//   - Launcher auto-boots whatever firmware is in the slot (instant).
//   - "Firmwares" lists *.bin on the SD; pick one -> if it's the one already in the
//     slot just boot it, else FLASH it into the slot and boot ("recovery flash on
//     demand"). The currently-installed firmware is tracked in NVS.
//   - (Next: snapshot each firmware's data to SD before overwriting, restore on switch.)
//
// Reuses the Meshcomod_Touch T-Deck UI base (Adafruit ST7789 + TDeck* inputs).

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_image_format.h"
#include "esp_heap_caps.h"
#include "mbedtls/md.h"
#include "backup.h"
#include "launcher_bootloader.h"
#include "TDeckTrackball.h"
#include "TDeckKeyboard.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static const int16_t SCRW = 320, SCRH = 240;
#define PIN_KB_SDA 18
#define PIN_KB_SCL 8
#define PIN_LORA_RST 17              // SX1262 reset (shares SPI MISO/GPIO38 with the SD)
#define LAUNCH_MAGIC 0x4C41554Eu     // 'L','A','U','N' — must match the bootloader

SPIClass spiBus(HSPI);
Adafruit_ST7789 tft(&spiBus, PIN_TFT_CS, PIN_TFT_DC, -1);
static bool s_disp = false;
static Preferences s_prefs;

// Shared 4 KB scratch for the SD/flash copy loops, kept in PSRAM (lazily allocated) to free
// internal SRAM. These loops never overlap (a backup always completes before an install /
// restore writes), so one buffer is safe; a bad flash write is caught by esp_image_verify.
#define IO_SZ 4096
static uint8_t *ioBuf() {
    static uint8_t *b = (uint8_t *)heap_caps_malloc(IO_SZ, MALLOC_CAP_SPIRAM);
    if (!b) b = (uint8_t *)malloc(IO_SZ);   // fallback to internal RAM if PSRAM is unavailable
    return b;
}

#define COL_BG  ST77XX_BLACK
#define COL_HDR 0x02B5
#define COL_TXT ST77XX_WHITE
#define COL_DIM 0x8410
#define COL_SEL 0x034F
#define COL_OK  ST77XX_GREEN
#define COL_ERR ST77XX_RED
#define COL_BAR 0x07E0

// ---------------- drawing ----------------
static void header(const char *t) {
    if (!s_disp) return;
    tft.fillRect(0, 0, SCRW, 30, COL_HDR);
    tft.setTextColor(COL_TXT); tft.setTextSize(2); tft.setCursor(8, 7); tft.print(t);
    tft.setTextSize(1);
}
static void bodyClear() { if (s_disp) tft.fillRect(0, 30, SCRW, SCRH - 30, COL_BG); }
static void textAt(int16_t x, int16_t y, uint16_t c, uint8_t sz, const char *s) {
    if (!s_disp) return;
    tft.setTextColor(c, COL_BG); tft.setTextSize(sz); tft.setCursor(x, y); tft.print(s);
}
static void footer(const char *s) {
    if (!s_disp) return;
    tft.fillRect(0, SCRH - 18, SCRW, 18, COL_BG);
    tft.setTextColor(COL_DIM, COL_BG); tft.setTextSize(1); tft.setCursor(8, SCRH - 14); tft.print(s);
}
static void printHashTwoLines(const char *hex, int16_t y) {
    if (!s_disp) return;
    tft.setTextColor(COL_TXT, COL_BG); tft.setTextSize(1);
    tft.setCursor(8, y);      tft.print(String(hex).substring(0, 32));
    tft.setCursor(8, y + 12); tft.print(String(hex).substring(32));
}

// ---------------- menu ----------------
static const char *MENU[] = {
    "Firmwares", "Add firmware", "Install over Wi-Fi", "Backup to SD card", "Device info",
    "Restore from SD", "Boot firmware", "Reboot device"
};
#define MENU_N 8
static void actWifiInstall();   // fwd decl (defined with the Wi-Fi block below)
static int s_sel = 0;
static void runLauncherCountdown();          // fwd decl
static bool backupFirmware(const String &folder, bool wait);   // fwd decl (P3 auto-backup)
static void ensureSdLayout();   // fwd decl: auto-create the SD library + seed "meshcomod" (called on every mount)
static String latestBackupDir(const String &folder);           // fwd decl
static void loadSnapshot(const String &dir);                   // fwd decl (P3 restore snapshot)
static void drawMenu() {
    if (!s_disp) return;
    header("meshcomod recovery");
    bodyClear();
    const int16_t y0 = 40, rh = 24;
    for (int i = 0; i < MENU_N; i++) {
        int16_t y = y0 + i * rh;
        if (i == s_sel) { tft.fillRect(6, y - 5, SCRW - 12, rh - 4, COL_SEL); tft.setTextColor(COL_TXT, COL_SEL); }
        else            { tft.setTextColor(COL_TXT, COL_BG); }
        tft.setTextSize(2); tft.setCursor(16, y); tft.print(MENU[i]);
    }
    footer("trackball: move    click / Enter: select");
}

// ---------------- input ----------------
enum NavEv { NAV_NONE, NAV_UP, NAV_DOWN, NAV_SELECT };
static bool s_prevClick = false;
static int  s_accumY = 0;
static bool clickPressEdge() { bool now = tdeckTrackballClickHeld(); bool e = now && !s_prevClick; s_prevClick = now; return e; }
static NavEv pollNav() {
    int k = tdeckKeyboardReadKey();
    if (k == '\r' || k == '\n' || k == ' ') return NAV_SELECT;
    if (k == 'w' || k == 'W') return NAV_UP;
    if (k == 's' || k == 'S') return NAV_DOWN;
    if (clickPressEdge()) return NAV_SELECT;
    int dx = 0, dy = 0;
    if (tdeckTrackballReadMotion(&dx, &dy)) {
        s_accumY += dy;
        if (s_accumY >= 2)  { s_accumY = 0; return NAV_DOWN; }
        if (s_accumY <= -2) { s_accumY = 0; return NAV_UP;   }
    }
    return NAV_NONE;
}
static void drainTrackball() { int dx, dy; tdeckTrackballReadMotion(&dx, &dy); s_accumY = 0; }
static void armClick()       { s_prevClick = tdeckTrackballClickHeld(); }
static bool anyInput() {
    if (tdeckKeyboardReadKey() != 0) return true;
    if (clickPressEdge()) return true;
    int dx, dy; if (tdeckTrackballReadMotion(&dx, &dy) && (dx || dy)) return true;
    return false;
}
static void waitForSelect() {
    armClick();
    for (;;) {
        tdeckKeyboardPoll();
        int k = tdeckKeyboardReadKey();
        if (k == '\r' || k == '\n' || k == ' ') return;
        if (clickPressEdge()) return;
        delay(15);
    }
}

// ---------------- hardware ----------------
static void displayBegin() {
    pinMode(PIN_TFT_BL, OUTPUT); digitalWrite(PIN_TFT_BL, HIGH);
    spiBus.begin(PIN_SPI_SCLK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);
    tft.init(240, 320); tft.setRotation(3); tft.setSPISpeed(80000000); tft.fillScreen(COL_BG);
    s_disp = true;
}
// Defensive SD power-up clocking: with every CS on the shared bus de-asserted (HIGH),
// clock out 0xFF bytes so the idle card sees the SD spec's >=74 idle clock edges before
// we attempt CMD0 -- good practice that helps a genuinely stuck card recover.
// NOTE: this is belt-and-suspenders, NOT the fix for the "SD needs a re-insert"
// regression -- that was the chip-select ordering corrected in setup() (the SD card's CS
// was floating during the 80 MHz display init, latching the card into a dead state).
static void sdWakeCard() {
    pinMode(PIN_TFT_CS, OUTPUT);   digitalWrite(PIN_TFT_CS, HIGH);   // deselect display
    pinMode(PIN_LORA_NSS, OUTPUT); digitalWrite(PIN_LORA_NSS, HIGH); // deselect radio
    pinMode(PIN_SD_CS, OUTPUT);    digitalWrite(PIN_SD_CS, HIGH);    // SD idle (CS high)
    spiBus.beginTransaction(SPISettings(200000, MSBFIRST, SPI_MODE0));
    for (int i = 0; i < 24; i++) spiBus.transfer(0xFF);             // ~192 clocks, CS high
    spiBus.endTransaction();
    delay(2);
}
static bool sdBegin() {
    const uint32_t freqs[] = { 20000000, 4000000, 400000 };
    for (int round = 0; round < 2; round++) {
        sdWakeCard();                                   // flush/idle a stuck card first
        for (uint32_t f : freqs) {
            if (SD.begin(PIN_SD_CS, spiBus, f, "/sd", 5, false)) { ensureSdLayout(); return true; }
            SD.end(); delay(20);
        }
        delay(120);
    }
    return false;
}
// Low-level SD-SPI identify probe (bypasses SD.h) to reveal WHY a mount fails:
//   "CMD0 ... no response" = card not talking (electrical / not present).
//   "ACMD41 timeout"       = card responds but didn't finish init in 5 s.
//   "ID OK ... init=NNNms"  = card identifies fine; if NN > 1000 ms, SD.h's 1 s ACMD41
//                            timeout is too short for it; if mount still fails it's FAT.
static uint8_t sdCmd(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t *resp, int rlen) {
    spiBus.transfer(0xFF);
    spiBus.transfer(0x40 | cmd);
    spiBus.transfer((uint8_t)(arg >> 24)); spiBus.transfer((uint8_t)(arg >> 16));
    spiBus.transfer((uint8_t)(arg >> 8));  spiBus.transfer((uint8_t)arg);
    spiBus.transfer(crc);
    uint8_t r1 = 0xFF;
    for (int i = 0; i < 12 && (r1 & 0x80); i++) r1 = spiBus.transfer(0xFF);
    for (int i = 0; i < rlen; i++) resp[i] = spiBus.transfer(0xFF);
    return r1;
}
static bool sdReadLBA(uint32_t lba, uint8_t *buf) {              // CS already LOW, card ready
    if (sdCmd(17, lba, 0x01, NULL, 0) != 0x00) return false;     // READ_SINGLE_BLOCK
    uint8_t tok = 0xFF; uint32_t t0 = millis();
    do { tok = spiBus.transfer(0xFF); } while (tok == 0xFF && (millis() - t0) < 300);
    if (tok != 0xFE) return false;                              // 0xFE = data start token
    for (int i = 0; i < 512; i++) buf[i] = spiBus.transfer(0xFF);
    spiBus.transfer(0xFF); spiBus.transfer(0xFF);               // discard CRC
    return true;
}
static void sdProbe(String &id, String &rd) {
    pinMode(PIN_TFT_CS, OUTPUT);   digitalWrite(PIN_TFT_CS, HIGH);
    pinMode(PIN_LORA_NSS, OUTPUT); digitalWrite(PIN_LORA_NSS, HIGH);
    pinMode(PIN_SD_CS, OUTPUT);    digitalWrite(PIN_SD_CS, HIGH);
    spiBus.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
    for (int i = 0; i < 10; i++) spiBus.transfer(0xFF);          // >=74 power-up clocks, CS high
    digitalWrite(PIN_SD_CS, LOW);
    uint8_t rsp[4]; rd = "";
    uint8_t r0 = sdCmd(0, 0, 0x95, rsp, 0);                      // CMD0 -> idle (0x01)
    if (r0 != 0x01) { id = String("CMD0 r1=0x") + String(r0, HEX) + " no response"; }
    else {
        uint8_t r8 = sdCmd(8, 0x1AA, 0x87, rsp, 4);              // CMD8 (v2 check)
        bool v2 = (r8 == 0x01 && rsp[3] == 0xAA);
        uint32_t t0 = millis(), ms = 0; uint8_t ra = 0xFF;
        do { sdCmd(55, 0, 0x01, rsp, 0);
             ra = sdCmd(41, v2 ? 0x40000000UL : 0, 0x01, rsp, 0);
             ms = millis() - t0;
        } while (ra != 0x00 && ms < 5000);
        if (ra != 0x00) id = String("ACMD41 timeout ") + ms + "ms ra=0x" + String(ra, HEX);
        else {
            sdCmd(58, 0, 0x01, rsp, 4); bool ccs = rsp[0] & 0x40;
            id = String("ID OK ") + (ccs ? "SDHC/XC" : "SD") + " init=" + ms + "ms" + (v2 ? " v2" : " v1");
            // Do raw block reads work, and what FAT layout is FATFS handed?
            // Read LBA0 TWICE (does a dummy first read prime it?), plus partition area
            // + a high sector. Report each boot signature. 55AA = real data, 0000 = blank.
            static uint8_t blk[512];
            uint32_t lbas[4] = {0, 0, 8192, 1000000};
            const char *nm[4] = {"0a", "0b", "8k", "1M"};
            rd = "";
            for (int j = 0; j < 4; j++) {
                if (!sdReadLBA(lbas[j], blk)) rd += String(nm[j]) + ":FAIL ";
                else { char s[14]; snprintf(s, sizeof s, "%s:%02X%02X ", nm[j], blk[510], blk[511]); rd += s; }
            }
        }
    }
    digitalWrite(PIN_SD_CS, HIGH); spiBus.transfer(0xFF);
    spiBus.endTransaction();
}
static void nextBackupPaths(char *img, size_t isz, char *man, size_t msz) {
    for (int i = 1; i <= 9999; i++) {
        snprintf(img, isz, "/BKUP%04d.IMG", i);
        if (!SD.exists(img)) { snprintf(man, msz, "/BKUP%04d.TXT", i); return; }
    }
    snprintf(img, isz, "/BKUP9999.IMG"); snprintf(man, msz, "/BKUP9999.TXT");
}
static uint32_t s_next_mark = 0;
static void progressCb(uint32_t done, uint32_t total) {
    if (done < s_next_mark && done < total) return;
    s_next_mark = done + (total / 50);
    if (s_disp) {
        int pct = total ? (int)((uint64_t)done * 100 / total) : 0;
        const int16_t x = 12, y = 150, w = SCRW - 24, h = 24;
        tft.drawRect(x, y, w, h, COL_TXT);
        tft.fillRect(x + 2, y + 2, (int16_t)((w - 4) * pct / 100), h - 4, COL_BAR);
        char b[40]; snprintf(b, sizeof b, "%d%%   %u / %u KB   ", pct, (unsigned)(done/1024), (unsigned)(total/1024));
        textAt(x, y + h + 8, COL_TXT, 1, b);
    }
}

// ---------------- partition / slot ----------------
static const esp_partition_t *otaSlot() {
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
}
static bool slotHasApp(const esp_partition_t *p) {
    if (!p) return false;
    uint8_t magic = 0; esp_partition_read(p, 0, &magic, 1);
    if (magic != 0xE9) return false;
    // Don't trust the 0xE9 byte alone -- a half-written / bad restore starts with 0xE9 too and
    // would auto-boot every countdown into a reboot loop. Verify the whole image so a bad slot
    // sends us to the menu instead.
    esp_partition_pos_t pos = { .offset = p->address, .size = p->size };
    esp_image_metadata_t md;
    return esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &md) == ESP_OK;
}
// ---- A/B slot selection (STANDARD bootloader: otadata picks the slot) ----
static const esp_partition_t *otaSlot1() {
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
}
// Choose which ota slot to boot back into:
//   1) the slot otadata already selects (set when we land here as a fallback), else
//   2) the slot meshcomod recorded in NVS just before "reboot to recovery", else
//   3) any slot that holds a verified app (ota_0 first).
static const esp_partition_t *pickBootSlot() {
    const esp_partition_t *o0 = otaSlot(), *o1 = otaSlot1();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    if (boot && boot->type == ESP_PARTITION_TYPE_APP &&
        (boot->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 || boot->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) &&
        slotHasApp(boot)) return boot;
    s_prefs.begin("mcboot", true); String pref = s_prefs.getString("slot", ""); s_prefs.end();
    if (pref == "ota_1" && slotHasApp(o1)) return o1;
    if (pref == "ota_0" && slotHasApp(o0)) return o0;
    if (slotHasApp(o0)) return o0;
    if (slotHasApp(o1)) return o1;
    return NULL;
}
// Point otadata at an app slot (default: the best valid slot) and reboot into it. The stock
// bootloader then boots that slot; factory(recovery) is the fallback when otadata is invalid.
static void launchFirmware(const esp_partition_t *target = NULL) {
    if (!target) target = pickBootSlot();
    SD.end();        // unmount -> flush pending SD writes (e.g. current.txt) before the reboot
    delay(150);
    if (target) {
        const char *sub = (target->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) ? "ota_1" : "ota_0";
        s_prefs.begin("mcboot", false); s_prefs.putString("slot", sub); s_prefs.end();
        esp_ota_set_boot_partition(target);   // otadata -> which A/B slot the BL boots
        // Raise the custom bootloader's one-shot so it boots that slot NEXT instead
        // of returning to the recovery (it auto-clears the flag after one boot).
        const esp_partition_t *bsel = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "bootsel");
        if (bsel) { uint32_t magic = LAUNCH_MAGIC; esp_partition_erase_range(bsel, 0, bsel->size); esp_partition_write(bsel, 0, &magic, sizeof magic); }
    }
    esp_restart();
}
// Which SD firmware is currently flashed into the slot (tracked across reboots).
static String installedFw() { s_prefs.begin("mcboot", true); String s = s_prefs.getString("fw", ""); s_prefs.end(); return s; }
static void   setInstalledFw(const String &n) { s_prefs.begin("mcboot", false); s_prefs.putString("fw", n); s_prefs.end(); }

// ---------------- SD browser ----------------
static int scanFiles(String *names, int maxn, const char *needPrefix, const char *needSuffix) {
    File root = SD.open("/"); if (!root) return 0;
    int n = 0; File e;
    String pre = String(needPrefix); pre.toUpperCase();
    String suf = String(needSuffix); suf.toUpperCase();
    while (n < maxn && (e = root.openNextFile())) {
        String nm = e.name(); int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1);
        String up = nm; up.toUpperCase();
        // skip dirs + macOS AppleDouble/dotfiles (._NAME, .Spotlight-V100, .Trashes, ...)
        if (!e.isDirectory() && !nm.startsWith(".") &&
            (pre.length() == 0 || up.startsWith(pre)) && up.endsWith(suf)) names[n++] = nm;
        e.close();
    }
    root.close(); return n;
}
static void drawFileList(const char *title, String *names, int n, int sel, const char *hint) {
    if (!s_disp) return;
    header(title); bodyClear();
    const int total = n + 1, VIS = 10; int start = (sel < VIS) ? 0 : (sel - VIS + 1);
    const int16_t y0 = 40, rh = 18;
    for (int i = 0; i < VIS && start + i < total; i++) {
        int idx = start + i; int16_t y = y0 + i * rh;
        if (idx == sel) { tft.fillRect(4, y - 2, SCRW - 8, rh - 2, COL_SEL); tft.setTextColor(COL_TXT, COL_SEL); }
        else            { tft.setTextColor(COL_TXT, COL_BG); }
        tft.setTextSize(1); tft.setCursor(10, y);
        tft.print(idx == 0 ? "< Back" : names[idx - 1].c_str());
    }
    char f[48]; snprintf(f, sizeof f, "%d item(s)   %s", n, hint); footer(f);
}
static int browseList(const char *title, String *names, int n, const char *hint) {
    int sel = 0; bool redraw = true; const int total = n + 1; drainTrackball(); armClick();
    for (;;) {
        if (redraw) { drawFileList(title, names, n, sel, hint); redraw = false; }
        tdeckKeyboardPoll();
        NavEv e = pollNav();
        if (e == NAV_UP)        { sel = (sel + total - 1) % total; redraw = true; }
        else if (e == NAV_DOWN) { sel = (sel + 1) % total;         redraw = true; }
        else if (e == NAV_SELECT) { return sel == 0 ? -1 : sel - 1; }
        delay(12);
    }
}
// Like browseList, but also surfaces U/D hotkeys on the highlighted item. *action: 0 = select,
// 1 = update (U), 2 = delete (D). Returns the item index for any action, or -1 for back/cancel.
static int browseListEx(const char *title, String *names, int n, const char *hint, int *action) {
    int sel = 0; bool redraw = true; const int total = n + 1; drainTrackball(); armClick();
    *action = 0;
    for (;;) {
        if (redraw) { drawFileList(title, names, n, sel, hint); redraw = false; }
        tdeckKeyboardPoll();
        int k = tdeckKeyboardReadKey();
        if      (k == 'w' || k == 'W') { sel = (sel + total - 1) % total; redraw = true; }
        else if (k == 's' || k == 'S') { sel = (sel + 1) % total;         redraw = true; }
        else if (k == '\r' || k == '\n' || k == ' ') { *action = 0; return sel == 0 ? -1 : sel - 1; }
        else if ((k == 'u' || k == 'U') && sel > 0)  { *action = 1; return sel - 1; }
        else if ((k == 'd' || k == 'D') && sel > 0)  { *action = 2; return sel - 1; }
        if (clickPressEdge()) { *action = 0; return sel == 0 ? -1 : sel - 1; }
        int dx = 0, dy = 0;
        if (tdeckTrackballReadMotion(&dx, &dy)) {
            s_accumY += dy;
            if      (s_accumY >= 2)  { s_accumY = 0; sel = (sel + 1) % total;         redraw = true; }
            else if (s_accumY <= -2) { s_accumY = 0; sel = (sel + total - 1) % total; redraw = true; }
        }
        delay(12);
    }
}
static bool readManifestSha(const String &imgName, char *out, size_t outsz) {
    String man = "/" + imgName; man.replace(".IMG", ".TXT");
    File m = SD.open(man, FILE_READ); if (!m) return false;
    bool found = false;
    while (m.available()) {
        String line = m.readStringUntil('\n');
        if (line.startsWith("sha256=")) {
            String v = line.substring(7); size_t j = 0;
            for (size_t i = 0; i < v.length() && j < outsz - 1 && j < 64; i++) {
                char c = v[i]; if (c >= 'A' && c <= 'F') c = (char)(c + 32);
                if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) out[j++] = c;
            }
            out[j] = '\0'; found = (j == 64); break;
        }
    }
    m.close(); return found;
}

// ---------------- actions ----------------
static void mountWithRetry(const char *title) {
    header(title); bodyClear();
    while (!sdBegin()) {
        textAt(8, 60, COL_ERR, 1, "No SD card. Insert a FAT32 card,");
        textAt(8, 76, COL_ERR, 1, "then click / Enter to retry.");
        footer("click / Enter: retry"); waitForSelect(); bodyClear();
    }
}
// --- merged-image extraction (Stage 1) ----------------------------------------------
// A merged/factory bin is a whole-flash image with a partition table at file offset
// 0x8000. Parse it to find where the app lives inside the file; we flash just that app
// into our ota_0 slot, discarding the bin's own bootloader/table (we keep ours).
// Find the app image inside a .bin by its signature: an image header (0xE9) whose app
// descriptor magic (0xABCD5432) sits at +0x20. Robust to layout -- works for an app-only
// image (offset 0) and a merged/factory bin (app at a 64 KB-aligned offset), regardless of
// where (or whether) a partition table sits. Returns the app image's file offset.
// Read source for the installer: an SD File OR an in-RAM (PSRAM) buffer, so a Wi-Fi download
// reuses the exact same install path. Mirrors File's size()/seek()/read() so call sites are
// unchanged — only the helper/installer signatures flip from File& to BinSrc&.
struct BinSrc {
    File          *file = nullptr;
    const uint8_t *mem  = nullptr; uint32_t mlen = 0, mpos = 0;
    uint32_t size() { return file ? (uint32_t)file->size() : mlen; }
    bool     seek(uint32_t p) { if (file) return file->seek(p); if (p > mlen) return false; mpos = p; return true; }
    int      read(uint8_t *b, uint32_t n) {
        if (file) return file->read(b, n);
        if (mpos >= mlen) return 0; uint32_t k = mlen - mpos; if (k > n) k = n;
        memcpy(b, mem + mpos, k); mpos += k; return (int)k;
    }
    void     close() { if (file) file->close(); }
};

static bool findAppImage(BinSrc &f, uint32_t fsz, uint32_t *appOff) {
    for (uint32_t off = 0; off + 0x24 <= fsz; off += 0x10000) {      // app parts are 64KB-aligned
        uint8_t b0 = 0, d[4] = {0};
        if (!f.seek(off) || f.read(&b0, 1) != 1) break;
        if (b0 != 0xE9) continue;                                    // no image header here
        if (f.seek(off + 0x20) && f.read(d, 4) == 4 &&
            (uint32_t)(d[0] | (d[1] << 8) | (d[2] << 16) | ((uint32_t)d[3] << 24)) == 0xABCD5432u) {
            *appOff = off; return true;
        }
    }
    return false;
}
// Exact byte length of the ESP app image at file offset `base`: 24-byte header + segments
// + 1 checksum byte (16-aligned) + optional appended SHA-256. 0 = not a valid image.
static uint32_t espAppImageLen(BinSrc &f, uint32_t base) {
    uint8_t hdr[24];
    if (!f.seek(base) || f.read(hdr, 24) != 24 || hdr[0] != 0xE9) return 0;
    uint8_t segs = hdr[1]; bool hashApp = (hdr[23] == 1);
    uint32_t off = 24;
    for (uint8_t i = 0; i < segs; i++) {
        uint8_t sh[8];
        if (!f.seek(base + off) || f.read(sh, 8) != 8) return 0;
        uint32_t dlen = sh[4] | (sh[5] << 8) | (sh[6] << 16) | ((uint32_t)sh[7] << 24);
        if (dlen > 0x200000) return 0;                               // sanity
        off += 8 + dlen;
    }
    off = (off + 1 + 15) & ~15U;                                     // checksum byte, 16-align
    if (hashApp) off += 32;                                          // appended SHA-256
    return off;
}
// ---- Stage 2: synthesize a per-firmware partition table (recovery preserved) --------
struct PEntry { uint8_t type, sub; uint32_t off, size; char label[17]; };
static void md5sum(const uint8_t *d, size_t n, uint8_t out[16]) {
    mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_MD5), d, n, out);
}
// Read the firmware merged bin's OWN partition table (file offset 0x8000).
static int mergedParseTable(BinSrc &f, PEntry *out, int maxn) {
    int n = 0; uint8_t e[32];
    for (int i = 0; i < 32 && n < maxn; i++) {
        if (!f.seek(0x8000 + (uint32_t)i * 32) || f.read(e, 32) != 32) break;
        if ((uint16_t)(e[0] | (e[1] << 8)) != 0x50AA) break;   // ESP_PARTITION_MAGIC (LE bytes AA 50)
        out[n].type = e[2]; out[n].sub = e[3];
        out[n].off  = e[4] | (e[5] << 8) | (e[6] << 16) | ((uint32_t)e[7] << 24);
        out[n].size = e[8] | (e[9] << 8) | (e[10] << 16) | ((uint32_t)e[11] << 24);
        memcpy(out[n].label, &e[12], 16); out[n].label[16] = 0;
        n++;
    }
    return n;
}
// Build our recovery-preserving table: fixed prefix + ota_0 (firmware app, sized to it)
// + the firmware's own data partitions relocated to pack after ota_0 (labels/sizes kept).
static int synthTable(PEntry *fw, int fwn, uint32_t appLen, PEntry *s, int maxn) {
    int n = 0;
    #define ADD(T,S,O,SZ,L) do { if (n<maxn){ s[n].type=(T); s[n].sub=(S); s[n].off=(O); s[n].size=(SZ); strncpy(s[n].label,(L),16); s[n].label[16]=0; n++; } } while(0)
    ADD(1, 0x02, 0x9000,  0x4000,   "nvs");
    ADD(1, 0x40, 0xd000,  0x1000,   "bootsel");
    ADD(1, 0x00, 0xe000,  0x2000,   "otadata");
    ADD(0, 0x00, 0x10000, 0x200000, "factory");           // recovery (fixed)
    uint32_t ota0 = (appLen + 0xFFFFu) & ~0xFFFFu; if (ota0 < 0x100000) ota0 = 0x100000;
    ADD(0, 0x10, 0x210000, ota0, "ota_0");                // firmware app
    uint32_t cur = 0x210000 + ota0;
    for (int i = 0; i < fwn; i++) {                       // the firmware's own data parts
        if (fw[i].type == 0) continue;                                  // apps -> ota_0
        if (fw[i].off == 0x9000 || fw[i].off == 0xe000 || fw[i].sub == 0x02) continue;
        if (!strcmp(fw[i].label,"nvs") || !strcmp(fw[i].label,"otadata") || !strcmp(fw[i].label,"phy_init")) continue;
        cur = (cur + 0xFFFFu) & ~0xFFFFu;
        if ((uint64_t)cur + fw[i].size > 0x1000000ULL) continue;        // wouldn't fit in 16MB
        ADD(fw[i].type, fw[i].sub, cur, fw[i].size, fw[i].label);
        cur += fw[i].size;
    }
    #undef ADD
    return n;
}
// Serialize entries + the 0xEBEB/MD5 trailer into a 0xC00 table image (rest 0xFF = end).
static void buildPartBin(PEntry *s, int n, uint8_t *out) {
    memset(out, 0xFF, 0xC00);
    int p = 0;
    for (int i = 0; i < n; i++, p += 32) {
        out[p]=0xAA; out[p+1]=0x50; out[p+2]=s[i].type; out[p+3]=s[i].sub;   // magic 0x50AA, LE
        out[p+4]=s[i].off;  out[p+5]=s[i].off>>8;  out[p+6]=s[i].off>>16;  out[p+7]=s[i].off>>24;
        out[p+8]=s[i].size; out[p+9]=s[i].size>>8; out[p+10]=s[i].size>>16; out[p+11]=s[i].size>>24;
        memset(&out[p+12], 0, 16); memcpy(&out[p+12], s[i].label, strnlen(s[i].label, 16));
        out[p+28]=out[p+29]=out[p+30]=out[p+31]=0;        // flags
    }
    uint8_t md[16]; md5sum(out, p, md);
    out[p]=0xEB; out[p+1]=0xEB; memset(&out[p+2], 0xFF, 14); memcpy(&out[p+16], md, 16);
}
// Gate before flashing a synthesized table to 0x8000: every entry must have the right magic
// and fit in flash, the MD5 must match, and -- critically -- the recovery (factory@0x10000),
// launch flag (bootsel@0xd000) and slot (ota_0) must all be present. Anything off => no flash.
static bool validatePartBin(const uint8_t *pt) {
    int p = 0; bool fac = false, bs = false, o0p = false;
    for (; p + 32 <= 0xC00; p += 32) {
        uint16_t mg = pt[p] | (pt[p+1] << 8);
        if (mg == 0xEBEB) break;                                  // md5 trailer reached
        if (mg != 0x50AA) return false;                           // bad/garbage entry
        uint8_t type = pt[p+2], sub = pt[p+3];
        uint32_t off = pt[p+4] | (pt[p+5]<<8) | (pt[p+6]<<16) | ((uint32_t)pt[p+7]<<24);
        uint32_t sz  = pt[p+8] | (pt[p+9]<<8) | (pt[p+10]<<16) | ((uint32_t)pt[p+11]<<24);
        if ((uint64_t)off + sz > 0x1000000ULL || (off & 0xFFF)) return false;
        if (type==0 && sub==0x00 && off==0x10000 && sz==0x200000) fac = true;  // recovery
        if (type==1 && off==0xd000) bs = true;                                 // launch flag
        if (type==0 && sub==0x10) o0p = true;                                  // slot
    }
    if (p == 0 || p + 32 > 0xC00) return false;
    uint8_t md[16]; md5sum(pt, p, md);
    return (memcmp(md, &pt[p+16], 16) == 0) && fac && bs && o0p;
}
// Install an SD .bin into ota_0. Accepts an app-only image OR a merged/factory bin (we
// extract the app from the latter). Returns true on success (esp_ota validates the image).
static bool installBinCore(BinSrc &f, const String &name) {
    header("Switch firmware"); bodyClear();
    textAt(8, 44, COL_TXT, 1, ("Installing " + name).c_str());
    const esp_partition_t *o0 = otaSlot();
    if (!o0) { textAt(8, 70, COL_ERR, 1, "No OTA slot."); return false; }
    uint32_t fsz = f.size();
    uint32_t appOff = 0, appLen = 0;
    if (!findAppImage(f, fsz, &appOff)) {
        // No app signature found -- dump what the file actually starts with, so we can tell.
        uint8_t b0 = 0, d20[4] = {0}, p8[4] = {0};
        f.seek(0);      f.read(&b0, 1);
        f.seek(0x20);   f.read(d20, 4);
        f.seek(0x8000); f.read(p8, 4);
        textAt(8, 64, COL_ERR, 1, "No app image found in this file.");
        char m[60]; snprintf(m, sizeof m, "sz=%uKB b0=%02X @20=%02X%02X%02X%02X @8000=%02X%02X",
                 (unsigned)(fsz / 1024), b0, d20[0], d20[1], d20[2], d20[3], p8[0], p8[1]);
        textAt(8, 80, COL_DIM, 1, m);
        f.close(); return false;
    }
    appLen = espAppImageLen(f, appOff);
    if (appLen == 0 || (uint64_t)appOff + appLen > fsz) {
        char m[48]; snprintf(m, sizeof m, "bad app image @0x%X len=%uKB", (unsigned)appOff, (unsigned)(appLen / 1024));
        textAt(8, 64, COL_ERR, 1, m); f.close(); return false;
    }
    if (appLen > o0->size) { textAt(8, 64, COL_ERR, 1, "App larger than the slot."); f.close(); return false; }
    // Stage 2: for a merged bin, synthesize a per-firmware partition table (recovery kept) so
    // the firmware runs with its OWN filesystem. App-only images run under our resident table.
    static uint8_t pt[0xC00]; static PEntry fw[16], syn[16];
    bool haveTable = false; int sn = 0;
    if (appOff == 0) textAt(8, 64, COL_DIM, 1, "app-only image");
    else {
        char m[56]; snprintf(m, sizeof m, "merged bin: app @0x%X  %u KB", (unsigned)appOff, (unsigned)(appLen / 1024)); textAt(8, 64, COL_DIM, 1, m);
        int fwn = mergedParseTable(f, fw, 16);
        sn = synthTable(fw, fwn, appLen, syn, 16);
        buildPartBin(syn, sn, pt);
        if (!validatePartBin(pt)) { textAt(8, 82, COL_ERR, 1, "Synthesized table invalid - aborting."); f.close(); return false; }
        haveTable = true;
        char m2[48]; snprintf(m2, sizeof m2, "Stage2: %d-part table (recovery kept)", sn); textAt(8, 82, COL_OK, 1, m2);
    }
    // SAFETY: never erase the partition we're running from.
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (run && run->address == o0->address) {
        bodyClear(); textAt(8, 44, COL_ERR, 2, "SLOT == RUNNING");
        char rs[60]; snprintf(rs, sizeof rs, "run=%s@0x%X slot=%s@0x%X", run->label, (unsigned)run->address, o0->label, (unsigned)o0->address);
        textAt(8, 82, COL_DIM, 1, rs); textAt(8, 100, COL_DIM, 1, "Refusing to erase the running app.");
        f.close(); return false;
    }
    char b[48]; snprintf(b, sizeof b, "Writing %u KB into the slot...", (unsigned)(appLen / 1024)); textAt(8, 100, COL_TXT, 1, b);
    // RAW per-sector erase+write of the app into ota_0 (esp_ota_begin conflicts in our
    // launch-flag model; per-sector keeps each flash op short so the watchdog stays fed).
    uint8_t *buf = ioBuf(); uint32_t done = 0; esp_err_t err = ESP_OK; s_next_mark = 0;
    f.seek(appOff);
    while (done < appLen) {
        uint32_t want = appLen - done; if (want > IO_SZ) want = IO_SZ;
        int rd = f.read(buf, want); if (rd <= 0) { err = ESP_FAIL; break; }
        if (esp_partition_erase_range(o0, done, 0x1000) != ESP_OK) { err = ESP_FAIL; break; }
        if (esp_partition_write(o0, done, buf, rd) != ESP_OK)      { err = ESP_FAIL; break; }
        done += rd; progressCb(done, appLen); delay(0);
    }
    f.close();
    if (err != ESP_OK) { bodyClear(); textAt(8, 44, COL_ERR, 2, "FLASH FAILED"); textAt(8, 80, COL_DIM, 1, "write / read error"); return false; }
    // Validate the written app image (catches a bad extraction before we commit the table).
    esp_partition_pos_t pp = { .offset = o0->address, .size = o0->size };
    esp_image_metadata_t mdj;
    if (esp_image_verify(ESP_IMAGE_VERIFY, &pp, &mdj) != ESP_OK) {
        bodyClear(); textAt(8, 44, COL_ERR, 2, "IMAGE INVALID"); textAt(8, 82, COL_DIM, 1, "extracted app failed validation."); return false;
    }
    // Stage 2: commit the per-firmware table LAST (app written + verified). Blank each
    // relocated data partition's first sector so the firmware formats a fresh filesystem.
    if (haveTable) {
        for (int i = 0; i < sn; i++)
            if (syn[i].type == 1 && syn[i].off >= 0x210000) esp_flash_erase_region(NULL, syn[i].off, 0x1000);
        if (esp_flash_erase_region(NULL, 0x8000, 0x1000) != ESP_OK || esp_flash_write(NULL, pt, 0x8000, 0xC00) != ESP_OK) {
            bodyClear(); textAt(8, 44, COL_ERR, 2, "TABLE FLASH FAIL");
            textAt(8, 80, COL_DIM, 1, "Old table intact; recovery still boots."); return false;
        }
    }
    textAt(8, 120, COL_OK, 1, "Installed. Booting firmware...");
    delay(800);
    return true;
}
// SD entry point: open the file, wrap it, run the shared installer.
static bool installBin(const String &path) {
    String name = path; int sl0 = name.lastIndexOf('/'); if (sl0 >= 0) name = name.substring(sl0 + 1);
    File file = SD.open(path, FILE_READ);
    if (!file) { header("Switch firmware"); bodyClear(); textAt(8, 70, COL_ERR, 1, "Cannot open file."); return false; }
    BinSrc f; f.file = &file;
    bool ok = installBinCore(f, name);
    file.close();
    return ok;
}
// Wi-Fi entry point: install a bin already downloaded into a (PSRAM) buffer.
static bool installBinMem(const uint8_t *buf, uint32_t len, const String &name) {
    BinSrc f; f.mem = buf; f.mlen = len;
    return installBinCore(f, name);
}
// The firmware library: list SD *.bin, mark the one in the slot, run the chosen one.
// ---------------- firmware library: /firmwares/<name>/{bins,backups} ----------------
// The currently-loaded firmware is tracked on the SD (firmwares can't wipe it the way they
// can our shared nvs), so the "loaded" marker stays correct across switches.
static String currentFw() {
    File f = SD.open("/firmwares/current.txt", FILE_READ); if (!f) return "";
    String s = f.readStringUntil('\n'); f.close(); s.trim(); return s;
}
static void setCurrentFw(const String &n) {
    SD.mkdir("/firmwares"); SD.remove("/firmwares/current.txt");
    File f = SD.open("/firmwares/current.txt", FILE_WRITE);
    if (f) { f.print(n); f.print('\n'); f.flush(); f.close(); }
    setInstalledFw(n);     // NVS copy too -- the launcher countdown shows it before the SD mounts
}
// Record the loaded firmware on the SD (+ an NVS copy) BEFORE any flash op, while the card is
// freshly mounted -- so the "loaded" marker stays correct even if a later step reboots us.
static void markLoaded(const String &folder) {
    setCurrentFw(folder);
}
static int listFirmwareFolders(String *names, int maxn) {
    SD.mkdir("/firmwares");
    File root = SD.open("/firmwares"); if (!root) return 0;
    int n = 0; File e;
    while (n < maxn && (e = root.openNextFile())) {
        String nm = e.name(); int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1);
        if (e.isDirectory() && !nm.startsWith(".")) names[n++] = nm;   // each folder = a firmware
        e.close();
    }
    root.close(); return n;
}
// Full path to the first *.bin inside /firmwares/<folder>/bins/, or "" if none.
static String firmwareBinPath(const String &folder) {
    String dir = "/firmwares/" + folder + "/bins";
    File d = SD.open(dir); if (!d) return "";
    String found = ""; File e;
    while ((e = d.openNextFile())) {
        String nm = e.name(); int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1);
        String up = nm; up.toUpperCase();
        if (!e.isDirectory() && !nm.startsWith(".") && up.endsWith(".BIN")) { found = nm; e.close(); break; }
        e.close();
    }
    d.close();
    return found.length() ? (dir + "/" + found) : "";
}
static void actUpdateFirmware(const String &folder);   // fwd decl (U: update app from UPDATE/)
static bool confirmDelete(const String &folder);       // fwd decl (D: irreversible confirm)

// Dump the running ota_0 app image to a seed bin on the SD (only md.image_len
// bytes, not the whole slot). This is what makes the auto-created "meshcomod"
// library entry a real, re-flashable/bootable firmware. delay() feeds the WDT
// across the multi-MB copy. Returns false (and cleans up) on any read/write error.
static bool dumpOtaApp(const esp_partition_t *o0, const String &path) {
    if (!o0) return false;
    esp_partition_pos_t pos = { .offset = o0->address, .size = o0->size };
    esp_image_metadata_t md;
    if (esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &md) != ESP_OK) return false;
    size_t len = md.image_len;
    if (len == 0 || len > o0->size) return false;
    SD.remove(path);
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    const size_t CH = 8192;
    uint8_t *buf = (uint8_t *)malloc(CH);
    if (!buf) { f.close(); SD.remove(path); return false; }
    bool ok = true;
    for (size_t off = 0; off < len; off += CH) {
        size_t n = (len - off < CH) ? (len - off) : CH;
        if (esp_partition_read(o0, off, buf, n) != ESP_OK || f.write(buf, n) != n) { ok = false; break; }
        if ((off & 0xFFFF) == 0) delay(1);   // yield/feed the watchdog during the ~3 MB copy
    }
    free(buf);
    f.close();
    if (!ok) SD.remove(path);
    return ok;
}

// Run on every successful SD mount (idempotent): make sure the firmware library
// and the recovery-card folders exist, and seed a bootable "meshcomod" entry
// from whatever firmware is installed in ota_0 -- so a freshly-inserted card
// immediately offers meshcomod in the Firmwares menu, no manual "Add firmware".
static void ensureSdLayout() {
    SD.mkdir("/firmwares");
    static const char *kFolders[] = { "/BINS", "/UPDATES", "/RECBCK", "/SETTINGS", "/MAPS", "/LOGS" };
    for (auto d : kFolders) if (!SD.exists(d)) SD.mkdir(d);

    // Seed "meshcomod" from the installed slot if the library doesn't have it yet.
    if (firmwareBinPath("meshcomod").length() == 0) {
        const esp_partition_t *o0 = otaSlot();
        if (slotHasApp(o0)) {
            SD.mkdir("/firmwares/meshcomod");
            SD.mkdir("/firmwares/meshcomod/bins");
            SD.mkdir("/firmwares/meshcomod/backups");
            if (dumpOtaApp(o0, "/firmwares/meshcomod/bins/meshcomod.bin")) {
                // Mark it the loaded firmware (only if nothing else claims it) so the
                // Firmwares menu shows it as current and "just boots" it instantly.
                if (currentFw().length() == 0) setCurrentFw("meshcomod");
            }
        }
    }
}
static bool rmRf(const String &path);                  // fwd decl (recursive folder delete)
static void actFirmwares() {
    mountWithRetry("Firmwares");
    static String folders[24];
    int n = listFirmwareFolders(folders, 24);
    if (n == 0) {
        bodyClear();
        textAt(8, 56, COL_ERR, 1, "No firmwares on the SD card.");
        textAt(8, 78, COL_DIM, 1, "Create /firmwares/<name>/bins/ and put");
        textAt(8, 92, COL_DIM, 1, "a firmware .bin inside it.");
        footer("click / Enter: back"); waitForSelect(); return;
    }
    String cur = currentFw();
    static String disp[24];
    for (int i = 0; i < n; i++) disp[i] = (folders[i] == cur ? "* " : "  ") + folders[i];
    for (;;) {
        int act = 0;
        int idx = browseListEx("Firmwares", disp, n, "click:load  U:update  D:delete", &act);
        if (idx < 0) return;
        String folder = folders[idx];
        if (act == 1) {                                  // U -> update this firmware's app in place
            actUpdateFirmware(folder);                   // boots on success; returns on abort/failure
            cur = currentFw();
            for (int i = 0; i < n; i++) disp[i] = (folders[i] == cur ? "* " : "  ") + folders[i];
            continue;
        }
        if (act == 2) {                                  // D -> delete the whole firmware folder
            if (confirmDelete(folder)) {
                header("Delete firmware"); bodyClear();
                textAt(8, 60, COL_TXT, 1, ("Deleting " + folder + " ...").c_str());
                bool okd = rmRf("/firmwares/" + folder);
                if (currentFw() == folder) SD.remove("/firmwares/current.txt");   // drop dangling ref
                bodyClear(); textAt(8, 60, okd ? COL_OK : COL_ERR, 2, okd ? "DELETED" : "DELETE FAILED");
                delay(900);
                n = listFirmwareFolders(folders, 24);
                if (n == 0) return;                      // nothing left -> back to menu
                cur = currentFw();
                for (int i = 0; i < n; i++) disp[i] = (folders[i] == cur ? "* " : "  ") + folders[i];
            }
            continue;
        }
        if (folder == cur && slotHasApp(otaSlot())) {   // already the loaded firmware -> just boot
            header("Boot"); bodyClear();
            textAt(8, 60, COL_TXT, 1, ("Booting " + folder + " ...").c_str());
            launchFirmware();                           // reboots; never returns
        }
        String snap = latestBackupDir(folder);           // a saved snapshot from a previous session?
        String binpath = firmwareBinPath(folder);        // the seed bin (first install only)
        if (snap.length() == 0 && binpath.length() == 0) {
            header("Firmwares"); bodyClear();
            textAt(8, 60, COL_ERR, 1, ("Nothing to load for " + folder).c_str());
            textAt(8, 80, COL_DIM, 1, ("Put a .bin in " + folder + "/bins/").c_str());
            footer("click / Enter: back"); waitForSelect(); continue;
        }
        if (cur.length() && cur != folder) backupFirmware(cur, false);  // snapshot the outgoing firmware
        markLoaded(folder);                              // record the new loaded firmware (SD freshly mounted)
        if (snap.length()) loadSnapshot(snap);           // has a backup -> load it back exactly as saved
        else if (installBin(binpath)) launchFirmware(otaSlot());  // first time -> flash the seed bin into ota_0, then boot it
        else { footer("click / Enter: back"); waitForSelect(); }
    }
}
// Add a firmware from a .bin dropped in the SD ROOT: derive a folder name, create
// /firmwares/<name>/{bins,backups}, move the bin in, then install + load it.
static void actAddFirmware() {
    mountWithRetry("Add firmware");
    static String names[24];
    int n = scanFiles(names, 24, "", ".BIN");          // *.bin in the SD root
    if (n == 0) {
        bodyClear();
        textAt(8, 56, COL_ERR, 1, "No .bin in the SD card root.");
        textAt(8, 78, COL_DIM, 1, "Copy a firmware .bin to the card root,");
        textAt(8, 92, COL_DIM, 1, "then use Add firmware.");
        footer("click / Enter: back"); waitForSelect(); return;
    }
    int idx = browseList("Add firmware (root)", names, n, "click: add + load");
    if (idx < 0) return;
    String bin = names[idx];
    // folder name = bin basename minus ".bin" and a trailing "-merged"
    String fld = bin; int dot = fld.lastIndexOf('.'); if (dot >= 0) fld = fld.substring(0, dot);
    String low = fld; low.toLowerCase(); if (low.endsWith("-merged")) fld = fld.substring(0, fld.length() - 7);
    String fdir = "/firmwares/" + fld;
    header("Add firmware"); bodyClear();
    textAt(8, 44, COL_TXT, 1, ("Adding '" + fld + "'").c_str());
    SD.mkdir("/firmwares"); SD.mkdir(fdir); SD.mkdir(fdir + "/bins"); SD.mkdir(fdir + "/backups");
    String dest = fdir + "/bins/" + bin;
    SD.remove(dest);                                   // overwrite if re-adding
    if (!SD.rename("/" + bin, dest)) {
        textAt(8, 70, COL_ERR, 1, "Could not move the .bin into the folder.");
        footer("click / Enter: back"); waitForSelect(); return;
    }
    textAt(8, 64, COL_DIM, 1, ("-> " + dest).c_str());
    String prev = currentFw();
    if (prev.length() && prev != fld) backupFirmware(prev, false);   // snapshot the outgoing firmware
    markLoaded(fld);
    if (installBin(dest)) launchFirmware();                          // fresh install from the seed bin, then boot
    else { footer("click / Enter: back"); waitForSelect(); }
}
// ---- P2: per-firmware backup -- snapshot the loaded firmware's writable partitions ----
// True if a serialized partition table (0xC00 bytes from 0x8000) is OUR recovery layout:
// recovery at factory@0x10000 AND the firmware slot at ota_0@0x210000. Guards against
// snapshotting or loading a FOREIGN firmware table (e.g. an A/B layout with ota_0@0x10000 and no
// recovery) -- restoring one would write over recovery and brick the launcher.
static bool tableIsRecoveryLayout(const uint8_t *tbl) {
    bool fac = false, o0 = false;
    for (int o = 0; o + 32 <= 0xC00; o += 32) {
        uint16_t mg = tbl[o] | (tbl[o + 1] << 8);
        if (mg != 0x50AA) break;
        uint8_t type = tbl[o + 2], sub = tbl[o + 3];
        uint32_t off = tbl[o + 4] | (tbl[o + 5] << 8) | (tbl[o + 6] << 16) | ((uint32_t)tbl[o + 7] << 24);
        if (type == ESP_PARTITION_TYPE_APP && sub == ESP_PARTITION_SUBTYPE_APP_FACTORY && off == 0x10000)  fac = true;
        if (type == ESP_PARTITION_TYPE_APP && sub == ESP_PARTITION_SUBTYPE_APP_OTA_0   && off == 0x110000) o0  = true;
    }
    return fac && o0;
}
// A snapshot dir is loadable only if it has a recovery-layout table.img AND the app (ota_0.img).
static bool snapshotIsValid(const String &dir) {
    File tf = SD.open(dir + "/table.img", FILE_READ);
    if (!tf) return false;
    static uint8_t t[0xC00]; bool got = (tf.read(t, 0xC00) == 0xC00); tf.close();
    if (!got || !tableIsRecoveryLayout(t)) return false;
    File af = SD.open(dir + "/ota_0.img", FILE_READ);
    if (!af) return false;
    af.close(); return true;
}
static int nextBackupSeq(const String &bdir) {
    File d = SD.open(bdir); if (!d) return 1;
    int maxn = 0; File e;
    while ((e = d.openNextFile())) {
        String nm = e.name(); int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1);
        if (e.isDirectory()) { int v = nm.toInt(); if (v > maxn) maxn = v; }
        e.close();
    }
    d.close(); return maxn + 1;
}
// Newest LOADABLE snapshot for a firmware, or "" if none. Skips junk/foreign backups (no
// table.img / wrong layout / no app) so a bad backup never blocks a clean fresh-install.
static String latestBackupDir(const String &folder) {
    String bdir = "/firmwares/" + folder + "/backups";
    static int seqs[64]; int ns = 0;
    File d = SD.open(bdir);
    if (d) { File e;
        while (ns < 64 && (e = d.openNextFile())) {
            String nm = e.name(); int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1);
            if (e.isDirectory()) { int v = nm.toInt(); if (v > 0) seqs[ns++] = v; }
            e.close();
        }
        d.close();
    }
    for (int pass = 0; pass < ns; pass++) {           // newest-first; return the first that loads
        int best = -1, bi = -1;
        for (int i = 0; i < ns; i++) if (seqs[i] > best) { best = seqs[i]; bi = i; }
        if (bi < 0) break;
        char sub[8]; snprintf(sub, sizeof sub, "%04d", best);
        String cand = bdir + "/" + sub;
        if (snapshotIsValid(cand)) return cand;
        seqs[bi] = -1;                                 // discard this one, try the next-newest
    }
    return String();
}
static bool backupOnePartition(const esp_partition_t *p, const String &path) {
    File f = SD.open(path, FILE_WRITE); if (!f) return false;
    uint8_t *buf = ioBuf(); uint32_t done = 0; bool ok = true; s_next_mark = 0;
    while (done < p->size) {
        uint32_t want = p->size - done; if (want > IO_SZ) want = IO_SZ;
        if (esp_partition_read(p, done, buf, want) != ESP_OK) { ok = false; break; }
        if (f.write(buf, want) != (size_t)want) { ok = false; break; }
        done += want; progressCb(done, p->size); delay(0);
    }
    f.flush(); f.close();
    return ok;
}
// A backup is a complete, loadable SNAPSHOT of the loaded firmware: its partition table, its
// app (ota_0) and every data partition -> /firmwares/<folder>/backups/NNNN/{table.img,<label>.img}.
// Loading it later restores the firmware exactly as saved. Recovery (factory) and our own
// bootsel/otadata are left out -- they're never the firmware's to keep or restore.
static bool backupFirmware(const String &folder, bool wait) {
    if (folder.length() == 0) return false;
    // Refuse to snapshot unless the device is in OUR recovery layout -- otherwise we'd capture a
    // foreign firmware table (e.g. A/B ota_0@0x10000) that can never be loaded back safely.
    static uint8_t tbl[0xC00];
    if (esp_flash_read(NULL, tbl, 0x8000, 0xC00) != ESP_OK || !tableIsRecoveryLayout(tbl)) {
        bodyClear();
        textAt(8, 44, COL_ERR, 2, "CANNOT BACK UP");
        textAt(8, 80, COL_DIM, 1, "Device not in recovery layout");
        textAt(8, 96, COL_DIM, 1, "(foreign partition table).");
        if (wait) { footer("click / Enter: back"); waitForSelect(); } else delay(1200);
        return false;
    }
    String fdir = "/firmwares/" + folder, bdir = fdir + "/backups";
    SD.mkdir("/firmwares"); SD.mkdir(fdir); SD.mkdir(bdir);
    char sub[8]; snprintf(sub, sizeof sub, "%04d", nextBackupSeq(bdir));
    String sdir = bdir + "/" + sub; SD.mkdir(sdir);
    bool ok = true;
    // 1) the partition table -- makes the snapshot self-describing, so it can be loaded on its own.
    { File tf = SD.open(sdir + "/table.img", FILE_WRITE);
      if (tf) { ok = (tf.write(tbl, 0xC00) == 0xC00); tf.flush(); tf.close(); } else ok = false;
    }
    // 2) the app (ota_0) + every data partition. Skip recovery (factory) and our bootsel/otadata.
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; ok && it != NULL; it = esp_partition_next(it)) {
        const esp_partition_t *p = esp_partition_get(it);
        if (p->type == ESP_PARTITION_TYPE_APP && p->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) continue; // recovery
        if (!strcmp(p->label, "bootsel") || !strcmp(p->label, "otadata")) continue;                          // ours
        header("Backup"); bodyClear();
        textAt(8, 44, COL_TXT, 1, ("Backup '" + folder + "' #" + sub).c_str());
        char l[48]; snprintf(l, sizeof l, "%s  (%u KB)", p->label, (unsigned)(p->size / 1024));
        textAt(8, 70, COL_DIM, 1, l);
        if (!backupOnePartition(p, sdir + "/" + String(p->label) + ".img")) ok = false;
    }
    if (it) esp_partition_iterator_release(it);
    bodyClear();
    if (ok) { textAt(8, 44, COL_OK, 2, "BACKUP DONE"); textAt(8, 84, COL_DIM, 1, (folder + "/backups/" + sub).c_str()); }
    else    { textAt(8, 44, COL_ERR, 2, "BACKUP FAILED"); }
    if (wait) { footer("click / Enter: back"); waitForSelect(); } else delay(900);
    return ok;
}
// Restore a backup SNAPSHOT (table + app + data) and boot it -- the firmware comes back exactly
// as it was saved, WITHOUT touching the bin. Recovery (factory) and our bootsel/otadata are never
// written. Used for "load a firmware that already has a backup" and the Restore screen.
static void loadSnapshot(const String &dir) {
    static uint8_t tbl[0xC00];
    File tf = SD.open(dir + "/table.img", FILE_READ);
    bool haveTbl = (tf && tf.read(tbl, 0xC00) == 0xC00);
    if (tf) tf.close();
    if (!haveTbl) {                                   // can't restore a layout we don't have
        header("Load backup"); bodyClear();
        textAt(8, 44, COL_ERR, 2, "SNAPSHOT INCOMPLETE");
        textAt(8, 84, COL_DIM, 1, "missing table.img -- cannot load.");
        footer("click / Enter: back"); waitForSelect(); return;
    }
    if (!tableIsRecoveryLayout(tbl)) {                // foreign table -> would overwrite recovery
        header("Load backup"); bodyClear();
        textAt(8, 44, COL_ERR, 2, "INCOMPATIBLE BACKUP");
        textAt(8, 80, COL_DIM, 1, "Not a recovery-layout snapshot");
        textAt(8, 96, COL_DIM, 1, "(foreign table -- refusing).");
        footer("click / Enter: back"); waitForSelect(); return;
    }
    // Restore each saved partition to the offset its OWN table entry names (absolute flash ops,
    // independent of the table currently resident). Partitions FIRST, table LAST -- so an
    // interrupted restore leaves the old, working table in place and recovery still boots.
    const esp_partition_t *run = esp_ota_get_running_partition();
    bool ok = true;
    uint32_t o0off = 0, o0sz = 0; bool o0found = false; int nent = 0, nrest = 0;
    for (int o = 0; o + 32 <= 0xC00; o += 32) {
        uint16_t mg = tbl[o] | (tbl[o + 1] << 8);
        if (mg != 0x50AA) break;                      // 0xEBEB (md5 trailer) / 0xFFFF (erased) -> end
        nent++;
        uint8_t  type = tbl[o + 2], sub = tbl[o + 3];
        uint32_t off  = tbl[o + 4] | (tbl[o + 5] << 8) | (tbl[o + 6] << 16) | ((uint32_t)tbl[o + 7] << 24);
        uint32_t sz   = tbl[o + 8] | (tbl[o + 9] << 8) | (tbl[o + 10] << 16) | ((uint32_t)tbl[o + 11] << 24);
        char label[17]; memcpy(label, &tbl[o + 12], 16); label[16] = 0;
        bool isApp0 = (type == ESP_PARTITION_TYPE_APP && sub == ESP_PARTITION_SUBTYPE_APP_OTA_0);
        if (isApp0) { o0off = off; o0sz = sz; }       // remember the app slot for the verify gate
        if (type == ESP_PARTITION_TYPE_APP && sub == ESP_PARTITION_SUBTYPE_APP_FACTORY) continue; // recovery
        if (!strcmp(label, "bootsel") || !strcmp(label, "otadata")) continue;                      // ours
        if (run && off == run->address) continue;     // never write the partition we run from
        File rf = SD.open(dir + "/" + String(label) + ".img", FILE_READ);
        if (!rf) continue;                            // not in this snapshot -> leave region as-is
        header("Load backup"); bodyClear();
        textAt(8, 44, COL_TXT, 1, ("Restoring " + String(label)).c_str());
        char l[48]; snprintf(l, sizeof l, "%s  @0x%X", label, (unsigned)off); textAt(8, 70, COL_DIM, 1, l);
        uint8_t *rb = ioBuf(); uint32_t done = 0, tot = (uint32_t)rf.size();
        if (tot > sz) tot = sz; s_next_mark = 0;
        while (done < tot) {
            uint32_t want = tot - done; if (want > IO_SZ) want = IO_SZ;
            int rd = rf.read(rb, want); if (rd <= 0) { ok = false; break; }
            if (esp_flash_erase_region(NULL, off + done, 0x1000) != ESP_OK) { ok = false; break; }
            if (esp_flash_write(NULL, rb, off + done, (rd + 3) & ~3) != ESP_OK) { ok = false; break; }
            done += rd; progressCb(done, tot); delay(0);
        }
        rf.close();
        if (!ok) break;
        nrest++; if (isApp0) o0found = true;
    }
    if (!ok) {
        header("Load backup"); bodyClear();
        textAt(8, 44, COL_ERR, 2, "RESTORE FAILED");
        textAt(8, 84, COL_DIM, 1, "Old table intact; recovery still boots.");
        footer("click / Enter: back"); waitForSelect(); return;
    }
    // VERIFY the restored app BEFORE committing the table / booting. A bad or incomplete snapshot
    // must never set the launch flag -- otherwise the bootloader boots a broken slot and we loop.
    esp_err_t vr = ESP_FAIL; esp_image_metadata_t md = {};
    if (o0found && o0off) {
        esp_partition_pos_t pos = { .offset = o0off, .size = (o0sz ? o0sz : 0x700000u) };
        vr = esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &md);
    }
    header("Load backup"); bodyClear();
    { char d1[60]; snprintf(d1, sizeof d1, "parts=%d restored=%d ota0img=%s", nent, nrest, o0found ? "YES" : "NO");
      textAt(8, 40, COL_DIM, 1, d1);
      char d2[60]; snprintf(d2, sizeof d2, "ota_0 @0x%X  %u KB", (unsigned)o0off, (unsigned)(o0sz / 1024));
      textAt(8, 58, COL_DIM, 1, d2);
      char d3[60]; snprintf(d3, sizeof d3, "verify=%s (0x%X) app=%u KB", vr == ESP_OK ? "OK" : "FAIL",
                            (unsigned)vr, (unsigned)(md.image_len / 1024));
      textAt(8, 76, vr == ESP_OK ? COL_OK : COL_ERR, 1, d3); }
    if (!o0found || vr != ESP_OK) {                   // abort -> menu, no boot loop
        textAt(8, 104, COL_ERR, 2, "NOT BOOTING");
        textAt(8, 136, COL_DIM, 1, o0found ? "restored app failed verify." : "no ota_0.img in this backup.");
        footer("click / Enter: back"); waitForSelect(); return;
    }
    textAt(8, 104, COL_TXT, 1, "App verified -- writing table...");
    delay(1500);
    // Commit the firmware's own table LAST, then boot it.
    if (esp_flash_erase_region(NULL, 0x8000, 0x1000) != ESP_OK || esp_flash_write(NULL, tbl, 0x8000, 0xC00) != ESP_OK) {
        header("Load backup"); bodyClear();
        textAt(8, 44, COL_ERR, 2, "TABLE FLASH FAIL");
        textAt(8, 84, COL_DIM, 1, "Old table intact; recovery still boots.");
        footer("click / Enter: back"); waitForSelect(); return;
    }
    header("Load backup"); bodyClear();
    textAt(8, 44, COL_OK, 2, "LOADED FROM BACKUP");
    delay(800);
    launchFirmware(otaSlot());                        // boot the restored ota_0 slot; never returns
}
// ---- U: update a loaded firmware's app in place (keeps its data) from /firmwares/<fw>/UPDATE/ ----
// List *.bin in an arbitrary directory (skips subdirs + macOS dotfiles).
static int listBinsIn(const String &dir, String *out, int maxn) {
    File d = SD.open(dir); if (!d) return 0;
    int n = 0; File e;
    while (n < maxn && (e = d.openNextFile())) {
        String nm = e.name(); int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1);
        String low = nm; low.toLowerCase();
        if (!e.isDirectory() && low.endsWith(".bin") && !nm.startsWith(".")) out[n++] = nm;
        e.close();
    }
    d.close(); return n;
}
// Write app image [appOff, appOff+appLen) from f into the ota_0 slot (raw, per-sector) + verify.
// Leaves the partition table and every data partition untouched.
static bool flashAppFromFile(File &f, uint32_t appOff, uint32_t appLen, const esp_partition_t *o0) {
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (!o0 || (run && run->address == o0->address)) return false;   // never erase the running app
    uint8_t *buf = ioBuf(); uint32_t done = 0; s_next_mark = 0; f.seek(appOff);
    while (done < appLen) {
        uint32_t want = appLen - done; if (want > IO_SZ) want = IO_SZ;
        int rd = f.read(buf, want); if (rd <= 0) return false;
        if (esp_partition_erase_range(o0, done, 0x1000) != ESP_OK) return false;
        if (esp_partition_write(o0, done, buf, rd) != ESP_OK)      return false;
        done += rd; progressCb(done, appLen); delay(0);
    }
    esp_partition_pos_t pp = { .offset = o0->address, .size = o0->size };
    esp_image_metadata_t mdj;
    return esp_image_verify(ESP_IMAGE_VERIFY, &pp, &mdj) == ESP_OK;
}
// Do the firmware's own data partitions in a freshly synthesized table already exist resident
// (same label + size)? If so the new app can reuse the existing data -> we only flash the app.
static bool synthMatchesResident(const PEntry *nsyn, int nsn, const uint8_t *rtbl) {
    for (int i = 0; i < nsn; i++) {
        if (nsyn[i].type != 1 || nsyn[i].off < 0x210000) continue;   // firmware data only (skip prefix)
        bool found = false;
        for (int o = 0; o + 32 <= 0xC00; o += 32) {
            uint16_t mg = rtbl[o] | (rtbl[o + 1] << 8); if (mg != 0x50AA) break;
            if (rtbl[o + 2] != 1) continue;                          // DATA entries only
            uint32_t sz = rtbl[o + 8] | (rtbl[o + 9] << 8) | (rtbl[o + 10] << 16) | ((uint32_t)rtbl[o + 11] << 24);
            char lbl[17]; memcpy(lbl, &rtbl[o + 12], 16); lbl[16] = 0;
            if (!strcmp(lbl, nsyn[i].label) && sz == nsyn[i].size) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}
// Re-flash our embedded launcher 2nd-stage bootloader to 0x0 (we run from factory, never 0x0).
// esp_flash_write can't source from memory-mapped flash, so copy each chunk to RAM first; then
// read back + compare -- a bad write here would brick the boot, so verify before relying on it.
static bool flashLauncherBootloader() {
    static uint8_t tmp[0x1000];
    uint32_t len = LAUNCHER_BL_LEN, done = 0; s_next_mark = 0;
    while (done < len) {
        uint32_t want = len - done; if (want > sizeof tmp) want = sizeof tmp;
        uint32_t wlen = (want + 3) & ~3u;
        memcpy(tmp, LAUNCHER_BL + done, want);
        if (wlen > want) memset(tmp + want, 0xFF, wlen - want);
        if (esp_flash_erase_region(NULL, done, 0x1000) != ESP_OK) return false;
        if (esp_flash_write(NULL, tmp, done, wlen) != ESP_OK)      return false;
        done += want; progressCb(done, len); delay(0);
    }
    done = 0;                                          // read-back verify before we trust it
    while (done < len) {
        uint32_t want = len - done; if (want > sizeof tmp) want = sizeof tmp;
        if (esp_flash_read(NULL, tmp, done, (want + 3) & ~3u) != ESP_OK) return false;
        if (memcmp(tmp, LAUNCHER_BL + done, want) != 0) return false;
        done += want;
    }
    return true;
}
// Write a serialized partition table (0xC00 bytes, RAM source) to 0x8000.
static bool flashTable(const uint8_t *tbl) {
    if (esp_flash_erase_region(NULL, 0x8000, 0x1000) != ESP_OK) return false;
    return esp_flash_write(NULL, tbl, 0x8000, 0xC00) == ESP_OK;
}
// Erase a synthesized table's firmware data partitions (>= 0x210000) -> fresh userdata. 64 KB
// blocks keep each erase short enough for the watchdog (a whole-partition erase in one call panics).
static bool eraseDataPartitions(const PEntry *syn, int sn) {
    for (int i = 0; i < sn; i++) {
        if (syn[i].type != 1 || syn[i].off < 0x210000) continue;       // firmware data only
        header("Update firmware"); bodyClear();
        textAt(8, 44, COL_TXT, 1, ("Erasing " + String(syn[i].label)).c_str());
        uint32_t done = 0, sz = syn[i].size; s_next_mark = 0;
        while (done < sz) {
            uint32_t chunk = sz - done; if (chunk > 0x10000) chunk = 0x10000;
            if (esp_flash_erase_region(NULL, syn[i].off + done, chunk) != ESP_OK) return false;
            done += chunk; progressCb(done, sz); delay(0);
        }
    }
    return true;
}
static void actUpdateFirmware(const String &folder) {
    // In-place app update keeps the firmware's data, so it only works on the LOADED firmware --
    // its app+data+table are resident. For any other firmware the slot/data belong elsewhere.
    static uint8_t rtbl[0xC00];
    bool layoutOK = (esp_flash_read(NULL, rtbl, 0x8000, 0xC00) == ESP_OK) && tableIsRecoveryLayout(rtbl);
    const esp_partition_t *o0 = otaSlot();
    if (!layoutOK || !o0) {
        header("Update firmware"); bodyClear();
        textAt(8, 50, COL_ERR, 1, "Not in the recovery layout.");
        footer("click / Enter: back"); waitForSelect(); return;
    }
    String udir = "/firmwares/" + folder + "/UPDATE";
    static String bins[24];
    int nb = listBinsIn(udir, bins, 24);
    if (nb == 0) {
        header("Update firmware"); bodyClear();
        textAt(8, 50, COL_ERR, 1, "No .bin in the UPDATE folder.");
        textAt(8, 74, COL_DIM, 1, "Put a newer .bin in:");
        textAt(8, 90, COL_DIM, 1, (folder + "/UPDATE/").c_str());
        footer("click / Enter: back"); waitForSelect(); return;
    }
    String title = "Update " + folder;
    int bi = browseList(title.c_str(), bins, nb, "click: use this bin");
    if (bi < 0) return;
    String path = udir + "/" + bins[bi];

    header("Update firmware"); bodyClear();
    textAt(8, 44, COL_TXT, 1, ("Updating " + folder).c_str());
    textAt(8, 64, COL_DIM, 1, bins[bi].c_str());
    File f = SD.open(path, FILE_READ);
    if (!f) { textAt(8, 90, COL_ERR, 1, "Cannot open the update bin."); footer("click / Enter: back"); waitForSelect(); return; }
    uint32_t fsz = (uint32_t)f.size(), appOff = 0;
    BinSrc bs; bs.file = &f;
    if (!findAppImage(bs, fsz, &appOff)) { f.close(); textAt(8, 90, COL_ERR, 1, "No app image in this bin."); footer("click / Enter: back"); waitForSelect(); return; }
    uint32_t appLen = espAppImageLen(bs, appOff);
    if (appLen == 0 || (uint64_t)appOff + appLen > fsz) { f.close(); textAt(8, 90, COL_ERR, 1, "Bad app image in this bin."); footer("click / Enter: back"); waitForSelect(); return; }
    if (appLen > o0->size) { f.close(); textAt(8, 90, COL_ERR, 1, "App larger than the slot."); footer("click / Enter: back"); waitForSelect(); return; }
    // app-only OR merged bin: flash just the app into ota_0 (keeps the fixed table + all data),
    // then boot it. A/B over-the-air OTA from inside the firmware is the normal update path now;
    // this is the manual "drop a .bin in UPDATE/ and reflash from recovery" fallback.
    textAt(8, 90, COL_DIM, 1, appOff == 0 ? "app-only bin" : "extracting app from merged bin");
    textAt(8, 112, COL_TXT, 1, "Flashing app into the slot...");
    bool ok = flashAppFromFile(f, appOff, appLen, o0);
    f.close();
    if (!ok) { bodyClear(); textAt(8, 44, COL_ERR, 2, "UPDATE FAILED"); textAt(8, 84, COL_DIM, 1, "write/verify error; data untouched."); footer("click / Enter: back"); waitForSelect(); return; }
    setCurrentFw(folder);
    bodyClear(); textAt(8, 44, COL_OK, 2, "UPDATED"); textAt(8, 84, COL_DIM, 1, "Booting the updated firmware..."); delay(900);
    launchFirmware(o0);                              // boot ota_0; never returns
}
// ---- D: delete a firmware (its whole folder) after an explicit, irreversible confirmation ----
static bool rmRf(const String &path) {
    File d = SD.open(path); if (!d) return false;
    if (!d.isDirectory()) { d.close(); return SD.remove(path); }
    d.close();
    for (;;) {                                       // reopen each pass: don't delete while iterating
        File dd = SD.open(path); if (!dd) break;
        File e = dd.openNextFile();
        if (!e) { dd.close(); break; }               // no children left
        String c = e.name(); if (!c.startsWith("/")) c = path + "/" + c;
        bool isdir = e.isDirectory(); e.close(); dd.close();
        if (isdir) { if (!rmRf(c)) return false; }
        else       { if (!SD.remove(c)) return false; }
    }
    return SD.rmdir(path);
}
static bool confirmDelete(const String &folder) {
    int sel = 0; bool redraw = true; drainTrackball(); armClick();   // default = Cancel (the safe one)
    for (;;) {
        if (redraw) {
            header("Delete firmware"); bodyClear();
            textAt(8, 40, COL_ERR, 2, "IRREVERSIBLE");
            textAt(8, 74, COL_TXT, 1, ("Delete '" + folder + "'").c_str());
            textAt(8, 92, COL_DIM, 1, "and its ENTIRE folder --");
            textAt(8, 108, COL_DIM, 1, "bins, backups, updates, all of it.");
            textAt(8, 140, sel == 0 ? COL_TXT : COL_DIM, 2, sel == 0 ? "> Cancel" : "  Cancel");
            textAt(8, 170, sel == 1 ? COL_ERR : COL_DIM, 2, sel == 1 ? "> DELETE" : "  DELETE");
            footer("up=Cancel  down=DELETE  click: choose"); redraw = false;
        }
        tdeckKeyboardPoll();
        int k = tdeckKeyboardReadKey();
        if      (k == 'w' || k == 'W') { sel = 0; redraw = true; }
        else if (k == 's' || k == 'S') { sel = 1; redraw = true; }
        else if (k == '\r' || k == '\n' || k == ' ') return sel == 1;
        if (clickPressEdge()) return sel == 1;
        int dx = 0, dy = 0;
        if (tdeckTrackballReadMotion(&dx, &dy)) {
            s_accumY += dy;
            if      (s_accumY >= 2)  { s_accumY = 0; sel = 1; redraw = true; }
            else if (s_accumY <= -2) { s_accumY = 0; sel = 0; redraw = true; }
        }
        delay(12);
    }
}
static void actBackup() {
    mountWithRetry("Backup");
    String cur = currentFw();
    if (cur.length() == 0) {
        bodyClear();
        textAt(8, 60, COL_ERR, 1, "No firmware is loaded to back up.");
        textAt(8, 80, COL_DIM, 1, "Load one from Firmwares first.");
        footer("click / Enter: back"); waitForSelect(); return;
    }
    backupFirmware(cur, true);
}
static void actInfo() {
    header("Device info"); bodyClear();
    uint32_t fsz = 0; esp_flash_get_size(NULL, &fsz);
    String inst = installedFw();
    char b[64]; int16_t y = 44;
    snprintf(b, sizeof b, "Chip:  %s rev %d", ESP.getChipModel(), ESP.getChipRevision()); textAt(8, y, COL_TXT, 1, b); y += 18;
    snprintf(b, sizeof b, "Flash: %u MB   PSRAM: %u MB", (unsigned)(fsz/(1024*1024)), (unsigned)(ESP.getPsramSize()/(1024*1024))); textAt(8, y, COL_TXT, 1, b); y += 18;
    snprintf(b, sizeof b, "MAC:   %012llx", (unsigned long long)ESP.getEfuseMac()); textAt(8, y, COL_TXT, 1, b); y += 18;
    const esp_partition_t *o0 = otaSlot();
    if (slotHasApp(o0)) { snprintf(b, sizeof b, "Slot:  %s", inst.length() ? inst.c_str() : "(firmware present)"); textAt(8, y, COL_OK, 1, b); }
    else                  textAt(8, y, COL_DIM, 1, "Slot:  empty");
    y += 18;
    String pid, prd; sdProbe(pid, prd);                    // raw card-level probe (always, for diag)
    if (sdBegin()) { snprintf(b, sizeof b, "SD:    %llu MB", (unsigned long long)(SD.cardSize()/(1024*1024))); textAt(8, y, COL_OK, 1, b); }
    else           { textAt(8, y, COL_ERR, 1, "SD:    not mounted"); }
    y += 16;
    textAt(8, y, COL_DIM, 1, ("probe: " + pid).c_str()); y += 12;
    if (prd.length()) textAt(8, y, COL_DIM, 1, ("  " + prd).c_str());
    footer("click / Enter: back"); waitForSelect();
}
static void actVerify(const String &imgName) {
    header("Verify backup"); bodyClear();
    textAt(8, 44, COL_TXT, 1, imgName.c_str());
    File f = SD.open("/" + imgName, FILE_READ);
    if (!f) { textAt(8, 70, COL_ERR, 1, "Cannot open image."); footer("click / Enter: back"); waitForSelect(); return; }
    uint32_t sz = (uint32_t)f.size();
    char b[56]; snprintf(b, sizeof b, "size: %u bytes (%u MB)", (unsigned)sz, (unsigned)(sz/(1024*1024))); textAt(8, 62, COL_DIM, 1, b);
    char devhash[65] = {0}; bool haveMan = readManifestSha(imgName, devhash, sizeof devhash);
    textAt(8, 82, COL_TXT, 1, "Recomputing SHA-256 from SD...");
    s_next_mark = 0; char calc[65] = {0}; sha256_file(&f, calc, progressCb); f.close();
    bodyClear(); textAt(8, 44, COL_TXT, 1, imgName.c_str());
    if (haveMan) {
        bool ok = (strcmp(calc, devhash) == 0);
        textAt(8, 70, ok ? COL_OK : COL_ERR, 2, ok ? "VERIFIED OK" : "MISMATCH!");
        textAt(8, 102, COL_DIM, 1, ok ? "matches manifest sha256:" : "file hash != manifest:");
    } else { textAt(8, 70, COL_DIM, 2, "no manifest"); textAt(8, 102, COL_DIM, 1, "file sha256:"); }
    printHashTwoLines(calc, 116);
    footer("click / Enter: back"); waitForSelect();
}
// List a firmware's saved snapshots (the NNNN dirs under .../backups), sorted oldest->newest.
static int listBackups(const String &folder, String *out, int maxn) {
    String bdir = "/firmwares/" + folder + "/backups";
    File d = SD.open(bdir); if (!d) return 0;
    int n = 0; File e;
    while (n < maxn && (e = d.openNextFile())) {
        String nm = e.name(); int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1);
        if (e.isDirectory() && !nm.startsWith(".")) out[n++] = nm;     // each NNNN dir = one snapshot
        e.close();
    }
    d.close();
    for (int i = 1; i < n; i++) { String k = out[i]; int j = i - 1;   // insertion sort (lists are tiny)
        while (j >= 0 && out[j] > k) { out[j + 1] = out[j]; j--; } out[j + 1] = k; }
    return n;
}
// P4: Restore screen -- pick a firmware, then one of its saved snapshots (roll back or forward),
// then load it. The currently-loaded firmware is snapshotted first, so the live state is never lost.
static void actRestore() {
    mountWithRetry("Restore from SD");
    static String folders[24];
    int n = listFirmwareFolders(folders, 24);
    if (n == 0) { bodyClear(); textAt(8, 60, COL_ERR, 1, "No firmwares on the SD card."); footer("click / Enter: back"); waitForSelect(); return; }
    for (;;) {
        int fi = browseList("Restore: pick firmware", folders, n, "click: choose");
        if (fi < 0) return;
        String folder = folders[fi];
        static String snaps[48];
        int sn = listBackups(folder, snaps, 48);
        if (sn == 0) {
            header("Restore from SD"); bodyClear();
            textAt(8, 60, COL_ERR, 1, ("No backups for " + folder).c_str());
            textAt(8, 80, COL_DIM, 1, "Switch to it once to create one.");
            footer("click / Enter: back"); waitForSelect(); continue;
        }
        String title = "Restore: " + folder;
        int si = browseList(title.c_str(), snaps, sn, "click: load this backup");
        if (si < 0) continue;                            // back to the firmware list
        String dir = "/firmwares/" + folder + "/backups/" + snaps[si];
        String cur = currentFw();
        if (cur.length()) backupFirmware(cur, false);    // preserve the live state before overwriting it
        markLoaded(folder);
        loadSnapshot(dir);                               // load the chosen snapshot (boots; returns only on error)
    }
}
static void actBootFirmware() {
    if (!slotHasApp(pickBootSlot())) {
        header("Boot firmware"); bodyClear();
        textAt(8, 60, COL_ERR, 1, "No bootable firmware in a slot.");
        textAt(8, 80, COL_DIM, 1, "Install one via Firmwares first.");
        footer("click / Enter: back"); waitForSelect(); return;
    }
    header("Boot firmware"); bodyClear();
    textAt(8, 60, COL_TXT, 1, "Booting firmware ...");
    delay(400);
    launchFirmware();              // boot the best valid slot; never returns
}
static void actRebootDevice() {
    header("Reboot"); bodyClear();
    textAt(8, 60, COL_TXT, 1, "Rebooting into the launcher...");
    delay(400);
    esp_restart();
}

// ---------------- launcher countdown ----------------
static void drawCountdown(int t, const char *fw) {
    if (!s_disp) return;
    header("meshcomod recovery"); bodyClear();
    textAt(8, 56, COL_TXT, 1, "Auto-booting in:");
    char b[8]; snprintf(b, sizeof b, "%d", t);
    tft.setTextColor(COL_OK, COL_BG); tft.setTextSize(5); tft.setCursor(220, 50); tft.print(b);
    if (fw && fw[0]) textAt(8, 110, COL_TXT, 1, fw);
    textAt(8, 175, COL_DIM, 1, "Move the trackball / press a key to choose.");
    footer("any input: open the recovery menu");
}
static void runLauncherCountdown() {
    if (!slotHasApp(pickBootSlot())) return;     // no bootable slot -> straight to menu
    String inst = installedFw();
    drainTrackball(); armClick();
    for (int t = 5; t >= 1; t--) {
        drawCountdown(t, inst.length() ? inst.c_str() : "(installed firmware)");
        uint32_t end = millis() + 1000;
        while ((int32_t)(millis() - end) < 0) {
            tdeckKeyboardPoll();
            if (anyInput()) return;          // cancel -> caller shows the menu
            delay(12);
        }
    }
    launchFirmware();                        // boots the slot; never returns
}

// ---------------- arduino ----------------
// ===================== Wi-Fi + firmware catalog (install/update over Wi-Fi) =====================
// A JSON catalog lists installable firmwares; each entry carries its OWN download URL (the
// firmwares are NOT hosted by meshcomod — the catalog just points to them). All fetches are
// plain HTTP (mbedTLS won't fit the 1 MB factory partition); the meshcomod mirror serves the
// catalog + meshcomod bins over HTTP, and a foreign HTTPS-only bin would be exposed via a
// meshcomod HTTP proxy. Install reuses the existing installBin() path (app-only or merged).
static const char *CATALOG_URL = "http://app.meshcomod.com/firmware-download/recovery/catalog.json";

struct CatEntry { String name, version, type, url, md5; uint32_t size; };
static const int CAT_MAX = 16;
static CatEntry s_cat[CAT_MAX];
static int s_catN = 0;

// Filesystem-safe SD library folder name for a firmware (one folder per firmware).
static String fsSafe(const String &s) {
    String o; for (uint32_t i = 0; i < s.length(); i++) { char c = s[i];
        o += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_'; }
    return o.length() ? o : String("firmware");
}

// Always return a folder that doesn't exist yet: base, base_2, base_3, ... so every install
// gets its OWN new firmware folder instead of overwriting/reusing an existing one.
static String uniqueFwFolder(const String &base) {
    String name = base;
    for (int i = 2; i < 1000; i++) {
        File d = SD.open(("/firmwares/" + name).c_str());
        bool exists = d && d.isDirectory(); if (d) d.close();
        if (!exists) break;
        name = base + "_" + String(i);
    }
    return name;
}

// Read the companion app's saved Wi-Fi creds from shared NVS (namespace "meshcomod",
// keys written by WifiRuntimeStore in the touch firmware). Returns true if an SSID exists.
static bool readSavedWifi(String &ssid, String &pwd) {
    Preferences p;
    if (!p.begin("meshcomod", true)) return false;   // read-only
    ssid = p.getString("wifi_ssid", "");
    pwd  = p.getString("wifi_pwd", "");
    p.end();
    return ssid.length() > 0;
}

static bool wifiConnect(const String &ssid, const String &pwd) {
    header("Wi-Fi"); bodyClear();
    textAt(8, 44, COL_TXT, 1, ("Connecting to " + ssid).c_str());
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pwd.length() ? pwd.c_str() : nullptr);
    uint32_t t0 = millis(); int dots = 0;
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
        delay(250);
        if (++dots % 4 == 0) { tft.fillRect(8, 62, SCRW - 16, 12, COL_BG);
            char d[20]; int nd = (dots/4) % 8; memset(d,'.',nd); d[nd]=0; textAt(8,62,COL_DIM,1,d); }
    }
    if (WiFi.status() != WL_CONNECTED) { textAt(8, 80, COL_ERR, 1, "Connect failed - check password / signal."); return false; }
    char ip[44]; snprintf(ip, sizeof ip, "Connected: %s", WiFi.localIP().toString().c_str());
    textAt(8, 80, COL_OK, 1, ip);
    char hp[44]; snprintf(hp, sizeof hp, "free heap %u KB", (unsigned)(ESP.getFreeHeap() / 1024));
    textAt(8, 98, COL_DIM, 1, hp);
    return true;
}

// On-screen-keyboard password entry. Enter = submit, Backspace = delete, Esc(`) = cancel.
static bool enterPassword(const String &ssid, String &out) {
    out = "";
    header("Wi-Fi password"); bodyClear();
    textAt(8, 40, COL_DIM, 1, ssid.c_str());
    footer("type password    Enter: connect    `: cancel");
    for (;;) {
        tdeckKeyboardPoll();
        int k = tdeckKeyboardReadKey();
        if (k == '\r' || k == '\n') return out.length() > 0;
        else if (k == 8 || k == 127) { if (out.length()) out.remove(out.length() - 1); }
        else if (k == '`' || k == 27) return false;
        else if (k >= 32 && k < 127) out += (char)k;
        else { delay(8); continue; }
        tft.fillRect(8, 64, SCRW - 16, 22, COL_BG);
        String mask; for (uint32_t i = 0; i < out.length(); i++) mask += '*';
        textAt(8, 64, COL_TXT, 2, mask.c_str());
        delay(8);
    }
}

static bool wifiScanPick(String &ssid) {
    header("Wi-Fi"); bodyClear(); textAt(8, 44, COL_TXT, 1, "Scanning for networks...");
    WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(100);
    int n = WiFi.scanNetworks();
    if (n <= 0) { textAt(8, 70, COL_ERR, 1, "No networks found (2.4 GHz only)"); delay(1400); return false; }
    static String names[24]; if (n > 24) n = 24;
    for (int i = 0; i < n; i++) names[i] = WiFi.SSID(i);
    int idx = browseList("Select network", names, n, "click: choose    (top row: back)");
    if (idx < 0) return false;
    ssid = names[idx];
    return true;
}

// Bring Wi-Fi up: reuse the app's saved creds first (one-tap), else scan + pick + password.
static bool ensureWifi() {
    if (WiFi.status() == WL_CONNECTED) return true;
    String ssid, pwd;
    if (readSavedWifi(ssid, pwd) && wifiConnect(ssid, pwd)) { delay(600); return true; }
    if (!wifiScanPick(ssid)) return false;
    if (!enterPassword(ssid, pwd)) return false;
    bool ok = wifiConnect(ssid, pwd); delay(800); return ok;
}

// GET a (small) URL into a String. HTTP only — mbedTLS/HTTPS would blow the 1 MB factory
// partition. The catalog + meshcomod bins live on the HTTP mirror; a foreign firmware whose
// URL is HTTPS-only must be exposed over HTTP (a meshcomod download proxy, like the tile proxy).
// Integrity doesn't rely on TLS: bins are MD5-checked against the catalog before flashing.
static bool httpGetString(const String &url, String &out) {
    WiFiClient c; HTTPClient http;
    if (!http.begin(c, url)) return false;
    http.setUserAgent("meshcomod-recovery");
    int code = http.GET();
    if (code == 200) out = http.getString();
    http.end();
    return code == 200;
}

static bool fetchCatalog() {
    header("Firmware catalog"); bodyClear(); textAt(8, 44, COL_TXT, 1, "Fetching catalog...");
    String json;
    if (!httpGetString(CATALOG_URL, json)) { textAt(8, 70, COL_ERR, 1, "Catalog fetch failed"); delay(1600); return false; }
    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, json)) { textAt(8, 70, COL_ERR, 1, "Catalog parse error"); delay(1600); return false; }
    s_catN = 0;
    for (JsonObject o : doc["firmwares"].as<JsonArray>()) {
        if (s_catN >= CAT_MAX) break;
        CatEntry &c = s_cat[s_catN++];
        c.name = (const char *)(o["name"] | "?"); c.version = (const char *)(o["version"] | "");
        c.type = (const char *)(o["type"] | "app"); c.url = (const char *)(o["url"] | "");
        c.md5 = (const char *)(o["md5"] | ""); c.size = o["size"] | 0u;
    }
    if (s_catN == 0) { textAt(8, 70, COL_ERR, 1, "Catalog has no firmwares"); delay(1600); return false; }
    return true;
}

static void drawDlBar(uint32_t got, uint32_t total) {
    if (!s_disp || !total) return;
    int16_t x = 8, y = 110, w = SCRW - 16, h = 14;
    tft.drawRect(x, y, w, h, COL_DIM);
    int fw = (int)((uint64_t)(w - 2) * got / total); if (fw < 0) fw = 0; if (fw > w - 2) fw = w - 2;
    tft.fillRect(x + 1, y + 1, fw, h - 2, COL_BAR);
    char p[40]; snprintf(p, sizeof p, "%u / %u KB", (unsigned)(got / 1024), (unsigned)(total / 1024));
    tft.fillRect(x, y + h + 3, w, 12, COL_BG); textAt(x, y + h + 3, COL_DIM, 1, p);
}

static String md5Hex(const uint8_t *buf, uint32_t len) {
    uint8_t out[16];
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_MD5);
    if (!info || mbedtls_md(info, buf, len, out) != 0) return "";
    char hex[33]; for (int i = 0; i < 16; i++) sprintf(hex + i * 2, "%02x", out[i]); hex[32] = 0;
    return String(hex);
}

// Stream a bin over HTTP into a PSRAM buffer (T-Deck has 8 MB; bins ~2.7 MB) with a progress
// bar. Returns the heap_caps buffer + length (caller frees with heap_caps_free), or false.
// Enforces the catalog's expected size when given.
static bool httpDownloadPsram(const String &url, uint32_t expectLen, uint8_t **outBuf, uint32_t *outLen) {
    *outBuf = nullptr; *outLen = 0;
    WiFiClient c; HTTPClient http;
    if (!http.begin(c, url)) return false;
    http.setUserAgent("meshcomod-recovery");
    int code = http.GET();
    if (code != 200) { http.end(); return false; }
    int clen = http.getSize();
    uint32_t cap = (clen > 0) ? (uint32_t)clen : (expectLen ? expectLen : 4u * 1024 * 1024);
    uint8_t *buf = (uint8_t *)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!buf) { http.end(); return false; }
    WiFiClient *st = http.getStreamPtr();
    uint32_t got = 0, last = 0, total = (clen > 0) ? (uint32_t)clen : cap;
    while (got < cap) {
        size_t avail = st->available();
        if (avail) {
            uint32_t want = (got + avail > cap) ? (cap - got) : (uint32_t)avail;
            int r = st->readBytes(buf + got, want);
            if (r <= 0) break;
            got += (uint32_t)r;
            if (millis() - last > 120) { drawDlBar(got, total); last = millis(); }
        } else {
            if (!http.connected()) break;
            delay(2);
        }
        if (clen > 0 && got >= (uint32_t)clen) break;
    }
    http.end();
    drawDlBar(got, total);
    bool ok = (got > 0) && (clen <= 0 || got == (uint32_t)clen) && (expectLen == 0 || got == expectLen);
    if (!ok) { heap_caps_free(buf); return false; }
    *outBuf = buf; *outLen = got;
    return true;
}

// Main menu action: connect -> fetch catalog -> pick -> download (MD5) -> install -> boot.
static void actWifiInstall() {
    if (!ensureWifi()) { delay(800); return; }
    if (!fetchCatalog()) return;
    static String names[CAT_MAX];
    for (int i = 0; i < s_catN; i++) {
        names[i] = s_cat[i].name;
        if (s_cat[i].version.length()) names[i] += " " + s_cat[i].version;
    }
    for (;;) {
        int idx = browseList("Install over Wi-Fi", names, s_catN, "click: install   (top: back)");
        if (idx < 0) return;
        CatEntry &e = s_cat[idx];
        // confirm
        header("Install firmware"); bodyClear();
        textAt(8, 44, COL_TXT, 2, e.name.c_str());
        char l[60]; snprintf(l, sizeof l, "type=%s   %u KB", e.type.c_str(), (unsigned)(e.size / 1024));
        textAt(8, 76, COL_DIM, 1, l);
        textAt(8, 104, COL_TXT, 1, "Replace the installed firmware (ota_0)?");
        footer("click / Enter: install      `: cancel");
        bool go = false; armClick();
        for (;;) {
            tdeckKeyboardPoll(); int k = tdeckKeyboardReadKey();
            if (k == '\r' || k == '\n' || k == ' ') { go = true; break; }
            if (k == '`' || k == 27) break;
            if (clickPressEdge()) { go = true; break; }
            delay(12);
        }
        if (!go) continue;
        // download -> PSRAM
        header("Install firmware"); bodyClear();
        textAt(8, 40, COL_TXT, 1, ("Downloading " + e.name).c_str()); footer("");
        uint8_t *buf = nullptr; uint32_t len = 0;
        if (!httpDownloadPsram(e.url, e.size, &buf, &len)) {
            textAt(8, 150, COL_ERR, 1, "Download failed (HTTP / size)."); footer("click: back"); waitForSelect(); continue;
        }
        // verify MD5 against the catalog before flashing
        if (e.md5.length()) {
            textAt(8, 150, COL_DIM, 1, "Verifying MD5...");
            if (!md5Hex(buf, len).equalsIgnoreCase(e.md5)) {
                heap_caps_free(buf);
                textAt(8, 150, COL_ERR, 1, "MD5 mismatch - not flashing."); footer("click: back"); waitForSelect(); continue;
            }
            textAt(8, 150, COL_OK, 1, "MD5 verified."); delay(500);
        }
        // If an SD card is present, mirror the SD-based install: snapshot the outgoing firmware,
        // then make this a first-class library entry (folder + saved bin) and mark it current — so
        // a Wi-Fi install shows in Firmwares, is restorable, and is tracked like any SD install.
        // sdBegin() is false with no card, so all of this is skipped then (install still proceeds).
        if (sdBegin()) {
            String cur = currentFw();
            if (cur.length()) backupFirmware(cur, false);          // roll-back point for the outgoing fw
            String fid  = uniqueFwFolder(fsSafe(e.name));           // always a fresh folder, never reuse
            SD.mkdir("/firmwares");
            String fdir = "/firmwares/" + fid;
            SD.mkdir(fdir.c_str()); SD.mkdir((fdir + "/bins").c_str());
            String binpath = fdir + "/bins/" + fid + ".bin";
            SD.remove(binpath.c_str());
            File wf = SD.open(binpath.c_str(), FILE_WRITE);
            if (wf) {
                textAt(8, 168, COL_DIM, 1, "Saving to SD library...");
                uint32_t off = 0; bool werr = false;
                while (off < len) {
                    uint32_t w = (len - off > 4096) ? 4096 : (len - off);
                    if (wf.write(buf + off, w) != w) { werr = true; break; }
                    off += w; if ((off & 0x1FFFF) == 0) delay(0);
                }
                wf.flush(); wf.close();
                if (werr) SD.remove(binpath.c_str()); else setCurrentFw(fid);   // track it as loaded
            }
        }
        // install via the shared installer (app-only or merged), then boot it
        bool ok = installBinMem(buf, len, e.name);
        heap_caps_free(buf);
        if (ok) { delay(500); launchFirmware(); }     // reboots into the new firmware; never returns
        footer("click: back"); waitForSelect();
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_PERIPH_POWER, OUTPUT); digitalWrite(PIN_PERIPH_POWER, HIGH);
    delay(200);
    // CRITICAL ordering fix: park EVERY chip-select on the shared SPI bus HIGH *before*
    // displayBegin() runs. displayBegin() blasts the ST7789 init sequence over the bus at
    // 80 MHz; if the SD card's CS (GPIO39) is still floating then, the card latches onto
    // that traffic and drops into a state where it ignores the SPI-mode CMD0 from then on
    // (reads as cardType=none). A power cycle doesn't help -- the next boot's display init
    // re-corrupts it -- which is exactly why only physically re-inserting the card (a
    // power cycle that happens *after* init, when CS is finally HIGH) brought it back.
    // Deselecting SD + LoRa up front means the card never sees the display burst.
    pinMode(PIN_SD_CS,    OUTPUT); digitalWrite(PIN_SD_CS,    HIGH);
    pinMode(PIN_LORA_NSS, OUTPUT); digitalWrite(PIN_LORA_NSS, HIGH);
    pinMode(PIN_TFT_CS,   OUTPUT); digitalWrite(PIN_TFT_CS,   HIGH);
    // Reset the SX1262 (shares MISO) to standby so it can't drive the shared bus either.
    pinMode(PIN_LORA_RST, OUTPUT);
    digitalWrite(PIN_LORA_RST, LOW);  delay(10);
    digitalWrite(PIN_LORA_RST, HIGH); delay(10);
    const esp_partition_t *run = esp_ota_get_running_partition();
    Serial.printf("\n\n==== meshcomod_boot recovery launcher (from %s) ====\n", run ? run->label : "?");
    displayBegin();
    tdeckTrackballBegin(); tdeckTrackballSetRotation(3);
    Wire.begin(PIN_KB_SDA, PIN_KB_SCL); Wire.setClock(400000); Wire.setTimeOut(20);
    tdeckKeyboardBegin(); tdeckKeyboardSetBacklight(0x10);

    // Deliberate "reboot to recovery" (meshcomod sets NVS mcboot/torec=1) -> show the menu.
    // Otherwise we only reach the recovery as a FALLBACK (otadata invalid / no bootable slot),
    // so self-heal: auto-boot a valid slot after a short countdown (any input cancels to menu).
    bool deliberate = false;
    { s_prefs.begin("mcboot", false); deliberate = (s_prefs.getUInt("torec", 0) != 0);
      if (deliberate) s_prefs.putUInt("torec", 0); s_prefs.end(); }
    if (!deliberate) runLauncherCountdown();   // auto-boots a valid slot unless the user intervenes
    drawMenu();
}
void loop() {
    tdeckKeyboardPoll();
    NavEv e = pollNav();
    if (e == NAV_UP)        { s_sel = (s_sel + MENU_N - 1) % MENU_N; drawMenu(); }
    else if (e == NAV_DOWN) { s_sel = (s_sel + 1) % MENU_N;         drawMenu(); }
    else if (e == NAV_SELECT) {
        switch (s_sel) {
            case 0: actFirmwares();       break;
            case 1: actAddFirmware();     break;
            case 2: actWifiInstall();     break;
            case 3: actBackup();          break;
            case 4: actInfo();            break;
            case 5: actRestore();         break;
            case 6: actBootFirmware();    break;
            case 7: actRebootDevice();    break;
        }
        drainTrackball(); armClick(); drawMenu();
    }
    delay(12);
}
