#pragma once
#include <stddef.h>
#include <stdint.h>
#include "settings.h"

// Purchases and reversals that still have to reach the backend.
//
// A transaction is written here and flushed to flash *before* anything is sent,
// so a purchase survives a power cut, a dead router or a backend outage. Entries
// keep their transaction id across every retry, which is what lets the backend
// recognise a replay and record the drink exactly once.
namespace transaction_queue {

enum class Kind : uint8_t { Purchase = 0, Void = 1 };

struct Entry {
  char transaction_id[24];
  char barcode[14];
  char product_name[40];
  char resident_id[12];
  char resident_name[24];
  int32_t price_rappen;
  bool free_item;
  Kind kind;
};

void begin();

uint8_t count();
bool empty();
bool full();

// Oldest entry still waiting, or nullptr.
const Entry* head();

// Indexed access, oldest first. The summary adds queued purchases to the
// backend's figures.
const Entry* at(uint8_t index);

// Appends. Returns false when the queue is full: the caller must tell the user
// rather than silently dropping either this purchase or an older one.
bool push(const Entry& entry);

// Removes the head after the backend has acknowledged it.
void pop();

// Drops a queued purchase by transaction id, for undoing one that never left the
// device. Returns true if it was found, in which case no reversal needs sending
// because nothing was ever recorded.
bool remove_purchase(const char* transaction_id);

// True if this id is still waiting to be sent.
bool contains(const char* transaction_id);

// Writes the queue if it changed. Driven from the main loop, like the catalog.
void flush();
bool dirty();

}  // namespace transaction_queue
