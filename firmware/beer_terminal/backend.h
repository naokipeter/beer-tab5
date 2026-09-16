#pragma once
#include <stdint.h>

// Sits between the state machine and the network. Owns the request lifecycle,
// applies a sync to the catalog and resident list, and keeps the state machine
// free of HTTP. Nothing here blocks.
//
// Every start_* call returns false when the request could not even be attempted
// — unconfigured, offline, or one already in flight. The state machine then
// falls back to its simulated path, so a device without secrets.h still
// demonstrates the whole flow.
namespace backend {

enum class Result : uint8_t { Idle, Pending, Success, Failure };

void begin();

// Pumps the client, applies finished syncs, and starts a periodic sync when the
// catalog is stale. Call from the main loop.
void update(uint32_t now_ms);

// True when an endpoint and token are compiled in and https.
bool configured();

bool submit_purchase(const char* transaction_id, const char* barcode,
                     const char* product_name, int32_t price_rappen, bool free_item,
                     const char* resident_id);
bool void_purchase(const char* transaction_id);

// Asks for a catalog and resident refresh. Results are applied by update().
bool request_sync();

// State of the caller-visible operation, i.e. a purchase or a void. A background
// sync never disturbs this.
Result result();
const char* error();
// True when the backend already held this transaction id. That is a success:
// the purchase is recorded exactly once.
bool duplicate();
void clear();

// For the admin screen.
uint32_t last_sync_ms();
bool sync_in_flight();

}  // namespace backend
