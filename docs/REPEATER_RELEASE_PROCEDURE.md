# TCP repeater release procedure (prebuilt / flasher)

**Repeater TCP** uses a four-part **`rX.Y.Z.W`** directory under **`prebuilt/releases/repeater/`** (parallel numeric train to companion **`vX.Y.Z.W`**) plus a fixed **`‑repeater-tcp`** suffix in **`REPEATER_FIRMWARE_VERSION`** so `out/` filenames and on-device strings stay distinct from companion builds. **`build.sh`** prefixes the compile-time macro with **`meshcomod-`**, e.g. **`meshcomod-r1.14.1.0-repeater-tcp-<gitsha>`** for new builds.

**Shipped `r1.14.1.0` bins** in this repo still report **`meshcomod-v1.14.1.0-repeater-tcp-<gitsha>`** (built before the **`r*`** naming cutover). **`copy-repeater-release-bins.sh v1.14.1.0`** still works: it maps to **`repeater/r1.14.1.0/`** and globs **`v1.14.1.0-repeater-tcp`** in **`out/`**.

**Legacy:** older drops used **`repeater-1.0.x`**; the copy script places those under **`prebuilt/releases/repeater/repeater-1.0.x/`** if you recreate them in a fork.

**Independence from companion:** Repeater does **not** need to ship on every companion version. The **release id** **`rX.Y.Z.W`** is the pin the flasher uses; it **may** match the companion train (**`v1.14.1.0`** ↔ **`r1.14.1.0`**) or a **different** four-part train. See **[`AGENTS.md`](../AGENTS.md)**.

---

## 1. Choose the release folder (`rX.Y.Z.W`)

Pick the four-part **`r*`** name under **`prebuilt/releases/repeater/`** where these binaries should land (and what the meshcomod client will list for pinned TCP repeater downloads).

Set:

- **`REPEATER_FIRMWARE_VERSION=rX.Y.Z.W-repeater-tcp`** (recommended)

Examples:

- Same train as companion **`v1.14.1.0`**: **`REPEATER_FIRMWARE_VERSION=r1.14.1.0-repeater-tcp`** → folder **`repeater/r1.14.1.0/`**
- Repeater-only bump while companion is newer: **`r1.14.1.1-repeater-tcp`** → **`repeater/r1.14.1.1/`** (companion may already be **`v1.14.1.5`** or higher)

Document the choice in **`RELEASES.md`** and **`prebuilt/releases/repeater/rX.Y.Z.W/notes.md`**.

---

## 2. Build from `MeshCore/`

```bash
export REPEATER_FIRMWARE_VERSION=r1.14.1.0-repeater-tcp
export DISABLE_DEBUG=1   # recommended for release
sh build.sh build-repeater-firmwares
```

- You do **not** need **`FIRMWARE_VERSION`** if **`REPEATER_FIRMWARE_VERSION`** is set.
- `out/` will contain e.g. **`heltec_v4_repeater_tcp-meshcomod-r1.14.1.0-repeater-tcp-<sha>.bin`** and the V3 / V4 TFT analogues, plus optional **`…-merged.bin`** (full image at **0x0**) after **`mergebin`**.

---

## 3. Promote to `prebuilt/`

Pass **`rX.Y.Z.W`** (or legacy **`vX.Y.Z.W`**, which maps to **`repeater/rX.Y.Z.W/`**):

```bash
sh scripts/copy-repeater-release-bins.sh r1.14.1.0
```

You can also pass **`r1.14.1.0-repeater-tcp`** explicitly — same result.

Produces / updates:

- **`prebuilt/heltec_v4_repeater_tcp.bin`**, **`prebuilt/Heltec_v3_repeater_tcp.bin`**, optional **`prebuilt/heltec_v4_tft_repeater_tcp*.bin`**
- The same stable names under **`prebuilt/releases/repeater/r1.14.1.0/`**

Document TCP repeater rows in **`prebuilt/releases/repeater/r1.14.1.0/notes.md`** and the top section of **`RELEASES.md`**.

**Legacy copy:**

```bash
export REPEATER_FIRMWARE_VERSION=repeater-1.0.0
sh build.sh build-repeater-firmwares
sh scripts/copy-repeater-release-bins.sh repeater-1.0.0
```

→ **`prebuilt/releases/repeater/repeater-1.0.0/`**

---

## 4. Commit (and push)

Stage **`prebuilt/`**, **`prebuilt/releases/repeater/…`**, **`RELEASES.md`**, and notes. Push to your meshcomod remote (e.g. **`allfather`**).

**Git tag (optional):** bookkeeping tags are fine; canonical binaries for flasher / OTA live under **`prebuilt/`** on **`main`**. CI: **Actions → Build Repeater Firmwares** — use the same **`REPEATER_FIRMWARE_VERSION`** string you built with.

---

## Summary

| Step | Action |
|------|--------|
| 1 | Set **`REPEATER_FIRMWARE_VERSION=rX.Y.Z.W-repeater-tcp`** (folder = **`repeater/rX.Y.Z.W/`**) |
| 2 | **`sh build.sh build-repeater-firmwares`** |
| 3 | **`sh scripts/copy-repeater-release-bins.sh rX.Y.Z.W`** |
| 4 | Update **`notes.md`** / **`RELEASES.md`**, commit, push |

See also: [`prebuilt/README.md`](../prebuilt/README.md), [`REPEATER_TCP_COMPANION.md`](REPEATER_TCP_COMPANION.md), [`AGENTS.md`](../AGENTS.md).
