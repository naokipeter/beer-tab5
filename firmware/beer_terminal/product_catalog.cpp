#include "product_catalog.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "catalog_codec.h"
#include "device_storage.h"

namespace product_catalog {
namespace {

Product g_items[settings::max_products];
uint8_t g_count = 0;
uint32_t g_revision = 0;
bool g_dirty = false;

constexpr const char* kPath = "/catalog.bin";

// Static rather than on the stack: the blob is several kilobytes and the loop
// task's stack is not the place for it.
uint8_t g_blob[catalog_codec::kMaxCatalogBytes];

void add(const char* barcode, const char* name, int32_t rappen, bool free_item,
         const char* image_url, bool active);

void seed() {
  g_count = 0;
  // Mock catalog for the prototype. Barcodes are plausible but not authoritative;
  // real values arrive with the backend catalog in milestone 7. Two active
  // products match the usual stock; the rest start archived so the restore flow
  // has something to show.
  add("7610807000019", "Feldschlösschen Original", 180, false, "", true);
  add("7610827000024", "Appenzeller Quöllfrisch", 220, false, "", true);
  add("7610900000038", "Calanda Bräu", 190, false, "", false);
  add("7613100000045", "Chopfab Draft", 260, false, "", false);
  add("7610827000055", "Appenzeller Naturperle", 240, false, "", false);
  add("7610013000062", "Boxer Old", 170, false, "", false);
  add("7610700000079", "Valaisanne Pale Ale", 280, false, "", false);
  add("7613300000086", "Turbinenbräu Gassenhauer", 0, true, "", false);
}

void add(const char* barcode, const char* name, int32_t rappen, bool free_item,
         const char* image_url, bool active) {
  if (g_count >= settings::max_products) return;
  Product& p = g_items[g_count++];
  snprintf(p.barcode, sizeof(p.barcode), "%s", barcode);
  snprintf(p.name, sizeof(p.name), "%s", name);
  p.price_rappen = rappen;
  p.free_item = free_item;
  snprintf(p.image_url, sizeof(p.image_url), "%s", image_url);
  p.active = active;
}

int8_t nth_matching(uint8_t index, bool want_active) {
  uint8_t seen = 0;
  for (uint8_t i = 0; i < g_count; ++i) {
    if (g_items[i].active != want_active) continue;
    if (seen == index) return static_cast<int8_t>(i);
    ++seen;
  }
  return -1;
}

}  // namespace

void begin() {
  g_revision = 0;
  g_dirty = false;

  size_t len = 0;
  if (device_storage::read(kPath, g_blob, sizeof(g_blob), &len)) {
    uint8_t count = 0;
    uint32_t revision = 0;
    if (catalog_codec::decode_catalog(g_blob, len, g_items, settings::max_products,
                                      &count, &revision)) {
      g_count = count;
      g_revision = revision;
      Serial.printf("[catalog] loaded %u products, revision %lu\n",
                    static_cast<unsigned>(g_count),
                    static_cast<unsigned long>(g_revision));
      return;
    }
    // Refuse a damaged file rather than running on half of it. Reseeding loses
    // local edits, which is why the write path is atomic in the first place.
    Serial.println("[catalog] stored catalog rejected; reseeding");
  }

  seed();
  g_dirty = true;
  flush();
}

uint32_t revision() { return g_revision; }

void set_revision(uint32_t r) {
  if (r == g_revision) return;
  g_revision = r;
  g_dirty = true;
}

bool dirty() { return g_dirty; }

void flush() {
  if (!g_dirty) return;
  const size_t len =
      catalog_codec::encode_catalog(g_items, g_count, g_revision, g_blob, sizeof(g_blob));
  if (len == 0) {
    Serial.println("[catalog] encode failed; not writing");
    return;
  }
  if (!device_storage::write(kPath, g_blob, len)) {
    // Leave the dirty flag set so the next flush retries rather than losing the
    // change silently.
    Serial.println("[catalog] write failed; will retry");
    return;
  }
  g_dirty = false;
  Serial.printf("[catalog] saved %u products (%u bytes)\n",
                static_cast<unsigned>(g_count), static_cast<unsigned>(len));
}

uint8_t active_count() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < g_count; ++i) {
    if (g_items[i].active) ++n;
  }
  return n;
}

uint8_t archived_count() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < g_count; ++i) {
    if (!g_items[i].active) ++n;
  }
  return n;
}

const Product* active_at(uint8_t index) {
  const int8_t s = nth_matching(index, true);
  return s < 0 ? nullptr : &g_items[s];
}

const Product* archived_at(uint8_t index) {
  const int8_t s = nth_matching(index, false);
  return s < 0 ? nullptr : &g_items[s];
}

int8_t storage_index_of_active(uint8_t index) { return nth_matching(index, true); }
int8_t storage_index_of_archived(uint8_t index) { return nth_matching(index, false); }

const Product* at_storage(int8_t storage_index) {
  if (storage_index < 0 || storage_index >= static_cast<int8_t>(g_count)) return nullptr;
  return &g_items[storage_index];
}

bool archive(int8_t storage_index) {
  if (storage_index < 0 || storage_index >= static_cast<int8_t>(g_count)) return false;
  if (!g_items[storage_index].active) return false;
  g_items[storage_index].active = false;
  g_dirty = true;
  return true;
}

bool can_restore() { return active_count() < settings::max_active_products; }

bool restore(int8_t storage_index) {
  if (storage_index < 0 || storage_index >= static_cast<int8_t>(g_count)) return false;
  if (g_items[storage_index].active || !can_restore()) return false;
  g_items[storage_index].active = true;
  g_dirty = true;
  return true;
}

int8_t add_product(const char* name, int32_t price_rappen, bool free_item) {
  if (!name || name[0] == '\0') return -1;
  if (!can_restore() || g_count >= settings::max_products) return -1;
  add("", name, price_rappen, free_item, "", true);
  g_dirty = true;
  return static_cast<int8_t>(g_count - 1);
}

int8_t find(const char* barcode) {
  if (!barcode || barcode[0] == '\0') return -1;
  for (uint8_t i = 0; i < g_count; ++i) {
    if (strcmp(g_items[i].barcode, barcode) == 0) return static_cast<int8_t>(i);
  }
  return -1;
}

void format_rappen(int32_t rappen, bool free_item, char* out, size_t len) {
  if (free_item) {
    snprintf(out, len, "Gratis");
    return;
  }
  // Integer division keeps the amount exact; no float ever touches a price.
  snprintf(out, len, "CHF %ld.%02ld", static_cast<long>(rappen / 100),
           static_cast<long>(rappen % 100));
}

void format_price(const Product& p, char* out, size_t len) {
  format_rappen(p.price_rappen, p.free_item, out, len);
}

uint32_t fallback_colour(const Product& p) {
  // FNV-1a over the barcode, or the name for an ad hoc product, mapped into a
  // warm band so tiles stay on-theme.
  const char* key = p.barcode[0] ? p.barcode : p.name;
  uint32_t h = 2166136261u;
  for (const char* c = key; *c; ++c) {
    h ^= static_cast<uint8_t>(*c);
    h *= 16777619u;
  }
  const uint8_t r = 0x50 + (h & 0x3F);
  const uint8_t g = 0x38 + ((h >> 8) & 0x2F);
  const uint8_t b = 0x1C + ((h >> 16) & 0x1F);
  return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

}  // namespace product_catalog
