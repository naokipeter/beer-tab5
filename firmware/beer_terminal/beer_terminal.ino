// M5Stack Tab5 beer terminal.
// Milestone 3: LVGL screens and the explicit state machine, driven by a mock
// catalog. No camera, networking, backend or sleep implementation yet.
// Resident names come from resident_directory, which the backend replaces in
// milestone 7; they are never hard-coded into a screen.
#include <M5Unified.h>

#include "app_state.h"
#include "device_storage.h"
#include "display_ui.h"
#include "product_catalog.h"
#include "purchase_log.h"
#include "resident_directory.h"
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

  device_storage::begin();
  product_catalog::begin();
  resident_directory::begin();
  purchase_log::begin();

  if (!display_ui::begin()) {
    // Without draw buffers there is no UI to report the failure through.
    M5.Display.setTextSize(2);
    M5.Display.println("LVGL init failed - see serial log");
    return;
  }

  // Registered after the display exists, so the first transition can draw.
  app_state::begin(on_state_change);
  Serial.printf("[boot] device=%s active=%u archived=%u residents=%u (%s)\n",
                settings::device_id,
                static_cast<unsigned>(product_catalog::active_count()),
                static_cast<unsigned>(product_catalog::archived_count()),
                static_cast<unsigned>(resident_directory::count()),
                resident_directory::from_backend() ? "backend" : "fallback");
}

void loop() {
  M5.update();
  app_state::update(millis());
  display_ui::update();
  // Persist off the UI event path, so a flash write never delays a touch.
  product_catalog::flush();
  resident_directory::flush();
  delay(5);
}
