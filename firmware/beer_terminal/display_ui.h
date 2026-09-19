#pragma once
#include <stdint.h>

// LVGL port: display flush over M5GFX, touch input over M5Unified, and the tick
// source. Screen construction lives in ui_screens, not here.
namespace display_ui {

// Returns false if the LVGL draw buffers could not be allocated.
bool begin();

// Pumps LVGL. Call every loop; never blocks.
void update();

// Backlight and panel. Off is the large power saving on a 5-inch display;
// touch keeps working, so the screen can be woken by tapping it.
void set_awake(bool awake);
bool awake();

// Renders pending changes immediately instead of waiting for the next refresh.
// Used to get the new screen onto the panel before the backlight comes back, so
// waking never shows the previous one.
void refresh_now();

}  // namespace display_ui
