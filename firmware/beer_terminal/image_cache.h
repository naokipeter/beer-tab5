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

// An LVGL image source for a product's photo, or nullptr when none is cached.
//
// The descriptor and the JPEG bytes behind it are owned by the cache and stay at
// the same address for as long as the photo does. That is deliberate: LVGL's
// decoded-image cache keys on the source pointer, so freeing one while an entry
// still referenced it — which is what a per-screen release did — let a later
// allocation at the same address score a cache hit on freed memory.
//
// Screens may hold the returned pointer only until the next release(); they must
// never free it.
const void* source_for(const product_catalog::Product& p);

// Drops a product's decoded entry from LVGL's cache and frees its buffers. Call
// when the photo behind it changed, never merely because a screen went away.
void release(const product_catalog::Product& p);

// Same for everything, e.g. after a sync replaced the catalog.
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
