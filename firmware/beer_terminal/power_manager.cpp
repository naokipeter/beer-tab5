#include "power_manager.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include "app_state.h"
#include "display_ui.h"
#include "settings.h"

namespace power_manager {
namespace {

uint32_t g_last_activity = 0;
uint32_t g_next_heap_log = 0;

void log_memory(uint32_t now_ms) {
  const size_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const size_t internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  const size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  // The minimum ever seen is the number that matters: a steadily falling floor
  // is a leak, while a low instantaneous value may just be a busy moment.
  Serial.printf("[mem] internal %u free (min %u), psram %u free, up %lu s\n",
                static_cast<unsigned>(internal), static_cast<unsigned>(internal_min),
                static_cast<unsigned>(psram),
                static_cast<unsigned long>(now_ms / 1000));
}

}  // namespace

void begin() {
  g_last_activity = millis();
  g_next_heap_log = g_last_activity;
}

void note_activity(uint32_t now_ms) {
  g_last_activity = now_ms;
  // Waking is a state change; the backlight follows from it.
  if (!display_ui::awake()) app_state::dispatch(app_state::Event::Wake);
}

void update(uint32_t now_ms) {
  if (static_cast<int32_t>(now_ms - g_next_heap_log) >= 0) {
    g_next_heap_log = now_ms + settings::heap_log_interval_ms;
    log_memory(now_ms);
  }

  if (!display_ui::awake()) return;
  if (static_cast<int32_t>(now_ms - (g_last_activity + settings::display_off_ms)) < 0) {
    return;
  }
  app_state::dispatch(app_state::Event::Sleep);
}

}  // namespace power_manager
