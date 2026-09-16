#pragma once
#include <stddef.h>

// Resolves the deployment secrets. secrets.h is git-ignored; the firmware must
// still build without it, so every value falls back to an empty placeholder and
// the device reports itself unconfigured at runtime instead of failing to
// compile. Copy secrets.example.h to secrets.h to configure a real device.
#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef BEER_WIFI_SSID
#define BEER_WIFI_SSID ""
#endif
#ifndef BEER_WIFI_PASSWORD
#define BEER_WIFI_PASSWORD ""
#endif
#ifndef BEER_API_ENDPOINT
#define BEER_API_ENDPOINT ""
#endif
#ifndef BEER_DEVICE_TOKEN
#define BEER_DEVICE_TOKEN ""
#endif

namespace config {

inline constexpr char wifi_ssid[] = BEER_WIFI_SSID;
inline constexpr char wifi_password[] = BEER_WIFI_PASSWORD;
inline constexpr char api_endpoint[] = BEER_API_ENDPOINT;
inline constexpr char device_token[] = BEER_DEVICE_TOKEN;

// Network timeouts. Short enough that a dead backend does not leave someone
// holding a beer in front of a spinner, long enough for Apps Script, which is
// slow to start and redirects once before answering.
inline constexpr uint32_t connect_timeout_ms = 8000;
inline constexpr uint32_t request_timeout_ms = 12000;
inline constexpr uint32_t wifi_connect_timeout_ms = 15000;
// How long the radio stays up after the last request, so a purchase followed by
// an undo does not pay the association cost twice.
inline constexpr uint32_t wifi_linger_ms = 30000;

constexpr bool has(const char* s) { return s != nullptr && s[0] != '\0'; }

// A constexpr prefix check, so the https requirement is enforced at compile time
// as well as reported at runtime.
constexpr bool starts_with(const char* s, const char* prefix) {
  return *prefix == '\0' ? true : (*s != *prefix ? false : starts_with(s + 1, prefix + 1));
}

inline constexpr bool endpoint_is_https = starts_with(api_endpoint, "https://");

inline constexpr bool configured =
    has(wifi_ssid) && has(api_endpoint) && has(device_token) && endpoint_is_https;

// A configured but plaintext endpoint is a mistake worth stopping the build for:
// the device token would go out in clear.
static_assert(!has(api_endpoint) || endpoint_is_https,
              "BEER_API_ENDPOINT must be https://");

}  // namespace config
