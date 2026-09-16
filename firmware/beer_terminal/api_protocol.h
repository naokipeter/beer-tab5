#pragma once
#include <stddef.h>
#include <stdint.h>
#include "product_catalog.h"
#include "resident_directory.h"

// The wire format between the device and the Apps Script backend. Pure: it
// builds request bodies and interprets response bodies, but performs no I/O, so
// the whole contract is exercised on the host. api_client does the HTTPS.
//
// Requests are JSON objects posted to a single endpoint, distinguished by
// "action". Every response is a JSON object with a boolean "ok"; on failure it
// carries "error". Milestone 8 implements the server side of exactly this.
namespace api_protocol {

// Bounds chosen so callers can use static buffers.
inline constexpr size_t kMaxRequestBytes = 640;
// A sync response carries the whole catalog and resident list.
inline constexpr size_t kMaxResponseBytes = 8192;

// {"action":"sync","token":..,"device":..,"since":<revision>}
// Response: {"ok":true,"revision":N,"changed":bool,
//            "products":[{barcode,name,price_rappen,free,image_url,active}],
//            "residents":[{id,name}]}
// Both lists are authoritative and replace what the device holds.
size_t build_sync(char* out, size_t capacity, const char* token, const char* device,
                  uint32_t since_revision);

// {"action":"recordPurchase",...,"transaction_id":..}
// Response: {"ok":true,"duplicate":bool}. `duplicate` true means the backend
// already had this transaction id, which is a success, not an error.
size_t build_record_purchase(char* out, size_t capacity, const char* token,
                             const char* device, const char* transaction_id,
                             const char* barcode, const char* product_name,
                             int32_t price_rappen, bool free_item,
                             const char* resident_id);

// {"action":"voidPurchase","transaction_id":..}. Reverses a recorded purchase.
size_t build_void_purchase(char* out, size_t capacity, const char* token,
                           const char* device, const char* transaction_id);

struct SyncResult {
  uint32_t revision;
  bool changed;
  uint8_t product_count;
  uint8_t resident_count;
  product_catalog::Product products[settings::max_products];
  resident_directory::Entry residents[settings::max_residents];
};

enum class Outcome : uint8_t {
  Ok,
  Malformed,     // not JSON, or missing the fields the action requires
  Rejected,      // ok:false, with a message in `error`
  TooManyItems,  // more products or residents than this build can hold
};

// `error` receives a bounded, sanitised copy of any server message.
Outcome parse_sync(const char* body, size_t len, SyncResult* out, char* error,
                   size_t error_capacity);

// For recordPurchase and voidPurchase. `duplicate` is set for recordPurchase.
Outcome parse_ack(const char* body, size_t len, bool* duplicate, char* error,
                  size_t error_capacity);

}  // namespace api_protocol
