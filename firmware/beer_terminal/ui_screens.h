#pragma once
#include <stdint.h>
#include "app_state.h"

// Builds one LVGL screen per application state. Screens are rebuilt on
// transition, never per frame, so no allocation happens in the main loop.
namespace ui_screens {

void begin();

// Tears down the active screen and builds the one for `current`.
void show(app_state::State current);

}  // namespace ui_screens
