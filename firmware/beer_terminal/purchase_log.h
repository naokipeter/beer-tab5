#pragma once
#include <stdint.h>
#include "settings.h"

// The consumption summary.
//
// The figures come from the backend, which is the only place that knows about
// purchases made before this device booted, and are persisted so a restart shows
// them even before the first sync completes.
//
// Transactions still waiting in the local queue are added on top, because the
// backend cannot know about them yet. That keeps the total right without ever
// double counting: an entry is either acknowledged by the server, and therefore
// in its figures, or still in the queue, and therefore added here.
namespace purchase_log {

struct Tally {
  char resident_id[12];
  char resident_name[24];
  uint16_t drinks;
  int32_t total_rappen;  // Free items count as a drink but add nothing.
};

void begin();

// Replaces the baseline with the backend's figures and persists it.
void set_baseline(const Tally* entries, uint8_t n);
bool has_baseline();

// Baseline plus everything still queued. This is what the summary screen shows.
// Recomputed on demand; the queue is short and this runs once per screen build.
uint8_t count();
const Tally* at(uint8_t index);

uint16_t total_drinks();
int32_t total_rappen();

void flush();

}  // namespace purchase_log
