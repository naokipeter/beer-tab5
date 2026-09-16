#pragma once
#include <stddef.h>
#include <stdint.h>
#include "product_catalog.h"

// Product photos: downloaded once, kept on LittleFS, decoded by LVGL.
//
// The cache is keyed by a hash of the image URL, not by barcode, so a product
// whose photo changes simply maps to a different file and is refetched. Nothing
// has to compare timestamps.
namespace image_cache {

void begin();

// Fills `out` with "/img/<hash>.jpg" for a product, or an empty string when the
// product has no usable URL.
void path_for(const product_catalog::Product& p, char* out, size_t capacity);

bool have(const product_catalog::Product& p);

// Loads a cached JPEG into a PSRAM buffer owned by the cache and returns it, or
// nullptr. Valid until release_all(). The bytes stay compressed; LVGL decodes
// them and caches the result itself.
const uint8_t* load(const product_catalog::Product& p, size_t* out_size);

// Frees every buffer handed out by load(). Called when a screen is torn down.
void release_all();

// Starts one download if a product is missing its photo and the network is
// otherwise idle. Photos never compete with a purchase. Call from the loop.
void update(uint32_t now_ms);

// Deletes cached files no active product refers to any more.
void prune();

// For the admin screen.
uint8_t cached_count();
bool busy();

}  // namespace image_cache
