#include "wifi_manager.h"
#include <Arduino.h>
#include <WiFi.h>
#include "config.h"

namespace wifi_manager {
namespace {

State g_state = State::Off;
uint32_t g_attempt_started = 0;
uint32_t g_linger_until = 0;
uint32_t g_retry_after = 0;
uint8_t g_failures = 0;
bool g_wanted = false;

// Exponential backoff, capped. A fridge terminal that cannot reach the router
// should not keep the radio busy retrying every second.
uint32_t backoff_ms() {
  const uint32_t base = 2000u << (g_failures < 5 ? g_failures : 5);
  return base > 60000u ? 60000u : base;
}

void radio_off() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  g_state = State::Off;
}

void start_attempt(uint32_t now) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.begin(config::wifi_ssid, config::wifi_password);
  g_attempt_started = now;
  g_state = State::Connecting;
  Serial.printf("[wifi] connecting to %s\n", config::wifi_ssid);
}

}  // namespace

void begin() {
  if (!config::has(config::wifi_ssid)) {
    g_state = State::Unconfigured;
    Serial.println("[wifi] no SSID compiled in; see secrets.example.h");
    return;
  }
  radio_off();
}

void request_online() {
  if (g_state == State::Unconfigured) return;
  g_wanted = true;
  g_linger_until = millis() + config::wifi_linger_ms;
}

void request_offline() {
  if (g_state == State::Unconfigured) return;
  g_wanted = false;
  g_linger_until = 0;
  if (g_state != State::Off) {
    Serial.println("[wifi] radio off");
    radio_off();
  }
}

void update(uint32_t now_ms) {
  if (g_state == State::Unconfigured) return;

  // Drop the link once nothing has asked for it for a while.
  if (!g_wanted || static_cast<int32_t>(now_ms - g_linger_until) >= 0) {
    if (g_state == State::Connected || g_state == State::Connecting) {
      Serial.println("[wifi] idle, powering the radio down");
      radio_off();
      g_wanted = false;
    }
    return;
  }

  switch (g_state) {
    case State::Off:
    case State::Failed:
      if (static_cast<int32_t>(now_ms - g_retry_after) >= 0) start_attempt(now_ms);
      break;

    case State::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        g_failures = 0;
        g_state = State::Connected;
        Serial.printf("[wifi] connected, %s, rssi %ld\n",
                      WiFi.localIP().toString().c_str(),
                      static_cast<long>(WiFi.RSSI()));
      } else if (static_cast<int32_t>(now_ms - g_attempt_started) >
                 static_cast<int32_t>(config::wifi_connect_timeout_ms)) {
        if (g_failures < 255) ++g_failures;
        g_state = State::Failed;
        g_retry_after = now_ms + backoff_ms();
        WiFi.disconnect(true);
        Serial.printf("[wifi] attempt %u timed out, retrying in %lu ms\n",
                      static_cast<unsigned>(g_failures),
                      static_cast<unsigned long>(backoff_ms()));
      }
      break;

    case State::Connected:
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[wifi] link lost");
        g_state = State::Failed;
        g_retry_after = now_ms;
      }
      break;

    case State::Unconfigured:
      break;
  }
}

State state() { return g_state; }
bool online() { return g_state == State::Connected; }
int32_t rssi() { return g_state == State::Connected ? WiFi.RSSI() : 0; }

const char* status_text() {
  switch (g_state) {
    case State::Unconfigured: return "nicht konfiguriert";
    case State::Off:          return "aus";
    case State::Connecting:   return "verbindet";
    case State::Connected:    return "verbunden";
    case State::Failed:       return "kein Netz";
  }
  return "?";
}

}  // namespace wifi_manager
