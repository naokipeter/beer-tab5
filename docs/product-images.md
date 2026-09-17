# Product images

Tiles show the Open Food Facts photo when one is cached, and the deterministic
colour with the product's initial when not. The fallback is permanent, not a
placeholder: Open Food Facts has no usable picture for a good share of Swiss beer.

## Why the first attempt crashed

The first version rebooted the device continuously. The cause was ownership, not
storage — LittleFS had 3.5 MB free and a photo is about 15 KB, so space was never
the constraint.

Each tile was given a freshly allocated `lv_image_dsc_t`, freed when the screen
was torn down, and the JPEG bytes behind it were freed with it. LVGL's
decoded-image cache keys on the **source pointer**, so its entries still referred
to that memory. The next screen allocated a new descriptor, often at the same
address, and LVGL scored a cache hit on freed memory.

Now `image_cache` owns both the descriptor and the bytes, and they keep the same
address for as long as the photo does. Screens borrow the pointer and never free
it. Buffers are released only when the photo actually changes or a sync replaces
the catalog, and `lv_image_cache_drop()` is called **before** the memory goes, not
after.

`LV_CACHE_DEF_SIZE` is back to 0 for now, so nothing is cached between redraws.
That costs a decode per redraw and is the deliberate conservative choice: the
cache is exactly what turned a lifetime bug into a crash. Turning it on is the
next step once this is confirmed stable.

## How it works

The `image_url` already travelled through sync, validation and storage. What was
missing was everything device-side.

**Keyed by the URL, not the barcode.** `/img/<hash>.jpg`, where the hash is FNV-1a
of the image URL. A product whose photo changes maps to a different file and is
refetched automatically — no timestamp comparison, no invalidation logic.
`prune()` deletes files no active product refers to any more.

**Downloaded at the lowest priority.** A fetch starts only when the link is up,
no backend request is running, and the transaction queue is empty. A photo must
never compete with a purchase for the single request slot. One download at a
time; a URL that fails is remembered for the rest of the boot rather than retried
on every pass.

**Streamed to flash, not to RAM.** `api_client::fetch_to_file` writes the body
straight to LittleFS through a `Stream` sink, capped at 96 KB so a wrong URL
cannot fill the volume. The file is deleted before each attempt, so a half-written
one is never appended to.

**Decoded by LVGL, into PSRAM.** `LV_USE_TJPGD` is enabled and decodes directly
from the compressed bytes — no LVGL filesystem driver needed.

The decode target is the reason for the one piece of LVGL surgery here: only the
**image** draw-buffer handlers are redirected to PSRAM, through
`lv_draw_buf_get_image_handlers()`. Fonts and the render buffers keep the default
allocator. A 200 px photo is 80 KB of RGB565 and a full screen is several hundred,
which internal RAM cannot spare beside TLS and LVGL's own working set.

**Letterboxed, never stretched.** LVGL 9.2 has no "contain" alignment, so the
scale factor is computed from the JPEG header via `lv_image_decoder_get_info` and
applied uniformly. Stretching a bottle to a square would look worse than no photo.

## Security

`api_protocol::image_source_allowed` restricts downloads to https on
`openfoodfacts.org` and `googleusercontent.com` — the second so a beer Open Food
Facts has no picture for can be given one by hand from Google Photos. The backend validates the URL too, but the device decides
for itself what it will connect to rather than trusting a value it was handed —
the same reasoning as the redirect check. Host-tested against suffix lookalikes,
userinfo smuggling and plain http.

## URL length

`image_url` is **256 bytes**. An Open Food Facts URL fits in about 100, but a
Google Photos link runs past 220, and the old 160-byte field truncated it into a
URL that could only fail — silently, since a failed download just leaves the
placeholder. Widening it changes the catalog record size, which the header
advertises, so stored files reject themselves and the catalog reseeds.

A caveat worth knowing: Google Photos links can rotate. If one stops working the
tile falls back to the colour placeholder and serial logs the failed download.

## Sizing

Images are linked at **200 px**. That is the deliberate choice: 80 KB decoded,
against 320 KB for a 400 px image. The only tile where the difference is visible
is the full-width one shown when a single beer is in the fridge.

## Verification

    ./tools/test.sh
    ./tools/build.sh

| Build | Result | Flash | Static RAM |
|---|---|---:|---:|
| `./tools/build.sh` (C++17), clean | PASS | 1,760,264 bytes | 87,840 bytes |

## Requires the physical Tab5

1. With photos linked in the sheet, serial should show `[img] fetching ...` then
   `[img] cached /img/<hash>.jpg, N bytes`, one at a time.
2. The tiles should show the photos after the next screen rebuild. Admin reports
   how many of the active products are cached.
3. **Power-cycle**: photos must come from flash without any download.
4. Change a photo's URL in the sheet. The new one must be fetched and the old file
   pruned.
5. Watch free heap during a screen rebuild with 8 tiles. This is the memory case
   with the least margin, and the one to report if anything looks wrong.
