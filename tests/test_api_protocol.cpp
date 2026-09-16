// Host test for the device/backend wire contract. Run with tools/test.sh.
#include "../firmware/beer_terminal/api_protocol.h"
#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
  if (ok) {
    std::printf("  ok %s\n", what);
  } else {
    std::printf("FAIL %s\n", what);
    ++g_failures;
  }
}

std::string field(const char* json, const char* key) {
  JsonDocument d;
  if (deserializeJson(d, json) != DeserializationError::Ok) return "<bad json>";
  if (!d[key].is<const char*>()) return "<missing>";
  return d[key].as<const char*>();
}

}  // namespace

int main() {
  char req[api_protocol::kMaxRequestBytes];

  std::printf("request building\n");
  {
    const size_t n = api_protocol::build_sync(req, sizeof(req), "tok", "fridge-01", 12);
    check(n > 0 && field(req, "action") == "sync", "sync carries its action");
    JsonDocument d;
    deserializeJson(d, req);
    check(d["since"].as<uint32_t>() == 12, "sync carries the known revision");
  }
  {
    // A name with a quote and an umlaut is the classic way a hand-rolled encoder
    // produces invalid JSON.
    const size_t n = api_protocol::build_record_purchase(
        req, sizeof(req), "tok", "fridge-01", "fridge-01-abc-1", "7610807000016",
        "Feldschlösschen \"Original\"\n", 180, false, "r1");
    check(n > 0, "purchase encodes");
    check(field(req, "name") == "Feldschlösschen \"Original\"\n",
          "quotes, newlines and umlauts survive a round trip");
    JsonDocument d;
    deserializeJson(d, req);
    check(d["price_rappen"].as<int32_t>() == 180 && d["free"].as<bool>() == false,
          "price stays an integer and free stays a boolean");
  }
  {
    char tiny[16];
    check(api_protocol::build_void_purchase(tiny, sizeof(tiny), "tok", "d", "t") == 0,
          "a request that does not fit refuses rather than truncating");
  }

  std::printf("\nsync responses\n");
  api_protocol::SyncResult r{};
  char err[64];
  {
    const char* body =
        "{\"ok\":true,\"revision\":9,\"changed\":true,"
        "\"products\":[{\"barcode\":\"7610807000016\",\"name\":\"Feldschlösschen\","
        "\"price_rappen\":180,\"free\":false,\"image_url\":\"https://x/y.jpg\","
        "\"active\":true},"
        "{\"barcode\":\"761\",\"name\":\"Gratisbier\",\"price_rappen\":0,"
        "\"free\":true,\"active\":false}],"
        "\"residents\":[{\"id\":\"r1\",\"name\":\"Naoki\"},"
        "{\"id\":\"r2\",\"name\":\"Jörg\"}]}";
    const auto o = api_protocol::parse_sync(body, std::strlen(body), &r, err, sizeof(err));
    check(o == api_protocol::Outcome::Ok, "a well-formed sync parses");
    check(r.revision == 9 && r.changed, "revision and changed flag survive");
    check(r.product_count == 2 && r.resident_count == 2, "both lists are read");
    check(std::strcmp(r.products[0].name, "Feldschlösschen") == 0 &&
              r.products[0].price_rappen == 180 && r.products[0].active,
          "product fields survive, including umlauts");
    check(r.products[1].free_item && !r.products[1].active,
          "free and archived flags survive");
    check(std::strcmp(r.residents[1].name, "Jörg") == 0, "resident names survive");
  }
  {
    // A product without the active flag must default to visible, not vanish.
    const char* body =
        "{\"ok\":true,\"revision\":1,\"products\":[{\"barcode\":\"1\",\"name\":\"X\","
        "\"price_rappen\":100}],\"residents\":[]}";
    api_protocol::SyncResult s{};
    api_protocol::parse_sync(body, std::strlen(body), &s, err, sizeof(err));
    check(s.product_count == 1 && s.products[0].active,
          "a missing active flag defaults to active");
  }
  {
    const char* body = "{\"ok\":true,\"revision\":1,\"products\":[{\"barcode\":\"1\","
                       "\"name\":\"\",\"price_rappen\":1}],\"residents\":[]}";
    api_protocol::SyncResult s{};
    api_protocol::parse_sync(body, std::strlen(body), &s, err, sizeof(err));
    check(s.product_count == 0, "a nameless product is dropped");
  }
  {
    const char* body = "{\"ok\":false,\"error\":\"Token abgelehnt\"}";
    const auto o = api_protocol::parse_sync(body, std::strlen(body), &r, err, sizeof(err));
    check(o == api_protocol::Outcome::Rejected && std::strcmp(err, "Token abgelehnt") == 0,
          "a server rejection surfaces its message");
  }
  {
    const char* body = "{\"ok\":false,\"error\":\"bad\\u0007\\u001bthing\"}";
    api_protocol::parse_sync(body, std::strlen(body), &r, err, sizeof(err));
    check(std::strcmp(err, "badthing") == 0,
          "control characters are stripped from a server message");
  }
  {
    const char* body = "not json at all";
    check(api_protocol::parse_sync(body, std::strlen(body), &r, err, sizeof(err)) ==
              api_protocol::Outcome::Malformed,
          "a non-JSON body is malformed");
  }
  {
    const char* body = "{\"revision\":3}";
    check(api_protocol::parse_sync(body, std::strlen(body), &r, err, sizeof(err)) ==
              api_protocol::Outcome::Malformed,
          "a response without ok is malformed");
  }
  {
    std::string big = "{\"ok\":true,\"revision\":1,\"products\":[";
    for (int i = 0; i < settings::max_products + 1; ++i) {
      if (i) big += ",";
      big += "{\"barcode\":\"1\",\"name\":\"X\",\"price_rappen\":1}";
    }
    big += "],\"residents\":[]}";
    api_protocol::SyncResult s{};
    s.product_count = 77;
    check(api_protocol::parse_sync(big.c_str(), big.size(), &s, err, sizeof(err)) ==
              api_protocol::Outcome::TooManyItems,
          "more products than this build can hold is rejected");
    check(s.product_count == 77, "an over-capacity response leaves the result untouched");
  }
  {
    // An over-long name must be bounded, not overrun the fixed field.
    std::string body = "{\"ok\":true,\"revision\":1,\"products\":[{\"barcode\":\"1\","
                       "\"name\":\"" + std::string(300, 'N') +
                       "\",\"price_rappen\":1}],\"residents\":[]}";
    api_protocol::SyncResult s{};
    api_protocol::parse_sync(body.c_str(), body.size(), &s, err, sizeof(err));
    check(s.product_count == 1 && std::strlen(s.products[0].name) == 39,
          "an over-long name is truncated to its field");
  }

  std::printf("\nredirect targets\n");
  {
    using api_protocol::redirect_target_allowed;
    check(redirect_target_allowed(
              "https://script.googleusercontent.com/macros/echo?user_content_key=x"),
          "the Apps Script echo host is allowed");
    check(redirect_target_allowed("https://script.google.com/macros/s/AB/exec"),
          "script.google.com is allowed");
    check(!redirect_target_allowed("http://script.google.com/x"),
          "plain http is refused");
    check(!redirect_target_allowed("https://evil.example.com/x"),
          "another host is refused");
    check(!redirect_target_allowed("https://google.com.evil.example/x"),
          "a suffix lookalike is refused");
    check(!redirect_target_allowed("https://images.openfoodfacts.org/x.jpg"),
          "an image host is not an API redirect target");
    check(!redirect_target_allowed("https://notgoogle.com/x"),
          "a host merely ending in the wrong place is refused");
    check(!redirect_target_allowed("https://evil.example/?x=.google.com"),
          "the suffix appearing in the path or query does not count");
    check(!redirect_target_allowed("https://script.google.com@evil.example/x"),
          "userinfo before an at-sign cannot smuggle a host past the check");
    check(!redirect_target_allowed(""), "an empty location is refused");
    check(!redirect_target_allowed("https://"), "a bare scheme is refused");
    // The bare apex is intentionally refused: every host we talk to is a
    // subdomain, and allowing it would widen the surface for nothing.
    check(!redirect_target_allowed("https://google.com/x"), "the bare apex is refused");
  }

  std::printf("\nimage sources\n");
  {
    using api_protocol::image_source_allowed;
    check(image_source_allowed("https://images.openfoodfacts.org/images/x.400.jpg"),
          "an Open Food Facts image host is allowed");
    check(image_source_allowed("https://openfoodfacts.org/x.jpg"),
          "the bare Open Food Facts domain is allowed");
    check(!image_source_allowed("http://images.openfoodfacts.org/x.jpg"),
          "plain http is refused, so a photo cannot be swapped in transit");
    check(!image_source_allowed("https://evil.example/x.jpg"),
          "another host is refused");
    check(!image_source_allowed("https://openfoodfacts.org.evil.example/x.jpg"),
          "a suffix lookalike is refused");
    check(!image_source_allowed("https://notopenfoodfacts.org/x.jpg"),
          "a host ending in the wrong place is refused");
    check(!image_source_allowed("https://images.openfoodfacts.org@evil.example/x"),
          "userinfo before an at-sign cannot smuggle a host past the check");
    check(!image_source_allowed(""), "an empty url is refused");
    // The device checks for itself rather than trusting the backend's validation.
    check(!image_source_allowed("https://script.google.com/x.jpg"),
          "our backend host is not an image source");
    // Google's user content host, so a beer Open Food Facts has no picture for
    // can be given one by hand.
    check(image_source_allowed("https://lh3.googleusercontent.com/pw/AP1Gcz=w150-h200"),
          "a Google Photos link is allowed");
    check(!image_source_allowed("https://googleusercontent.com.evil.example/x.jpg"),
          "a Google user content lookalike is refused");
  }

  std::printf("\nacknowledgements\n");
  {
    bool dup = true;
    const char* body = "{\"ok\":true}";
    check(api_protocol::parse_ack(body, std::strlen(body), &dup, err, sizeof(err)) ==
              api_protocol::Outcome::Ok && !dup,
          "a plain ack parses and is not a duplicate");
  }
  {
    bool dup = false;
    const char* body = "{\"ok\":true,\"duplicate\":true}";
    check(api_protocol::parse_ack(body, std::strlen(body), &dup, err, sizeof(err)) ==
              api_protocol::Outcome::Ok && dup,
          "a replayed transaction reports duplicate, not failure");
  }
  {
    bool dup = false;
    const char* body = "{\"ok\":false,\"error\":\"unbekannte Transaktion\"}";
    check(api_protocol::parse_ack(body, std::strlen(body), &dup, err, sizeof(err)) ==
              api_protocol::Outcome::Rejected,
          "a rejected void surfaces as rejected");
  }
  {
    bool dup = false;
    check(api_protocol::parse_ack("", 0, &dup, err, sizeof(err)) ==
              api_protocol::Outcome::Malformed,
          "an empty body is malformed");
  }

  std::printf(g_failures ? "\n%d failure(s)\n" : "\nall checks passed\n", g_failures);
  return g_failures ? 1 : 0;
}
