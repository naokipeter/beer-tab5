// M5Stack Tab5 beer terminal.
// Milestone 7: LVGL screens, the explicit state machine, a persisted catalog and
// an HTTPS client that talks to the Apps Script backend over the ESP32-C6.
// Without secrets.h the device still runs: purchases are simulated locally.
// No camera and no sleep implementation yet.
// Resident names come from resident_directory, which the backend replaces in
// milestone 7; they are never hard-coded into a screen.
#include <M5Unified.h>

#include "api_client.h"
#include "app_state.h"
#include "backend.h"
#include "device_storage.h"
#include "display_ui.h"
#include "product_catalog.h"
#include "purchase_log.h"
#include "resident_directory.h"
#include "settings.h"
#include "transaction_queue.h"
#include "wifi_manager.h"
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
  transaction_queue::begin();
  purchase_log::begin();

  if (!display_ui::begin()) {
    // Without draw buffers there is no UI to report the failure through.
    M5.Display.setTextSize(2);
    M5.Display.println("LVGL init failed - see serial log");
    return;
  }

  // Networking comes up after the UI, so a slow association never delays the
  // first frame. The device is fully usable before the radio associates.
  wifi_manager::begin();
  api_client::begin();
  backend::begin();

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
  const uint32_t now = millis();
  M5.update();
  wifi_manager::update(now);
  backend::update(now);
  app_state::update(now);
  display_ui::update();
  // Persist off the UI event path, so a flash write never delays a touch.
  product_catalog::flush();
  resident_directory::flush();
  transaction_queue::flush();
  purchase_log::flush();
  delay(5);
}
