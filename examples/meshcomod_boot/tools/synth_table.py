#!/usr/bin/env python3
# Stage-2 prototype: given a firmware MERGED bin, synthesize a recovery-preserving
# partition table for meshcomod_boot:
#   fixed prefix (nvs, bootsel, otadata, factory=recovery) + ota_0 (the firmware's app,
#   sized to it) + the firmware's own data/FS partitions (tiles/spiffs/littlefs/... )
#   relocated to pack after ota_0, keeping their labels/subtypes/sizes.
# The firmware's app (app0/factory in its table) goes into ota_0; recovery stays at factory.
# Verified by re-decoding the output with the canonical gen_esp32part.py.
import sys, hashlib, struct, subprocess

SECTOR = 0x1000
FLASH  = 0x1000000           # 16 MB
PT_OFF = 0x8000
MAGIC  = 0x50AA
MD5_BEGIN = b"\xEB\xEB" + b"\xFF" * 14

TYPE_APP, TYPE_DATA = 0x00, 0x01
ST_FACTORY, ST_OTA0 = 0x00, 0x10
ST_NVS, ST_OTADATA, ST_PHY = 0x02, 0x00, 0x01   # data subtypes we PROVIDE ourselves

def rdtable(data, off=PT_OFF):
    out = []
    for i in range(0, 0x1000, 32):
        e = data[off+i:off+i+32]
        if len(e) < 32 or struct.unpack("<H", e[0:2])[0] != MAGIC:
            break
        typ, sub = e[2], e[3]
        offset, size = struct.unpack("<II", e[4:12])
        label = e[12:28].split(b"\x00")[0].decode("ascii", "replace")
        flags = struct.unpack("<I", e[28:32])[0]
        out.append([typ, sub, offset, size, label, flags])
    return out

def app_image_len(data, base):
    if data[base] != 0xE9: return 0
    segs, hash_app = data[base+1], data[base+23] == 1
    off = 24
    for _ in range(segs):
        dlen = struct.unpack("<I", data[base+off+4:base+off+8])[0]
        off += 8 + dlen
    off = (off + 1 + 15) & ~15
    if hash_app: off += 32
    return off

def roundup(x, a): return (x + a - 1) & ~(a - 1)

def synth(fw, app_len):
    # fixed prefix (matches our resident partitions.csv)
    out = [
        [TYPE_DATA, ST_NVS,     0x9000, 0x4000, "nvs",     0],
        [TYPE_DATA, 0x40,       0xd000, 0x1000, "bootsel", 0],   # launch flag (custom)
        [TYPE_DATA, ST_OTADATA, 0xe000, 0x2000, "otadata", 0],
        [TYPE_APP,  ST_FACTORY, 0x10000, 0x200000, "factory", 0],# recovery (fixed)
    ]
    ota0_size = max(roundup(app_len, 0x10000), 0x100000)
    out.append([TYPE_APP, ST_OTA0, 0x210000, ota0_size, "ota_0", 0])  # firmware app
    cur = 0x210000 + ota0_size
    # the firmware's own data partitions (skip the ones we already provide + skip apps)
    for typ, sub, off, size, label, flags in fw:
        if typ == TYPE_APP:                      # app(s) -> handled via ota_0
            continue
        if (off in (0x9000, 0xe000)) or sub in (ST_NVS,) or label in ("nvs", "otadata", "phy_init"):
            continue                              # provided by our prefix
        cur = roundup(cur, 0x10000)
        if cur + size > FLASH:
            sys.stderr.write("WARN: %s (%dKB) does not fit, skipping\n" % (label, size//1024))
            continue
        out.append([typ, sub, cur, size, label, flags])
        cur += size
    return out

def to_bin(entries):
    raw = b""
    for typ, sub, off, size, label, flags in entries:
        raw += struct.pack("<HBBII", MAGIC, typ, sub, off, size)
        raw += label.encode("ascii")[:16].ljust(16, b"\x00")
        raw += struct.pack("<I", flags)
    raw += MD5_BEGIN + hashlib.md5(raw).digest()
    raw += b"\xFF" * (0xC00 - len(raw))          # erased remainder = end-of-table marker
    return raw

def main():
    mb = sys.argv[1]
    gen = sys.argv[2]
    data = open(mb, "rb").read()
    fw = rdtable(data)
    print("=== firmware merged-bin table ===")
    for e in fw: print("  %-10s type=%d sub=0x%02x @0x%06x %6dKB" % (e[4], e[0], e[1], e[2], e[3]//1024))
    # app is the first APP partition (factory or ota_0)
    app_off = next((e[2] for e in fw if e[0] == TYPE_APP), None)
    app_len = app_image_len(data, app_off)
    print("app @0x%06x  image_len=%dKB" % (app_off, app_len//1024))
    syn = synth(fw, app_len)
    print("=== synthesized recovery table ===")
    for e in syn: print("  %-10s type=%d sub=0x%02x @0x%06x %6dKB" % (e[4], e[0], e[1], e[2], e[3]//1024))
    binout = to_bin(syn)
    open("/tmp/synth_part.bin", "wb").write(binout)
    print("table bytes=%d  (entries=%d + md5)" % (len(binout), len(syn)))
    print("=== verify with gen_esp32part.py (checks structure + MD5) ===")
    r = subprocess.run([sys.executable, gen, "/tmp/synth_part.bin"], capture_output=True, text=True)
    print(r.stdout); print(r.stderr)
    print("VERIFY:", "OK" if r.returncode == 0 else "FAILED rc=%d" % r.returncode)

main()
