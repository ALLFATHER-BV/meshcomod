#!/usr/bin/env python3
"""
Upload the map tile pack to the device's dedicated LittleFS "tiles"
partition WITHOUT touching the SPIFFS partition that holds the operator's
Profile / Radio / contacts / chat-history data.

This is the replacement for `pio run -t uploadfs`, which would have
overwritten the entire SPIFFS partition every time the user refreshed
their map.

Workflow:
    # 1. Build a tile pack centered on your location
    python scripts/build/tile-pack.py --lat 50.85 --lon 4.35 --radius-km 10 --zoom 12,13

    # 2. Push it to the tiles partition (SPIFFS untouched)
    python scripts/build/upload-tiles.py

Implementation:
    - mklittlefs (shipped with PIO) builds a LittleFS image from data/tiles/
    - esptool flashes the image to the offset declared for the "tiles"
      partition in variants/heltec_v4/partitions_tft_touch.csv

Run from the project root.
"""

import argparse
import csv
import glob
import os
import shutil
import subprocess
import sys
import tempfile


PARTITION_CSV  = "variants/heltec_v4/partitions_tft_touch.csv"
DEFAULT_DATA   = "data/tiles"
PARTITION_NAME = "tiles"


def find_partition(csv_path, name):
    """Parse the partition CSV; return (offset, size) for the named entry."""
    with open(csv_path) as f:
        for row in csv.reader(f, delimiter=",", skipinitialspace=True):
            if not row or row[0].startswith("#") or len(row) < 5:
                continue
            if row[0].strip() == name:
                return int(row[3], 0), int(row[4], 0)
    return None, None


def find_pio_tool(name):
    """Locate a PIO-bundled tool (mklittlefs, esptool.py)."""
    pio_packages = os.path.expanduser("~/.platformio/packages")
    # Glob for any version
    matches = glob.glob(os.path.join(pio_packages, f"tool-{name}*", name)) + \
              glob.glob(os.path.join(pio_packages, f"tool-{name}*", f"{name}.py"))
    if matches:
        return matches[0]
    return None


def detect_port():
    """Best-effort serial port detection. macOS / Linux only."""
    for pattern in ("/dev/cu.usbmodem*", "/dev/ttyUSB*", "/dev/ttyACM*"):
        ports = glob.glob(pattern)
        if ports:
            return ports[0]
    return None


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--data", default=DEFAULT_DATA,
                    help=f"Directory with tile files (default: {DEFAULT_DATA})")
    ap.add_argument("--partition-csv", default=PARTITION_CSV,
                    help="Path to partition CSV (default: variants/heltec_v4/partitions_tft_touch.csv)")
    ap.add_argument("--port", help="Serial port (auto-detect if omitted)")
    ap.add_argument("--baud", type=int, default=921600, help="Upload baud rate")
    ap.add_argument("--dry-run", action="store_true",
                    help="Build the LittleFS image but skip the esptool flash step")
    args = ap.parse_args()

    # 1. Validate inputs
    if not os.path.isdir(args.data):
        sys.exit(f"ERROR: {args.data} not found. Run scripts/build/tile-pack.py first.")
    if not os.path.exists(args.partition_csv):
        sys.exit(f"ERROR: partition CSV {args.partition_csv} not found.")

    offset, size = find_partition(args.partition_csv, PARTITION_NAME)
    if offset is None:
        sys.exit(f"ERROR: '{PARTITION_NAME}' partition not found in {args.partition_csv}.")

    print(f"Partition '{PARTITION_NAME}': offset 0x{offset:X}, size {size/1024/1024:.2f} MB")

    # 2. Locate PIO tools
    mklittlefs = find_pio_tool("mklittlefs")
    if not mklittlefs:
        sys.exit("ERROR: mklittlefs not found. Build the project once with `pio run` so PIO installs it.")
    esptool = find_pio_tool("esptoolpy")
    if not esptool:
        # Some PIO installs name it esptool.py directly
        esptool = find_pio_tool("esptool")
    if not esptool:
        sys.exit("ERROR: esptool not found. Build the project once with `pio run` so PIO installs it.")

    print(f"mklittlefs: {mklittlefs}")
    print(f"esptool:    {esptool}")

    # 3. mklittlefs expects the source directory to BE the FS root. The
    #    firmware reads paths like "/tiles/<z>/<x>/<y>.jpg", so we need
    #    the tiles to live under a "tiles/" subdirectory in the staging
    #    area, not at the root.
    image_dir  = os.path.join(".pio", "build",
                              "heltec_v4_tft_companion_radio_usb_tcp_touch")
    os.makedirs(image_dir, exist_ok=True)
    image_path = os.path.join(image_dir, "tiles.bin")

    with tempfile.TemporaryDirectory(prefix="meshcomod-tiles-") as staging:
        target = os.path.join(staging, "tiles")
        print(f"Staging tiles from {args.data} → {target}")
        shutil.copytree(args.data, target)
        cmd = [
            mklittlefs,
            "-c", staging,
            "-s", str(size),
            "-p", "256",
            "-b", "4096",
            image_path,
        ]
        print("Building LittleFS image:", " ".join(cmd))
        subprocess.check_call(cmd)

    print(f"Image: {image_path} ({os.path.getsize(image_path)/1024/1024:.2f} MB)")
    if args.dry_run:
        print("Dry-run: skipping flash. Image ready at the path above.")
        return

    # 4. Flash
    port = args.port or detect_port()
    if not port:
        sys.exit("ERROR: no serial port found. Pass --port /dev/cu.usbmodemXXXX.")
    flash_cmd = [
        sys.executable, esptool,
        "--chip", "esp32s3",
        "--port", port,
        "--baud", str(args.baud),
        "write_flash",
        f"0x{offset:X}", image_path,
    ]
    print("Flashing:", " ".join(flash_cmd))
    subprocess.check_call(flash_cmd)
    print("\nDone. Tiles partition updated. SPIFFS (Profile / Radio / contacts /")
    print("chat history) was NOT touched.")


if __name__ == "__main__":
    main()
