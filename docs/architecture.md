# Proposed structure — not implemented beyond milestone 1

Keep the Arduino sketch thin. Add modules under `firmware/beer_terminal/src/`
(Arduino CLI compiles this subtree), each with an explicit public header:

| Module | Responsibility / milestone |
|---|---|
| `app_state` | Explicit events/transitions, 3 |
| `display_ui` | LVGL display/touch adapters, screens and input widgets, 3 |
| `camera` | Official board camera initialization, borrowed frames and shutdown, 4 |
| `barcode_scanner` | Decoder interface, check digit and two-frame stability, 5 |
| `product_repository` | Mock then bounded local cache, 6 |
| `product_lookup` | Own DB first; optional OFF fallback, 7 |
| `wifi_manager` | C6 connection lifecycle and reconnect scheduling, 7 |
| `purchase_logger` | HTTPS submissions and response validation, 7 |
| `transaction_queue` | Durable pending record before sending, retries with same ID, 9 |
| `power_manager` | Verified rail shutdown and sleep/wake policy, 10 |
| `settings.h` | Resident IDs/names, device ID and admin options, centralized |
| `secrets.h` | Ignored endpoint URL and replaceable device token |
| `backend/` | Google Apps Script, setup and server-side validation, 8 |

States: SLEEPING, WAKING, SCANNING, LOOKING_UP, PRODUCT_FOUND, NEW_PRODUCT,
SELECTING_USER, SUBMITTING, SUCCESS, ERROR, ADMIN. Initial milestone 1 stays awake
and does not pretend to implement those states. Later errors have explicit
retry/cancel paths; admin changes require confirmation. New-product and admin
screens use integer-rappen entry plus an explicit Free option. Default UI: high
contrast, landscape, large resident buttons, minimal text. Real resident names
are required only when deploying the user-selection UI, not to build now.

Proposed sheets (headers in row 1):

- Products: `barcode,name,price_rappen,free,image_url,updated_at`
- Purchases: `transaction_id,timestamp,barcode,product_name,price_rappen,resident_id,resident_name,device_id`
- Residents: `resident_id,name,active`

Backend actions: `lookupProduct`, `createProduct`, `changePrice`, `recordPurchase`.
Validate barcode/check digit, bounded sanitized names, integer prices, free/price
consistency and resident/device authorization server-side. Keep auth tokens in
Script Properties; use an independently authorized admin operation for price edits.
Client TLS must validate certificates, have deadlines and handle Apps Script's
redirects without leaking tokens to arbitrary destinations. Exact API is milestone 7/8.

Generate a transaction ID once; persist the complete transaction before sending.
Backend checks and appends under one lock, checking the ID in Purchases itself
so a crash between append and response remains idempotent. Retries retain the ID;
remove locally only after validated acknowledgement. Define price-change conflict
and queue-full policies before enabling purchases. No volatile-only offline queue.

Proceed in the user's 12-milestone order; build at each step and document physical
checks. The camera prerequisite remains an explicit gate, even if simulated UI
work is approved. Stop for user confirmation before hardware-dependent milestones.
