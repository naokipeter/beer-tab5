// Host test for the persistence format. Build and run with tools/test-codec.sh.
#include "../firmware/beer_terminal/catalog_codec.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

product_catalog::Product make(const char* barcode, const char* name, int32_t rappen,
                              bool free_item, bool active, const char* url) {
  product_catalog::Product p{};
  std::snprintf(p.barcode, sizeof(p.barcode), "%s", barcode);
  std::snprintf(p.name, sizeof(p.name), "%s", name);
  p.price_rappen = rappen;
  p.free_item = free_item;
  p.active = active;
  std::snprintf(p.image_url, sizeof(p.image_url), "%s", url);
  return p;
}

bool same(const product_catalog::Product& a, const product_catalog::Product& b) {
  return std::strcmp(a.barcode, b.barcode) == 0 && std::strcmp(a.name, b.name) == 0 &&
         a.price_rappen == b.price_rappen && a.free_item == b.free_item &&
         a.active == b.active && std::strcmp(a.image_url, b.image_url) == 0;
}

}  // namespace

int main() {
  std::printf("catalog persistence format\n");

  std::vector<product_catalog::Product> in;
  in.push_back(make("7610807000016", "Feldschlösschen Original", 180, false, true,
                    "https://images.openfoodfacts.org/x/front_de.4.400.jpg"));
  in.push_back(make("7613300000088", "Turbinenbräu Gassenhauer", 0, true, false, ""));
  // A name and URL that exactly fill their fields, to catch an off-by-one in the
  // terminator handling.
  in.push_back(make("1234567890128", std::string(39, 'N').c_str(), 9999, false, true,
                    std::string(159, 'u').c_str()));

  std::vector<uint8_t> buf(catalog_codec::max_catalog_bytes());
  const size_t len = catalog_codec::encode_catalog(
      in.data(), static_cast<uint8_t>(in.size()), 42, 7, buf.data(), buf.size());
  check(len > 0, "encode returns a length");

  product_catalog::Product out[settings::max_products]{};
  uint8_t count = 0;
  uint32_t revision = 0;
  uint16_t generation = 0;
  check(catalog_codec::decode_catalog(buf.data(), len, out, settings::max_products,
                                      &count, &revision, &generation),
        "round trip decodes");
  check(count == in.size(), "record count survives");
  check(revision == 42, "revision survives");
  check(generation == 7, "seed generation survives");
  bool all_same = count == in.size();
  for (uint8_t i = 0; i < count && all_same; ++i) all_same = same(in[i], out[i]);
  check(all_same, "every field survives, including umlauts and full-width strings");

  // Corruption must be rejected, not half-applied.
  {
    std::vector<uint8_t> bad = buf;
    bad[len / 2] ^= 0x01;
    uint8_t c = 0;
    check(!catalog_codec::decode_catalog(bad.data(), len, out, settings::max_products,
                                         &c, nullptr, nullptr),
          "a single flipped payload bit is rejected");
  }
  {
    std::vector<uint8_t> bad = buf;
    bad[8] ^= 0x01;  // revision field, inside the CRC's coverage
    uint8_t c = 0;
    check(!catalog_codec::decode_catalog(bad.data(), len, out, settings::max_products,
                                         &c, nullptr, nullptr),
          "a flipped header bit is rejected");
  }
  {
    uint8_t c = 0;
    check(!catalog_codec::decode_catalog(buf.data(), len - 1, out,
                                         settings::max_products, &c, nullptr, nullptr),
          "a truncated blob is rejected");
  }
  {
    std::vector<uint8_t> bad = buf;
    bad[0] ^= 0xFF;
    uint8_t c = 0;
    check(!catalog_codec::decode_catalog(bad.data(), len, out, settings::max_products,
                                         &c, nullptr, nullptr),
          "a foreign file is rejected on its magic");
  }
  {
    std::vector<uint8_t> bad = buf;
    bad[4] = 99;  // unknown format version
    uint8_t c = 0;
    check(!catalog_codec::decode_catalog(bad.data(), len, out, settings::max_products,
                                         &c, nullptr, nullptr),
          "an unknown format version is rejected");
  }
  {
    // A blob holding more records than this build can hold must not overrun.
    uint8_t c = 0;
    check(!catalog_codec::decode_catalog(buf.data(), len, out, 1, &c, nullptr, nullptr),
          "a blob exceeding capacity is rejected");
  }
  {
    // A rejected decode must leave the caller's count untouched.
    uint8_t c = 123;
    catalog_codec::decode_catalog(buf.data(), len - 1, out, settings::max_products, &c,
                                  nullptr, nullptr);
    check(c == 123, "a rejected decode does not write out_count");
  }

  // Empty catalog is a legitimate state: every beer archived away.
  {
    std::vector<uint8_t> e(catalog_codec::max_catalog_bytes());
    const size_t n =
        catalog_codec::encode_catalog(in.data(), 0, 7, 1, e.data(), e.size());
    uint8_t c = 9;
    uint32_t rev = 0;
    check(n > 0 && catalog_codec::decode_catalog(e.data(), n, out,
                                                 settings::max_products, &c, &rev,
                                                 nullptr) &&
              c == 0 && rev == 7,
          "an empty catalog round trips");
  }

  // A buffer one byte too small must refuse rather than overrun.
  {
    std::vector<uint8_t> small(len - 1);
    check(catalog_codec::encode_catalog(in.data(), static_cast<uint8_t>(in.size()), 1,
                                        1, small.data(), small.size()) == 0,
          "encoding into too small a buffer refuses");
  }

  std::printf("\nresidents\n");
  resident_directory::Entry rin[3]{};
  std::snprintf(rin[0].id, sizeof(rin[0].id), "r1");
  std::snprintf(rin[0].name, sizeof(rin[0].name), "Naoki");
  std::snprintf(rin[1].id, sizeof(rin[1].id), "r2");
  std::snprintf(rin[1].name, sizeof(rin[1].name), "Miriam");
  std::snprintf(rin[2].id, sizeof(rin[2].id), "r3");
  std::snprintf(rin[2].name, sizeof(rin[2].name), "Jörg");

  std::vector<uint8_t> rbuf(catalog_codec::max_residents_bytes());
  const size_t rlen = catalog_codec::encode_residents(rin, 3, rbuf.data(), rbuf.size());
  resident_directory::Entry rout[settings::max_residents]{};
  uint8_t rcount = 0;
  check(rlen > 0 && catalog_codec::decode_residents(rbuf.data(), rlen, rout,
                                                    settings::max_residents, &rcount),
        "residents round trip");
  check(rcount == 3 && std::strcmp(rout[2].name, "Jörg") == 0,
        "resident names survive, including umlauts");

  // The generation travels in the header, so a device can tell that a stored
  // catalog descends from a seed that has since been corrected.
  {
    std::vector<uint8_t> g(catalog_codec::max_catalog_bytes());
    const size_t n = catalog_codec::encode_catalog(in.data(), 1, 0, 1, g.data(), g.size());
    uint8_t c = 0;
    uint32_t rev = 0;
    uint16_t gen = 0;
    catalog_codec::decode_catalog(g.data(), n, out, settings::max_products, &c, &rev,
                                  &gen);
    check(rev == 0 && gen == 1, "a never-synced catalog reports its seed generation");
  }

  // The two blob kinds must not be interchangeable.
  {
    uint8_t c = 0;
    check(!catalog_codec::decode_catalog(rbuf.data(), rlen, out, settings::max_products,
                                         &c, nullptr, nullptr),
          "a residents blob is not accepted as a catalog");
  }

  std::printf("\ntransaction queue\n");
  {
    transaction_queue::Entry q[3] = {};
    std::snprintf(q[0].transaction_id, sizeof(q[0].transaction_id), "fridge-01-abc-1");
    std::snprintf(q[0].barcode, sizeof(q[0].barcode), "7610807000016");
    std::snprintf(q[0].product_name, sizeof(q[0].product_name), "Feldschlösschen");
    std::snprintf(q[0].resident_id, sizeof(q[0].resident_id), "r1");
    std::snprintf(q[0].resident_name, sizeof(q[0].resident_name), "Jörg");
    q[0].price_rappen = 180;
    q[0].free_item = false;
    q[0].kind = transaction_queue::Kind::Purchase;

    std::snprintf(q[1].transaction_id, sizeof(q[1].transaction_id), "fridge-01-abc-2");
    std::snprintf(q[1].product_name, sizeof(q[1].product_name), "Gratisbier");
    q[1].price_rappen = 0;
    q[1].free_item = true;
    q[1].kind = transaction_queue::Kind::Purchase;

    std::snprintf(q[2].transaction_id, sizeof(q[2].transaction_id), "fridge-01-abc-3");
    q[2].kind = transaction_queue::Kind::Void;

    std::vector<uint8_t> qb(catalog_codec::max_queue_bytes());
    const size_t n = catalog_codec::encode_queue(q, 3, qb.data(), qb.size());
    transaction_queue::Entry back[settings::max_queued_transactions] = {};
    uint8_t qc = 0;
    check(n > 0 && catalog_codec::decode_queue(qb.data(), n, back,
                                               settings::max_queued_transactions, &qc),
          "the queue round trips");
    check(qc == 3, "every entry survives");
    check(std::strcmp(back[0].transaction_id, "fridge-01-abc-1") == 0 &&
              back[0].price_rappen == 180 && !back[0].free_item,
          "the transaction id and price survive, which is what makes a retry safe");
    check(std::strcmp(back[0].resident_name, "Jörg") == 0, "resident names survive");
    check(back[1].free_item && back[1].price_rappen == 0, "a free drink survives");
    check(back[2].kind == transaction_queue::Kind::Void, "a reversal stays a reversal");
    check(back[0].kind == transaction_queue::Kind::Purchase,
          "a purchase stays a purchase");

    // Order is the whole point of a queue: retries must not reshuffle drinks.
    check(std::strcmp(back[1].transaction_id, "fridge-01-abc-2") == 0 &&
              std::strcmp(back[2].transaction_id, "fridge-01-abc-3") == 0,
          "order is preserved");

    // Product changes travel through the same queue, so their kind must survive
    // too — and an unknown byte must fall back to Purchase, never to a reversal.
    transaction_queue::Entry k[3] = {};
    std::snprintf(k[0].transaction_id, sizeof(k[0].transaction_id), "t1");
    k[0].kind = transaction_queue::Kind::Archive;
    std::snprintf(k[1].transaction_id, sizeof(k[1].transaction_id), "t2");
    k[1].kind = transaction_queue::Kind::Restore;
    std::snprintf(k[2].transaction_id, sizeof(k[2].transaction_id), "t3");
    k[2].kind = transaction_queue::Kind::Purchase;
    std::vector<uint8_t> kb(catalog_codec::max_queue_bytes());
    const size_t kn = catalog_codec::encode_queue(k, 3, kb.data(), kb.size());
    uint8_t kc = 0;
    catalog_codec::decode_queue(kb.data(), kn, back,
                                settings::max_queued_transactions, &kc);
    check(kc == 3 && back[0].kind == transaction_queue::Kind::Archive &&
              back[1].kind == transaction_queue::Kind::Restore &&
              back[2].kind == transaction_queue::Kind::Purchase,
          "archive and restore survive as themselves");

    // A beer added at the fridge carries its price, which the create request
    // needs; the archive path does not.
    transaction_queue::Entry made[1] = {};
    std::snprintf(made[0].transaction_id, sizeof(made[0].transaction_id), "t9");
    std::snprintf(made[0].product_name, sizeof(made[0].product_name), "Hausbier");
    made[0].price_rappen = 250;
    made[0].kind = transaction_queue::Kind::Create;
    const size_t mn = catalog_codec::encode_queue(made, 1, kb.data(), kb.size());
    uint8_t mc = 0;
    catalog_codec::decode_queue(kb.data(), mn, back,
                                settings::max_queued_transactions, &mc);
    check(mc == 1 && back[0].kind == transaction_queue::Kind::Create &&
              back[0].price_rappen == 250 &&
              std::strcmp(back[0].product_name, "Hausbier") == 0,
          "a created product survives with its price");

    // An unknown kind must never become a reversal.
    std::vector<uint8_t> weird = kb;
    weird[catalog_codec::kHeaderBytes + catalog_codec::kQueueRecordBytes - 1] = 99;
    // The CRC now fails, which is the stronger guarantee: the blob is rejected.
    uint8_t wc = 7;
    check(!catalog_codec::decode_queue(weird.data(), mn, back,
                                       settings::max_queued_transactions, &wc) &&
              wc == 7,
          "a tampered kind byte fails the checksum rather than being guessed at");

    std::vector<uint8_t> bad = qb;
    bad[n / 2] ^= 0x01;
    uint8_t c2 = 42;
    check(!catalog_codec::decode_queue(bad.data(), n, back,
                                       settings::max_queued_transactions, &c2) &&
              c2 == 42,
          "a corrupted queue is rejected whole, not partly replayed");

    uint8_t c3 = 0;
    check(!catalog_codec::decode_queue(qb.data(), n, back, 1, &c3),
          "a queue longer than this build can hold is rejected");
    check(!catalog_codec::decode_catalog(qb.data(), n, out, settings::max_products, &c3,
                                         nullptr, nullptr),
          "a queue blob is not accepted as a catalog");

    // An empty queue is the normal steady state and must round trip.
    const size_t e = catalog_codec::encode_queue(q, 0, qb.data(), qb.size());
    uint8_t c4 = 9;
    check(e > 0 && catalog_codec::decode_queue(qb.data(), e, back,
                                               settings::max_queued_transactions, &c4) &&
              c4 == 0,
          "an empty queue round trips");
  }

  std::printf(g_failures ? "\n%d failure(s)\n" : "\nall checks passed\n", g_failures);
  return g_failures ? 1 : 0;
}
