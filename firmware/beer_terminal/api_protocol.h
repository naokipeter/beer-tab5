#pragma once
#include <stddef.h>
#include <stdint.h>
#include "product_catalog.h"
#include "purchase_log.h"
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

// {"action":"createProduct","barcode":..,"name":..,"price_rappen":..,"free":bool}
// A beer added at the fridge. Idempotent server-side: a retry updates the row it
// already created rather than appending a second one.
size_t build_create_product(char* out, size_t capacity, const char* token,
                            const char* device, const char* barcode,
                            const char* product_name, int32_t price_rappen,
                            bool free_item);

// {"action":"changePrice","barcode":..,"name":..,"price_rappen":..,"free":bool}
size_t build_change_price(char* out, size_t capacity, const char* token,
                          const char* device, const char* barcode,
                          const char* product_name, int32_t price_rappen,
                          bool free_item);

// {"action":"setActive","barcode":..,"name":..,"active":bool}
// Shelves or archives a product. Identified by barcode when it has one, by name
// otherwise, which is how a product added at the terminal is found.
size_t build_set_active(char* out, size_t capacity, const char* token,
                        const char* device, const char* barcode,
                        const char* product_name, bool active);

// {"action":"mySummary","resident_id":..}
// Response: {ok, resident_name, drinks, total_rappen, products:[{name, drinks,
// total_rappen}]}. One person's own consumption, per product.
size_t build_my_summary(char* out, size_t capacity, const char* token,
                        const char* device, const char* resident_id);

// True only for an https URL on openfoodfacts.org. Product photo URLs come from
// the backend, which validates them too, but the device decides for itself what
// it will connect to rather than trusting a value it was handed.
bool image_source_allowed(const char* url);

struct SyncResult {
  uint32_t revision;
  bool changed;
  uint8_t product_count;
  uint8_t resident_count;
  uint8_t summary_count;
  // Whether the response carried a summary at all. An absent summary is not an
  // empty one: a backend that predates this field must not be read as "nobody
  // has drunk anything", which would wipe the stored history.
  bool has_summary;
  product_catalog::Product products[settings::max_products];
  resident_directory::Entry residents[settings::max_residents];
  // Acknowledged purchases per resident. Sent on every sync, unlike the catalog,
  // because it changes with every purchase while the revision does not.
  purchase_log::Tally summary[settings::max_residents];
};

enum class Outcome : uint8_t {
  Ok,
  Malformed,     // not JSON, or missing the fields the action requires
  Rejected,      // ok:false, with a message in `error`
  TooManyItems,  // more products or residents than this build can hold
};

// What the overview screen shows. Bounded so it fits a static buffer.
inline constexpr uint8_t kMaxSummaryProducts = 12;

struct MySummary {
  char resident_name[24];
  uint16_t drinks;
  int32_t total_rappen;
  uint8_t product_count;
  struct Row {
    char name[40];
    uint16_t drinks;
    int32_t total_rappen;
  } products[kMaxSummaryProducts];
};

Outcome parse_my_summary(const char* body, size_t len, MySummary* out, char* error,
                         size_t error_capacity);

// True only for an https URL on one of Google's own hosts. A redirect target
// arrives over the network and decides where the next request goes, so it is a
// trust decision, not a formatting one — which is why it lives here, beside the
// rest of the contract, and is tested on the host.
bool redirect_target_allowed(const char* url);

// `error` receives a bounded, sanitised copy of any server message.
Outcome parse_sync(const char* body, size_t len, SyncResult* out, char* error,
                   size_t error_capacity);

// For recordPurchase and voidPurchase. `duplicate` is set for recordPurchase.
Outcome parse_ack(const char* body, size_t len, bool* duplicate, char* error,
                  size_t error_capacity);

}  // namespace api_protocol
