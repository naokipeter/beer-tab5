#!/usr/bin/env bash
# Regenerates the UI fonts. LVGL's built-in Montserrat fonts cover ASCII only,
# so German and Swiss-French letters render as blanks. These subsets add them.
#
# Needs Node (for npx). Run after changing the glyph set, then commit the
# generated firmware/beer_terminal/font_de_*.c files: Arduino IDE has no build
# step that could produce them.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/firmware/beer_terminal"
FONTS="$ROOT/.arduino/user/libraries/lvgl/scripts/built_in_font"

if [[ ! -f "$FONTS/Montserrat-Medium.ttf" ]]; then
  echo "Montserrat-Medium.ttf not found. Run tools/setup.sh to install lvgl first." >&2
  exit 1
fi

# Latin-1 letters used by German and by Swiss product and resident names, plus
# German typographic quotes and an en dash.
SYMS='ÄÖÜäöüßÉéÈèÀàÂâÊêÎîÔôÛûÇç„“–'
# The LVGL symbol glyphs, taken from the library's own built_in_font_gen.py.
FA="61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650"

gen() { # size, extra args
  local size="$1"; shift
  npx --yes lv_font_conv@1.5.3 --bpp 4 --size "$size" \
    --font "$FONTS/Montserrat-Medium.ttf" -r '0x20-0x7F,0xB0,0x2022' --symbols "$SYMS" \
    "$@" \
    --no-compress --force-fast-kern-format \
    --format lvgl --lv-include lvgl.h -o "$OUT/font_de_$size.c"
}

# Only the default size carries the LVGL symbol glyphs: widget internals such as
# the keyboard's control keys draw with LV_FONT_DEFAULT. The display sizes are
# pure text, so merging FontAwesome into them would be dead weight.
gen 20 --font "$FONTS/FontAwesome5-Solid+Brands+Regular.woff" -r "$FA"
for s in 28 32 48; do gen "$s"; done

ls -la "$OUT"/font_de_*.c
