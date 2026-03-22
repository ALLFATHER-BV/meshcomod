## repeater-1.0.6 — 2026-03-22

**TCP repeater prebuilt** (Heltec WiFi LoRa 32 **V4** + **V3**). Train: **`repeater-X.Y.Z`**, independent of companion **`v1.14.0.x`**.

**Compile-time version string:** `meshcomod-repeater-1.0.6-b7c4d079` (git SHA at link).

**Changes vs repeater-1.0.5**

- **OLED boot splash:** Uses the **same** layout as companion **`ui-new`** **`SplashScreen`**: **`drawTextCentered`** at **y = 4 / 24 / 44**, text sizes **2 / 2 / 1**; version line uses a **12-byte** buffer (max **11** chars + NUL) like the companion, after stripping **`-<gitsha>`** and the **`meshcomod-`** prefix on the second line (title already shows **meshcomod**). Fixes garbled / distorted splash on Heltec OLED.
- Includes **1.0.5** behaviour: **`meshcomod-`** version prefix, device query **v27** (102 B), MESHCM OTA newline trim, GitHub raw + redirects, **`OTA:`** lines.

**Build:** **`DISABLE_DEBUG=1`**.

**Images (app-only — flash at app offset, typically 0x10000):**

| Device | Binary |
|--------|--------|
| Heltec V4 | [heltec_v4_repeater_tcp.bin](heltec_v4_repeater_tcp.bin) |
| Heltec V3 | [Heltec_v3_repeater_tcp.bin](Heltec_v3_repeater_tcp.bin) |

**Procedure:** [`docs/REPEATER_RELEASE_PROCEDURE.md`](../../../docs/REPEATER_RELEASE_PROCEDURE.md). **Protocol / client:** [`docs/REPEATER_TCP_COMPANION.md`](../../../docs/REPEATER_TCP_COMPANION.md).
