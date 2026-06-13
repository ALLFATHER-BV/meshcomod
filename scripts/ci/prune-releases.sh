#!/usr/bin/env bash
#
# Rolling release retention: keep the newest N GitHub Releases for a given track
# (by tag prefix) and delete the older ones — including their git tags and the
# attached binary assets — so the release list (and the repo's tag namespace)
# stays small while GitHub Releases are the source of truth for the bins.
#
# Tracks are identified by tag prefix, e.g.:
#   companion-v1.16.0.1   -> prefix "companion-"
#   repeater-v1.16.0.1    -> prefix "repeater-"
#   room-server-v1.16.0.1 -> prefix "room-server-"
#   touch-pre-alpha_17    -> prefix "touch-"
#
# Usage:
#   scripts/ci/prune-releases.sh <tag-prefix> [keep_count]
#
# Env:
#   GH_TOKEN / GITHUB_TOKEN  required (gh auth) — provided automatically in CI.
#   DRY_RUN=1                print what would be deleted, delete nothing.
#
# Requires the GitHub CLI (`gh`), preinstalled on GitHub-hosted runners.

set -euo pipefail

PREFIX="${1:?usage: prune-releases.sh <tag-prefix> [keep_count]}"
KEEP="${2:-5}"
DRY_RUN="${DRY_RUN:-0}"

if ! command -v gh >/dev/null 2>&1; then
  echo "error: gh (GitHub CLI) not found" >&2
  exit 1
fi

echo "Pruning releases with tag prefix '${PREFIX}', keeping newest ${KEEP} (DRY_RUN=${DRY_RUN})"

# Newest-first list of tags on this track. Sort by createdAt desc so "newest N"
# is deterministic regardless of GitHub's default ordering.
mapfile -t tags < <(
  gh release list --limit 300 --json tagName,createdAt,isDraft \
    --jq "[.[] | select(.tagName | startswith(\"${PREFIX}\"))]
          | sort_by(.createdAt) | reverse | .[].tagName"
)

total="${#tags[@]}"
if [ "$total" -le "$KEEP" ]; then
  echo "Found ${total} release(s) on this track; nothing to prune (<= ${KEEP})."
  exit 0
fi

idx=0
pruned=0
for tag in "${tags[@]}"; do
  idx=$((idx + 1))
  if [ "$idx" -le "$KEEP" ]; then
    echo "  keep   ${tag}"
    continue
  fi
  if [ "$DRY_RUN" = "1" ]; then
    echo "  PRUNE  ${tag}  (dry-run, not deleted)"
  else
    echo "  PRUNE  ${tag}"
    # --cleanup-tag also removes the underlying git tag; --yes skips the prompt.
    gh release delete "$tag" --yes --cleanup-tag
  fi
  pruned=$((pruned + 1))
done

echo "Done. ${total} release(s) on track, kept ${KEEP}, pruned ${pruned}."
