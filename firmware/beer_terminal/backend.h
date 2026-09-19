#pragma once
#include <stdint.h>
#include "api_protocol.h"

// Sits between the state machine and the network. Owns the request lifecycle,
// applies a sync to the catalog and resident list, and keeps the state machine
// free of HTTP. Nothing here blocks.
//
// Every start_* call returns false when the request could not even be attempted
// — unconfigured, offline, or one already in flight. The state machine then
// falls back to its simulated path, so a device without secrets.h still
// demonstrates the whole flow.
namespace backend {

enum class Result : uint8_t {
  Idle,
  Pending,
  Success,
  // Never reached the backend. The transaction stays queued and will go out on
  // its own, so the user is told it is booked, not that it failed.
  Unreachable,
  // The backend answered and refused — an unknown person, an invalid barcode.
  // Retrying unchanged would fail identically, so the entry is dropped and the
  // user sees the reason.
  Rejected,
};

void begin();

// Pumps the client, applies finished syncs, and starts a periodic sync when the
// catalog is stale. Call from the main loop.
void update(uint32_t now_ms);

// True when an endpoint and token are compiled in and https.
bool configured();

// Tries to send the head of the transaction queue now. Returns false when it
// could not be started; the entry stays queued either way, so nothing is lost.
bool send_queued_now();

// Asks for a catalog and resident refresh. Results are applied by update().
bool request_sync();

// Whether somebody is actually using the terminal. While inactive the periodic
// refresh is suspended, so the radio is not woken every ten minutes to fetch
// prices nobody is looking at. Queued transactions still go out: a purchase
// must reach the backend whether or not the screen is on.
void set_active(bool active);

// Fetches one resident's own consumption for the overview screen. Never
// preempts a purchase; the queue is drained first.
enum class MySummaryState : uint8_t { Idle, Loading, Ready, Unavailable };
bool request_my_summary(const char* resident_id);
MySummaryState my_summary_state();
const api_protocol::MySummary& my_summary();

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
bool busy();

}  // namespace backend
