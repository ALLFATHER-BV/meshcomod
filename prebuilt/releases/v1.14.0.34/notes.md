## v1.14.0.34 — 2026-03-21

**First promoted TCP repeater prebuilts** (Heltec WiFi LoRa 32 **V4** + **V3**), same `prebuilt/` layout as companion.

**Firmware version string (compile-time):** `v1.14.0.34-22e00b1e` (tree may be dirty locally; these bins match the commit used at link time).

**Images:**

| Device | App-only (flash at app offset, typically **0x10000**) |
|--------|--------------------------------------------------------|
| Heltec V4 | [heltec_v4_repeater_tcp.bin](heltec_v4_repeater_tcp.bin) |
| Heltec V3 | [Heltec_v3_repeater_tcp.bin](Heltec_v3_repeater_tcp.bin) |

**Merged** (single file from **0x0**) was not published for this drop; generate with `pio run -t mergebin -e <env>` if needed.

**Protocol / client:** [`docs/REPEATER_TCP_COMPANION.md`](../../../docs/REPEATER_TCP_COMPANION.md), [`docs/REPEATER_MESHCOMOD_CLIENT_INTEGRATION.md`](../../../docs/REPEATER_MESHCOMOD_CLIENT_INTEGRATION.md).
