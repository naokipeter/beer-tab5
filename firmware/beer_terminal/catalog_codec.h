#pragma once
#include <stddef.h>
#include <stdint.h>
#include "product_catalog.h"
#include "settings.h"
#include "resident_directory.h"

// Serialisation for everything the device keeps across a reboot. Deliberately
// free of Arduino and LittleFS so the format can be exercised on the host; the
// file I/O lives in device_storage.
//
// The on-disk layout is explicit and little-endian rather than a memcpy of the
// in-memory structs, so a compiler padding change cannot silently invalidate
// stored data. Every blob carries a magic, a format version and a CRC32: a
// truncated or corrupted file is rejected whole rather than half-loaded.
namespace catalog_codec {

inline constexpr uint16_t kFormatVersion = 1;

// On-disk sizes. Constants rather than functions so callers can declare a
// correctly sized static buffer: the catalog blob is several kilobytes, which
// has no business on the loop task's stack.
inline constexpr size_t kHeaderBytes = 20;
inline constexpr size_t kCatalogRecordBytes = 219;
inline constexpr size_t kResidentRecordBytes = 36;
inline constexpr size_t kMaxCatalogBytes =
    kHeaderBytes + static_cast<size_t>(settings::max_products) * kCatalogRecordBytes;
inline constexpr size_t kMaxResidentsBytes =
    kHeaderBytes + static_cast<size_t>(settings::max_residents) * kResidentRecordBytes;

size_t max_catalog_bytes();
size_t max_residents_bytes();

// Return the number of bytes written, or 0 if `capacity` is too small.
// `seed_generation` records which compiled-in seed the data descends from, so a
// device can notice that its stored catalog predates a correction to that seed.
// It occupies the header field previously reserved.
size_t encode_catalog(const product_catalog::Product* items, uint8_t count,
                      uint32_t revision, uint16_t seed_generation, uint8_t* out,
                      size_t capacity);
size_t encode_residents(const resident_directory::Entry* entries, uint8_t count,
                        uint8_t* out, size_t capacity);

// Return false on a bad magic, an unknown version, a length mismatch, a CRC
// mismatch, or a count beyond `capacity_items`. `out_count` is only written on
// success, so a rejected blob leaves the caller's data untouched.
bool decode_catalog(const uint8_t* in, size_t len, product_catalog::Product* items,
                    uint8_t capacity_items, uint8_t* out_count, uint32_t* out_revision,
                    uint16_t* out_seed_generation);
bool decode_residents(const uint8_t* in, size_t len, resident_directory::Entry* entries,
                      uint8_t capacity_items, uint8_t* out_count);

// Exposed for the host test.
uint32_t crc32(const uint8_t* data, size_t len);

}  // namespace catalog_codec
