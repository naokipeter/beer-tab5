#include "backend.h"
#include <Arduino.h>
#include <string.h>
#include "api_client.h"
#include "api_protocol.h"
#include "config.h"
#include "product_catalog.h"
#include "resident_directory.h"
#include "settings.h"
#include "wifi_manager.h"

namespace backend {
namespace {

enum class Op : uint8_t { None, Sync, Purchase, Void };

// One operation at a time. It may be waiting for the radio before it is handed
// to the client, which is why `posted` is separate from `op`: a configured
// device that is momentarily offline must wait for the association rather than
// report a result it never obtained.
Op g_op = Op::None;
bool g_posted = false;
uint32_t g_deadline = 0;
size_t g_request_len = 0;

Result g_result = Result::Idle;
char g_error[64] = {};
bool g_duplicate = false;

uint32_t g_last_sync = 0;

// Static: these are kilobytes and the loop task's stack is not the place for them.
char g_request[api_protocol::kMaxRequestBytes];
api_protocol::SyncResult g_sync;

void finish(Result r, const char* message) {
  // A background sync must never surface in the purchase UI, so it resolves to
  // Idle rather than Success or Failure.
  g_result = (g_op == Op::Sync) ? Result::Idle : r;
  snprintf(g_error, sizeof(g_error), "%s", message ? message : "");
  g_op = Op::None;
  g_posted = false;
  api_client::reset();
}

// Takes ownership of whatever was just built into g_request.
bool start(Op op, size_t len, uint32_t now) {
  if (len == 0) {
    Serial.println("[backend] request did not fit");
    return false;
  }
  g_op = op;
  g_posted = false;
  g_request_len = len;
  // Long enough to associate and then complete the request.
  g_deadline = now + config::wifi_connect_timeout_ms + config::request_timeout_ms + 5000;
  if (op != Op::Sync) {
    g_result = Result::Pending;
    g_error[0] = '\0';
    g_duplicate = false;
  }
  wifi_manager::request_online();
  return true;
}

void apply_sync() {
  char err[64];
  const auto outcome = api_protocol::parse_sync(api_client::response(),
                                                api_client::response_len(), &g_sync,
                                                err, sizeof(err));
  if (outcome != api_protocol::Outcome::Ok) {
    // Print what actually arrived: an unreadable body is almost always an HTML
    // error page or a login redirect, which the first line makes obvious.
    Serial.printf("[sync] rejected: %s (%u bytes: %.160s)\n", err,
                  static_cast<unsigned>(api_client::response_len()),
                  api_client::response());
    return;
  }
  if (!g_sync.changed && g_sync.revision == product_catalog::revision()) {
    Serial.printf("[sync] unchanged at revision %lu\n",
                  static_cast<unsigned long>(g_sync.revision));
    return;
  }
  // Only replace the resident list when the backend actually sent one, so a
  // partial response cannot leave the device with nobody to charge.
  if (g_sync.resident_count > 0) {
    resident_directory::replace_all(g_sync.residents, g_sync.resident_count);
  }
  product_catalog::replace_all(g_sync.products, g_sync.product_count, g_sync.revision);
  Serial.printf("[sync] applied revision %lu: %u products, %u residents\n",
                static_cast<unsigned long>(g_sync.revision),
                static_cast<unsigned>(g_sync.product_count),
                static_cast<unsigned>(g_sync.resident_count));
}

}  // namespace

void begin() {
  g_op = Op::None;
  g_posted = false;
  g_result = Result::Idle;
  g_last_sync = 0;
  if (!configured()) {
    Serial.println("[backend] not configured; purchases stay on the device");
    return;
  }
  request_sync();
}

bool configured() { return config::configured; }

bool request_sync() {
  // A sync never preempts a purchase.
  if (!configured() || g_op != Op::None) return false;
  const size_t len =
      api_protocol::build_sync(g_request, sizeof(g_request), config::device_token,
                               settings::device_id, product_catalog::revision());
  return start(Op::Sync, len, millis());
}

bool submit_purchase(const char* transaction_id, const char* barcode,
                     const char* product_name, int32_t price_rappen, bool free_item,
                     const char* resident_id) {
  if (!configured() || g_op != Op::None) return false;
  const size_t len = api_protocol::build_record_purchase(
      g_request, sizeof(g_request), config::device_token, settings::device_id,
      transaction_id, barcode, product_name, price_rappen, free_item, resident_id);
  return start(Op::Purchase, len, millis());
}

bool void_purchase(const char* transaction_id) {
  if (!configured() || g_op != Op::None) return false;
  const size_t len = api_protocol::build_void_purchase(
      g_request, sizeof(g_request), config::device_token, settings::device_id,
      transaction_id);
  return start(Op::Void, len, millis());
}

void update(uint32_t now_ms) {
  if (!configured()) return;

  if (g_op == Op::None) {
    // Periodic refresh. Milestone 10 replaces this timer with a sync on wake.
    if (static_cast<int32_t>(now_ms - (g_last_sync + settings::sync_interval_ms)) >= 0) {
      request_sync();
    }
    return;
  }

  // Waiting for the radio. This is the case that must not be mistaken for a
  // result: the request has not been sent yet.
  if (!g_posted) {
    if (wifi_manager::online()) {
      if (api_client::post(g_request, g_request_len)) {
        g_posted = true;
      } else if (api_client::error() != api_client::Error::Busy) {
        Serial.printf("[backend] cannot send: %s\n", api_client::error_text());
        const bool was_sync = g_op == Op::Sync;
        finish(Result::Failure, api_client::error_text());
        if (was_sync) g_last_sync = now_ms;
      }
    } else if (static_cast<int32_t>(now_ms - g_deadline) >= 0) {
      Serial.println("[backend] gave up waiting for the radio");
      const bool was_sync = g_op == Op::Sync;
      finish(Result::Failure, "Kein WLAN");
      if (was_sync) g_last_sync = now_ms;
    }
    return;
  }

  const api_client::Status s = api_client::status();
  if (s == api_client::Status::Busy) return;

  if (s == api_client::Status::Failed) {
    Serial.printf("[backend] request failed: %s\n", api_client::error_text());
    const bool was_sync = g_op == Op::Sync;
    finish(Result::Failure, api_client::error_text());
    if (was_sync) g_last_sync = now_ms;
    return;
  }

  if (s != api_client::Status::Done) return;

  if (g_op == Op::Sync) {
    apply_sync();
    g_last_sync = now_ms;
    finish(Result::Idle, "");
    return;
  }

  bool duplicate = false;
  char err[64];
  const auto outcome = api_protocol::parse_ack(api_client::response(),
                                               api_client::response_len(), &duplicate,
                                               err, sizeof(err));
  if (outcome == api_protocol::Outcome::Ok) {
    g_duplicate = duplicate;
    if (duplicate) Serial.println("[backend] already recorded; treating as success");
    finish(Result::Success, "");
  } else {
    Serial.printf("[backend] unreadable answer (%u bytes): %.160s\n",
                  static_cast<unsigned>(api_client::response_len()),
                  api_client::response());
    finish(Result::Failure, err[0] ? err : "Antwort unlesbar");
  }
}

Result result() { return g_result; }
const char* error() { return g_error; }
bool duplicate() { return g_duplicate; }
void clear() {
  g_result = Result::Idle;
  g_error[0] = '\0';
  g_duplicate = false;
}

uint32_t last_sync_ms() { return g_last_sync; }
bool sync_in_flight() { return g_op == Op::Sync; }

}  // namespace backend
