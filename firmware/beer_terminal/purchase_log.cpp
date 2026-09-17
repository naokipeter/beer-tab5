#include "purchase_log.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "catalog_codec.h"
#include "device_storage.h"
#include "transaction_queue.h"

namespace purchase_log {
namespace {

constexpr const char* kPath = "/summary.bin";

// What the backend last told us.
Tally g_baseline[settings::max_residents];
uint8_t g_baseline_count = 0;
bool g_has_baseline = false;
bool g_dirty = false;

// Baseline plus the queue, rebuilt whenever the view is asked for.
Tally g_view[settings::max_residents];
uint8_t g_view_count = 0;

uint8_t g_blob[catalog_codec::kMaxSummaryBytes];

Tally* find_or_add(const char* id, const char* name) {
  for (uint8_t i = 0; i < g_view_count; ++i) {
    if (strcmp(g_view[i].resident_id, id) == 0) return &g_view[i];
  }
  if (g_view_count >= settings::max_residents) return nullptr;
  Tally& t = g_view[g_view_count++];
  t = Tally{};
  snprintf(t.resident_id, sizeof(t.resident_id), "%s", id);
  snprintf(t.resident_name, sizeof(t.resident_name), "%s", name ? name : id);
  return &t;
}

void rebuild() {
  g_view_count = 0;
  for (uint8_t i = 0; i < g_baseline_count && g_view_count < settings::max_residents;
       ++i) {
    g_view[g_view_count++] = g_baseline[i];
  }
  // Anything the backend has not acknowledged yet is still owed, so add it.
  for (uint8_t i = 0; i < transaction_queue::count(); ++i) {
    const transaction_queue::Entry* e = transaction_queue::at(i);
    if (!e || e->kind != transaction_queue::Kind::Purchase) continue;
    if (e->resident_id[0] == '\0') continue;
    Tally* t = find_or_add(e->resident_id, e->resident_name);
    if (!t) continue;
    ++t->drinks;
    if (!e->free_item) t->total_rappen += e->price_rappen;
  }
}

}  // namespace

void begin() {
  g_baseline_count = 0;
  g_has_baseline = false;
  g_dirty = false;

  size_t len = 0;
  if (device_storage::read(kPath, g_blob, sizeof(g_blob), &len)) {
    uint8_t count = 0;
    if (catalog_codec::decode_summary(g_blob, len, g_baseline,
                                      settings::max_residents, &count)) {
      g_baseline_count = count;
      g_has_baseline = true;
      Serial.printf("[summary] restored %u resident(s) from storage\n",
                    static_cast<unsigned>(count));
    } else {
      Serial.println("[summary] stored summary rejected; waiting for a sync");
    }
  }
  rebuild();
}

void set_baseline(const Tally* entries, uint8_t n) {
  if (!entries || n > settings::max_residents) return;
  for (uint8_t i = 0; i < n; ++i) g_baseline[i] = entries[i];
  g_baseline_count = n;
  g_has_baseline = true;
  g_dirty = true;
  rebuild();
}

bool has_baseline() { return g_has_baseline; }

uint8_t count() {
  rebuild();
  return g_view_count;
}

const Tally* at(uint8_t index) {
  return index < g_view_count ? &g_view[index] : nullptr;
}

uint16_t total_drinks() {
  rebuild();
  uint16_t n = 0;
  for (uint8_t i = 0; i < g_view_count; ++i)
    n = static_cast<uint16_t>(n + g_view[i].drinks);
  return n;
}

int32_t total_rappen() {
  rebuild();
  int32_t n = 0;
  for (uint8_t i = 0; i < g_view_count; ++i) n += g_view[i].total_rappen;
  return n;
}

void flush() {
  if (!g_dirty) return;
  const size_t len = catalog_codec::encode_summary(g_baseline, g_baseline_count,
                                                   g_blob, sizeof(g_blob));
  if (len == 0 || !device_storage::write(kPath, g_blob, len)) {
    Serial.println("[summary] write failed; will retry");
    return;
  }
  g_dirty = false;
}

}  // namespace purchase_log
