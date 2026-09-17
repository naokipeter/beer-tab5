#pragma once
#include <stdint.h>
#include "settings.h"

// Explicit state machine. Every transition is listed in app_state.cpp; the UI
// only dispatches events and redraws on change, it never sets a state directly.
namespace app_state {

enum class State : uint8_t {
  Sleeping,
  Waking,
  SelectingProduct,
  LookingUp,
  ProductFound,
  NewProduct,
  SelectingUser,
  SelectingArchived,  // restore an archived beer before offering the new form
  ConfirmArchive,     // confirm removing a beer from the fridge grid
  Submitting,
  Undoing,            // reversing the purchase just recorded
  Success,
  Summary,            // consumption table, reached from the confirmation screen
  Error,
  Admin,
};

enum class Event : uint8_t {
  Wake,
  ProductSelected,   // a catalog tile was tapped
  NotListed,         // the "Nicht gelistet" footer button
  CreateNewProduct,  // skip past the archived list to the new-product form
  ProductRestored,
  NewProductReady,   // name and price entered for an ad hoc product
  ResidentSelected,
  ArchiveRequested,  // the archive button on the resident screen
  ArchiveConfirmed,
  ShowSummary,
  SubmitSucceeded,
  SubmitFailed,
  Undo,
  UndoSucceeded,
  UndoFailed,
  Retry,
  Cancel,
  Dwell,             // a timed screen finished
  AdminRequested,
  AdminDone,
  Sleep,
};

// The in-flight purchase. Sized fixed; nothing here allocates.
struct Context {
  int8_t product_index;   // catalog index, or -1 for an ad hoc product
  char product_name[40];
  char barcode[14];
  int32_t price_rappen;
  bool free_item;
  int8_t resident_index;
  int8_t archive_storage_index;  // product awaiting archive confirmation
  char transaction_id[24];
  char message[64];       // error detail shown in the UI
  bool undone;            // the confirmation is acknowledging a reversal
  // The purchase is stored and will be sent, but has not reached the backend
  // yet. The confirmation says so rather than claiming a clean booking.
  bool deferred;
  // Which operation ERROR should retry. Retrying must never turn a failed undo
  // back into a second submission.
  bool undo_in_flight;
};

using ChangeHandler = void (*)(State previous, State current);

void begin(ChangeHandler on_change);

State state();
const char* state_name(State s);
Context& context();

void dispatch(Event e);

// Makes the next simulated submission fail, so the ERROR path and its retry can
// be exercised on hardware. Cleared once it has fired.
void simulate_next_failure(bool on);
bool failure_simulated();

// Total dwell of the current state in milliseconds, or 0 when it is not timed.
// The UI's countdown bar reads this rather than repeating the constant, so the
// bar cannot disagree with the deadline it is showing.
uint32_t current_dwell_ms();

// Rebuilds the current screen without a transition, for data that arrives after
// the screen was drawn.
void redraw();

// Queues a shelve (active=true) or archive so the change reaches the backend.
void queue_product_change(int8_t storage_index, bool active);

// Queues a beer added at the fridge so the spreadsheet learns about it. Without
// this the product existed only on this terminal.
void queue_product_create(int8_t storage_index);

// Drives timed transitions (mock submit latency, success dwell). Non-blocking.
void update(uint32_t now_ms);

// Loads the context from an active catalog entry before dispatching ProductSelected.
void select_product(uint8_t active_index);
// Loads the context for an ad hoc product before dispatching NewProductReady.
void set_ad_hoc_product(const char* name, int32_t price_rappen, bool free_item);

}  // namespace app_state
