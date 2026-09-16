#include "resident_directory.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "catalog_codec.h"
#include "device_storage.h"

namespace resident_directory {
namespace {

Entry g_entries[settings::max_residents];
uint8_t g_count = 0;
bool g_from_backend = false;
bool g_dirty = false;

constexpr const char* kPath = "/residents.bin";
uint8_t g_blob[catalog_codec::kMaxResidentsBytes];

void seed() {
  g_count = 0;
  for (uint8_t i = 0; i < settings::default_resident_count &&
                      i < settings::max_residents;
       ++i) {
    Entry& e = g_entries[g_count++];
    snprintf(e.id, sizeof(e.id), "%s", settings::default_residents[i].id);
    snprintf(e.name, sizeof(e.name), "%s", settings::default_residents[i].name);
  }
}

bool usable(const Entry& e) { return e.id[0] != '\0' && e.name[0] != '\0'; }

}  // namespace

void begin() {
  g_from_backend = false;
  g_dirty = false;

  size_t len = 0;
  if (device_storage::read(kPath, g_blob, sizeof(g_blob), &len)) {
    uint8_t count = 0;
    if (catalog_codec::decode_residents(g_blob, len, g_entries,
                                        settings::max_residents, &count) &&
        count > 0) {
      g_count = count;
      // A stored list only ever comes from a successful sync, so the admin screen
      // is right to report it as the backend's rather than the local fallback.
      g_from_backend = true;
      Serial.printf("[residents] loaded %u from storage\n",
                    static_cast<unsigned>(g_count));
      return;
    }
    Serial.println("[residents] stored list rejected; using defaults");
  }
  seed();
  Serial.printf("[residents] using %u compiled-in defaults\n",
                static_cast<unsigned>(g_count));
}

void flush() {
  if (!g_dirty) return;
  const size_t len =
      catalog_codec::encode_residents(g_entries, g_count, g_blob, sizeof(g_blob));
  if (len == 0 || !device_storage::write(kPath, g_blob, len)) {
    Serial.println("[residents] write failed; will retry");
    return;
  }
  g_dirty = false;
  Serial.printf("[residents] saved %u\n", static_cast<unsigned>(g_count));
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
  g_dirty = true;
  return true;
}

bool from_backend() { return g_from_backend; }

}  // namespace resident_directory
