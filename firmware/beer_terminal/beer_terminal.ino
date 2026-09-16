#include <Arduino.h>
#include <M5Unified.h>
#include "settings.h"
#include "display_ui.h"

#if !defined(ARDUINO_M5STACK_TAB5) || !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "Select the official M5Stack M5Tab5 board."
#endif
static_assert(__cplusplus >= 201703L, "C++17 or newer required");

// Milestone 2: display and touch validation before the LVGL workflow.
void setup() {
  auto cfg = M5.config();
  cfg.internal_mic = false;
  cfg.internal_spk = false;
  cfg.internal_imu = false;
  M5.begin(cfg);
  Serial.begin(settings::serial_baud);
  M5.Display.setRotation(settings::display_rotation);
  M5.Display.setBrightness(80);
  display_ui::begin();
  Serial.println("Beer terminal: milestone 2 ready (rotation 3)");
  Serial.printf("PSRAM: %u bytes; free heap: %u bytes\n",
                ESP.getPsramSize(), ESP.getFreeHeap());
}

void loop() {
  M5.update();
  display_ui::update();
  const uint32_t now = millis();
  static uint32_t last_report = 0;
  if (static_cast<uint32_t>(now - last_report) >= 5000) {
    last_report = now;
    Serial.println("Milestone 2 heartbeat");
  }
  delay(5);  // Yield to system tasks; this is awake idle, not a sleep mode.
}
