# Release log

Versioned prebuilts are listed here so you can **roll back** if a newer release causes issues. The **latest** build is always in [`prebuilt/`](prebuilt/) (same files are updated on each release). For a specific version, use the links below.

---

## v1.13.0.1 — 2026-02-23

**Firmware version:** v1.13.0.1 (meshcomod).

**Highlights:**
- Fix duplicate channel messages in Home Assistant: advance per-client history when pushing channel messages so sync does not re-deliver them.
- USB app connection fix (console only when first byte is a letter).
- HA: recommend TCP for stability (see Known bugs in README).

**Prebuilt binaries (flash with [flasher.meshcore.co.uk](https://flasher.meshcore.co.uk) → Custom firmware):**

| Device   | Merged (recommended) | Non-merged |
|----------|----------------------|------------|
| Heltec V4 | [heltec_v4_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.13.0.1/heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](prebuilt/releases/v1.13.0.1/heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](prebuilt/releases/v1.13.0.1/Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](prebuilt/releases/v1.13.0.1/Heltec_v3_companion_radio_usb_tcp.bin) |

---

*For each new release, add a section above this line and copy the new prebuilts into `prebuilt/releases/<version>/`.*
