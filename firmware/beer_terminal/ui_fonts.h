#pragma once
#include "ui_lvgl.h"

// Montserrat subsets generated with lv_font_conv from the TTF that ships with
// LVGL, extended with the German and Swiss-French letters the UI actually uses.
// LVGL's built-in Montserrat fonts cover ASCII only, which is why umlauts were
// missing. Regenerate with tools/generate-fonts.sh.
//
// font_de_20 is LV_FONT_DEFAULT and additionally carries the LVGL symbol glyphs,
// because widget internals (keyboard keys, arrows) draw with the default font.
extern "C" {
extern const lv_font_t font_de_20;
extern const lv_font_t font_de_28;
extern const lv_font_t font_de_32;
extern const lv_font_t font_de_48;
}
