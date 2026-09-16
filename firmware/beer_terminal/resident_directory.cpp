#include "resident_directory.h"
#include <stdio.h>
#include <string.h>

namespace resident_directory {
namespace {

Entry g_entries[settings::max_residents];
uint8_t g_count = 0;
bool g_from_backend = false;

bool usable(const Entry& e) { return e.id[0] != '\0' && e.name[0] != '\0'; }

}  // namespace

void begin() {
  g_count = 0;
  g_from_backend = false;
  for (uint8_t i = 0; i < settings::default_resident_count &&
                      i < settings::max_residents;
       ++i) {
    Entry& e = g_entries[g_count++];
    snprintf(e.id, sizeof(e.id), "%s", settings::default_residents[i].id);
    snprintf(e.name, sizeof(e.name), "%s", settings::default_residents[i].name);
  }
}

uint8_t count() { return g_count; }

const Entry* at(uint8_t index) {
  return index < g_count ? &g_entries[index] : nullptr;
}

bool replace_all(const Entry* entries, uint8_t n) {
  if (!entries || n == 0 || n > settings::max_residents) return false;
  for (uint8_t i = 0; i < n; ++i) {
    if (!usable(entries[i])) return false;
  }
  for (uint8_t i = 0; i < n; ++i) {
    snprintf(g_entries[i].id, sizeof(g_entries[i].id), "%s", entries[i].id);
    snprintf(g_entries[i].name, sizeof(g_entries[i].name), "%s", entries[i].name);
  }
  g_count = n;
  g_from_backend = true;
  return true;
}

bool from_backend() { return g_from_backend; }

}  // namespace resident_directory
