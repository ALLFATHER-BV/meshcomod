## v1.16.0.0 — 2026-06-07

**Firmware version:** v1.16.0.0 (meshcomod on upstream **MeshCore 1.16.0**).

**Highlights**

- **Rebased on MeshCore 1.16.0.** Full upstream 1.16.0 protocol + radio integration,
  keeping the meshcomod companion extras (multi-transport USB+BLE+TCP+WS, message
  sync, Wi-Fi OTA). Interoperates with 1.16 nodes on the air:
  - Extended 6-byte ACK delivery receipts.
  - Un-scoped flood + default flood scope; raw-packet send; anonymous / non-contact requests.
  - Longer preamble at low spreading factors (range); refactored radio driver.
  - EU repeater frequency corrected (869.000 → 869.495 MHz).
- Companion matrix: V4 OLED, V3, Wireless Paper (E213), Xiao S3 WIO — `*_companion_radio_usb_tcp`.
  (V4 TFT+touch ships separately under `releases/TOUCH/beta_3/`.)

**Primary prebuilts (flasher / OTA)**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_companion_radio_usb_tcp-merged.bin](heltec_v4_companion_radio_usb_tcp-merged.bin) | [heltec_v4_companion_radio_usb_tcp.bin](heltec_v4_companion_radio_usb_tcp.bin) |
| Heltec V3 | [Heltec_v3_companion_radio_usb_tcp-merged.bin](Heltec_v3_companion_radio_usb_tcp-merged.bin) | [Heltec_v3_companion_radio_usb_tcp.bin](Heltec_v3_companion_radio_usb_tcp.bin) |
| Heltec Wireless Paper (E213) | [Heltec_Wireless_Paper_companion_radio_usb_tcp-merged.bin](Heltec_Wireless_Paper_companion_radio_usb_tcp-merged.bin) | [Heltec_Wireless_Paper_companion_radio_usb_tcp.bin](Heltec_Wireless_Paper_companion_radio_usb_tcp.bin) |
| Seeed Xiao S3 WIO SX1262 | [Xiao_S3_WIO_companion_radio_usb_tcp-merged.bin](Xiao_S3_WIO_companion_radio_usb_tcp-merged.bin) | [Xiao_S3_WIO_companion_radio_usb_tcp.bin](Xiao_S3_WIO_companion_radio_usb_tcp.bin) |

**Build:** `FIRMWARE_VERSION=v1.16.0.0`, git **`f561fef3`**.
