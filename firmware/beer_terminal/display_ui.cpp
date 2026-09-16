#include "display_ui.h"
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include "settings.h"

namespace display_ui {
namespace {

// Partial render mode: LVGL draws into these, we blit each area to the panel.
// 40 lines is a compromise between blit count and PSRAM footprint (~100 KB each).
constexpr int16_t kBufferLines = 40;
constexpr size_t kBufferPixels = static_cast<size_t>(settings::screen_w) * kBufferLines;
constexpr size_t kBufferBytes = kBufferPixels * sizeof(uint16_t);

lv_display_t* g_display = nullptr;
lv_indev_t* g_touch = nullptr;
uint16_t* g_buf1 = nullptr;
uint16_t* g_buf2 = nullptr;

void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;
  // LVGL renders native-endian RGB565, which matches lgfx::rgb565_t. If colours
  // come out inverted on hardware, this is the line to swap for swap565_t.
  M5.Display.pushImage(area->x1, area->y1, w, h,
                       reinterpret_cast<const lgfx::rgb565_t*>(px_map));
  lv_display_flush_ready(disp);
}

void touch_read_cb(lv_indev_t*, lv_indev_data_t* data) {
  // M5.update() runs in the main loop; this only reads the latest sample.
  if (M5.Touch.getCount() > 0) {
    const auto t = M5.Touch.getDetail(0);
    data->point.x = t.x;
    data->point.y = t.y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

uint32_t tick_cb() { return millis(); }

void log_cb(lv_log_level_t, const char* buf) {
  Serial.print("[lvgl] ");
  Serial.println(buf);
}

}  // namespace

bool begin() {
  M5.Display.setRotation(settings::display_rotation);
  M5.Display.setBrightness(180);
  M5.Display.fillScreen(TFT_BLACK);

  lv_init();
  lv_tick_set_cb(tick_cb);
  lv_log_register_print_cb(log_cb);

  // Draw buffers go to PSRAM; internal RAM is reserved for the network and
  // decoder work that arrives in later milestones.
  g_buf1 = static_cast<uint16_t*>(heap_caps_malloc(kBufferBytes, MALLOC_CAP_SPIRAM));
  g_buf2 = static_cast<uint16_t*>(heap_caps_malloc(kBufferBytes, MALLOC_CAP_SPIRAM));
  if (!g_buf1 || !g_buf2) {
    Serial.printf("[ui] draw buffer allocation failed (%u bytes each)\n",
                  static_cast<unsigned>(kBufferBytes));
    return false;
  }

  g_display = lv_display_create(settings::screen_w, settings::screen_h);
  if (!g_display) return false;
  lv_display_set_flush_cb(g_display, flush_cb);
  lv_display_set_buffers(g_display, g_buf1, g_buf2, kBufferBytes,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  g_touch = lv_indev_create();
  lv_indev_set_type(g_touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(g_touch, touch_read_cb);

  Serial.printf("[ui] LVGL %d.%d.%d ready, %ux%u\n", LVGL_VERSION_MAJOR,
                LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
                static_cast<unsigned>(settings::screen_w),
                static_cast<unsigned>(settings::screen_h));
  return true;
}

void update() { lv_timer_handler(); }

}  // namespace display_ui
