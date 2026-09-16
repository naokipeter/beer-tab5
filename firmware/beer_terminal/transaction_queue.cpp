#include "transaction_queue.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "catalog_codec.h"
#include "device_storage.h"

namespace transaction_queue {
namespace {

constexpr const char* kPath = "/queue.bin";

// A plain array rather than a ring: the queue is short, drained from the front,
// and being able to encode it in order keeps the on-disk format trivial to
// reason about after a crash.
Entry g_items[settings::max_queued_transactions];
uint8_t g_count = 0;
bool g_dirty = false;

uint8_t g_blob[catalog_codec::kMaxQueueBytes];

}  // namespace

void begin() {
  g_count = 0;
  g_dirty = false;

  size_t len = 0;
  if (device_storage::read(kPath, g_blob, sizeof(g_blob), &len)) {
    uint8_t count = 0;
    if (catalog_codec::decode_queue(g_blob, len, g_items,
                                    settings::max_queued_transactions, &count)) {
      g_count = count;
      if (g_count) {
        Serial.printf("[queue] %u transaction(s) still to send\n",
                      static_cast<unsigned>(g_count));
      }
      return;
    }
    // A damaged queue is the one file where losing data costs money, so say so
    // loudly rather than letting it disappear quietly.
    Serial.println("[queue] stored queue is unreadable; pending transactions lost");
    g_dirty = true;
  }
}

uint8_t count() { return g_count; }
bool empty() { return g_count == 0; }
bool full() { return g_count >= settings::max_queued_transactions; }

const Entry* head() { return g_count ? &g_items[0] : nullptr; }

const Entry* at(uint8_t index) { return index < g_count ? &g_items[index] : nullptr; }

bool push(const Entry& entry) {
  if (full()) return false;
  g_items[g_count++] = entry;
  g_dirty = true;
  return true;
}

void pop() {
  if (g_count == 0) return;
  for (uint8_t i = 1; i < g_count; ++i) g_items[i - 1] = g_items[i];
  --g_count;
  g_dirty = true;
}

bool contains(const char* transaction_id) {
  if (!transaction_id) return false;
  for (uint8_t i = 0; i < g_count; ++i) {
    if (strcmp(g_items[i].transaction_id, transaction_id) == 0) return true;
  }
  return false;
}

bool remove_purchase(const char* transaction_id) {
  if (!transaction_id) return false;
  for (uint8_t i = 0; i < g_count; ++i) {
    if (g_items[i].kind != Kind::Purchase) continue;
    if (strcmp(g_items[i].transaction_id, transaction_id) != 0) continue;
    for (uint8_t j = i + 1; j < g_count; ++j) g_items[j - 1] = g_items[j];
    --g_count;
    g_dirty = true;
    return true;
  }
  return false;
}

bool dirty() { return g_dirty; }

void flush() {
  if (!g_dirty) return;
  const size_t len =
      catalog_codec::encode_queue(g_items, g_count, g_blob, sizeof(g_blob));
  if (len == 0 || !device_storage::write(kPath, g_blob, len)) {
    Serial.println("[queue] write failed; will retry");
    return;
  }
  g_dirty = false;
}

}  // namespace transaction_queue
