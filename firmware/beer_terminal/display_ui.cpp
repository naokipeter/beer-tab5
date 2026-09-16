#include "display_ui.h"
#include <M5Unified.h>

namespace {
struct Target {
  int x, y;
  const char* label;
  bool passed;
};
constexpr int margin = 24;
constexpr int target_width = 240;
constexpr int target_height = 112;
Target targets[4];
int screen_width, screen_height;
bool marker_visible = false;
int marker_x, marker_y;
uint32_t last_refresh = 0;

bool inside(int x, int y, int left, int top, int width, int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}

void draw_target(const Target& target) {
  M5.Display.fillRoundRect(target.x, target.y, target_width, target_height, 16,
                          target.passed ? TFT_DARKGREEN : TFT_NAVY);
  M5.Display.drawRoundRect(target.x, target.y, target_width, target_height, 16, TFT_WHITE);
  M5.Display.setTextColor(TFT_WHITE, target.passed ? TFT_DARKGREEN : TFT_NAVY);
  M5.Display.setCursor(target.x + 16, target.y + 25);
  M5.Display.println(target.label);
  M5.Display.setCursor(target.x + 16, target.y + 64);
  M5.Display.print(target.passed ? "OK" : "Tap here");
}

void draw_progress() {
  unsigned passed = 0;
  for (const auto& target : targets) passed += target.passed;
  M5.Display.fillRect(24, 210, screen_width - 48, 38, TFT_BLACK);
  M5.Display.setTextColor(passed == 4 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(32, 218);
  if (passed == 4) M5.Display.print("All corners OK - drag in the centre");
  else M5.Display.printf("Corners checked: %u / 4", passed);
}

void clear_marker() {
  if (marker_visible) M5.Display.fillCircle(marker_x, marker_y, 12, TFT_BLACK);
  marker_visible = false;
}
}

namespace display_ui {
void begin() {
  screen_width = M5.Display.width();
  screen_height = M5.Display.height();
  targets[0] = {margin, margin, "Top left", false};
  targets[1] = {screen_width - margin - target_width, margin, "Top right", false};
  targets[2] = {margin, screen_height - margin - target_height, "Bottom left", false};
  targets[3] = {screen_width - margin - target_width,
                screen_height - margin - target_height, "Bottom right", false};
  marker_visible = false;
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextSize(3);
  for (const auto& target : targets) draw_target(target);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(32, 165);
  M5.Display.print("Touch test - tap all four corners");
  draw_progress();
  M5.Display.drawRect(24, 270, screen_width - 48, screen_height - 440, TFT_DARKGREY);
  const int reset_x = (screen_width - target_width) / 2;
  const int reset_y = screen_height - margin - target_height;
  M5.Display.drawRoundRect(reset_x, reset_y, target_width, target_height, 16, TFT_WHITE);
  M5.Display.setCursor(reset_x + 24, reset_y + 42);
  M5.Display.print("Start again");
  Serial.printf("Display: %d x %d; touch enabled: %s\n", screen_width, screen_height,
                M5.Touch.isEnabled() ? "yes" : "no");
}

void update() {
  // M5Unified uses the display's coordinate conversion, including its rotation.
  // Do not apply a second manual 180-degree touch transformation.
  const auto count = M5.Touch.getCount();
  bool pressed = false;
  for (uint8_t i = 0; i < count; ++i) {
    const auto touch = M5.Touch.getDetail(i);
    if (!touch.isPressed()) continue;
    pressed = true;
    if (touch.wasPressed()) {
      if (inside(touch.x, touch.y, (screen_width - target_width) / 2,
                 screen_height - margin - target_height, target_width, target_height)) {
        begin();
        return;
      }
      for (auto& target : targets) {
        if (!target.passed && inside(touch.x, touch.y, target.x, target.y,
                                    target_width, target_height)) {
          target.passed = true;
          draw_target(target);
          draw_progress();
          Serial.printf("Touch target: %s (%d, %d)\n", target.label, touch.x, touch.y);
        }
      }
    }
    // Limit repaint frequency; events above are still processed every loop.
    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - last_refresh) < 33) continue;
    last_refresh = now;
    clear_marker();
    M5.Display.fillRect(24, 250, screen_width - 48, 20, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(32, 250);
    M5.Display.printf("Touch: x=%d y=%d", touch.x, touch.y);
    M5.Display.setTextSize(3);
    if (inside(touch.x, touch.y, 38, 284, screen_width - 76, screen_height - 468)) {
      marker_x = touch.x;
      marker_y = touch.y;
      marker_visible = true;
      M5.Display.fillCircle(marker_x, marker_y, 10, TFT_CYAN);
    }
  }
  if (!pressed) clear_marker();
}
}
