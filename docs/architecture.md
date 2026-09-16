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
| `display_ui` | LVGL display/touch adapters, screens and input widgets, 3 |
| `product_catalog` | Bounded cached catalog, tile grid model, revision sync, 6 |
| `product_lookup` | Own DB first; optional OFF fallback for manual EAN entry, 7 |
| `wifi_manager` | C6 connection lifecycle and reconnect scheduling, 7 |
| `purchase_logger` | HTTPS submissions and response validation, 7 |
| `transaction_queue` | Durable pending record before sending, retries with same ID, 9 |
| `power_manager` | Verified rail shutdown and sleep/wake policy, 10 |
| `settings.h` | Resident IDs/names, device ID and admin options, centralized |
| `secrets.h` | Ignored endpoint URL and replaceable device token |
| `backend/` | Google Apps Script, `manage` page, setup and server-side validation, 8 |
| `camera`, `barcode_scanner` | **Parked.** Keep the decoder interface shape in mind so a USB HID scanner or a future camera can feed the same "a barcode arrived" event. |

## States

SLEEPING, WAKING, SELECTING_PRODUCT, LOOKING_UP, PRODUCT_FOUND, NEW_PRODUCT,
SELECTING_USER, SUBMITTING, SUCCESS, ERROR, ADMIN.

`SCANNING` becomes `SELECTING_PRODUCT`: a paged grid of large tiles (name +
price), 8 per page. Tap a tile → PRODUCT_FOUND → resident buttons → submit.
`LOOKING_UP` survives for manual EAN entry in the admin flow. An explicit
"Other / not listed" tile enters NEW_PRODUCT, which records the purchase and
optionally registers the product. Errors have explicit retry/cancel paths;
admin changes require confirmation. New-product and admin screens use integer-rappen
entry plus an explicit Free option. Default UI: high contrast, landscape, large
resident buttons, minimal text. Real resident names are required only when
deploying the user-selection UI.

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
while the tile grid shows only `active=TRUE`. Admin long-press on a tile →
confirmation, the same pattern as price changes. Stock returns often; restore is one tap.

## Sheets and backend

- Products: `barcode,name,price_rappen,free,image_url,active,updated_at`
- Purchases: `transaction_id,timestamp,barcode,product_name,price_rappen,resident_id,resident_name,device_id`
- Residents: `resident_id,name,active`

Actions: `catalog`, `lookupProduct`, `createProduct`, `changePrice`,
`archiveProduct`, `restoreProduct`, `recordPurchase`.

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
