#pragma once
#include <stdint.h>

// Central configuration. Resident names, layout metrics and theme live here
// rather than being scattered through the UI code.
namespace settings {

inline constexpr uint32_t serial_baud = 115200;
inline constexpr char device_id[] = "fridge-01";
inline constexpr uint8_t display_rotation = 3;  // 180 degrees from original landscape (1).

// Panel is 720x1280 native; rotation 3 presents it as landscape.
inline constexpr int16_t screen_w = 1280;
inline constexpr int16_t screen_h = 720;

// Catalog grid metrics, in pixels. See docs/architecture.md.
inline constexpr int16_t header_h = 64;
inline constexpr int16_t footer_h = 72;
inline constexpr int16_t grid_margin = 16;
inline constexpr int16_t grid_gap = 16;

// The fridge never holds more than this many types, so the grid never pages.
inline constexpr uint8_t max_products = 8;

// A tile wider than this width:height ratio puts the photo beside the text
// instead of above it. Scaled by 100 so the layout unit needs no floating point.
inline constexpr int16_t tile_horizontal_ratio_x100 = 135;

struct Resident {
  const char* id;
  const char* name;
};

// Replace with the real household before deploying the user-selection screen.
inline constexpr Resident residents[] = {
  {"r1", "Naoki"},  {"r2", "Lena"},   {"r3", "Tobias"},
  {"r4", "Miriam"}, {"r5", "Samuel"}, {"r6", "Gast"},
};
inline constexpr uint8_t resident_count = sizeof(residents) / sizeof(residents[0]);

// High-contrast dark theme, chosen to stay readable in a dim kitchen.
namespace theme {
inline constexpr uint32_t bg = 0x12120F;
inline constexpr uint32_t surface = 0x22221D;
inline constexpr uint32_t surface_alt = 0x2C2C26;
inline constexpr uint32_t text = 0xF2F0E9;
inline constexpr uint32_t text_muted = 0x9A988F;
inline constexpr uint32_t accent = 0xE5C46A;   // price, primary confirmation
inline constexpr uint32_t ok = 0x63991F;
inline constexpr uint32_t danger = 0xC0392B;
}  // namespace theme

// Simulated backend latency for the milestone 3 prototype, in milliseconds.
inline constexpr uint32_t mock_submit_ms = 800;
// How long SUCCESS stays on screen before returning to the catalog.
inline constexpr uint32_t success_dwell_ms = 2000;

}  // namespace settings
