#pragma once
#include <stdint.h>

// Idle handling. For now this is the backlight only, which is the dominant
// consumer on a 5-inch panel and costs nothing in risk: touch keeps working
// while the screen is dark, so tapping it wakes the terminal.
//
// Light sleep for the SoC itself is the next step and needs current
// measurements on the real device before it is worth choosing a mode.
namespace power_manager {

void begin();

// Call whenever the user touched the screen.
void note_activity(uint32_t now_ms);

// Turns the display off after the idle period, and reports memory so a crash
// after hours of idling can be told apart from a random fault.
void update(uint32_t now_ms);

}  // namespace power_manager
