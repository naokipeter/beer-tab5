#include "app_state.h"
#include <stdio.h>
#include <string.h>
#include <Arduino.h>
#include "product_catalog.h"
#include "purchase_log.h"
#include "resident_directory.h"

namespace app_state {
namespace {

State g_state = State::Sleeping;
Context g_ctx = {};
ChangeHandler g_on_change = nullptr;

// Deadline for the current timed state, or 0 when the state is not timed.
uint32_t g_deadline = 0;
// The span that deadline was set from, so the UI can draw a matching countdown.
uint32_t g_dwell_total = 0;
// Set when a submission is deliberately failed, so Retry can succeed instead.
bool g_fail_next_submit = false;

void enter(State next) {
  if (next == g_state) return;
  const State previous = g_state;
  g_state = next;
  g_deadline = 0;
  g_dwell_total = 0;

  switch (next) {
    case State::Submitting:
      g_dwell_total = settings::mock_submit_ms;
      break;
    case State::Undoing:
      g_dwell_total = settings::mock_submit_ms;
      break;
    case State::Success:
      // An acknowledged reversal needs no decision, so it clears faster than the
      // confirmation that still offers undo.
      g_dwell_total = g_ctx.undone ? settings::undo_dwell_ms
                                   : settings::success_dwell_ms;
      break;
    case State::Summary:
      // Long enough to read the table, but it still returns on its own so the
      // terminal never sits lit in front of the fridge.
      g_dwell_total = settings::summary_dwell_ms;
      break;
    default:
      break;
  }

  if (g_dwell_total > 0) g_deadline = millis() + g_dwell_total;

  Serial.printf("[state] %s -> %s\n", state_name(previous), state_name(next));
  if (g_on_change) g_on_change(previous, next);
}

void reset_context() {
  g_ctx = Context{};
  g_ctx.product_index = -1;
  g_ctx.resident_index = -1;
  g_ctx.archive_storage_index = -1;
  g_ctx.undone = false;
  g_ctx.undo_in_flight = false;
}

void make_transaction_id() {
  // Generated once per purchase attempt and reused across retries, so a
  // duplicate submission is recognisable by the backend in milestone 8.
  snprintf(g_ctx.transaction_id, sizeof(g_ctx.transaction_id), "%s-%08lx-%04x",
           settings::device_id, static_cast<unsigned long>(millis()),
           static_cast<unsigned>(esp_random() & 0xFFFF));
}

}  // namespace

void begin(ChangeHandler on_change) {
  g_on_change = on_change;
  reset_context();
  g_state = State::Sleeping;
  // Milestone 3 has no sleep implementation yet, so come straight up.
  enter(State::Waking);
  enter(State::SelectingProduct);
}

State state() { return g_state; }
uint32_t current_dwell_ms() { return g_dwell_total; }
Context& context() { return g_ctx; }

const char* state_name(State s) {
  switch (s) {
    case State::Sleeping:         return "SLEEPING";
    case State::Waking:           return "WAKING";
    case State::SelectingProduct: return "SELECTING_PRODUCT";
    case State::Undoing:          return "UNDOING";
    case State::SelectingArchived: return "SELECTING_ARCHIVED";
    case State::ConfirmArchive:   return "CONFIRM_ARCHIVE";
    case State::Summary:          return "SUMMARY";
    case State::LookingUp:        return "LOOKING_UP";
    case State::ProductFound:     return "PRODUCT_FOUND";
    case State::NewProduct:       return "NEW_PRODUCT";
    case State::SelectingUser:    return "SELECTING_USER";
    case State::Submitting:       return "SUBMITTING";
    case State::Success:          return "SUCCESS";
    case State::Error:            return "ERROR";
    case State::Admin:            return "ADMIN";
  }
  return "?";
}

void select_product(uint8_t active_index) {
  const product_catalog::Product* p = product_catalog::active_at(active_index);
  if (!p) return;
  g_ctx.product_index = product_catalog::storage_index_of_active(active_index);
  snprintf(g_ctx.product_name, sizeof(g_ctx.product_name), "%s", p->name);
  snprintf(g_ctx.barcode, sizeof(g_ctx.barcode), "%s", p->barcode);
  g_ctx.price_rappen = p->price_rappen;
  g_ctx.free_item = p->free_item;
}

void set_ad_hoc_product(const char* name, int32_t price_rappen, bool free_item) {
  g_ctx.product_index = -1;
  snprintf(g_ctx.product_name, sizeof(g_ctx.product_name), "%s", name);
  g_ctx.barcode[0] = '\0';
  g_ctx.price_rappen = price_rappen;
  g_ctx.free_item = free_item;
}

void simulate_next_failure(bool on) { g_fail_next_submit = on; }
bool failure_simulated() { return g_fail_next_submit; }

void dispatch(Event e) {
  switch (g_state) {
    case State::Sleeping:
      if (e == Event::Wake) enter(State::Waking);
      break;

    case State::Waking:
      enter(State::SelectingProduct);
      break;

    case State::SelectingProduct:
      if (e == Event::ProductSelected)      enter(State::SelectingUser);
      else if (e == Event::NotListed) {
        // Offer the archived beers first; only fall through to the form when
        // there is nothing to restore.
        enter(product_catalog::archived_count() > 0 ? State::SelectingArchived
                                                    : State::NewProduct);
      }
      else if (e == Event::AdminRequested)  enter(State::Admin);
      else if (e == Event::Sleep)           enter(State::Sleeping);
      break;

    case State::LookingUp:
      if (e == Event::ProductSelected)  enter(State::ProductFound);
      else if (e == Event::SubmitFailed) enter(State::Error);
      else if (e == Event::Cancel)      enter(State::SelectingProduct);
      break;

    case State::ProductFound:
      if (e == Event::ProductSelected) enter(State::SelectingUser);
      else if (e == Event::Cancel)     enter(State::SelectingProduct);
      break;

    case State::SelectingArchived:
      // Restoring is offered before the form because a returning beer is far
      // more common than a genuinely new one.
      if (e == Event::ProductRestored)      enter(State::SelectingProduct);
      else if (e == Event::CreateNewProduct) enter(State::NewProduct);
      else if (e == Event::Cancel)          { reset_context(); enter(State::SelectingProduct); }
      break;

    case State::NewProduct:
      if (e == Event::NewProductReady) enter(State::SelectingUser);
      else if (e == Event::Cancel)     { reset_context(); enter(State::SelectingProduct); }
      break;

    case State::SelectingUser:
      if (e == Event::ResidentSelected) { make_transaction_id(); enter(State::Submitting); }
      else if (e == Event::ArchiveRequested) {
        g_ctx.archive_storage_index = g_ctx.product_index;
        enter(State::ConfirmArchive);
      } else if (e == Event::Cancel)    { reset_context(); enter(State::SelectingProduct); }
      break;

    case State::ConfirmArchive:
      if (e == Event::ArchiveConfirmed) {
        product_catalog::archive(g_ctx.archive_storage_index);
        reset_context();
        enter(State::SelectingProduct);
      } else if (e == Event::Cancel) {
        g_ctx.archive_storage_index = -1;
        enter(State::SelectingUser);
      }
      break;

    case State::Submitting:
      if (e == Event::SubmitSucceeded) enter(State::Success);
      else if (e == Event::SubmitFailed) enter(State::Error);
      break;

    case State::Undoing:
      if (e == Event::UndoSucceeded) enter(State::Success);
      else if (e == Event::UndoFailed) enter(State::Error);
      break;

    case State::Success:
      if (e == Event::Undo && !g_ctx.undone) {
        g_ctx.undo_in_flight = true;
        enter(State::Undoing);
      } else if (e == Event::ShowSummary) enter(State::Summary);
      else if (e == Event::Dwell || e == Event::Cancel) {
        reset_context();
        enter(State::SelectingProduct);
      }
      break;

    case State::Summary:
      if (e == Event::Dwell || e == Event::Cancel) {
        reset_context();
        enter(State::SelectingProduct);
      }
      break;

    case State::Error:
      // The transaction ID survives a retry, so a submission that actually
      // landed before the error cannot be recorded twice. A failed undo retries
      // the undo, never the submission that preceded it.
      if (e == Event::Retry)       enter(g_ctx.undo_in_flight ? State::Undoing
                                                              : State::Submitting);
      else if (e == Event::Cancel) { reset_context(); enter(State::SelectingProduct); }
      break;

    case State::Admin:
      if (e == Event::AdminDone || e == Event::Cancel) enter(State::SelectingProduct);
      break;
  }
}

void update(uint32_t now_ms) {
  if (g_deadline == 0 || static_cast<int32_t>(now_ms - g_deadline) < 0) return;
  g_deadline = 0;

  switch (g_state) {
    case State::Submitting:
      if (g_fail_next_submit) {
        g_fail_next_submit = false;
        snprintf(g_ctx.message, sizeof(g_ctx.message), "Keine Verbindung zum Server");
        dispatch(Event::SubmitFailed);
      } else {
        const resident_directory::Entry* r =
            g_ctx.resident_index >= 0
                ? resident_directory::at(static_cast<uint8_t>(g_ctx.resident_index))
                : nullptr;
        if (r) {
          purchase_log::record(r->id, r->name, g_ctx.price_rappen, g_ctx.free_item);
        }
        dispatch(Event::SubmitSucceeded);
      }
      break;
    case State::Undoing: {
      // From milestone 8 this is a void of transaction_id on the backend, which
      // is why the ID is kept rather than regenerated: the server matches the
      // reversal to the original row.
      const resident_directory::Entry* r =
          g_ctx.resident_index >= 0
              ? resident_directory::at(static_cast<uint8_t>(g_ctx.resident_index))
              : nullptr;
      if (r) {
        purchase_log::unrecord(r->id, g_ctx.price_rappen, g_ctx.free_item);
      }
      Serial.printf("[undo] transaction=%s\n", g_ctx.transaction_id);
      g_ctx.undo_in_flight = false;
      g_ctx.undone = true;
      dispatch(Event::UndoSucceeded);
      break;
    }
    case State::Success:
    case State::Summary:
      dispatch(Event::Dwell);
      break;
    default:
      break;
  }
}

}  // namespace app_state
