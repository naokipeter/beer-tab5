#pragma once
#include <stdint.h>
namespace settings {
inline constexpr uint32_t serial_baud = 115200;
inline constexpr char device_id[] = "fridge-01";
inline constexpr uint8_t display_rotation = 3;  // 180 degrees from original landscape (1).
// Resident IDs/names and admin settings will be added here in milestone 3.
}
