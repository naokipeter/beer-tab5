#include "display_ui.h"
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include "ui_lvgl.h"
#include "settings.h"

namespace display_ui {
namespace {

// Partial render mode: LVGL draws into this, we blit each area to the panel.
//
// The buffer lives in *internal* RAM. LVGL fills it pixel by pixel, and PSRAM is
// several times slower per access, so rendering there dominated the time a
// screen took to appear. Drawing internally and blitting once to the panel's
// PSRAM framebuffer is the cheaper way round.
//
// A single buffer, not two: the flush is synchronous — pushImage returns before
// lv_display_flush_ready is called — so a second buffer would never be rendered
// into while the first was in flight. It would cost 80 KB of internal RAM for
// nothing.
constexpr int16_t kBufferLines = 32;
constexpr size_t kBufferPixels = static_cast<size_t>(settings::screen_w) * kBufferLines;
constexpr size_t kBufferBytes = kBufferPixels * sizeof(uint16_t);

lv_display_t* g_display = nullptr;
lv_indev_t* g_touch = nullptr;
uint16_t* g_buf = nullptr;

void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;
  // LVGL renders native-endian RGB565, which matches lgfx::rgb565_t. If colours
  // come out inverted on hardware, this is the line to swap for swap565_t.
  // startWrite/endWrite brackets the transfer so the panel is set up once per
  // area rather than once per call inside pushImage.
  M5.Display.startWrite();
  M5.Display.pushImage(area->x1, area->y1, w, h,
                       reinterpret_cast<const lgfx::rgb565_t*>(px_map));
  M5.Display.endWrite();
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

  // Internal RAM if it fits, PSRAM otherwise: a slow display beats none.
  g_buf = static_cast<uint16_t*>(heap_caps_malloc(kBufferBytes, MALLOC_CAP_INTERNAL));
  if (g_buf) {
    Serial.printf("[ui] draw buffer in internal RAM (%u bytes)\n",
                  static_cast<unsigned>(kBufferBytes));
  } else {
    g_buf = static_cast<uint16_t*>(heap_caps_malloc(kBufferBytes, MALLOC_CAP_SPIRAM));
    Serial.println("[ui] draw buffer fell back to PSRAM; redraws will be slower");
  }
  if (!g_buf) {
    Serial.printf("[ui] draw buffer allocation failed (%u bytes)\n",
                  static_cast<unsigned>(kBufferBytes));
    return false;
  }

  g_display = lv_display_create(settings::screen_w, settings::screen_h);
  if (!g_display) return false;
  lv_display_set_flush_cb(g_display, flush_cb);
  lv_display_set_buffers(g_display, g_buf, nullptr, kBufferBytes,
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

uint32_t update() { return lv_timer_handler(); }

}  // namespace display_ui
