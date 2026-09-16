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
  Submitting,
  Success,
  Error,
  Admin,
};

enum class Event : uint8_t {
  Wake,
  ProductSelected,   // a catalog tile was tapped
  NotListed,         // the "Nicht gelistet" footer button
  NewProductReady,   // name and price entered for an ad hoc product
  ResidentSelected,
  SubmitSucceeded,
  SubmitFailed,
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
  char transaction_id[24];
  char message[64];       // error detail shown in the UI
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

// Drives timed transitions (mock submit latency, success dwell). Non-blocking.
void update(uint32_t now_ms);

// Loads the context from a catalog entry before dispatching ProductSelected.
void select_product(uint8_t catalog_index);
// Loads the context for an ad hoc product before dispatching NewProductReady.
void set_ad_hoc_product(const char* name, int32_t price_rappen, bool free_item);

}  // namespace app_state
