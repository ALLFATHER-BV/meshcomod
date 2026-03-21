# TCP repeater release procedure (prebuilt / flasher)

**Repeater TCP** firmware uses its **own** release labels — **not** the companion `v1.14.0.x` line. Device query / OLED will show a string like **`repeater-1.0.0-<gitsha>`** when built for release.

---

## 1. Choose the next repeater version

Use a **`repeater-X.Y.Z`** tag (semver after the prefix). Example first release: **`repeater-1.0.0`**.

Bump only this label when you cut a new **repeater** prebuilt; companion releases do not need to change.

---

## 2. Build from `MeshCore/`

```bash
export REPEATER_FIRMWARE_VERSION=repeater-1.0.0
sh build.sh build-repeater-firmwares
```

- You do **not** need `FIRMWARE_VERSION` for this path if `REPEATER_FIRMWARE_VERSION` is set.
- `out/` will contain e.g. `heltec_v4_repeater_tcp-repeater-1.0.0-<sha>.bin` and the V3 analogue.

---

## 3. Promote to `prebuilt/`

The copy script’s `<version>` argument must equal **`REPEATER_FIRMWARE_VERSION`** exactly (the glob is `out/<env>-${VERSION}-*.bin`, e.g. **`repeater-1.0.0`**).

```bash
sh scripts/copy-repeater-release-bins.sh repeater-1.0.0
```

Produces:

- `prebuilt/heltec_v4_repeater_tcp.bin`, `prebuilt/Heltec_v3_repeater_tcp.bin`
- `prebuilt/releases/repeater-1.0.0/` with the same two files

Add **`prebuilt/releases/repeater-1.0.0/notes.md`** for that drop.

---

## 4. Commit (and push)

Stage the new **`prebuilt/releases/repeater-X.Y.Z/`** tree, updated root **`prebuilt/*repeater_tcp.bin`**, and any doc/script changes. Do not reuse companion **`prebuilt/releases/v1.14.0.*/`** folders for repeater binaries.

---

## Summary

| Step | Action |
|------|--------|
| 1 | Pick **`repeater-X.Y.Z`** |
| 2 | `export REPEATER_FIRMWARE_VERSION=repeater-X.Y.Z` → `sh build.sh build-repeater-firmwares` |
| 3 | `sh scripts/copy-repeater-release-bins.sh repeater-X.Y.Z` |
| 4 | `notes.md` + commit |

See also: [`prebuilt/README.md`](../prebuilt/README.md), [`REPEATER_TCP_COMPANION.md`](REPEATER_TCP_COMPANION.md).
