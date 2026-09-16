#include "api_protocol.h"
#include <ArduinoJson.h>
#include <string.h>

namespace api_protocol {
namespace {

// Copies a server-supplied string into a fixed field. Control characters are
// dropped rather than escaped: these end up in labels and the serial log, and
// nothing downstream should have to cope with a stray newline or terminal
// escape sequence from the network.
void copy_sanitised(char* dst, size_t dst_cap, const char* src) {
  if (dst_cap == 0) return;
  size_t w = 0;
  if (src) {
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(src);
         *p && w + 1 < dst_cap; ++p) {
      // Keep printable ASCII and anything in the UTF-8 multi-byte range, so
      // umlauts survive; drop C0 controls and DEL.
      if (*p >= 0x20 && *p != 0x7F) dst[w++] = static_cast<char>(*p);
    }
  }
  dst[w] = '\0';
}

void put_error(char* error, size_t cap, const char* msg) {
  if (error && cap > 0) copy_sanitised(error, cap, msg);
}

// serializeJson fills whatever space it is given and reports how much it wrote,
// so on a short buffer it yields truncated, invalid JSON. Measure first and
// refuse, rather than posting a body the backend cannot parse.
size_t serialise(const JsonDocument& doc, char* out, size_t capacity) {
  const size_t needed = measureJson(doc);
  if (needed + 1 > capacity) return 0;
  return serializeJson(doc, out, capacity);
}

}  // namespace

bool redirect_target_allowed(const char* url) {
  if (!url || strncmp(url, "https://", 8) != 0) return false;
  const char* host = url + 8;
  const char* slash = strchr(host, '/');
  size_t host_len = slash ? static_cast<size_t>(slash - host) : strlen(host);
  // Userinfo before an '@' would let "google.com@evil.example" pass a naive
  // suffix test, so refuse it outright rather than try to parse it.
  if (memchr(host, '@', host_len) != nullptr) return false;
  // Strip an explicit port before matching the suffix.
  const void* colon = memchr(host, ':', host_len);
  if (colon) host_len = static_cast<size_t>(static_cast<const char*>(colon) - host);
  if (host_len == 0 || host_len > 253) return false;

  static const char* const kSuffixes[] = {".google.com", ".googleusercontent.com"};
  for (const char* suffix : kSuffixes) {
    const size_t n = strlen(suffix);
    if (host_len > n && strncmp(host + host_len - n, suffix, n) == 0) return true;
  }
  return false;
}

size_t build_sync(char* out, size_t capacity, const char* token, const char* device,
                  uint32_t since_revision) {
  JsonDocument doc;
  doc["action"] = "sync";
  doc["token"] = token;
  doc["device"] = device;
  doc["since"] = since_revision;
  return serialise(doc, out, capacity);
}

size_t build_record_purchase(char* out, size_t capacity, const char* token,
                             const char* device, const char* transaction_id,
                             const char* barcode, const char* product_name,
                             int32_t price_rappen, bool free_item,
                             const char* resident_id) {
  JsonDocument doc;
  doc["action"] = "recordPurchase";
  doc["token"] = token;
  doc["device"] = device;
  doc["transaction_id"] = transaction_id;
  doc["barcode"] = barcode ? barcode : "";
  doc["name"] = product_name ? product_name : "";
  doc["price_rappen"] = price_rappen;
  doc["free"] = free_item;
  doc["resident_id"] = resident_id ? resident_id : "";
  return serialise(doc, out, capacity);
}

size_t build_void_purchase(char* out, size_t capacity, const char* token,
                           const char* device, const char* transaction_id) {
  JsonDocument doc;
  doc["action"] = "voidPurchase";
  doc["token"] = token;
  doc["device"] = device;
  doc["transaction_id"] = transaction_id;
  return serialise(doc, out, capacity);
}

Outcome parse_sync(const char* body, size_t len, SyncResult* out, char* error,
                   size_t error_capacity) {
  if (!body || !out) return Outcome::Malformed;
  put_error(error, error_capacity, "");

  JsonDocument doc;
  if (deserializeJson(doc, body, len) != DeserializationError::Ok) {
    put_error(error, error_capacity, "Antwort unlesbar");
    return Outcome::Malformed;
  }
  if (!doc["ok"].is<bool>()) {
    put_error(error, error_capacity, "Antwort unvollstaendig");
    return Outcome::Malformed;
  }
  if (!doc["ok"].as<bool>()) {
    put_error(error, error_capacity, doc["error"].is<const char*>()
                                         ? doc["error"].as<const char*>()
                                         : "Server hat abgelehnt");
    return Outcome::Rejected;
  }

  JsonArrayConst products = doc["products"].as<JsonArrayConst>();
  JsonArrayConst residents = doc["residents"].as<JsonArrayConst>();
  JsonArrayConst summary = doc["summary"].as<JsonArrayConst>();
  if (products.size() > settings::max_products ||
      residents.size() > settings::max_residents ||
      summary.size() > settings::max_residents) {
    put_error(error, error_capacity, "Zu viele Eintraege");
    return Outcome::TooManyItems;
  }

  // Only written once everything has validated, so a rejected response cannot
  // leave the caller with a half-updated catalog.
  out->revision = doc["revision"].as<uint32_t>();
  out->changed = doc["changed"].as<bool>();
  out->product_count = 0;
  out->resident_count = 0;
  out->summary_count = 0;

  for (JsonObjectConst p : products) {
    product_catalog::Product& d = out->products[out->product_count];
    d = product_catalog::Product{};
    copy_sanitised(d.barcode, sizeof(d.barcode), p["barcode"].as<const char*>());
    copy_sanitised(d.name, sizeof(d.name), p["name"].as<const char*>());
    d.price_rappen = p["price_rappen"].as<int32_t>();
    d.free_item = p["free"].as<bool>();
    copy_sanitised(d.image_url, sizeof(d.image_url), p["image_url"].as<const char*>());
    // Absent means active: a backend that omits the flag should not silently
    // empty the fridge.
    d.active = p["active"].is<bool>() ? p["active"].as<bool>() : true;
    if (d.name[0] == '\0') continue;  // a nameless product is unusable
    ++out->product_count;
  }

  for (JsonObjectConst r : residents) {
    resident_directory::Entry& d = out->residents[out->resident_count];
    d = resident_directory::Entry{};
    copy_sanitised(d.id, sizeof(d.id), r["id"].as<const char*>());
    copy_sanitised(d.name, sizeof(d.name), r["name"].as<const char*>());
    if (d.id[0] == '\0' || d.name[0] == '\0') continue;
    ++out->resident_count;
  }

  for (JsonObjectConst r : summary) {
    purchase_log::Tally& d = out->summary[out->summary_count];
    d = purchase_log::Tally{};
    copy_sanitised(d.resident_id, sizeof(d.resident_id),
                   r["resident_id"].as<const char*>());
    copy_sanitised(d.resident_name, sizeof(d.resident_name),
                   r["name"].as<const char*>());
    const int32_t drinks = r["drinks"].as<int32_t>();
    d.drinks = drinks > 0 ? static_cast<uint16_t>(drinks) : 0;
    d.total_rappen = r["total_rappen"].as<int32_t>();
    if (d.resident_id[0] == '\0') continue;
    ++out->summary_count;
  }

  return Outcome::Ok;
}

Outcome parse_ack(const char* body, size_t len, bool* duplicate, char* error,
                  size_t error_capacity) {
  if (duplicate) *duplicate = false;
  put_error(error, error_capacity, "");
  if (!body) return Outcome::Malformed;

  JsonDocument doc;
  if (deserializeJson(doc, body, len) != DeserializationError::Ok) {
    put_error(error, error_capacity, "Antwort unlesbar");
    return Outcome::Malformed;
  }
  if (!doc["ok"].is<bool>()) {
    put_error(error, error_capacity, "Antwort unvollstaendig");
    return Outcome::Malformed;
  }
  if (!doc["ok"].as<bool>()) {
    put_error(error, error_capacity, doc["error"].is<const char*>()
                                         ? doc["error"].as<const char*>()
                                         : "Server hat abgelehnt");
    return Outcome::Rejected;
  }
  if (duplicate) *duplicate = doc["duplicate"].as<bool>();
  return Outcome::Ok;
}

}  // namespace api_protocol
