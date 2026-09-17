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

  std::printf("\nconsumption summary\n");
  {
    const char* body =
        "{\"ok\":true,\"revision\":3,\"changed\":false,\"products\":[],"
        "\"residents\":[],\"summary\":["
        "{\"resident_id\":\"r1\",\"name\":\"Jörg\",\"drinks\":7,"
        "\"total_rappen\":1260},"
        "{\"resident_id\":\"r2\",\"name\":\"Miriam\",\"drinks\":2,"
        "\"total_rappen\":0}]}";
    api_protocol::SyncResult s2{};
    const auto o = api_protocol::parse_sync(body, std::strlen(body), &s2, err, sizeof(err));
    check(o == api_protocol::Outcome::Ok, "a sync carrying only a summary parses");
    check(!s2.changed && s2.summary_count == 2,
          "the summary arrives even when the catalog is unchanged");
    check(std::strcmp(s2.summary[0].resident_name, "Jörg") == 0 &&
              s2.summary[0].drinks == 7 && s2.summary[0].total_rappen == 1260,
          "drinks and totals survive");
    check(s2.summary[1].drinks == 2 && s2.summary[1].total_rappen == 0,
          "a resident who only drank free beer survives");
  }
  {
    // A negative count would underflow the unsigned tally and show a huge number.
    const char* body =
        "{\"ok\":true,\"revision\":1,\"products\":[],\"residents\":[],"
        "\"summary\":[{\"resident_id\":\"r1\",\"name\":\"X\","
        "\"drinks\":-5,\"total_rappen\":10}]}";
    api_protocol::SyncResult s2{};
    api_protocol::parse_sync(body, std::strlen(body), &s2, err, sizeof(err));
    check(s2.summary_count == 1 && s2.summary[0].drinks == 0,
          "a negative drink count is clamped rather than wrapping");
  }
  {
    const char* body =
        "{\"ok\":true,\"revision\":1,\"products\":[],\"residents\":[],"
        "\"summary\":[{\"name\":\"Nobody\",\"drinks\":3}]}";
    api_protocol::SyncResult s2{};
    api_protocol::parse_sync(body, std::strlen(body), &s2, err, sizeof(err));
    check(s2.summary_count == 0, "a summary row without a resident id is dropped");
  }
  {
    std::string big = "{\"ok\":true,\"revision\":1,\"products\":[],"
                      "\"residents\":[],\"summary\":[";
    for (int i = 0; i < settings::max_residents + 1; ++i) {
      if (i) big += ",";
      big += "{\"resident_id\":\"r\",\"name\":\"X\",\"drinks\":1}";
    }
    big += "]}";
    api_protocol::SyncResult s2{};
    check(api_protocol::parse_sync(big.c_str(), big.size(), &s2, err, sizeof(err)) ==
              api_protocol::Outcome::TooManyItems,
          "more summary rows than this build can hold is rejected");
  }

  {
    // A backend that has not been redeployed sends no summary at all. Reading
    // that as an empty one would wipe the stored history.
    const char* body =
        "{\"ok\":true,\"revision\":1,\"changed\":false,\"products\":[],"
        "\"residents\":[]}";
    api_protocol::SyncResult s2{};
    s2.has_summary = true;
    api_protocol::parse_sync(body, std::strlen(body), &s2, err, sizeof(err));
    check(!s2.has_summary, "an absent summary is reported as absent, not empty");
  }
  {
    // A genuinely empty summary is different: nobody has drunk anything yet.
    const char* body =
        "{\"ok\":true,\"revision\":1,\"products\":[],\"residents\":[],"
        "\"summary\":[]}";
    api_protocol::SyncResult s2{};
    api_protocol::parse_sync(body, std::strlen(body), &s2, err, sizeof(err));
    check(s2.has_summary && s2.summary_count == 0,
          "an empty summary is distinguished from a missing one");
  }

  std::printf("\nown consumption\n");
  {
    const size_t n = api_protocol::build_my_summary(req, sizeof(req), "tok",
                                                    "fridge-01", "r1");
    check(n > 0 && field(req, "action") == "mySummary" && field(req, "resident_id") == "r1",
          "the request names the action and the person");
  }
  {
    const char* body =
        "{\"ok\":true,\"resident_name\":\"Jörg\",\"drinks\":15,"
        "\"total_rappen\":2860,\"products\":["
        "{\"name\":\"Feldschlösschen\",\"drinks\":13,\"total_rappen\":2340},"
        "{\"name\":\"Gratisbier\",\"drinks\":2,\"total_rappen\":0}]}";
    api_protocol::MySummary m{};
    const auto o = api_protocol::parse_my_summary(body, std::strlen(body), &m, err,
                                                  sizeof(err));
    check(o == api_protocol::Outcome::Ok, "one person's breakdown parses");
    check(std::strcmp(m.resident_name, "Jörg") == 0 && m.drinks == 15 &&
              m.total_rappen == 2860,
          "the name and the grand total survive");
    check(m.product_count == 2 &&
              std::strcmp(m.products[0].name, "Feldschlösschen") == 0 &&
              m.products[0].drinks == 13 && m.products[0].total_rappen == 2340,
          "per-beer counts and totals survive");
    check(m.products[1].total_rappen == 0 && m.products[1].drinks == 2,
          "a free beer counts as drunk but costs nothing");
  }
  {
    // Someone who has never booked anything is a normal state, not an error.
    const char* body =
        "{\"ok\":true,\"resident_name\":\"Neu\",\"drinks\":0,"
        "\"total_rappen\":0,\"products\":[]}";
    api_protocol::MySummary m{};
    check(api_protocol::parse_my_summary(body, std::strlen(body), &m, err,
                                         sizeof(err)) == api_protocol::Outcome::Ok &&
              m.product_count == 0,
          "an empty breakdown parses");
  }
  {
    std::string big = "{\"ok\":true,\"drinks\":1,\"products\":[";
    for (int i = 0; i < api_protocol::kMaxSummaryProducts + 1; ++i) {
      if (i) big += ",";
      big += "{\"name\":\"X\",\"drinks\":1,\"total_rappen\":1}";
    }
    big += "]}";
    api_protocol::MySummary m{};
    m.product_count = 99;
    check(api_protocol::parse_my_summary(big.c_str(), big.size(), &m, err,
                                         sizeof(err)) ==
              api_protocol::Outcome::TooManyItems &&
              m.product_count == 99,
          "more beers than fit the screen is rejected without touching the result");
  }
  {
    const char* body = "{\"ok\":false,\"error\":\"Person fehlt\"}";
    api_protocol::MySummary m{};
    check(api_protocol::parse_my_summary(body, std::strlen(body), &m, err,
                                         sizeof(err)) ==
              api_protocol::Outcome::Rejected &&
              std::strcmp(err, "Person fehlt") == 0,
          "a refusal surfaces its reason");
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
