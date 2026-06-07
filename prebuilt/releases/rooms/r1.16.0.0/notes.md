## r1.16.0.0 — 2026-06-07

**Room multitransport version string (on device):** `meshcomod-r1.16.0.0-room-mt-<gitsha>`.

**Highlights**

- **Rebased on MeshCore 1.16.0.** meshcomod room-server multitransport (USB+BLE+LoRa+TCP+WS)
  rebuilt on the upstream 1.16.0 base. Mesh-protocol/radio changes (6-byte ACK,
  un-scoped flood, raw packet, longer low-SF preamble, EU 869.495 MHz repeat freq)
  carry over; the stock `_room_server` env is unchanged.
- Matrix: V4 OLED, V4 TFT+touch, V3, Wireless Paper, Xiao S3 WIO
  `*_room_server_multitransport` app + merged images.

**Prebuilts**

| Device | Merged (0x0) | App-only |
|--------|----------------|----------|
| Heltec V4 (OLED) | [heltec_v4_room_server_multitransport-merged.bin](heltec_v4_room_server_multitransport-merged.bin) | [heltec_v4_room_server_multitransport.bin](heltec_v4_room_server_multitransport.bin) |
| Heltec V4 TFT + touch | [heltec_v4_tft_room_server_multitransport-merged.bin](heltec_v4_tft_room_server_multitransport-merged.bin) | [heltec_v4_tft_room_server_multitransport.bin](heltec_v4_tft_room_server_multitransport.bin) |
| Heltec V3 | [Heltec_v3_room_server_multitransport-merged.bin](Heltec_v3_room_server_multitransport-merged.bin) | [Heltec_v3_room_server_multitransport.bin](Heltec_v3_room_server_multitransport.bin) |
| Heltec Wireless Paper (E213) | [Heltec_Wireless_Paper_room_server_multitransport-merged.bin](Heltec_Wireless_Paper_room_server_multitransport-merged.bin) | [Heltec_Wireless_Paper_room_server_multitransport.bin](Heltec_Wireless_Paper_room_server_multitransport.bin) |
| Seeed Xiao S3 WIO SX1262 | [Xiao_S3_WIO_room_server_multitransport-merged.bin](Xiao_S3_WIO_room_server_multitransport-merged.bin) | [Xiao_S3_WIO_room_server_multitransport.bin](Xiao_S3_WIO_room_server_multitransport.bin) |

**Build:** `ROOM_FIRMWARE_VERSION=r1.16.0.0-room-mt`, git **`f561fef3`**.
