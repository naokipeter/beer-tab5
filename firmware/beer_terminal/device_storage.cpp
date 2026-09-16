#include "device_storage.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>

namespace device_storage {
namespace {

bool g_mounted = false;

// Long enough to be distinctive, short enough for LittleFS's name limit.
constexpr const char* kTempSuffix = ".tmp";

}  // namespace

bool begin() {
  // The second argument formats the partition if it cannot be mounted, which is
  // what a first boot needs. A failure here is not fatal: the caller falls back
  // to compiled-in defaults.
  g_mounted = LittleFS.begin(true);
  if (!g_mounted) {
    Serial.println("[storage] LittleFS mount failed; running without persistence");
    return false;
  }
  Serial.printf("[storage] LittleFS mounted, %u bytes free\n",
                static_cast<unsigned>(free_bytes()));
  return true;
}

bool mounted() { return g_mounted; }

size_t free_bytes() {
  if (!g_mounted) return 0;
  return LittleFS.totalBytes() - LittleFS.usedBytes();
}

bool read(const char* path, uint8_t* buf, size_t capacity, size_t* out_len) {
  if (!g_mounted || !path || !buf || !out_len) return false;
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return false;
  const size_t size = f.size();
  if (size == 0 || size > capacity) {
    f.close();
    return false;
  }
  const size_t got = f.read(buf, size);
  f.close();
  if (got != size) return false;
  *out_len = size;
  return true;
}

bool write(const char* path, const uint8_t* data, size_t len) {
  if (!g_mounted || !path || !data || len == 0) return false;

  char tmp[64];
  snprintf(tmp, sizeof(tmp), "%s%s", path, kTempSuffix);

  File f = LittleFS.open(tmp, FILE_WRITE);
  if (!f) return false;
  const size_t written = f.write(data, len);
  f.close();
  if (written != len) {
    LittleFS.remove(tmp);
    return false;
  }

  // rename() will not replace an existing file on LittleFS, so clear the target
  // first. The window between the two is why the temporary file exists: if power
  // is lost here the next boot finds no catalog and reseeds, rather than finding
  // a truncated one.
  LittleFS.remove(path);
  if (!LittleFS.rename(tmp, path)) {
    LittleFS.remove(tmp);
    return false;
  }
  return true;
}

bool remove(const char* path) {
  if (!g_mounted || !path) return false;
  return LittleFS.remove(path);
}

bool exists(const char* path) {
  return g_mounted && path && LittleFS.exists(path);
}

size_t size_of(const char* path) {
  if (!g_mounted || !path) return 0;
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return 0;
  const size_t n = f.size();
  f.close();
  return n;
}

bool append(const char* path, const uint8_t* data, size_t len) {
  if (!g_mounted || !path || !data || len == 0) return false;
  File f = LittleFS.open(path, FILE_APPEND);
  if (!f) return false;
  const size_t written = f.write(data, len);
  f.close();
  return written == len;
}

void list(const char* dir, void (*visit)(const char* name, void* ctx), void* ctx) {
  if (!g_mounted || !dir || !visit) return;
  File root = LittleFS.open(dir);
  if (!root || !root.isDirectory()) return;
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    if (!f.isDirectory()) {
      const char* full = f.name();
      const char* slash = strrchr(full, '/');
      visit(slash ? slash + 1 : full, ctx);
    }
    f.close();
  }
  root.close();
}

bool make_dir(const char* path) {
  if (!g_mounted || !path) return false;
  if (LittleFS.exists(path)) return true;
  return LittleFS.mkdir(path);
}

}  // namespace device_storage
