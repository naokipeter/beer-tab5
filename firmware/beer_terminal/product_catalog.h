#pragma once
#include <stddef.h>
#include <stdint.h>
#include "settings.h"

// Bounded, fixed-capacity product catalog. Milestone 3 fills it from a compiled-in
// mock; milestone 6 replaces the source with the cached backend catalog. No
// allocation happens here, so the grid can be rebuilt without touching the heap.
namespace product_catalog {

struct Product {
  char barcode[14];      // EAN-13 plus terminator.
  char name[40];
  int32_t price_rappen;  // Integer rappen; never a floating-point CHF amount.
  bool free_item;
  char image_url[160];   // Open Food Facts 400px variant; empty when unavailable.
};

void begin();

uint8_t count();
const Product* at(uint8_t index);

// Index of a barcode, or -1. Used by the manual-entry and admin paths.
int8_t find(const char* barcode);

// Formats an integer rappen amount as "CHF 2.40", or "Gratis" when free.
// Writes at most `len` bytes including the terminator.
void format_price(const Product& p, char* out, size_t len);

// Deterministic tile colour derived from the barcode, used when a product has
// no usable Open Food Facts photo. Stable across reboots so a beer keeps its colour.
uint32_t fallback_colour(const Product& p);

}  // namespace product_catalog
