# Touch TXT send forensics — repro and decision tree

Firmware with always-on tracing: serial @115200 plus on-device **Settings → Diag** panel (same lines mirrored).

## One-shot repro (after flash)

1. **Cold boot** (disconnect USB/power briefly), reconnect serial.
2. Send DM messages: `aaa` → wait 3s → `bbb` → wait 3s → `ccc`.
3. Press **RST** on the board; confirm Diag shows a new `BOOT #…` line.
4. Send `ddd`.
5. Capture: full serial log, Diag screenshot, and receiver-side hash column for each send.

## What to look for in logs

- `BOOT #N r=R` — `N` must increment only on real resets. If `N` jumps between send #1 and #2 without you pressing RST → **silent reboot**.
- `SEND#k boot=N` — per UI send; `boot` should match `BOOT #` until next reset.
- `TX BUF len=… crc=…` — if CRC matches between sends with **different** text → UI/composer bug; if CRC differs but `TX_COMPOSE h=` matches → bug below compose.
- `TX_COMPOSE … seq=… p0_15=…` — must change between sends; includes FreeRTOS stack HWM (`stk=`) and RTC (`rtc=` / `uniq=`).
- `TX_ENQUEUE` / `TX_DEQUEUE` / `TX_START` / `RADIO_TX_START` / `TX_DONE` — same hash should flow; if compose hash ≠ dequeue hash → queue/pool issue; if `TX_START` raw differs from `RADIO_TX_START` → unlikely (same buffer); if compose differs but radio raw matches → radio replay/driver.

## Decision tree (after capture)

1. **Boot count changes between send #1 and #2?**  
   → Treat as **silent reset**; use logs + WDT/boot reason to find crash site. RNG seeding (`initTxtTxUniquenessFromRng`) still prevents identical ciphertext across resets once root reset is fixed.

2. **Boot stable, TX BUF CRC same for different text?**  
   → **UI path** bug (composer buffer).

3. **CRC differs, TX_COMPOSE hash identical?**  
   → **compose / pool** path (e.g. `composeMsgPacket` not reflecting new text).

4. **TX_COMPOSE differs, TX_DEQUEUE / TX_START differ?**  
   → Trace further toward radio; if on-air hash on peer still stuck → receiver/tooling.

## Merged binary

Build touch env, then merge target:

```bash
python3 -m platformio run -e heltec_v4_tft_companion_radio_usb_tcp_touch -t mergebin
```

Artifact: `.pio/build/heltec_v4_tft_companion_radio_usb_tcp_touch/firmware-merged.bin` (copy to `out/` with a dated name for flashing).
