# Milestone 6 — catalog cache and local repository

The catalog and the resident list now survive a reboot. Milestones 4 and 5 stay
parked, so this follows milestone 3 directly. There is still no networking: the
catalog is seeded from the compiled-in mock on first boot, and milestone 7
replaces that seed with a fetch from the backend.

## What was added

| File | Role |
|---|---|
| `catalog_codec.h/.cpp` | The on-disk format. Free of Arduino and LittleFS, so it is exercised on the host. |
| `device_storage.h/.cpp` | LittleFS mount and atomic file writes. |
| `tests/test_catalog_codec.cpp`, `tools/test.sh` | Host tests. `test.sh` replaces `test-layout.sh` and runs both suites. |

`product_catalog` gained `flush`, `dirty`, `revision` and `set_revision`;
`resident_directory` gained `flush`. The sketch mounts storage before either and
flushes both from `loop`. The admin screen reports free space, the catalog
revision and whether anything is still unsaved.

## Format

The volume is the 3.5 MB `spiffs` partition of the default table, mounted as
LittleFS. `/catalog.bin` and `/residents.bin` hold fixed-size little-endian
records behind a 20-byte header: magic, format version, record size, revision,
count and a CRC32 over the whole blob with its own field zeroed.

The layout is written field by field rather than as a `memcpy` of the in-memory
struct, so a compiler padding or field-order change cannot silently reinterpret
stored data. The header advertises its record size, so a build with different
field widths rejects another build's file instead of misreading it.

A blob that fails any check — magic, version, record size, length, CRC, or a
count beyond this build's capacity — is rejected **whole**. Nothing is
half-loaded, and a rejected decode does not even write the caller's count. The
catalog then reseeds; the resident list falls back to `settings::default_residents`.

Writes go to a temporary file which is then renamed over the target, so power
loss during a write leaves the previous version intact rather than a truncated
one. The one unprotected window is between removing the old file and completing
the rename, because LittleFS will not rename onto an existing file; losing power
exactly there means the next boot finds nothing and reseeds, which is recoverable,
rather than finding a half-written file, which is not.

Flushing happens in `loop`, not in the UI event that made the change, so a flash
write never delays a touch response. A failed write leaves the dirty flag set and
retries rather than dropping the change silently.

## Verification

Host, no hardware — this is where the format is actually tested:

    ./tools/test.sh

It round-trips a catalog including umlauts and strings that exactly fill their
fields, then asserts that each of these is rejected: a single flipped payload
bit, a flipped header bit, a truncated blob, a foreign magic, an unknown format
version, a record count beyond capacity, and a residents blob offered as a
catalog. It also checks that an empty catalog round-trips and that encoding into
too small a buffer refuses rather than overruns.

    ./tools/build.sh

| Build | Result | Flash | Static RAM |
|---|---|---:|---:|
| `./tools/build.sh` (C++17) | PASS | 957,650 bytes | 44,116 bytes |

Static RAM grew by 7,612 bytes: the two encode/decode buffers are static rather
than stack-allocated, because the catalog blob is 7,028 bytes and the loop task's
stack is not the place for it.

## Requires the physical Tab5

1. **First boot.** Serial should report the LittleFS mount and free space, then
   `[catalog] saved 8 products`. If the mount fails the admin screen says so in
   red and the device still runs from the seed.
2. **Persistence.** Archive a beer, wait a moment, then power-cycle. The beer must
   still be archived and the grid must re-flow accordingly. This is the milestone's
   actual acceptance test.
3. **Ad hoc products persist.** Add one through `Nicht gelistet`, reboot, confirm
   it is still on the grid.
4. **Admin reports storage.** Free space, revision 0, and `gesichert` shortly
   after any change.
5. **To retest a first boot**, erase the flash: Arduino IDE's
   "Erase All Flash Before Sketch Upload", or `esptool.py erase_flash`. Uploading
   a sketch alone does not clear the LittleFS partition.

The wear question is open: the catalog is rewritten whole on every change. With a
handful of changes a week that is irrelevant, but it is worth revisiting if the
backend sync in milestone 7 ends up rewriting on every poll. Only write when the
revision actually changed.
