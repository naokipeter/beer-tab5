#include "purchase_log.h"
#include <stdio.h>
#include <string.h>

namespace purchase_log {
namespace {

Tally g_tallies[settings::max_residents];
uint8_t g_count = 0;

}  // namespace

void begin() {
  g_count = 0;
  for (uint8_t i = 0; i < settings::max_residents; ++i) g_tallies[i] = Tally{};
}

void record(const char* resident_id, const char* resident_name, int32_t rappen,
            bool free_item) {
  if (!resident_id || resident_id[0] == '\0') return;
  for (uint8_t i = 0; i < g_count; ++i) {
    if (strcmp(g_tallies[i].resident_id, resident_id) == 0) {
      ++g_tallies[i].drinks;
      if (!free_item) g_tallies[i].total_rappen += rappen;
      return;
    }
  }
  if (g_count >= settings::max_residents) return;
  Tally& t = g_tallies[g_count++];
  snprintf(t.resident_id, sizeof(t.resident_id), "%s", resident_id);
  snprintf(t.resident_name, sizeof(t.resident_name), "%s",
           resident_name ? resident_name : resident_id);
  t.drinks = 1;
  t.total_rappen = free_item ? 0 : rappen;
}

uint8_t count() { return g_count; }

const Tally* at(uint8_t index) {
  return index < g_count ? &g_tallies[index] : nullptr;
}

uint16_t total_drinks() {
  uint16_t n = 0;
  for (uint8_t i = 0; i < g_count; ++i) n = static_cast<uint16_t>(n + g_tallies[i].drinks);
  return n;
}

int32_t total_rappen() {
  int32_t n = 0;
  for (uint8_t i = 0; i < g_count; ++i) n += g_tallies[i].total_rappen;
  return n;
}

}  // namespace purchase_log
