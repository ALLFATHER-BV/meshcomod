#!/usr/bin/env python3
import argparse
import glob
import time
import serial


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    def pick_port() -> str:
        if args.port:
            return args.port
        ports = sorted(glob.glob("/dev/cu.usbmodem*"))
        return ports[0] if ports else ""

    current = ""
    while True:
        port = pick_port()
        if not port:
            if current:
                print("[serial] waiting for /dev/cu.usbmodem*", flush=True)
                current = ""
            time.sleep(0.4)
            continue
        try:
            ser = serial.Serial(port, args.baud, timeout=0.2)
            current = port
            print(f"[serial] connected {port} @{args.baud}", flush=True)
            while True:
                line = ser.readline()
                if line:
                    print(line.decode("utf-8", "replace").rstrip("\r\n"), flush=True)
        except serial.SerialException as ex:
            print(f"[serial] disconnect: {ex}", flush=True)
            time.sleep(0.25)

    return 0

if __name__ == "__main__":
    raise SystemExit(main())
