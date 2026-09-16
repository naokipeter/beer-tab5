#include "catalog_codec.h"
#include <string.h>

namespace catalog_codec {
namespace {

// 'B','R','T','1' and 'R','R','T','1' — distinguishes these blobs from anything
// else that might end up on the volume.
constexpr uint32_t kMagicCatalog = 0x31545242u;
constexpr uint32_t kMagicResidents = 0x31545252u;

// magic(4) version(2) record_size(2) revision(4) count(2) reserved(2) crc(4)
constexpr size_t kCrcOffset = 16;

// Fixed on-disk field widths. Changing any of these needs a format version bump.
constexpr size_t kBarcode = 14;
constexpr size_t kName = 40;
constexpr size_t kImageUrl = 160;
constexpr size_t kCatalogRecord = kBarcode + kName + 4 + 1 + kImageUrl;  // 219
constexpr size_t kResidentId = 12;
constexpr size_t kResidentName = 24;
constexpr size_t kResidentRecord = kResidentId + kResidentName;  // 36

// The header advertises these sizes so a build with different field widths
// rejects another build's files; keep the public constants in step.
static_assert(kCatalogRecord == kCatalogRecordBytes, "catalog record size drifted");
static_assert(kResidentRecord == kResidentRecordBytes, "resident record size drifted");

constexpr uint8_t kFlagFree = 0x01;
constexpr uint8_t kFlagActive = 0x02;

void put_u16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
void put_u32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}
uint16_t get_u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t get_u32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Copies at most field-1 characters and zero-fills the rest, so a stored string
// is always terminated within its field.
void put_text(uint8_t* dst, size_t field, const char* src) {
  memset(dst, 0, field);
  if (!src) return;
  const size_t n = strnlen(src, field - 1);
  memcpy(dst, src, n);
}
void get_text(char* dst, size_t dst_cap, const uint8_t* src, size_t field) {
  const size_t n = strnlen(reinterpret_cast<const char*>(src), field);
  const size_t copy = n < dst_cap - 1 ? n : dst_cap - 1;
  memcpy(dst, src, copy);
  dst[copy] = '\0';
}

// Incremental CRC-32/ISO-HDLC. Needed because the checksum covers the header
// with its own field zeroed followed by the payload, which is two spans.
uint32_t crc_begin() { return 0xFFFFFFFFu; }
uint32_t crc_feed(uint32_t c, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    c ^= data[i];
    for (int b = 0; b < 8; ++b) c = (c >> 1) ^ (0xEDB88320u & (~(c & 1) + 1));
  }
  return c;
}
uint32_t crc_end(uint32_t c) { return ~c; }

void finalise(uint8_t* out, size_t len) {
  put_u32(out + kCrcOffset, 0);
  put_u32(out + kCrcOffset, crc32(out, len));
}

// Validates the header and the CRC. Returns the record count on success.
bool check(const uint8_t* in, size_t len, uint32_t magic, uint16_t record_size,
           uint8_t capacity_items, uint16_t* out_count, uint32_t* out_revision,
           uint16_t* out_seed_generation) {
  if (len < kHeaderBytes) return false;
  if (get_u32(in) != magic) return false;
  if (get_u16(in + 4) != kFormatVersion) return false;
  if (get_u16(in + 6) != record_size) return false;
  const uint16_t count = get_u16(in + 12);
  if (count > capacity_items) return false;
  if (len != kHeaderBytes + static_cast<size_t>(count) * record_size) return false;

  // The CRC is computed over the whole blob with its own field zeroed, so verify
  // the same way: a copy of the header with that field cleared, then the payload.
  const uint32_t stored = get_u32(in + kCrcOffset);
  uint8_t head[kHeaderBytes];
  memcpy(head, in, kHeaderBytes);
  put_u32(head + kCrcOffset, 0);
  uint32_t c = crc_feed(crc_begin(), head, kHeaderBytes);
  c = crc_feed(c, in + kHeaderBytes, len - kHeaderBytes);
  if (crc_end(c) != stored) return false;

  *out_count = count;
  if (out_revision) *out_revision = get_u32(in + 8);
  if (out_seed_generation) *out_seed_generation = get_u16(in + 14);
  return true;
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t len) {
  // Table-free CRC-32/ISO-HDLC. A table would cost 1 KB of flash to save time we
  // do not need: this runs on a catalog write, not per frame.
  return crc_end(crc_feed(crc_begin(), data, len));
}

size_t max_catalog_bytes() {
  return kHeaderBytes + static_cast<size_t>(settings::max_products) * kCatalogRecord;
}
size_t max_residents_bytes() {
  return kHeaderBytes + static_cast<size_t>(settings::max_residents) * kResidentRecord;
}

size_t encode_catalog(const product_catalog::Product* items, uint8_t count,
                      uint32_t revision, uint16_t seed_generation, uint8_t* out,
                      size_t capacity) {
  if (!items || !out) return 0;
  const size_t len = kHeaderBytes + static_cast<size_t>(count) * kCatalogRecord;
  if (capacity < len) return 0;

  put_u32(out + 0, kMagicCatalog);
  put_u16(out + 4, kFormatVersion);
  put_u16(out + 6, static_cast<uint16_t>(kCatalogRecord));
  put_u32(out + 8, revision);
  put_u16(out + 12, count);
  put_u16(out + 14, seed_generation);

  uint8_t* p = out + kHeaderBytes;
  for (uint8_t i = 0; i < count; ++i) {
    const product_catalog::Product& s = items[i];
    put_text(p, kBarcode, s.barcode);
    put_text(p + kBarcode, kName, s.name);
    put_u32(p + kBarcode + kName, static_cast<uint32_t>(s.price_rappen));
    p[kBarcode + kName + 4] = static_cast<uint8_t>((s.free_item ? kFlagFree : 0) |
                                                   (s.active ? kFlagActive : 0));
    put_text(p + kBarcode + kName + 5, kImageUrl, s.image_url);
    p += kCatalogRecord;
  }
  finalise(out, len);
  return len;
}

bool decode_catalog(const uint8_t* in, size_t len, product_catalog::Product* items,
                    uint8_t capacity_items, uint8_t* out_count,
                    uint32_t* out_revision, uint16_t* out_seed_generation) {
  if (!in || !items || !out_count) return false;
  uint16_t count = 0;
  if (!check(in, len, kMagicCatalog, static_cast<uint16_t>(kCatalogRecord),
             capacity_items, &count, out_revision, out_seed_generation)) {
    return false;
  }
  const uint8_t* p = in + kHeaderBytes;
  for (uint16_t i = 0; i < count; ++i) {
    product_catalog::Product& d = items[i];
    get_text(d.barcode, sizeof(d.barcode), p, kBarcode);
    get_text(d.name, sizeof(d.name), p + kBarcode, kName);
    d.price_rappen = static_cast<int32_t>(get_u32(p + kBarcode + kName));
    const uint8_t flags = p[kBarcode + kName + 4];
    d.free_item = (flags & kFlagFree) != 0;
    d.active = (flags & kFlagActive) != 0;
    get_text(d.image_url, sizeof(d.image_url), p + kBarcode + kName + 5, kImageUrl);
    p += kCatalogRecord;
  }
  *out_count = static_cast<uint8_t>(count);
  return true;
}

size_t encode_residents(const resident_directory::Entry* entries, uint8_t count,
                        uint8_t* out, size_t capacity) {
  if (!entries || !out) return 0;
  const size_t len = kHeaderBytes + static_cast<size_t>(count) * kResidentRecord;
  if (capacity < len) return 0;

  put_u32(out + 0, kMagicResidents);
  put_u16(out + 4, kFormatVersion);
  put_u16(out + 6, static_cast<uint16_t>(kResidentRecord));
  put_u32(out + 8, 0);
  put_u16(out + 12, count);
  put_u16(out + 14, 0);

  uint8_t* p = out + kHeaderBytes;
  for (uint8_t i = 0; i < count; ++i) {
    put_text(p, kResidentId, entries[i].id);
    put_text(p + kResidentId, kResidentName, entries[i].name);
    p += kResidentRecord;
  }
  finalise(out, len);
  return len;
}

bool decode_residents(const uint8_t* in, size_t len, resident_directory::Entry* entries,
                      uint8_t capacity_items, uint8_t* out_count) {
  if (!in || !entries || !out_count) return false;
  uint16_t count = 0;
  if (!check(in, len, kMagicResidents, static_cast<uint16_t>(kResidentRecord),
             capacity_items, &count, nullptr, nullptr)) {
    return false;
  }
  const uint8_t* p = in + kHeaderBytes;
  for (uint16_t i = 0; i < count; ++i) {
    get_text(entries[i].id, sizeof(entries[i].id), p, kResidentId);
    get_text(entries[i].name, sizeof(entries[i].name), p + kResidentId, kResidentName);
    p += kResidentRecord;
  }
  *out_count = static_cast<uint8_t>(count);
  return true;
}

}  // namespace catalog_codec
