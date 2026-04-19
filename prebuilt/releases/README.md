# Versioned prebuilts

## Layout

- **`companion/`** — pinned **companion** drops: **`v1.14.x.x`** (USB+TCP, TFT touch, V3) plus Heltec V4 **companion** extras (USB / BLE / Wi‑Fi only). No `*repeater*` binaries here.
- **`repeater/`** — pinned **repeater** drops: **`r1.14.x.x`** (same numeric train as companion **`v1.14.x.x`**, with an **`r`** prefix), plus optional legacy **`repeater-X.Y.Z`** folders. Plain mesh repeaters and TCP repeater (`*_repeater_tcp`) bins live here.

**Rolling “latest”** still lives at **`prebuilt/*.bin`** (updated by the copy scripts). Promoting **companion** vs **repeater** can happen on different schedules.

The **meshcomod** client lists **`prebuilt/releases/companion`** for the companion product and **`prebuilt/releases/repeater`** for the repeater product (TCP repeater pins are **`r*`** dirs from **r1.14.1.0** upward that contain a `*_repeater_tcp*.bin`, plus legacy **`repeater-*`**). `resolveArtifactUrl(..., version, product)` maps `prebuilt/<file>.bin` → `prebuilt/releases/<companion|repeater>/<version>/<file>.bin`.

**New TCP repeater builds:** use **`REPEATER_FIRMWARE_VERSION=r1.14.1.x-repeater-tcp`** (recommended). Current TCP pin: **`r1.14.1.1`** (**`meshcomod-r1.14.1.1-repeater-tcp-<gitsha>`**). Older **`r1.14.1.0`** bins may still embed legacy **`meshcomod-v1.14.1.0-repeater-tcp-<gitsha>`**.

See **[`RELEASES.md`](../../RELEASES.md)** and **[`prebuilt/README.md`](../README.md)** for copy scripts and procedures.
