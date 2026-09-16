#pragma once
#include <lvgl.h>

// This project targets the LVGL 9 API (lv_display_t, lv_button_create,
// lv_screen_active, ...). LVGL 8 renamed most of these, so building against it
// produces dozens of unrelated-looking "not declared in this scope" errors.
// Fail once, clearly, instead.
//
// Arduino IDE uses its own sketchbook library folder, which is separate from the
// project-local one the CLI build uses. Upgrading with the CLI does not upgrade
// the IDE: install lvgl 9.2.2 through the IDE's Library Manager as well.
#if LVGL_VERSION_MAJOR < 9
#error "This project needs LVGL 9.2.2. Arduino IDE is compiling against LVGL 8 from your sketchbook (Documents/Arduino/libraries/lvgl). Update lvgl to 9.2.2 in the IDE Library Manager."
#endif
