#pragma once
#include <stddef.h>
#include <stdint.h>

// Persistent storage on the board's LittleFS partition (3.5 MB, `spiffs` in the
// default partition table). Holds the catalog and resident list now, and the
// cached Open Food Facts images later.
//
// Every call is safe when the volume failed to mount: reads report no data and
// writes report failure, so the device still runs from its compiled-in defaults
// rather than refusing to start.
namespace device_storage {

bool begin();
bool mounted();

// Reads at most `capacity` bytes. Returns false if absent, unreadable, or larger
// than the buffer, in which case `out_len` is untouched.
bool read(const char* path, uint8_t* buf, size_t capacity, size_t* out_len);

// Writes via a temporary file and a rename, so a power cut during the write
// leaves the previous version intact rather than a half-written one.
bool write(const char* path, const uint8_t* data, size_t len);

bool remove(const char* path);
bool exists(const char* path);
size_t size_of(const char* path);

// Appends to a file, creating it if needed. Used to stream a download to flash
// without holding it in RAM first.
bool append(const char* path, const uint8_t* data, size_t len);

// Calls `visit` for every file directly inside `dir`, with the bare name.
void list(const char* dir, void (*visit)(const char* name, void* ctx), void* ctx);
bool make_dir(const char* path);

// Free space in bytes, for the serial log and later the image cache budget.
size_t free_bytes();

}  // namespace device_storage
