#include "product_catalog.h"
#include <stdio.h>
#include <string.h>

namespace product_catalog {
namespace {

Product g_items[settings::max_products];
uint8_t g_count = 0;

void add(const char* barcode, const char* name, int32_t rappen, bool free_item,
         const char* image_url) {
  if (g_count >= settings::max_products) return;
  Product& p = g_items[g_count++];
  snprintf(p.barcode, sizeof(p.barcode), "%s", barcode);
  snprintf(p.name, sizeof(p.name), "%s", name);
  p.price_rappen = rappen;
  p.free_item = free_item;
  snprintf(p.image_url, sizeof(p.image_url), "%s", image_url);
}

}  // namespace

void begin() {
  g_count = 0;
  // Mock catalog for the prototype. Barcodes are plausible but not authoritative;
  // real values arrive with the backend catalog in milestone 6.
  add("7610807000019", "Feldschlosschen Original", 180, false, "");
  add("7610827000024", "Appenzeller Quollfrisch", 220, false, "");
  add("7610900000038", "Calanda Brau", 190, false, "");
  add("7613100000045", "Chopfab Draft", 260, false, "");
  add("7610827000055", "Appenzeller Naturperle", 240, false, "");
  add("7610013000062", "Boxer Old", 170, false, "");
  add("7610700000079", "Valaisanne Pale Ale", 280, false, "");
  add("7613300000086", "Turbinenbrau Gassenhauer", 0, true, "");
}

uint8_t count() { return g_count; }

const Product* at(uint8_t index) {
  return index < g_count ? &g_items[index] : nullptr;
}

int8_t find(const char* barcode) {
  for (uint8_t i = 0; i < g_count; ++i) {
    if (strcmp(g_items[i].barcode, barcode) == 0) return static_cast<int8_t>(i);
  }
  return -1;
}

void format_price(const Product& p, char* out, size_t len) {
  if (p.free_item) {
    snprintf(out, len, "Gratis");
    return;
  }
  // Integer division keeps the amount exact; no float ever touches a price.
  snprintf(out, len, "CHF %ld.%02ld", static_cast<long>(p.price_rappen / 100),
           static_cast<long>(p.price_rappen % 100));
}

uint32_t fallback_colour(const Product& p) {
  // FNV-1a over the barcode, mapped into a warm band so tiles stay on-theme.
  uint32_t h = 2166136261u;
  for (const char* c = p.barcode; *c; ++c) {
    h ^= static_cast<uint8_t>(*c);
    h *= 16777619u;
  }
  const uint8_t r = 0x50 + (h & 0x3F);
  const uint8_t g = 0x38 + ((h >> 8) & 0x2F);
  const uint8_t b = 0x1C + ((h >> 16) & 0x1F);
  return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

}  // namespace product_catalog
