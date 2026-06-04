#!/usr/bin/env python3
"""
Tile-pack generator for the Meshcomod Touch map tab.

Downloads slippy tiles from a tile server, converts to JPEG, and writes them
to <out>/<z>/<x>/<y>.jpg. Default output is `data/tiles/`, which is what
PlatformIO ships to SPIFFS when you run `pio run -t uploadfs`.

The on-device map renderer reads `/tiles/<z>/<x>/<y>.jpg` from SPIFFS — paths
match this script's output by design.

Example:
    # 10 km radius around Brussels, zoom 13-15 (~250 tiles, ~3 MB)
    python scripts/build/tile-pack.py \\
        --lat 50.85 --lon 4.35 --radius-km 10 --zoom 13,14,15 \\
        --out data/tiles
    pio run -e heltec_v4_tft_companion_radio_usb_tcp_touch -t uploadfs

Tile sources:
    - OSM (default) — fine for personal/dev use; respect the policy
      (https://operations.osmfoundation.org/policies/tiles/).
      No bulk downloads; this script throttles by default.
    - Stadia Maps / MapTiler — drop in their URL template via --tile-server
      and add the API key in --ua or a query param.

Requirements:
    - Python 3.8+
    - Pillow (`pip install Pillow`) for PNG→JPEG conversion.
"""

import argparse
import io
import math
import os
import sys
import time
import urllib.request
import urllib.error


def deg_to_tile(lat: float, lon: float, zoom: int):
    """lat/lon → integer slippy tile (x, y)."""
    lat_rad = math.radians(lat)
    n = 2.0 ** zoom
    xtile = int((lon + 180.0) / 360.0 * n)
    ytile = int((1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * n)
    return xtile, ytile


def tile_bounds(lat: float, lon: float, radius_km: float, zoom: int):
    """
    Tile range covering a square `2*radius_km` on a side, centered on (lat, lon).
    Approximation: 1 deg latitude ≈ 111 km; longitude scales by cos(lat).
    """
    lat_delta = radius_km / 111.0
    lon_delta = radius_km / max(0.1, 111.0 * math.cos(math.radians(lat)))
    # NB: deg_to_tile's y axis runs from north (small y) to south (large y).
    x1, y_north = deg_to_tile(lat + lat_delta, lon - lon_delta, zoom)
    x2, y_south = deg_to_tile(lat - lat_delta, lon + lon_delta, zoom)
    return min(x1, x2), max(x1, x2), min(y_north, y_south), max(y_north, y_south)


def fetch_tile(url: str, ua: str, timeout: float) -> bytes:
    req = urllib.request.Request(url, headers={'User-Agent': ua})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        if r.status != 200:
            raise RuntimeError(f"HTTP {r.status}")
        return r.read()


def png_to_jpeg(png_bytes: bytes, quality: int = 78) -> bytes:
    """Convert PNG → JPEG via Pillow. SPIFFS storage is tight, JPEG halves it."""
    try:
        from PIL import Image
    except ImportError as e:
        sys.exit("ERROR: Pillow not installed. Run: pip install Pillow")
    img = Image.open(io.BytesIO(png_bytes)).convert('RGB')
    out = io.BytesIO()
    img.save(out, 'JPEG', quality=quality, optimize=True)
    return out.getvalue()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--lat', type=float, required=True, help='Center latitude (degrees)')
    ap.add_argument('--lon', type=float, required=True, help='Center longitude (degrees)')
    ap.add_argument('--radius-km', type=float, required=True,
                    help='Half-side of the square coverage area in kilometres')
    ap.add_argument('--zoom', required=True,
                    help='Comma-separated zoom levels, e.g. 13,14,15')
    ap.add_argument('--out', default='data/tiles',
                    help='Output directory (default: data/tiles — PIO uploadfs root)')
    ap.add_argument('--tile-server', default='https://tile.openstreetmap.org',
                    help='Tile server URL template root. {z}/{x}/{y}.png will be appended.')
    ap.add_argument('--ua', default='meshcomod-touch-tile-pack/1.0',
                    help='User-Agent — OSM requires identifying yourself.')
    ap.add_argument('--throttle-ms', type=int, default=200,
                    help='Sleep this many ms between requests (be polite).')
    ap.add_argument('--quality', type=int, default=78,
                    help='JPEG quality 1-95 (default 78 — slippy tiles compress well).')
    ap.add_argument('--skip-existing', action='store_true', default=True,
                    help="Don't re-download tiles already on disk.")
    ap.add_argument('--dry-run', action='store_true',
                    help='Print the plan, don\'t download anything.')
    args = ap.parse_args()

    zooms = sorted({int(z) for z in args.zoom.split(',') if z.strip()})
    if not zooms:
        sys.exit("ERROR: --zoom must list at least one integer level")
    for z in zooms:
        if not (0 <= z <= 19):
            sys.exit(f"ERROR: zoom {z} out of range (0..19)")

    # Plan first so the user knows the disk + bandwidth cost up front.
    plan = []
    total_planned = 0
    for z in zooms:
        x_min, x_max, y_min, y_max = tile_bounds(args.lat, args.lon, args.radius_km, z)
        count = (x_max - x_min + 1) * (y_max - y_min + 1)
        plan.append((z, x_min, x_max, y_min, y_max, count))
        total_planned += count
    print(f"Plan: {total_planned} tiles total")
    for z, x1, x2, y1, y2, n in plan:
        print(f"  z={z}: x={x1}..{x2} y={y1}..{y2} ({n} tiles)")
    if args.dry_run:
        return

    # Tiles now live on their own LittleFS partition; the SPIFFS-wipe
    # problem the old `pio run -t uploadfs` flow had is gone. Use
    # scripts/build/upload-tiles.py to push the pack — it only writes the
    # tiles partition and leaves Profile / Radio / contacts / chat history
    # alone.

    # OSM hard-limits high-volume scrapers — sanity check.
    if total_planned > 5000:
        print("\nWARNING: > 5000 tiles. OSM bulk policy says no — use a paid provider")
        print("         or self-host a tile server. Continuing in 5s; Ctrl-C to stop.")
        time.sleep(5)

    os.makedirs(args.out, exist_ok=True)

    n_ok = n_skip = n_fail = 0
    for z, x_min, x_max, y_min, y_max, _ in plan:
        out_z = os.path.join(args.out, str(z))
        for x in range(x_min, x_max + 1):
            out_x = os.path.join(out_z, str(x))
            os.makedirs(out_x, exist_ok=True)
            for y in range(y_min, y_max + 1):
                fn = os.path.join(out_x, f"{y}.jpg")
                if args.skip_existing and os.path.exists(fn):
                    n_skip += 1
                    continue
                url = f"{args.tile_server.rstrip('/')}/{z}/{x}/{y}.png"
                try:
                    png = fetch_tile(url, args.ua, timeout=20)
                    jpg = png_to_jpeg(png, quality=args.quality)
                    with open(fn, 'wb') as f:
                        f.write(jpg)
                    n_ok += 1
                    if n_ok % 25 == 0:
                        print(f"  ok {n_ok}/{total_planned - n_skip}: z={z} x={x} y={y} ({len(jpg)} B)")
                    if args.throttle_ms > 0:
                        time.sleep(args.throttle_ms / 1000.0)
                except (urllib.error.URLError, urllib.error.HTTPError, RuntimeError) as e:
                    n_fail += 1
                    print(f"  fail z={z} x={x} y={y}: {e}")
                except KeyboardInterrupt:
                    print(f"\nInterrupted. Got {n_ok}, skipped {n_skip}, failed {n_fail}.")
                    return

    print(f"\nDone. Got {n_ok}, skipped {n_skip}, failed {n_fail}.")
    print(f"Tiles at: {os.path.abspath(args.out)}")
    print("\nNext: python scripts/build/upload-tiles.py")
    print("       (only writes the tiles partition; Profile + chats stay put)")


if __name__ == '__main__':
    main()
