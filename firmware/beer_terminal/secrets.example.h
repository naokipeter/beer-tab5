#pragma once
// Copy to secrets.h and fill in. secrets.h is git-ignored and must never be
// committed. The firmware compiles without it: every value below has a
// placeholder default, and the device reports itself as unconfigured rather
// than failing to build.

// Wi-Fi. WPA2-Personal; the ESP32-C6 provides the radio over ESP-Hosted.
#define BEER_WIFI_SSID "my-network"
#define BEER_WIFI_PASSWORD "my-password"

// The Google Apps Script web app, deployed as "execute as me". Must be https.
// The device refuses any other scheme rather than sending its token in clear.
#define BEER_API_ENDPOINT "https://script.google.com/macros/s/AKfycb.../exec"

// Shared secret checked server-side on every request. Rotate by changing it in
// the Script Properties and here. Not a user credential: it only authorises this
// device to record purchases.
#define BEER_DEVICE_TOKEN "replace-me"
