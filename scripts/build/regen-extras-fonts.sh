#!/bin/sh
# Regenerate the extras_font_{12,14,16}.c fallback fonts for the touch UI.
#
# These are the FALLBACK fonts attached to g_font_{12,14,16} (see
# initTouchFontFallbacks in UITask.cpp). They carry everything Montserrat
# doesn't: Latin accents, punctuation/currency/math symbols, and a curated set
# of monochrome emoji. Whatever glyph is baked here renders across the whole UI
# (chat, names, etc.); anything NOT baked is shown as '*' by
# copyUtf8ReplacingMissingGlyphs, and the on-device emoji picker
# (k_emoji_items in UITask.cpp) only offers glyphs that exist here — so keep
# the two in sync when you change the set.
#
# Requirements:
#   - node/npx (uses lv_font_conv@1.5.3, fetched on first run)
#   - Arial Unicode.ttf (macOS: /Library/Fonts) for accents + symbols
#   - NotoEmoji-Regular.ttf MONOCHROME (NOT NotoColorEmoji — LVGL renders
#     1-bit/4-bpp bitmaps, color emoji are unusable). Source:
#       curl -L -o NotoEmoji-Regular.ttf \
#         https://raw.githubusercontent.com/googlefonts/noto-emoji/v2.034/fonts/NotoEmoji-Regular.ttf
#
# Usage:
#   NOTO_EMOJI=/path/to/NotoEmoji-Regular.ttf scripts/build/regen-extras-fonts.sh
#
set -e

ARIAL="${ARIAL_UNICODE:-/Library/Fonts/Arial Unicode.ttf}"
NOTO="${NOTO_EMOJI:-/tmp/emojifont/NotoEmoji-Regular.ttf}"
OUT="$(cd "$(dirname "$0")/../../examples/companion_radio/ui-new" && pwd)"

[ -f "$ARIAL" ] || { echo "missing Arial Unicode: $ARIAL"; exit 1; }
[ -f "$NOTO" ]  || { echo "missing mono NotoEmoji: $NOTO (see header)"; exit 1; }

# Special chars: bullet first so the --symbols arg doesn't start with a dash
# (npm/argparse treats a leading non-ascii dash as a bad flag).
SPECIAL="$(python3 -c "print(''.join(chr(c) for c in [0x2022,0x00B7,0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2026,0x2192,0x2190,0x2191,0x2193,0x00B0,0x00B1,0x00D7,0x00F7,0x20AC,0x00A3,0x00A5,0x00A7,0x00A9,0x00AE,0x2122,0x2260,0x2264,0x2265,0x00BD,0x00BC,0x00BE,0x2103,0x2109]))")"

# Curated emoji set (must exist in the v2.034 mono font). Keep in sync with
# k_emoji_items[] in UITask.cpp.
EMOJI="$(python3 -c "
cps=[0x1F600,0x1F603,0x1F604,0x1F601,0x1F606,0x1F605,0x1F602,0x1F609,0x1F60A,0x1F60D,0x1F618,0x1F60B,0x1F61C,0x1F60E,0x1F60F,0x1F612,0x1F61E,0x1F614,0x1F622,0x1F62D,0x1F621,0x1F620,0x1F631,0x1F634,0x1F62C,0x1F610,
0x1F44D,0x1F44E,0x1F44C,0x270C,0x1F44A,0x270A,0x1F44F,0x1F64C,0x1F64F,0x1F4AA,0x1F44B,
0x2764,0x1F494,0x1F495,0x1F499,0x1F49A,0x1F49B,0x1F49C,
0x1F389,0x1F38A,0x2728,0x1F525,0x1F4AF,0x2705,0x274C,0x2757,0x2753,0x26A0,0x1F4A9,0x2B50,0x1F31F,0x26A1,0x2600,0x2601,0x2744,0x2614,0x2615,0x1F37B,0x1F355,0x1F382,0x1F381,0x1F4AC,0x1F4A4,0x1F4A5,
0x1F680,0x1F4F7,0x1F4F1,0x1F4BB,0x1F4CD,0x1F4CC,0x1F4C5,0x1F50B,0x1F4E1,0x1F511,0x1F512,0x1F4E7,0x1F3E0,0x1F697,0x231A,0x1F4A1,
0x1F436,0x1F431,0x1F438,0x1F43B,0x1F427,0x1F41D]
print(''.join(chr(c) for c in cps))
")"

for sz in 12 14 16; do
  npx --yes lv_font_conv@1.5.3 \
    --size "$sz" --bpp 4 --no-compress --format lvgl \
    --font "$ARIAL" -r 0x00C0-0x00FF -r 0x0100-0x017F -r 0x0400-0x04FF --symbols "$SPECIAL" \
    --font "$NOTO" --symbols "$EMOJI" \
    --lv-font-name "extras_$sz" \
    -o "$OUT/extras_font_$sz.c"
  # Match the project's include convention (#if 1 / #include "lvgl.h").
  python3 - "$OUT/extras_font_$sz.c" <<'PY'
import sys
p=sys.argv[1]; s=open(p).read()
s=s.replace('#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n#include "lvgl.h"\n#else\n#include "lvgl/lvgl.h"\n#endif',
            '#if 1\n#include "lvgl.h"\n#else\n#include "lvgl/lvgl.h"\n#endif')
open(p,'w').write(s)
PY
  echo "  wrote extras_font_$sz.c"
done
echo "done. rebuild the touch envs to pick up the new glyphs."
