# Heltec V4 Touch Repo Bootstrap (Meshcomod-based)

This document is a copy-paste starter for creating a dedicated repository that keeps Meshcomod functionality while adding a custom touch OS/UI for Heltec V4 Touch.

## 1) Repository Baseline

Recommended new repository name:

- `meshcomod-heltec-v4-touch`

Suggested branch model:

- `main` = stable release branch
- `develop` = integration branch for normal feature work
- short-lived feature branches from `develop`

Recommended git remotes:

- `origin` -> your new dedicated repo
- `meshcomod` -> `https://github.com/ALLFATHER-BV/meshcomod`
- `upstream` -> `https://github.com/meshcore-dev/MeshCore.git`

Why this model:

- You keep your tested Meshcomod transport/features as baseline.
- You still preserve a clean path for periodic MeshCore syncs.

## 2) Initial Folder Layout

Use this structure in the new repo to isolate touch OS/UI from core logic:

```text
.
├─ boards/
├─ docs/
│  ├─ architecture/
│  │  ├─ os-ui-layer.md
│  │  └─ sync-strategy.md
│  ├─ testing/
│  │  └─ hardware-smoke-checklist.md
│  └─ releases/
│     └─ release-checklist.md
├─ scripts/
│  ├─ build-touch.sh
│  └─ collect-artifacts.sh
├─ src/
│  ├─ core/
│  │  ├─ companion/
│  │  ├─ mesh/
│  │  └─ services/
│  ├─ platform/
│  │  └─ heltec_v4_touch/
│  │     ├─ board_config/
│  │     ├─ drivers/
│  │     │  ├─ display/
│  │     │  └─ touch/
│  │     └─ power/
│  ├─ ui/
│  │  ├─ app/
│  │  ├─ navigation/
│  │  ├─ screens/
│  │  ├─ theme/
│  │  └─ widgets/
│  └─ main.cpp
├─ variants/
├─ .github/
│  ├─ ISSUE_TEMPLATE/
│  └─ workflows/
└─ platformio.ini
```

## 3) Build Environment Naming

Keep env names explicit and consistent:

- `heltec_v4_touch_dev` (fast debug defaults)
- `heltec_v4_touch_release` (release config)
- `heltec_v4_touch_release_merged` (artifact packaging target)

If you want compatibility with existing naming:

- keep `heltec_v4_tft_companion_radio_usb_tcp_touch` as alias env for transition period.

## 4) CI Workflow (Copy Into `.github/workflows/firmware-touch-ci.yml`)

```yaml
name: Firmware Touch CI

on:
  pull_request:
  push:
    branches: [main, develop]
  workflow_dispatch:

jobs:
  build:
    runs-on: ubuntu-latest
    strategy:
      fail-fast: false
      matrix:
        env:
          - heltec_v4_touch_dev
          - heltec_v4_touch_release
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Set up Python
        uses: actions/setup-python@v5
        with:
          python-version: "3.11"

      - name: Cache PlatformIO
        uses: actions/cache@v4
        with:
          path: |
            ~/.platformio/.cache
            ~/.platformio/packages
            ~/.platformio/platforms
          key: ${{ runner.os }}-pio-${{ hashFiles('platformio.ini') }}
          restore-keys: |
            ${{ runner.os }}-pio-

      - name: Install PlatformIO
        run: python -m pip install --upgrade pip platformio

      - name: Build firmware
        run: pio run -e ${{ matrix.env }}

      - name: Build merged binary (release only)
        if: matrix.env == 'heltec_v4_touch_release'
        run: pio run -e heltec_v4_touch_release -t mergebin

      - name: Upload artifacts
        uses: actions/upload-artifact@v4
        with:
          name: firmware-${{ matrix.env }}
          path: |
            .pio/build/${{ matrix.env }}/firmware.bin
            .pio/build/${{ matrix.env }}/firmware.elf
            .pio/build/${{ matrix.env }}/firmware-merged.bin
          if-no-files-found: ignore
```

## 5) Milestones to Create in GitHub

Create these milestones first:

1. **M1 - Hardware Bring-up Stable**
   - Goal: deterministic boot, display, touch, watchdog-safe runtime.
2. **M2 - Touch OS/UI Foundation**
   - Goal: navigation shell + screen framework + interaction model.
3. **M3 - Companion Parity**
   - Goal: USB/BLE/TCP/WiFi parity with existing Meshcomod behavior.
4. **M4 - First Public Alpha**
   - Goal: release process, docs, rollback path, prebuilt binaries.

## 6) First Issue Set (Create Immediately)

### Issue 1
- **Title:** Define Heltec V4 Touch board abstraction and pin mapping
- **Labels:** `platform`, `touch`, `priority:high`
- **Acceptance criteria:**
  - Display, touch, reset, and power pins documented in source.
  - Single board abstraction entry-point used by UI and drivers.
  - Boot-time hardware init logs show expected sequence.

### Issue 2
- **Title:** Implement display driver bootstrap and frame pipeline
- **Labels:** `ui`, `display`, `priority:high`
- **Acceptance criteria:**
  - Stable init across 10 cold boots.
  - Frame render loop with bounded frame time target.
  - Splash + fallback error screen implemented.

### Issue 3
- **Title:** Implement CHSC6x touch input service with debounce/gesture base
- **Labels:** `touch`, `input`, `priority:high`
- **Acceptance criteria:**
  - Tap and long-press events reliable across test matrix.
  - Input queue decoupled from screen rendering.
  - No lockups during rapid touch events.

### Issue 4
- **Title:** Build touch-first navigation shell (Home, Messages, Radio, Settings)
- **Labels:** `ui`, `navigation`, `priority:high`
- **Acceptance criteria:**
  - Screen state machine implemented.
  - Back/forward behavior deterministic.
  - Touch target sizing validated on device.

### Issue 5
- **Title:** Port Meshcomod companion status/control into touch settings screens
- **Labels:** `companion`, `ui`, `priority:high`
- **Acceptance criteria:**
  - BLE/TCP on/off and status shown in UI.
  - WiFi SSID/status/apply exposed in UI.
  - Existing command behavior preserved.

### Issue 6
- **Title:** Add hardware smoke tests and release gate checklist
- **Labels:** `qa`, `release`, `priority:high`
- **Acceptance criteria:**
  - Documented test matrix for USB/BLE/TCP/WiFi/touch/boot.
  - Mandatory checklist required before tagging release.
  - Known-failures section in release notes template.

### Issue 7
- **Title:** Add CI artifact pipeline for firmware and merged binaries
- **Labels:** `ci`, `release`, `priority:medium`
- **Acceptance criteria:**
  - Pull requests build all touch envs.
  - Release env publishes merged and non-merged artifacts.
  - Build metadata embedded in artifact naming.

### Issue 8
- **Title:** Define upstream sync protocol from MeshCore and meshcomod
- **Labels:** `maintenance`, `sync`, `priority:medium`
- **Acceptance criteria:**
  - Documented sync cadence and conflict policy.
  - Compatibility checklist after each sync.
  - One dry-run sync completed and recorded.

## 7) Labels to Add

Add these labels before creating issues:

- `priority:high`
- `priority:medium`
- `priority:low`
- `platform`
- `touch`
- `display`
- `input`
- `ui`
- `navigation`
- `companion`
- `qa`
- `release`
- `ci`
- `maintenance`
- `sync`

## 8) Definition of Done (for every merged PR)

- Builds in CI for target env(s)
- Hardware smoke test performed on real Heltec V4 touch device
- No regressions in USB/BLE/TCP companion functions
- Changelog entry added (if user-visible)
- Rollback path known (previous stable binary validated)

## 9) Day-1 Bring-up Checklist

- Clone baseline from `ALLFATHER-BV/meshcomod`
- Create new repo and push baseline commit
- Add remotes (`meshcomod`, `upstream`)
- Add CI workflow and validate first run
- Build and flash `heltec_v4_touch_dev`
- Verify boot/display/touch event capture
- Open and complete Issue 1 and Issue 2 first

---

If you want, the next step is to generate a ready-to-run `platformio.ini` starter block for the three touch envs above and a `build-touch.sh` script aligned to your existing release naming.
