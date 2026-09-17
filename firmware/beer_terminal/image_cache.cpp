#include "image_cache.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>
#include "api_client.h"
#include "ui_lvgl.h"
#include "api_protocol.h"
#include "backend.h"
#include "device_storage.h"
#include "settings.h"
#include "transaction_queue.h"
#include "wifi_manager.h"

namespace image_cache {
namespace {

constexpr const char* kDir = "/img";
// One loaded buffer per tile the grid can show.
constexpr uint8_t kMaxLoaded = settings::max_active_products;

// One slot per product that can be on the grid. Both the descriptor and the
// bytes live here for as long as the photo does, so their addresses are stable.
struct Loaded {
  char key[16];
  uint8_t* data;
  lv_image_dsc_t dsc;
};
Loaded g_loaded[kMaxLoaded];
uint8_t g_loaded_count = 0;

bool g_fetching = false;
char g_fetch_path[64] = {};
// Products whose download already failed this boot, so a permanently broken URL
// is not retried on every pass.
char g_failed[settings::max_products][12];
uint8_t g_failed_count = 0;

uint32_t hash_url(const char* url) {
  uint32_t h = 2166136261u;
  for (const char* c = url; *c; ++c) {
    h ^= static_cast<uint8_t>(*c);
    h *= 16777619u;
  }
  return h;
}

void key_for(const product_catalog::Product& p, char* out, size_t cap) {
  snprintf(out, cap, "%08lx", static_cast<unsigned long>(hash_url(p.image_url)));
}

bool failed_before(const char* key) {
  for (uint8_t i = 0; i < g_failed_count; ++i) {
    if (strcmp(g_failed[i], key) == 0) return true;
  }
  return false;
}

void mark_failed(const char* key) {
  if (g_failed_count >= settings::max_products) return;
  snprintf(g_failed[g_failed_count], sizeof(g_failed[0]), "%s", key);
  ++g_failed_count;
}

}  // namespace

void begin() {
  g_loaded_count = 0;
  g_failed_count = 0;
  g_fetching = false;
  device_storage::make_dir(kDir);
}

void path_for(const product_catalog::Product& p, char* out, size_t capacity) {
  if (!p.image_url[0] || !api_protocol::image_source_allowed(p.image_url)) {
    if (capacity) out[0] = '\0';
    return;
  }
  char key[16];
  key_for(p, key, sizeof(key));
  snprintf(out, capacity, "%s/%s.jpg", kDir, key);
}

bool have(const product_catalog::Product& p) {
  char path[64];
  path_for(p, path, sizeof(path));
  return path[0] && device_storage::exists(path) && device_storage::size_of(path) > 0;
}

const void* source_for(const product_catalog::Product& p) {
  char key[16];
  if (!p.image_url[0] || !api_protocol::image_source_allowed(p.image_url)) {
    return nullptr;
  }
  key_for(p, key, sizeof(key));

  // Already loaded: hand back the very same pointer, which is what keeps LVGL's
  // cache valid.
  for (uint8_t i = 0; i < g_loaded_count; ++i) {
    if (strcmp(g_loaded[i].key, key) == 0) return &g_loaded[i].dsc;
  }
  if (g_loaded_count >= kMaxLoaded) return nullptr;

  char path[64];
  path_for(p, path, sizeof(path));
  if (!path[0]) return nullptr;
  const size_t size = device_storage::size_of(path);
  if (size == 0) return nullptr;

  // PSRAM: a screenful of photos is several hundred kilobytes, and internal RAM
  // is needed for TLS and the decoder's own working set.
  uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM));
  if (!buf) return nullptr;

  size_t read = 0;
  if (!device_storage::read(path, buf, size, &read) || read != size) {
    heap_caps_free(buf);
    return nullptr;
  }

  Loaded& slot = g_loaded[g_loaded_count++];
  snprintf(slot.key, sizeof(slot.key), "%s", key);
  slot.data = buf;
  memset(&slot.dsc, 0, sizeof(slot.dsc));
  slot.dsc.header.cf = LV_COLOR_FORMAT_RAW;
  slot.dsc.data = buf;
  slot.dsc.data_size = size;
  return &slot.dsc;
}

void release_all() {
  for (uint8_t i = 0; i < g_loaded_count; ++i) {
    // Drop the decoded entry before the bytes it points at disappear. Freeing
    // first is exactly the use-after-free that made this crash on boot.
    lv_image_cache_drop(&g_loaded[i].dsc);
    heap_caps_free(g_loaded[i].data);
  }
  g_loaded_count = 0;
}

void release(const product_catalog::Product& p) {
  char key[16];
  key_for(p, key, sizeof(key));
  for (uint8_t i = 0; i < g_loaded_count; ++i) {
    if (strcmp(g_loaded[i].key, key) != 0) continue;
    lv_image_cache_drop(&g_loaded[i].dsc);
    heap_caps_free(g_loaded[i].data);
    for (uint8_t j = i + 1; j < g_loaded_count; ++j) g_loaded[j - 1] = g_loaded[j];
    --g_loaded_count;
    return;
  }
}

void update(uint32_t) {
  if (!device_storage::mounted() || !backend::configured()) return;

  if (g_fetching) {
    const api_client::Status s = api_client::status();
    if (s == api_client::Status::Busy) return;
    if (s == api_client::Status::Done && api_client::http_code() >= 200 &&
        api_client::http_code() < 300 && api_client::fetched_bytes() > 0) {
      Serial.printf("[img] cached %s, %u bytes\n", g_fetch_path,
                    static_cast<unsigned>(api_client::fetched_bytes()));
    } else {
      Serial.printf("[img] download failed for %s\n", g_fetch_path);
      device_storage::remove(g_fetch_path);
      const char* slash = strrchr(g_fetch_path, '/');
      if (slash) {
        char key[16];
        snprintf(key, sizeof(key), "%.8s", slash + 1);
        mark_failed(key);
      }
    }
    api_client::reset();
    g_fetching = false;
    return;
  }

  // Photos are the lowest priority thing the radio does. They never start while
  // a purchase is waiting, a request is running, or the link is down.
  if (!wifi_manager::online()) return;
  if (backend::busy() || !transaction_queue::empty()) return;

  for (uint8_t i = 0; i < product_catalog::active_count(); ++i) {
    const product_catalog::Product* p = product_catalog::active_at(i);
    if (!p || !p->image_url[0]) continue;
    char key[16];
    key_for(*p, key, sizeof(key));
    if (failed_before(key)) continue;
    if (have(*p)) continue;

    path_for(*p, g_fetch_path, sizeof(g_fetch_path));
    if (!g_fetch_path[0]) continue;
    if (api_client::fetch_to_file(p->image_url, g_fetch_path)) {
      Serial.printf("[img] fetching %.60s\n", p->image_url);
      g_fetching = true;
    }
    return;
  }
}

namespace {

struct PruneCtx {
  char keep[settings::max_products][16];
  uint8_t count;
};

void prune_visit(const char* name, void* ctx) {
  PruneCtx* c = static_cast<PruneCtx*>(ctx);
  for (uint8_t i = 0; i < c->count; ++i) {
    char expected[20];
    snprintf(expected, sizeof(expected), "%s.jpg", c->keep[i]);
    if (strcmp(name, expected) == 0) return;
  }
  char path[64];
  snprintf(path, sizeof(path), "%s/%s", kDir, name);
  if (device_storage::remove(path)) Serial.printf("[img] pruned %s\n", name);
}

}  // namespace

void prune() {
  static PruneCtx ctx;
  ctx.count = 0;
  for (uint8_t i = 0; i < product_catalog::active_count() && ctx.count < settings::max_products;
       ++i) {
    const product_catalog::Product* p = product_catalog::active_at(i);
    if (!p || !p->image_url[0]) continue;
    key_for(*p, ctx.keep[ctx.count], sizeof(ctx.keep[0]));
    ++ctx.count;
  }
  device_storage::list(kDir, prune_visit, &ctx);
}

uint8_t cached_count() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < product_catalog::active_count(); ++i) {
    const product_catalog::Product* p = product_catalog::active_at(i);
    if (p && have(*p)) ++n;
  }
  return n;
}

bool busy() { return g_fetching; }

}  // namespace image_cache
