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
  // Photo URL. 256 bytes because a Google Photos link runs past 220 characters,
  // where an Open Food Facts one fits in 100. Truncating one silently produces a
  // download that can only fail.
  char image_url[256];
  bool active;           // false = archived, hidden from the grid.
};

// Loads the persisted catalog. Falls back to a compiled-in seed when nothing is
// stored yet or the stored blob is unusable; milestone 7 replaces that seed with
// a fetch from the backend.
void begin();

// Writes the catalog if anything changed since the last call. Driven from the
// main loop rather than from the UI event that made the change, so a flash write
// never blocks a touch response.
void flush();
bool dirty();

// Backend catalog revision this device last synced. 0 means never synced.
uint32_t revision();
void set_revision(uint32_t r);

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

// Replaces the catalog with a synced list and records its revision. Products
// the device created locally and has never sent anywhere — those with no
// barcode — are carried over, so a sync cannot silently delete a beer somebody
// added at the fridge while it was offline. Milestone 9's queue makes this
// unnecessary by pushing them up instead.
bool replace_all(const Product* items, uint8_t count, uint32_t revision);

int8_t find(const char* barcode);

// Formats an integer rappen amount as "CHF 2.40", or "Gratis" when free.
void format_price(const Product& p, char* out, size_t len);
void format_rappen(int32_t rappen, bool free_item, char* out, size_t len);

// Deterministic tile colour derived from the barcode or name, used when a product
// has no usable Open Food Facts photo. Stable across reboots.
uint32_t fallback_colour(const Product& p);

}  // namespace product_catalog
