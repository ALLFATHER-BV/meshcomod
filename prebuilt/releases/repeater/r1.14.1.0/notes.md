## r1.14.1.0 — repeater train (companion **v1.14.1.0**)

**Release id:** **`r1.14.1.0`** (parallel to companion **`v1.14.1.0`**). **Plain mesh** and **TCP repeater** prebuilts built with that companion release live in this folder.

**TCP repeater:** newer meshcomod TCP repeater builds (SNTP, repeater web UI updates) ship under **[`../r1.14.1.1/`](../r1.14.1.1/)** — use **`r1.14.1.1`** for pinned flasher / OTA unless you need this older TCP drop.

**On-device TCP identity:** binaries here report **`meshcomod-v1.14.1.0-repeater-tcp-<gitsha>`** (built with **`REPEATER_FIRMWARE_VERSION=v1.14.1.0-repeater-tcp`**). New builds should use **`r1.14.1.x-repeater-tcp`** so `out/` names and UI pins stay aligned with **`r*`** folders.

**Plain mesh repeaters (app / OTA partition)**

| Build | File |
|-------|------|
| OLED repeater | [heltec_v4_repeater.bin](heltec_v4_repeater.bin) |
| TFT repeater | [heltec_v4_tft_repeater.bin](heltec_v4_tft_repeater.bin) |

**TCP repeater (Wi‑Fi companion / CLI)**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) TCP | [heltec_v4_repeater_tcp-merged.bin](heltec_v4_repeater_tcp-merged.bin) | [heltec_v4_repeater_tcp.bin](heltec_v4_repeater_tcp.bin) |
| Heltec V4 TFT TCP | [heltec_v4_tft_repeater_tcp-merged.bin](heltec_v4_tft_repeater_tcp-merged.bin) | [heltec_v4_tft_repeater_tcp.bin](heltec_v4_tft_repeater_tcp.bin) |
| Heltec V3 TCP | [Heltec_v3_repeater_tcp-merged.bin](Heltec_v3_repeater_tcp-merged.bin) | [Heltec_v3_repeater_tcp.bin](Heltec_v3_repeater_tcp.bin) |

**Build / promote:** [`docs/RELEASE_PROCEDURE.md`](../../../../docs/RELEASE_PROCEDURE.md), [`scripts/copy-repeater-release-bins.sh`](../../../../scripts/copy-repeater-release-bins.sh) (e.g. **`r1.14.1.0`** or legacy **`v1.14.1.0`**). Plain repeaters from extras: [`scripts/copy-heltec-v4-meshcomod-extras.sh`](../../../../scripts/copy-heltec-v4-meshcomod-extras.sh) **`v1.14.1.0`** → **`repeater/r1.14.1.0/`**.

**Companion-only binaries:** [`../../companion/v1.14.1.0/`](../../companion/v1.14.1.0/) and [`../../companion/v1.14.1.0/notes.md`](../../companion/v1.14.1.0/notes.md).
