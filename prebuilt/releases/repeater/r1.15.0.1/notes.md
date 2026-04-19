## r1.15.0.1 — 2026-04-19

**Repeater TCP version string (on device):** `meshcomod-r1.15.0.1-repeater-tcp-<gitsha>`.

**Highlights**

- **Heltec Wireless Paper:** New **`Heltec_Wireless_Paper_repeater_tcp`** (Wi‑Fi TCP + WebSocket repeater on E213, merged + app-only bins). **`MAX_LORA_TX_POWER`** added to the Wireless Paper variant base so repeater TCP commands compile.
- **Heltec V4 / V3:** Rebuilt at **`r1.15.0.1-repeater-tcp`** so rolling **`prebuilt/`** repeater stable names align with this train.

**Prebuilts**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_repeater_tcp-merged.bin](heltec_v4_repeater_tcp-merged.bin) | [heltec_v4_repeater_tcp.bin](heltec_v4_repeater_tcp.bin) |
| Heltec V3 | [Heltec_v3_repeater_tcp-merged.bin](Heltec_v3_repeater_tcp-merged.bin) | [Heltec_v3_repeater_tcp.bin](Heltec_v3_repeater_tcp.bin) |
| Heltec Wireless Paper (E213) | [Heltec_Wireless_Paper_repeater_tcp-merged.bin](Heltec_Wireless_Paper_repeater_tcp-merged.bin) | [Heltec_Wireless_Paper_repeater_tcp.bin](Heltec_Wireless_Paper_repeater_tcp.bin) |

**Build:** `DISABLE_DEBUG=1`, `REPEATER_FIRMWARE_VERSION=r1.15.0.1-repeater-tcp`, git **`cc213eea`**.

**Procedure:** [`docs/REPEATER_RELEASE_PROCEDURE.md`](../../../docs/REPEATER_RELEASE_PROCEDURE.md), [`scripts/copy-repeater-release-bins.sh`](../../../scripts/copy-repeater-release-bins.sh).
