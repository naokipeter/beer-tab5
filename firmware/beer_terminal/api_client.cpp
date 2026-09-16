#include "api_client.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <esp_heap_caps.h>
#include <string.h>
#include "api_protocol.h"
#include "config.h"
#include "wifi_manager.h"

namespace api_client {
namespace {

// The full Mozilla root store, compiled into the SDK by
// CONFIG_MBEDTLS_CERTIFICATE_BUNDLE. Using it rather than pinning one root means
// certificate rotation on Google's side cannot brick the fridge.
extern "C" const uint8_t kCaBundleStart[] asm("_binary_x509_crt_bundle_start");
extern "C" const uint8_t kCaBundleEnd[] asm("_binary_x509_crt_bundle_end");

// TLS needs a generous stack; 12 KB is the usual working figure for mbedTLS
// with a certificate bundle.
constexpr uint32_t kTaskStack = 12288;
constexpr UBaseType_t kTaskPriority = 3;
// Pinned away from the Arduino loop task so a TLS handshake cannot compete with
// the UI for the same core.
constexpr BaseType_t kTaskCore = 0;

TaskHandle_t g_task = nullptr;
char g_request[api_protocol::kMaxRequestBytes];
size_t g_request_len = 0;
char* g_response = nullptr;
size_t g_response_len = 0;
int g_http_code = 0;

volatile Status g_status = Status::Idle;
volatile Error g_error = Error::None;
// Formatted messages need somewhere to live, since error_text returns a pointer.
char g_error_detail[64] = {};

void set_status(Status s) {
  // Release: the buffers are written before the status the main loop polls.
  __atomic_store_n(reinterpret_cast<volatile uint8_t*>(&g_status),
                   static_cast<uint8_t>(s), __ATOMIC_RELEASE);
}

void perform() {
  g_response_len = 0;
  g_http_code = 0;
  if (g_response) g_response[0] = '\0';

  NetworkClientSecure client;
  client.setCACertBundle(kCaBundleStart,
                         static_cast<size_t>(kCaBundleEnd - kCaBundleStart));
  client.setTimeout(config::request_timeout_ms / 1000);

  HTTPClient http;
  http.setConnectTimeout(config::connect_timeout_ms);
  http.setTimeout(config::request_timeout_ms);
  // Apps Script answers a POST with a 302 to script.googleusercontent.com and
  // serves the body from there. The redirect target carries its own one-time
  // token in the URL; our device token stays in the request body, which a 302
  // does not resend, so it is never handed to the second host. A token in a
  // header would be resent.
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(false);

  if (!http.begin(client, config::api_endpoint)) {
    g_error = Error::Transport;
    set_status(Status::Failed);
    return;
  }
  http.addHeader("Content-Type", "application/json");

  // HTTPClient::POST takes a non-const pointer but does not modify the body.
  const int code =
      http.POST(reinterpret_cast<uint8_t*>(g_request), g_request_len);
  g_http_code = code;

  if (code <= 0) {
    Serial.printf("[api] transport error %d (%s)\n", code,
                  HTTPClient::errorToString(code).c_str());
    snprintf(g_error_detail, sizeof(g_error_detail), "%s",
             HTTPClient::errorToString(code).c_str());
    g_error = Error::Transport;
    http.end();
    set_status(Status::Failed);
    return;
  }

  const int size = http.getSize();
  if (size > static_cast<int>(api_protocol::kMaxResponseBytes - 1)) {
    Serial.printf("[api] response too large: %d bytes\n", size);
    g_error = Error::TooLarge;
    http.end();
    set_status(Status::Failed);
    return;
  }

  // Read bounded regardless of the advertised length, since a chunked response
  // reports -1.
  NetworkClient* stream = http.getStreamPtr();
  size_t written = 0;
  const uint32_t deadline = millis() + config::request_timeout_ms;
  while (http.connected() && written < api_protocol::kMaxResponseBytes - 1) {
    const size_t avail = stream->available();
    if (avail == 0) {
      if (static_cast<int32_t>(millis() - deadline) >= 0) break;
      if (size >= 0 && written >= static_cast<size_t>(size)) break;
      delay(5);
      continue;
    }
    const size_t room = api_protocol::kMaxResponseBytes - 1 - written;
    const size_t want = avail < room ? avail : room;
    const int got = stream->readBytes(g_response + written, want);
    if (got <= 0) break;
    written += static_cast<size_t>(got);
    if (size >= 0 && written >= static_cast<size_t>(size)) break;
  }
  g_response[written] = '\0';
  g_response_len = written;
  http.end();

  if (code < 200 || code >= 300) {
    // Carry the status onto the screen. "Server meldet einen Fehler" alone sends
    // someone looking for a serial cable; the number says which mistake it is.
    Serial.printf("[api] http %d, %u bytes: %.120s\n", code,
                  static_cast<unsigned>(written), g_response);
    snprintf(g_error_detail, sizeof(g_error_detail), "HTTP %d vom Server", code);
    g_error = Error::HttpStatus;
    set_status(Status::Failed);
    return;
  }

  g_error = Error::None;
  set_status(Status::Done);
}

void worker(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    perform();
  }
}

}  // namespace

bool begin() {
  // PSRAM: 8 KB of internal RAM is better spent on TLS and LVGL.
  g_response = static_cast<char*>(
      heap_caps_malloc(api_protocol::kMaxResponseBytes, MALLOC_CAP_SPIRAM));
  if (!g_response) {
    Serial.println("[api] response buffer allocation failed");
    return false;
  }
  if (xTaskCreatePinnedToCore(worker, "api", kTaskStack, nullptr, kTaskPriority, &g_task,
                              kTaskCore) != pdPASS) {
    Serial.println("[api] worker task creation failed");
    g_task = nullptr;
    return false;
  }
  return true;
}

bool post(const char* body, size_t len) {
  if (!config::configured) {
    g_error = Error::NotConfigured;
    return false;
  }
  if (!g_task || !g_response) {
    g_error = Error::Transport;
    return false;
  }
  if (__atomic_load_n(reinterpret_cast<volatile uint8_t*>(&g_status),
                      __ATOMIC_ACQUIRE) != static_cast<uint8_t>(Status::Idle)) {
    g_error = Error::Busy;
    return false;
  }
  if (!wifi_manager::online()) {
    g_error = Error::Offline;
    return false;
  }
  if (!body || len == 0 || len >= sizeof(g_request)) {
    g_error = Error::Transport;
    return false;
  }

  memcpy(g_request, body, len);
  g_request[len] = '\0';
  g_request_len = len;
  g_error = Error::None;
  g_error_detail[0] = '\0';
  set_status(Status::Busy);
  xTaskNotifyGive(g_task);
  return true;
}

Status status() {
  return static_cast<Status>(__atomic_load_n(
      reinterpret_cast<volatile uint8_t*>(&g_status), __ATOMIC_ACQUIRE));
}

Error error() { return g_error; }
const char* response() { return g_response ? g_response : ""; }
size_t response_len() { return g_response_len; }
int http_code() { return g_http_code; }

void reset() { set_status(Status::Idle); }

const char* error_text() {
  switch (g_error) {
    case Error::None:          return "";
    case Error::NotConfigured: return "Gerät nicht konfiguriert";
    case Error::Offline:       return "Kein WLAN";
    case Error::Busy:          return "Anfrage läuft bereits";
    case Error::Transport:
      return g_error_detail[0] ? g_error_detail : "Server nicht erreichbar";
    case Error::HttpStatus:
      return g_error_detail[0] ? g_error_detail : "Server meldet einen Fehler";
    case Error::TooLarge:      return "Antwort zu gross";
  }
  return "";
}

}  // namespace api_client
