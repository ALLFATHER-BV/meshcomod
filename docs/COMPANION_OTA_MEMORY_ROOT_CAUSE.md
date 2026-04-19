# Companion OTA HTTPS Memory Root Cause

## Summary

Companion URL OTA on ESP32 was failing before any firmware bytes were downloaded.

The visible error originally looked like:

- `ERR: HTTP -1 (connection refused)`

After adding lower-level TLS diagnostics, the real failure was:

- `OTA: get fail http=-1 tls=-32512 SSL - Memory allocation failed`

This was **not** a bad firmware URL, not a GitHub outage, and not a flasher proxy issue. The root cause was **insufficient allocatable internal RAM for the HTTPS/TLS client** on the companion while BLE and other companion transports were still alive.

## What the logs proved

Two distinct failure classes happened during this investigation:

1. **Mid-download flash failure**
   - Example:
     - `OTA: HTTP OK, flashing`
     - `OTA: downloading 57%`
     - `OTA: ERR flash write`
   - This proved OTA could sometimes reach the write path.

2. **Pre-download TLS allocation failure**
   - Example:
     - `OTA: fetch raw-github`
     - `OTA: get fail http=-1 tls=-32512 SSL - Memory allocation failed`
   - This proved some failing runs never reached HTTP 200 or any flash write at all.

The second failure class turned out to be the blocker for companion OTA in current testing.

## Why the original `HTTP -1` message was misleading

The OTA wrapper used `HTTPClient`, which collapses several connection/setup failures into `HTTPC_ERROR_CONNECTION_REFUSED` (`-1`). That made early logs look like generic network refusal even when the underlying failure was inside the TLS layer.

Adding `WiFiClientSecure::lastError(...)` exposed the real mbedTLS error string and code.

## Key evidence

The important runtime numbers were:

- `heap=35520`
- `max=24564`
- `psram=2072147`
- `pmax=2064372`

Interpretation:

- Total free heap was only around **35 KB**.
- Largest allocatable internal block was only around **24-25 KB**.
- PSRAM had plenty of free space, but the TLS stack needed **internal RAM**, not PSRAM.

So although the device still reported "some free heap", the HTTPS client could not allocate the memory needed for the TLS session.

## Why repeater OTA seemed better

Repeater and companion both use the same ESP32 HTTP OTA function, but companion keeps more runtime services alive:

- BLE
- TCP companion server
- optional WebSocket/WSS stack
- companion-specific transport bookkeeping

The biggest confirmed offender was BLE. The existing BLE "disable" path only stopped advertising and disconnected clients; it did **not** actually release the BLE stack memory.

## What fixed it

The successful fix was:

1. Keep OTA transport deterministic:
   - companion resolves meshcomod `firmware-download` URLs back to raw GitHub
   - tries raw GitHub first
   - falls back once to flasher HTTPS

2. Add better diagnostics:
   - selected/effective URL
   - content metadata and signature
   - TLS client error text
   - heap shape: free heap, max alloc heap, PSRAM, max alloc PSRAM

3. Lower TLS memory pressure:
   - asymmetric mbedTLS buffer settings for companion envs

4. **Release BLE memory before HTTPS OTA starts**
   - if the active reply target is not BLE, fully deinitialize BLE before `startHttpOtaFromUrl(...)`
   - this reclaimed enough internal RAM for HTTPS OTA to succeed

## Important implementation detail

A partial BLE shutdown was **not enough**.

These actions did not solve the issue by themselves:

- stopping BLE advertising
- disconnecting the BLE client
- shrinking TLS output buffer alone

The change that mattered was a **real BLE stack teardown** using the BLE library deinit path so internal memory was actually released before the OTA HTTPS session started.

## Recommended rule going forward

For ESP32 companion OTA over HTTPS:

- treat OTA as a memory-sensitive mode
- shut down non-essential transports before starting HTTPS, especially BLE
- log `max alloc heap`, not just total free heap
- do not assume PSRAM helps TLS failures

## Future debugging checklist

If companion OTA breaks again, check these in order:

1. Does the log reach `OTA: HTTP OK, flashing`?
   - If yes, it is no longer a TLS connect/setup problem.
2. Is there a `tls=` diagnostic line?
   - If yes, trust that over `HTTP -1`.
3. What are `heap=` and `max=` before OTA starts?
   - Low `max=` is the most useful signal for TLS allocation failures.
4. Which transports are active?
   - BLE is the first thing to suspect for internal RAM pressure.
5. Is the OTA command coming from BLE?
   - If yes, BLE cannot be torn down before the response path is preserved or switched.

## Result

After the BLE memory release change, companion OTA successfully completed using the same general OTA path that had previously failed with:

- `tls=-32512 SSL - Memory allocation failed`

That confirms the root cause was **companion transport memory pressure**, not the firmware artifact or the remote host.
