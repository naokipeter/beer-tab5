#pragma once
#include <stdint.h>
#include "settings.h"

// The resident list. Seeded from settings::default_residents so the device is
// usable before it has ever reached the backend, then replaced wholesale by the
// Residents sheet once milestone 7 can fetch it. Names are never hard-coded into
// the UI; every screen reads them from here.
namespace resident_directory {

struct Entry {
  char id[12];
  char name[24];
};

// Loads the persisted list, falling back to settings::default_residents when
// nothing is stored or the stored blob is unusable.
void begin();

// Writes the list if it changed. Driven from the main loop, like the catalog.
void flush();

uint8_t count();
const Entry* at(uint8_t index);

// Replaces the whole list and marks it for persisting. Returns false and keeps
// the previous list if `n` exceeds settings::max_residents or any entry is
// unusable, so a malformed backend response cannot leave the device with no way
// to record a purchase.
bool replace_all(const Entry* entries, uint8_t n);

// True once replace_all has succeeded at least once, i.e. the list on screen
// came from the backend rather than the compiled-in fallback.
bool from_backend();

}  // namespace resident_directory
