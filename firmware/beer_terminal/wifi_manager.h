#pragma once
#include <stdint.h>

// Wi-Fi lifecycle. The radio lives on the ESP32-C6 and reaches the P4 over
// ESP-Hosted SDIO, which the board variant already wires up; from Arduino it is
// the ordinary WiFi API.
//
// Nothing here blocks. The radio is only powered while it is needed, and lingers
// briefly after the last request so a purchase followed by an undo does not pay
// the association cost twice.
namespace wifi_manager {

enum class State : uint8_t {
  Unconfigured,  // no SSID compiled in; see secrets.example.h
  Off,
  Connecting,
  Connected,
  Failed,
};

void begin();

// Ask for the radio. Safe to call repeatedly; extends the linger window.
void request_online();
// Drop the link now, without waiting for the linger window.
void request_offline();

void update(uint32_t now_ms);

State state();
bool online();
// Short German label for the admin screen.
const char* status_text();
int32_t rssi();

}  // namespace wifi_manager
