#pragma once
#include <stdint.h>
#include "settings.h"

// Running consumption tally behind the summary screen. Milestone 3 accumulates
// it locally on each successful submission; from milestone 8 the authoritative
// figures come from the Purchases sheet and this becomes the offline view of
// what this device has recorded.
namespace purchase_log {

struct Tally {
  char resident_id[12];
  char resident_name[24];
  uint16_t drinks;
  int32_t total_rappen;  // Free items count as a drink but add nothing.
};

void begin();

// Adds one drink for a resident. Unknown residents are appended while there is
// room, so a purchase is never silently dropped from the summary.
void record(const char* resident_id, const char* resident_name, int32_t rappen,
            bool free_item);

// Reverses one recorded drink. Returns false when the resident has no drinks
// left to reverse, so a repeated undo cannot drive a tally negative.
bool unrecord(const char* resident_id, int32_t rappen, bool free_item);

uint8_t count();
const Tally* at(uint8_t index);

uint16_t total_drinks();
int32_t total_rappen();

}  // namespace purchase_log
