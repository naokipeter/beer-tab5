#pragma once
#include "ui_lvgl.h"

// German QWERTZ layout for the on-screen keyboard. LVGL's default map is a
// QWERTY layout with no umlaut keys, so a beer called "Feldschlösschen" could
// not be typed even once the fonts could render it.
namespace ui_keyboard {

void apply_german_layout(lv_obj_t* kb);

}  // namespace ui_keyboard
