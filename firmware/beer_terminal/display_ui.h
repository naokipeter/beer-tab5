#pragma once
#include <stdint.h>

// LVGL port: display flush over M5GFX, touch input over M5Unified, and the tick
// source. Screen construction lives in ui_screens, not here.
namespace display_ui {

// Returns false if the LVGL draw buffers could not be allocated.
bool begin();

// Pumps LVGL. Returns how long LVGL is happy to wait before the next call, so
// the loop can sleep instead of spinning without adding latency to a redraw.
uint32_t update();

}  // namespace display_ui
