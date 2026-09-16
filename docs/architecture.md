# Proposed structure — catalog-first, camera parked

Keep the Arduino sketch thin. Add modules under `firmware/beer_terminal/src/`
(Arduino CLI compiles this subtree), each with an explicit public header.
The current small `display_ui.h/.cpp` diagnostic lives beside the sketch for
easy Arduino IDE access; replace its internals with the LVGL adapter in milestone 3.

## Direction change, 2026-09-16

The Tab5 camera is not reachable from Arduino: the P4 SDK ships the CSI/ISP
peripheral drivers but not the sensor stack, and that cannot be installed as a
library. Verified by compilation — see [`hardware-audit.md`](hardware-audit.md).

The product therefore becomes **catalog-first rather than scan-first**. A fridge
holds roughly 6–20 beer types, so tapping a large tile indexes that set faster
than aiming a camera, and works one-handed while standing. Barcodes remain the
primary key in the spreadsheet; the Tab5 simply never produces one. Barcode
capture moves to a phone, which has a working camera and a browser that can decode.

Chosen path: **phone management page as primary, on-device numeric keypad as the
no-phone fallback.** A USB HID scanner remains a later option; unblocking the
camera (SDK rebuild or ESP-IDF migration) remains a parked side track.

## Modules

| Module | Responsibility / milestone |
|---|---|
| `app_state` | Explicit events/transitions, 3 |
| `display_ui` | LVGL port: flush over M5GFX, touch input, tick source, 3 |
| `ui_screens` | One builder per state; rebuilt on transition, never per frame, 3 |
| `ui_fonts`, `ui_keyboard` | German-capable font subsets and QWERTZ layout, 3 |
| `catalog_layout` | Pure adaptive-grid geometry for both grids; host-tested, 3 |
| `catalog_codec` | On-disk format, versioned and CRC-checked; host-tested, 6 |
| `device_storage` | LittleFS mount and atomic writes, 6 |
| `product_catalog` | Bounded catalog with archive/restore, revision sync, 6 |
| `resident_directory` | Resident list from the backend, compiled-in fallback, 3/7 |
| `purchase_log` | Consumption tally behind the summary screen, 3/8 |
| `product_lookup` | Own DB first; optional OFF fallback for manual EAN entry, 7 |
| `wifi_manager` | C6 connection lifecycle, backoff, radio off when idle, 7 |
| `api_protocol` | Wire contract; no I/O, host-tested, 7 |
| `api_client` | One HTTPS request at a time on its own task, 7 |
| `backend` | Bridges the state machine and the network; applies syncs, 7 |
| `config.h` | Resolves secrets.h with empty fallbacks so builds never need it |
| `purchase_logger` | HTTPS submissions and response validation, 7 |
| `transaction_queue` | Durable pending record before sending, retries with same ID, 9 |
| `power_manager` | Verified rail shutdown and sleep/wake policy, 10 |
| `settings.h` | Resident IDs/names, device ID and admin options, centralized |
| `secrets.h` | Ignored endpoint URL and replaceable device token |
| `backend/` | Google Apps Script, `manage` page, setup and server-side validation, 8 |
| `camera`, `barcode_scanner` | **Parked.** Keep the decoder interface shape in mind so a USB HID scanner or a future camera can feed the same "a barcode arrived" event. |

## States

SLEEPING, WAKING, SELECTING_PRODUCT, LOOKING_UP, PRODUCT_FOUND,
SELECTING_ARCHIVED, CONFIRM_ARCHIVE, NEW_PRODUCT, SELECTING_USER, SUBMITTING,
UNDOING, SUCCESS, SUMMARY, ERROR, ADMIN.

Four states were added beyond the original list. UNDOING mirrors SUBMITTING so
the reversal has somewhere to wait for the backend. SELECTING_ARCHIVED and
CONFIRM_ARCHIVE exist because archiving replaced scanning as the way stock
turns over, and both are destructive enough to deserve their own screen rather
than a modal. SUMMARY is the consumption table reached from the confirmation.

`SCANNING` becomes `SELECTING_PRODUCT`: a paged grid of large tiles (name +
price), 8 per page. Tap a tile → PRODUCT_FOUND → resident buttons → submit.
`LOOKING_UP` survives for manual EAN entry in the admin flow. An explicit
"Other / not listed" tile enters NEW_PRODUCT, which records the purchase and
optionally registers the product. Errors have explicit retry/cancel paths;
admin changes require confirmation. New-product and admin screens use integer-rappen
entry plus an explicit Free option. Default UI: high contrast, landscape, large
resident buttons, minimal text. Real resident names are required only when
deploying the user-selection UI.

## Catalog grid layout

Panel is 720x1280 native (M5GFX `cfg.memory_width/height`), so **1280x720 landscape**
at the configured rotation 3. Reserve a 64 px header and a 72 px footer; the grid
area is 1280 x 584 less a 16 px margin, with 16 px gaps.

Tile count adapts to stock rather than paging a fixed grid:

    rows = max(1, floor(sqrt(N)))
    cols = ceil(N / rows)

N=1..3 give a single row of N; 4 gives 2x2; 5..6 give 2x3; 7..8 give 2x4; 9 gives 3x3.
A partial last row is centred at the same tile size — never stretched, so tile size
is constant within a screen. Above ~12 products revisit this; a scrolling list with a
letter index beats a grid once tiles fall below roughly 240 px.

The card switches orientation on the tile's own aspect ratio: photo left / text right
when wider than 1.35:1 (as at N=4, 616x284), photo on top otherwise. This keeps the
grid rule above intact while avoiding wasted space in wide tiles.

"Nicht gelistet" (enters NEW_PRODUCT) and "Abbrechen" live in the fixed footer, not
in the grid, so product count alone drives layout and the constant actions never move.

## Product photos

Tiles show name, price and a product photo. Open Food Facts serves pre-sized image
variants (`...front_XX.N.400.jpg`, plus 200 and 100), so the existing `image_url`
column stores the 400 px URL directly and nothing needs server-side resizing.
`esp_driver_jpeg` ships in the P4 SDK, so decode is hardware-accelerated.

The phone `manage` page resolves the OFF image at registration time and writes the
URL to the sheet. **The device does not yet fetch or render them**: `image_url` is
synced, validated and stored, but every tile shows the deterministic colour.

Displaying them needs four things that do not exist yet: `LV_USE_TJPGD` enabled,
image draw buffers redirected to PSRAM through `lv_draw_buf_get_image_handlers`,
a download path to a second TLS host streaming straight to LittleFS as
`/img/<key>.jpg`, and cache invalidation keyed on the product's `updated_at`.

The size of the stored variant is the trade-off. A 400 px image decodes to
320 KB of RGB565, more than the free internal RAM, so decoded buffers must live
in PSRAM whichever variant is chosen; a 200 px image decodes to 80 KB and is
sharp enough for every tile except the full-width single-beer one.

Constraints:

- **Coverage is the main risk.** OFF's Swiss beer coverage is patchy; expect a
  significant share of products with no usable photo. The fallback — a deterministic
  colour derived from the barcode plus the product initial — is a first-class design
  case, not a placeholder, and the `manage` page needs a photo-replacement upload.
- Photos vary in crop, rotation and aspect. Letterbox into the photo box on a neutral
  ground; never stretch to fill.
- `images.openfoodfacts.org` is a second TLS host besides the Apps Script endpoint.
  Pin both roots explicitly rather than shipping a full root store.
- OFF images are CC-BY-SA. Acceptable for a private fridge display; do not republish.

## Registration and removal

Primary — **phone page**: an HTML `manage` page served by the same Apps Script web
app, opened on a phone at the fridge. `BarcodeDetector` where available, ZXing-wasm
fallback otherwise; autofill name/image from Open Food Facts; price in rappen or Free.
Prefer deploying as "execute as me / access: anyone with a Google account" with an
email allowlist over a shared admin token.

Fallback — **on-device keypad**: reuse the numeric keypad required for price entry.
Enter 8 or 13 digits, validate length and check digit on the device, then
`lookupProduct`, falling back to Open Food Facts for the name.

Removal is **archive, never delete**. Products gains an `active` column;
`lookupProduct` still resolves archived barcodes so purchase history stays readable,
while the tile grid shows only `active=TRUE`.

Archiving is reachable where stock actually turns over, not buried in admin: the
resident-selection screen carries the household's names plus one **archive**
button, so taking a finished beer off the grid is two taps from the catalog.
It asks for confirmation first. Restoring is the mirror image — **Nicht gelistet**
offers the archived beers before the new-product form, because a returning beer
is far more common than a genuinely new one. At most
`max_active_products` (8) are on the grid at once; restoring past that is refused
and the screen says to archive something first.

## Sheets and backend

- Products: `barcode,name,price_rappen,free,image_url,active,updated_at`
- Residents drive the selection screen and are never compiled into the UI. The
  device falls back to `settings::default_residents` only until its first
  successful sync, and the admin screen states which source is in use.
- Purchases: `transaction_id,timestamp,barcode,product_name,price_rappen,resident_id,resident_name,device_id`
- Residents: `resident_id,name,active`

Actions: `sync`, `lookupProduct`, `createProduct`, `changePrice`,
`archiveProduct`, `restoreProduct`, `recordPurchase`, `voidPurchase`.

`sync` replaces the separate catalog and residents calls: it takes the device's
known revision and returns `revision`, `changed`, `products[]` and `residents[]`
in one round trip, because on a battery device the association costs more than
the payload. The exact request and response shapes are fixed in
[milestone 7](milestone-7.md) and implemented by `api_protocol`.

`voidPurchase` takes the original `transaction_id` and marks that row reversed
rather than deleting it, so the sheet keeps an auditable trail of what happened.
It must be idempotent on the same id for the same reason `recordPurchase` is:
the device may retry after a timeout that actually succeeded.

Validate barcode/check digit, bounded sanitized names, integer prices, free/price
consistency and resident/device authorization server-side. Keep auth tokens in
Script Properties; use an independently authorized admin operation for price edits.
Client TLS must validate certificates, have deadlines and handle Apps Script's
redirects without leaking tokens to arbitrary destinations.

`catalog` returns `{revision, products[]}`, revision bumped by any product write.
The device caches catalog and revision in NVS and requests `?action=catalog&since=<rev>`
on wake. Bound it: cap 64 products, fixed-size records, no allocation in the loop.
This is more robust than scanning was — an unknown barcode scanned offline is
unresolvable, whereas a cached tile always carries a name and price, so the whole
purchase flow works with Wi-Fi down.

Generate a transaction ID once; persist the complete transaction before sending.
Backend checks and appends under one lock, checking the ID in Purchases itself
so a crash between append and response remains idempotent. Retries retain the ID;
remove locally only after validated acknowledgement. Define price-change conflict
and queue-full policies before enabling purchases. No volatile-only offline queue.

## Revised milestone order

1. ~~Project skeleton and reproducible CLI build~~ done
2. ~~Display and touchscreen test~~ done
3. LVGL screen/state-machine prototype with a mock catalog grid
4. ~~Camera preview~~ **parked** (see hardware audit)
5. ~~Offline barcode decoding~~ **parked**
6. Catalog cache and bounded local repository
7. Wi-Fi and backend API client
8. Apps Script, spreadsheet and the phone `manage` page
9. Offline transaction queue and retry
10. Sleep/wake and peripheral power management
11. Admin price editing, archive/restore, manual EAN entry
12. End-to-end testing and documentation

Parking 4 and 5 removes the project's largest technical risk from the critical
path — ZXing on P4, the 153 KB grayscale ROI buffers, decoder heap and untested
ISP grayscale negotiation — and frees that memory for LVGL. Build at each step
and document physical checks. Stop for confirmation before hardware-dependent milestones.
