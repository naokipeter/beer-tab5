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

// Apps Script answers a POST with a 302 to script.googleusercontent.com and
// serves the body from there, so at least one hop is always needed.
constexpr int kMaxRedirects = 3;
// Those echo URLs carry a long one-time key.
constexpr size_t kMaxUrlBytes = 768;
// Distinct from any HTTP status or HTTPClient error code.
constexpr int kResponseTooLarge = -1000;

TaskHandle_t g_task = nullptr;
char g_request[api_protocol::kMaxRequestBytes];
size_t g_request_len = 0;
char* g_response = nullptr;
size_t g_response_len = 0;
int g_http_code = 0;

volatile Status g_status = Status::Idle;
volatile Error g_error = Error::None;
// Bumped for every job and by abandon(). The worker publishes its result only
// if the id it started with is still current, so a late answer from a request
// nobody waits for any more cannot overwrite a newer one.
volatile uint32_t g_job_id = 0;
// Separate from the status: after abandon() the status is Idle but the worker
// still owns g_request and g_response.
volatile bool g_worker_busy = false;
// Formatted messages need somewhere to live, since error_text returns a pointer.
char g_error_detail[64] = {};

void set_status(Status s) {
  // Release: the buffers are written before the status the main loop polls.
  __atomic_store_n(reinterpret_cast<volatile uint8_t*>(&g_status),
                   static_cast<uint8_t>(s), __ATOMIC_RELEASE);
}

// Publishes only if this job is still the one being waited on.
void finish_job(uint32_t job, Status s) {
  if (__atomic_load_n(&g_job_id, __ATOMIC_ACQUIRE) != job) {
    Serial.println("[api] discarding the result of an abandoned request");
    return;
  }
  set_status(s);
}

// A fixed-capacity sink for HTTPClient::writeToStream. Reading the raw stream
// from getStreamPtr() instead would copy the transfer framing into the buffer:
// Apps Script replies chunked, and the library de-chunks inside writeToStream,
// not in the stream it hands out. Overflow is recorded rather than written.
class BoundedSink : public Stream {
 public:
  BoundedSink(char* buffer, size_t capacity) : buf_(buffer), cap_(capacity) {}

  size_t write(uint8_t c) override { return write(&c, 1); }

  size_t write(const uint8_t* data, size_t size) override {
    const size_t room = (len_ + 1 < cap_) ? cap_ - 1 - len_ : 0;
    const size_t n = size < room ? size : room;
    if (n) memcpy(buf_ + len_, data, n);
    len_ += n;
    if (n < size) overflow_ = true;
    // Claim everything so the library keeps draining the socket; the excess is
    // discarded here rather than left to stall the connection.
    return size;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }

  size_t length() const { return len_; }
  bool overflowed() const { return overflow_; }

 private:
  char* buf_;
  size_t cap_;
  size_t len_ = 0;
  bool overflow_ = false;
};

// One hop. Returns the HTTP status, or a negative HTTPClient error. On a
// redirect the target is copied into `location`.
int one_request(const char* url, bool post, char* location, size_t location_cap) {
  NetworkClientSecure client;
  client.setCACertBundle(kCaBundleStart,
                         static_cast<size_t>(kCaBundleEnd - kCaBundleStart));
  client.setTimeout(config::request_timeout_ms / 1000);

  HTTPClient http;
  http.setConnectTimeout(config::connect_timeout_ms);
  http.setTimeout(config::request_timeout_ms);
  // Redirects are followed by hand, one hop per call with a fresh HTTPClient.
  // Letting the library do it sends the POST's Content-Length and Content-Type
  // along on the follow-up GET — it only clears its header list when the new
  // location is a bare path, and Apps Script's is an absolute URL on another
  // host. Google answers that GET-with-a-body-length with 400.
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setReuse(false);

  if (!http.begin(client, url)) return HTTPC_ERROR_CONNECTION_REFUSED;

  int code;
  if (post) {
    http.addHeader("Content-Type", "application/json");
    code = http.POST(reinterpret_cast<uint8_t*>(g_request), g_request_len);
  } else {
    code = http.GET();
  }

  location[0] = '\0';
  if (code == HTTP_CODE_MOVED_PERMANENTLY || code == HTTP_CODE_FOUND ||
      code == HTTP_CODE_SEE_OTHER || code == HTTP_CODE_TEMPORARY_REDIRECT ||
      code == HTTP_CODE_PERMANENT_REDIRECT) {
    snprintf(location, location_cap, "%s", http.getLocation().c_str());
    http.end();
    return code;
  }

  if (code > 0) {
    BoundedSink sink(g_response, api_protocol::kMaxResponseBytes);
    const int written = http.writeToStream(&sink);
    g_response[sink.length()] = '\0';
    g_response_len = sink.length();
    if (sink.overflowed()) {
      Serial.printf("[api] response exceeded %u bytes\n",
                    static_cast<unsigned>(api_protocol::kMaxResponseBytes));
      http.end();
      return kResponseTooLarge;
    }
    if (written < 0) {
      Serial.printf("[api] body read failed: %s\n",
                    HTTPClient::errorToString(written).c_str());
    }
  }

  http.end();
  return code;
}

void perform(uint32_t job) {
  g_response_len = 0;
  g_http_code = 0;
  if (g_response) g_response[0] = '\0';

  char url[kMaxUrlBytes];
  char location[kMaxUrlBytes];
  snprintf(url, sizeof(url), "%s", config::api_endpoint);

  bool post = true;
  int code = 0;
  for (int hop = 0; hop <= kMaxRedirects; ++hop) {
    code = one_request(url, post, location, sizeof(location));
    g_http_code = code;

    if (code == kResponseTooLarge) {
      snprintf(g_error_detail, sizeof(g_error_detail), "Antwort zu gross");
      g_error = Error::TooLarge;
      finish_job(job, Status::Failed);
      return;
    }

    if (code <= 0) {
      Serial.printf("[api] transport error %d (%s)\n", code,
                    HTTPClient::errorToString(code).c_str());
      snprintf(g_error_detail, sizeof(g_error_detail), "%s",
               HTTPClient::errorToString(code).c_str());
      g_error = Error::Transport;
      finish_job(job, Status::Failed);
      return;
    }

    if (location[0] == '\0') break;  // not a redirect; this is the answer

    if (hop == kMaxRedirects) {
      Serial.println("[api] too many redirects");
      snprintf(g_error_detail, sizeof(g_error_detail), "Zu viele Weiterleitungen");
      g_error = Error::HttpStatus;
      finish_job(job, Status::Failed);
      return;
    }
    if (!api_protocol::redirect_target_allowed(location)) {
      Serial.printf("[api] refusing redirect to %.80s\n", location);
      snprintf(g_error_detail, sizeof(g_error_detail), "Weiterleitung abgelehnt");
      g_error = Error::HttpStatus;
      finish_job(job, Status::Failed);
      return;
    }
    // The body is deliberately not resent: the redirect drops to GET, so the
    // device token never reaches the second host.
    snprintf(url, sizeof(url), "%s", location);
    post = false;
  }

  if (code < 200 || code >= 300) {
    Serial.printf("[api] http %d, %u bytes: %.120s\n", code,
                  static_cast<unsigned>(g_response_len), g_response);
    snprintf(g_error_detail, sizeof(g_error_detail), "HTTP %d vom Server", code);
    g_error = Error::HttpStatus;
    finish_job(job, Status::Failed);
    return;
  }

  g_error = Error::None;
  finish_job(job, Status::Done);
}

void worker(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const uint32_t job = __atomic_load_n(&g_job_id, __ATOMIC_ACQUIRE);
    g_worker_busy = true;
    perform(job);
    g_worker_busy = false;
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
  if (!g_task || !g_response || g_worker_busy) {
    g_error = g_worker_busy ? Error::Busy : Error::Transport;
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

void abandon() {
  // The worker cannot be interrupted mid-handshake, so invalidate its job
  // instead and let it discard its own result when it eventually returns.
  __atomic_add_fetch(&g_job_id, 1, __ATOMIC_RELEASE);
  g_error = Error::Transport;
  snprintf(g_error_detail, sizeof(g_error_detail), "Zeitüberschreitung");
  set_status(Status::Idle);
}

bool worker_busy() { return g_worker_busy; }

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
