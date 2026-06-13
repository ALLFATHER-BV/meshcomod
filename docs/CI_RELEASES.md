# CI releases (GitHub Actions + GitHub Releases)

Firmware binaries are built by GitHub Actions and published as **GitHub Release
assets**. GitHub Releases are the **source of truth** for distribution (the web
flasher reads the release assets), so new bins are **not** committed into
`prebuilt/` anymore.

## How to cut a release

Push a tag on the track you want. The matching workflow builds the firmwares,
publishes a GitHub Release with the `.bin` files attached, and prunes old
releases on that track (keeping the newest 5).

| Track | Tag prefix | Example tag | Workflow | Builds |
|-------|-----------|-------------|----------|--------|
| Companion | `companion-` | `companion-v1.16.0.1` | build-companion-firmwares | `*_companion_radio_usb` / `_ble` |
| Room server | `room-server-` | `room-server-v1.16.0.1` | build-room-server-firmwares | `*_room_server` |
| Repeater | — | (manual `workflow_dispatch`) | build-repeater-firmwares | `*_repeater_tcp` |

> The **touch** UI firmware (Heltec V4 TFT, LilyGo T-Deck) is built and released
> from the **wadamesh** repo, not here. This repo is core / companion only.

```bash
# example: cut the companion v1.16.0.1 release
git tag companion-v1.16.0.1
git push origin companion-v1.16.0.1
```

The tag suffix becomes `FIRMWARE_VERSION` (e.g. `companion-v1.16.0.1` →
`v1.16.0.1`), so the in-firmware version string and the release name match the
tag.

## Rolling retention

After each release the workflow runs `scripts/ci/prune-releases.sh <prefix> 5`,
which keeps the **newest 5** releases on that track and deletes the older ones —
the GitHub Release, its git tag, and the attached assets. This keeps the release
list and the tag namespace small while still allowing rollback to the last few
versions.

To preview what would be pruned without deleting anything, run the workflow via
**Actions → (workflow) → Run workflow** after setting `DRY_RUN=1`, or locally:

```bash
DRY_RUN=1 scripts/ci/prune-releases.sh companion- 5
```

(Requires the GitHub CLI authenticated against the repo.)

## Migration notes

- The legacy flow committed bins to `prebuilt/` and `prebuilt/releases/<track>/<version>/`
  and logged them in `RELEASES.md`. That flow is retired for new releases; the
  large committed-bin tree can be cleaned up separately (its size lives in git
  history, so shrinking the clone needs a history rewrite — out of scope here).
- The web flasher (meshcomod-client) must read release assets from the GitHub
  Releases API (latest per track) instead of repo paths — tracked as the
  flasher-side follow-up to this change.
