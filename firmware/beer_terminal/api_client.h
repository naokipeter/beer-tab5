#pragma once
#include <stddef.h>
#include <stdint.h>

// One HTTPS request at a time, performed on its own FreeRTOS task so a slow
// backend never stalls LVGL. The caller posts a body, then polls status from the
// main loop; nothing here blocks the loop task.
namespace api_client {

enum class Status : uint8_t {
  Idle,
  Busy,
  Done,    // a response body was received; inspect http_code()
  Failed,  // never reached the server, or the response was unusable
};

enum class Error : uint8_t {
  None,
  NotConfigured,
  Offline,
  Busy,
  Transport,   // DNS, TLS, connection, or timeout
  HttpStatus,  // reached the server, which answered with a non-2xx status
  TooLarge,    // response exceeded the buffer
};

// Allocates the response buffer and starts the worker. Returns false if the
// buffer could not be allocated, in which case every post() fails cleanly.
bool begin();

// Queues a request. Returns false when unconfigured, offline, already busy, or
// the body does not fit; error() says which.
bool post(const char* body, size_t len);

Status status();
Error error();
const char* error_text();

// Valid while status() is Done. The buffer is null-terminated.
const char* response();
size_t response_len();
int http_code();

// Returns to Idle. Must be called before the next post().
void reset();

}  // namespace api_client
