#pragma once
#include <stdint.h>

// LVGL port: display flush over M5GFX, touch input over M5Unified, and the tick
// source. Screen construction lives in ui_screens, not here.
namespace display_ui {

// Returns false if the LVGL draw buffers could not be allocated.
bool begin();

// Pumps LVGL. Call every loop; never blocks.
void update();

}  // namespace display_ui
