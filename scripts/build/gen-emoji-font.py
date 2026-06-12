#!/usr/bin/env python3
# Generate examples/companion_radio/ui-touch/emoji_data.{c,h}: a curated set of
# Noto colour emoji baked to LVGL RGB565+alpha (LV_IMG_CF_TRUE_COLOR_ALPHA)
# lv_img_dsc_t C-arrays + a sorted codepoint->dsc lookup, wired in UITask via
# lv_imgfont as the tail of the font fallback chain.
#
#   python3 scripts/build/gen-emoji-font.py [px] [max]
#
# Panel is LV_COLOR_16_SWAP=0 (native little-endian RGB565), so each pixel is
# emitted as [lo, hi, alpha]. Source art: googlefonts/noto-emoji png/128 (OFL).
import os, sys, urllib.request, io
from PIL import Image

PX      = int(sys.argv[1]) if len(sys.argv) > 1 else 18
MAXN    = int(sys.argv[2]) if len(sys.argv) > 2 else 250
HERE    = os.path.dirname(os.path.abspath(__file__))
ROOT    = os.path.abspath(os.path.join(HERE, "..", ".."))
OUTDIR  = os.path.join(ROOT, "examples", "companion_radio", "ui-touch")
CACHE   = os.path.join(ROOT, "data", "emoji-cache")
BASEURL = "https://raw.githubusercontent.com/googlefonts/noto-emoji/main/png/128/emoji_u{:x}.png"
os.makedirs(CACHE, exist_ok=True)

# Curated candidate codepoints (single-codepoint emoji Noto ships as emoji_uXXXX.png).
# Generous list; any 404s self-prune. Ordered by rough usage so the MAXN cap keeps
# the most useful ones.
CAND = [
 # gestures / hands (very common)
 0x1F44D,0x1F44E,0x1F44C,0x270C,0x1F91E,0x1F91F,0x1F918,0x1F919,0x1F448,0x1F449,
 0x1F446,0x1F447,0x261D,0x270B,0x1F91A,0x1F590,0x1F596,0x1F44B,0x1F91D,0x1F64F,
 0x270A,0x1F44A,0x1F91B,0x1F91C,0x1F44F,0x1F64C,0x1F450,0x1F932,0x1F4AA,0x1F595,
 # hearts
 0x2764,0x1F9E1,0x1F49B,0x1F49A,0x1F499,0x1F49C,0x1F5A4,0x1F90D,0x1F90E,0x1F494,
 0x1F495,0x1F49E,0x1F493,0x1F497,0x1F496,0x1F498,0x1F49D,0x1F49F,0x2763,
 # smileys
 0x1F600,0x1F601,0x1F602,0x1F603,0x1F604,0x1F605,0x1F606,0x1F607,0x1F609,0x1F60A,
 0x1F60B,0x1F60C,0x1F60D,0x1F60E,0x1F60F,0x1F610,0x1F611,0x1F612,0x1F613,0x1F614,
 0x1F615,0x1F616,0x1F617,0x1F618,0x1F619,0x1F61A,0x1F61B,0x1F61C,0x1F61D,0x1F61E,
 0x1F61F,0x1F620,0x1F621,0x1F622,0x1F623,0x1F624,0x1F625,0x1F626,0x1F627,0x1F628,
 0x1F629,0x1F62A,0x1F62B,0x1F62C,0x1F62D,0x1F62E,0x1F62F,0x1F630,0x1F631,0x1F632,
 0x1F633,0x1F634,0x1F635,0x1F636,0x1F637,0x1F641,0x1F642,0x1F643,0x1F644,
 0x1F910,0x1F911,0x1F912,0x1F913,0x1F914,0x1F915,0x1F916,0x1F917,0x1F920,0x1F921,
 0x1F922,0x1F923,0x1F924,0x1F925,0x1F927,0x1F928,0x1F929,0x1F92A,0x1F92B,0x1F92C,
 0x1F92D,0x1F92E,0x1F92F,0x1F970,0x1F971,0x1F973,0x1F974,0x1F975,0x1F976,0x1F97A,
 0x1F9D0,0x1F480,0x1F47B,0x1F47D,0x1F47E,0x1F4A9,0x1F608,0x1F47F,0x1F921,
 # expressive symbols
 0x1F4A4,0x1F4A2,0x1F4A5,0x1F4AB,0x1F4A6,0x1F4A8,0x1F4AC,0x1F4AD,0x1F525,0x2728,
 0x2B50,0x1F31F,0x1F320,0x1F4A1,0x1F389,0x1F38A,0x1F381,0x1F388,0x1F3C6,0x1F451,
 # check / status / symbols
 0x2705,0x274C,0x2B55,0x2753,0x2757,0x26A0,0x1F6AB,0x1F4AF,0x1F514,0x1F515,
 0x1F4CC,0x1F4CD,0x1F517,0x1F50B,0x1F50C,0x1F511,0x1F512,0x1F513,0x2714,0x2716,
 0x2795,0x2796,0x2797,0x2755,0x2754,0x1F51A,0x1F519,0x1F51C,0x1F4A3,0x1F3AF,
 # nature
 0x2600,0x1F319,0x2601,0x1F308,0x2744,0x1F4A7,0x26A1,0x1F30A,0x1F31E,0x1F31B,
 0x1F33F,0x1F340,0x1F341,0x1F331,0x1F333,0x1F334,0x1F33B,0x1F337,0x1F338,0x1F339,
 0x1F33A,0x1F490,0x1F342,
 # animals
 0x1F436,0x1F431,0x1F42D,0x1F439,0x1F430,0x1F43A,0x1F43B,0x1F428,0x1F42F,0x1F981,
 0x1F42E,0x1F437,0x1F438,0x1F435,0x1F648,0x1F649,0x1F64A,0x1F414,0x1F427,0x1F426,
 0x1F986,0x1F985,0x1F989,0x1F43D,0x1F417,0x1F434,0x1F984,0x1F41D,0x1F98B,0x1F40C,
 0x1F41E,0x1F422,0x1F40D,0x1F419,0x1F420,0x1F41F,0x1F42C,0x1F433,0x1F40B,0x1F988,
 # food / drink
 0x1F34E,0x1F34F,0x1F351,0x1F352,0x1F353,0x1F95D,0x1F345,0x1F951,0x1F346,0x1F954,
 0x1F33D,0x1F344,0x1F9C0,0x1F357,0x1F354,0x1F35F,0x1F355,0x1F32D,0x1F32E,0x1F32F,
 0x1F373,0x1F37F,0x1F368,0x1F366,0x1F369,0x1F36A,0x1F382,0x1F370,0x1F36B,0x1F36D,
 0x1F36C,0x2615,0x1F375,0x1F37A,0x1F37B,0x1F377,0x1F378,0x1F379,0x1F942,
 # activity / objects / travel
 0x26BD,0x1F3C0,0x1F3C8,0x26BE,0x1F3BE,0x1F3B1,0x1F3AE,0x1F3B2,0x1F3B0,0x1F3B3,
 0x1F4F7,0x1F4F8,0x1F3A5,0x1F4FA,0x1F3A4,0x1F3A7,0x1F4F2,0x1F4BB,0x1F5A5,0x1F4BE,
 0x1F4BF,0x1F50D,0x1F4D6,0x1F4DA,0x270F,0x1F4DD,0x1F4CE,0x2702,0x1F4CB,0x1F4C5,
 0x1F4C8,0x1F4C9,0x1F4CA,0x1F680,0x2708,0x1F697,0x1F6B2,0x1F3E0,0x231A,0x23F0,
 0x1F4B0,0x1F4B8,0x1F4B5,0x1F948,0x1F947,0x1F949,0x1F3C5,0x1F451,0x1F48E,0x1F4A1,
]

# Variation selectors / ZWJ render as zero-width (a tiny transparent image), so an
# emoji written with VS16 (e.g. U+2764 U+FE0F) doesn't tofu on the trailing byte.
ZERO_CPS = [0xFE0F, 0xFE0E, 0x200D]

def fetch(cp):
    fn = os.path.join(CACHE, "u{:x}.png".format(cp))
    if os.path.exists(fn) and os.path.getsize(fn) > 0:
        return fn
    url = BASEURL.format(cp)
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "emoji-gen"})
        data = urllib.request.urlopen(req, timeout=20).read()
    except Exception as e:
        return None
    with open(fn, "wb") as f:
        f.write(data)
    return fn

def to_rgb565a8(img):
    # The imgfont pins each glyph's BOX bottom to the text baseline and forces
    # ofs_y=0, so where the visible emoji sits depends entirely on the pixels.
    # Noto art is centred with transparent margin → it floats above the baseline
    # ("a bit high"). Autocrop the margin and bottom-align the content (with a tiny
    # PAD_BOTTOM descent) so the glyph sits on the baseline like letters.
    PAD_BOTTOM = 0
    img = img.convert("RGBA")
    bbox = img.split()[3].getbbox()
    if bbox:
        img = img.crop(bbox)
    w, h = img.size
    avail_h = PX - PAD_BOTTOM
    scale = min(PX / w, avail_h / h)
    nw, nh = max(1, round(w * scale)), max(1, round(h * scale))
    content = img.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("RGBA", (PX, PX), (0, 0, 0, 0))
    canvas.paste(content, ((PX - nw) // 2, PX - PAD_BOTTOM - nh))   # h-centre, bottom-align
    img = canvas
    out = bytearray()
    for (r, g, b, a) in img.getdata():
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)   # RGB565
        out += bytes((v & 0xFF, (v >> 8) & 0xFF, a))            # LSB, MSB, alpha (swap=0)
    return out

def cname(cp):
    return "e_{:x}".format(cp)

# ---- build ----
seen, glyphs = set(), []   # glyphs: (cp, name, bytes)
for cp in CAND:
    if cp in seen: continue
    if len(glyphs) >= MAXN: break
    fn = fetch(cp)
    if not fn:
        sys.stderr.write("skip (404) U+{:X}\n".format(cp)); continue
    try:
        data = to_rgb565a8(Image.open(fn))
    except Exception as ex:
        sys.stderr.write("skip (bad img) U+{:X}: {}\n".format(cp, ex)); continue
    seen.add(cp); glyphs.append((cp, cname(cp), data))

# emit .h
with open(os.path.join(OUTDIR, "emoji_data.h"), "w") as h:
    h.write("// AUTO-GENERATED by scripts/build/gen-emoji-font.py — do not edit.\n")
    h.write("// %d Noto colour emoji baked to RGB565+alpha at %dpx.\n#pragma once\n" % (len(glyphs), PX))
    h.write('#include "lvgl.h"\n#ifdef __cplusplus\nextern "C" {\n#endif\n')
    h.write("// Returns the baked image for `cp`, or NULL if we have no glyph for it.\n")
    h.write("const lv_img_dsc_t* emojiGlyphLookup(uint32_t cp);\n")
    h.write("extern const uint16_t kEmojiGlyphCount;\n")
    h.write("#ifdef __cplusplus\n}\n#endif\n")

# emit .c
PXB = PX * PX * 3
with open(os.path.join(OUTDIR, "emoji_data.c"), "w") as c:
    c.write("// AUTO-GENERATED by scripts/build/gen-emoji-font.py — do not edit.\n")
    c.write('#include "emoji_data.h"\n\n')
    # transparent 2x2 for variation selectors / ZWJ
    c.write("static const uint8_t e_zero[] = {0,0,0, 0,0,0, 0,0,0, 0,0,0};\n")
    c.write("static const lv_img_dsc_t d_zero = { { LV_IMG_CF_TRUE_COLOR_ALPHA, 0, 0, 2, 2 }, sizeof(e_zero), e_zero };\n\n")
    for (cp, name, data) in glyphs:
        c.write("static const uint8_t %s[] = {" % name)
        c.write(",".join(str(b) for b in data))
        c.write("};\n")
        c.write("static const lv_img_dsc_t d_%x = { { LV_IMG_CF_TRUE_COLOR_ALPHA, 0, 0, %d, %d }, sizeof(%s), %s };\n"
                % (cp, PX, PX, name, name))
    # sorted lookup table (binary search in the getter)
    entries = sorted([(cp, "d_%x" % cp) for (cp, _, _) in glyphs])
    entries += [(z, "d_zero") for z in ZERO_CPS]
    entries.sort()
    c.write("\ntypedef struct { uint32_t cp; const lv_img_dsc_t* dsc; } EmojiGlyph;\n")
    c.write("static const EmojiGlyph kGlyphs[] = {\n")
    for (cp, ref) in entries:
        c.write("  { 0x%Xu, &%s },\n" % (cp, ref))
    c.write("};\n")
    c.write("const uint16_t kEmojiGlyphCount = %d;\n\n" % len(glyphs))
    c.write("const lv_img_dsc_t* emojiGlyphLookup(uint32_t cp) {\n")
    c.write("  int lo = 0, hi = (int)(sizeof(kGlyphs)/sizeof(kGlyphs[0])) - 1;\n")
    c.write("  while (lo <= hi) {\n")
    c.write("    int mid = (lo + hi) >> 1;\n")
    c.write("    uint32_t v = kGlyphs[mid].cp;\n")
    c.write("    if (cp == v) return kGlyphs[mid].dsc;\n")
    c.write("    if (cp < v) hi = mid - 1; else lo = mid + 1;\n")
    c.write("  }\n  return 0;\n}\n")

print("emoji_data.c: %d glyphs @ %dpx  (~%d KB pixel data)" % (len(glyphs), PX, len(glyphs)*PXB//1024))
