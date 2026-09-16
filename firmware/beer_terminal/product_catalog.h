#pragma once
#include <stddef.h>
#include <stdint.h>
#include "settings.h"

// Bounded, fixed-capacity product catalog. Milestone 3 fills it from a compiled-in
// mock; milestone 6 replaces the source with the cached backend catalog. No
// allocation happens here, so the grid can be rebuilt without touching the heap.
//
// Products are archived rather than deleted: purchase history keeps referencing
// their barcodes, and a beer that is out of stock this week usually returns.
namespace product_catalog {

struct Product {
  char barcode[14];      // EAN-13 plus terminator; empty for an ad hoc product.
  char name[40];
  int32_t price_rappen;  // Integer rappen; never a floating-point CHF amount.
  bool free_item;
  char image_url[160];   // Open Food Facts 400px variant; empty when unavailable.
  bool active;           // false = archived, hidden from the grid.
};

void begin();

// Active products, in catalog order. These are what the grid shows.
uint8_t active_count();
const Product* active_at(uint8_t index);

// Archived products, offered for restore before the new-product form.
uint8_t archived_count();
const Product* archived_at(uint8_t index);

// Storage indices, stable across archive and restore. The UI passes these around
// so a selection survives a list changing underneath it.
int8_t storage_index_of_active(uint8_t index);
int8_t storage_index_of_archived(uint8_t index);
const Product* at_storage(int8_t storage_index);

bool archive(int8_t storage_index);
// Fails when the grid is already full; the caller must archive something first.
bool restore(int8_t storage_index);
bool can_restore();

// Registers an ad hoc product. Returns its storage index, or -1 when the grid is
// full or the name is unusable.
int8_t add_product(const char* name, int32_t price_rappen, bool free_item);

int8_t find(const char* barcode);

// Formats an integer rappen amount as "CHF 2.40", or "Gratis" when free.
void format_price(const Product& p, char* out, size_t len);
void format_rappen(int32_t rappen, bool free_item, char* out, size_t len);

// Deterministic tile colour derived from the barcode or name, used when a product
// has no usable Open Food Facts photo. Stable across reboots.
uint32_t fallback_colour(const Product& p);

}  // namespace product_catalog
