// M5Stack Tab5 beer terminal.
// Milestone 3: LVGL screens and the explicit state machine, driven by a mock
// catalog. No camera, networking, backend or sleep implementation yet.
#include <M5Unified.h>

#include "app_state.h"
#include "display_ui.h"
#include "product_catalog.h"
#include "settings.h"
#include "ui_screens.h"

namespace {

void on_state_change(app_state::State, app_state::State current) {
  ui_screens::show(current);
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(settings::serial_baud);

  product_catalog::begin();

  if (!display_ui::begin()) {
    // Without draw buffers there is no UI to report the failure through.
    M5.Display.setTextSize(2);
    M5.Display.println("LVGL init failed - see serial log");
    return;
  }

  // Registered after the display exists, so the first transition can draw.
  app_state::begin(on_state_change);
  Serial.printf("[boot] device=%s products=%u residents=%u\n", settings::device_id,
                static_cast<unsigned>(product_catalog::count()),
                static_cast<unsigned>(settings::resident_count));
}

void loop() {
  M5.update();
  app_state::update(millis());
  display_ui::update();
  delay(5);
}
