# Backend — Google Apps Script

The terminal talks to a single Apps Script web app backed by one spreadsheet.
No Google service-account key exists and none belongs on the device: the script
runs as **you**, and the terminal authenticates with a shared token you generate.

Source is in [`apps-script/`](apps-script/). The wire contract it implements is
fixed in [`../docs/milestone-7.md`](../docs/milestone-7.md).

## Spreadsheet

Create one spreadsheet. The script creates the sheets and header rows on first
use, so you do not have to — but these are the columns it expects:

**Products**

| barcode | name | price_rappen | free | image_url | active | updated_at |
|---|---|---|---|---|---|---|

`price_rappen` is an integer: 240, never 2.40. `free` and `active` are TRUE or
FALSE. A row whose `active` is anything other than FALSE counts as on the shelf,
so a blank cell in a hand-edited sheet cannot empty the fridge.

**Purchases**

| transaction_id | timestamp | barcode | product_name | price_rappen | free | resident_id | resident_name | device_id | voided_at |
|---|---|---|---|---|---|---|---|---|---|

Undo sets `voided_at` rather than deleting the row, so the sheet keeps an
auditable trail. `resident_name` and `price_rappen` are copied in at the time of
purchase, so renaming a person or changing a price never rewrites history.

**Residents**

| resident_id | name | active |
|---|---|---|

The terminal shows these plus one archive button, so keep it to 11 or fewer.

## Setup

1. Create the spreadsheet. From it, **Extensions → Apps Script**.
2. Create the files from [`apps-script/`](apps-script/) in the editor, keeping the
   names: `Config.gs`, `Validate.gs`, `Data.gs`, `Api.gs`, `Admin.gs` and
   `manage.html`. Paste `appsscript.json` into the manifest (show it with
   **Project Settings → Show "appsscript.json"**).
3. **Project Settings → Script Properties**, add:

   | Property | Value |
   |---|---|
   | `DEVICE_TOKEN` | the shared secret, e.g. from `openssl rand -base64 24` |
   | `ADMIN_EMAILS` | comma-separated Google accounts allowed to manage products |
   | `SPREADSHEET_ID` | optional; only needed if the script is not bound to the sheet |

4. Deploy **twice**, from the same project. This is not optional: one deployment
   cannot serve both, because the access setting is per deployment and the two
   callers authenticate differently.

   | Deployment | Execute as | Who has access | Used by |
   |---|---|---|---|
   | Terminal | **Me** | **Anyone** | the Tab5, which has no Google login |
   | Management | **User accessing the web app** | **Anyone with a Google account** | your phone |

   The management deployment must execute as the *user*, not as you. Under
   "execute as me", `Session.getActiveUser().getEmail()` comes back empty for
   ordinary Gmail accounts, so `requireAdmin_` would refuse everyone including
   you. Executing as the caller also means each admin needs edit access to the
   spreadsheet — share it with them, or keep yourself as the only admin.

5. Put the **terminal** deployment's `/exec` URL and the same `DEVICE_TOKEN` into
   `firmware/beer_terminal/secrets.h` (copy `secrets.example.h`). Open the
   **management** deployment's URL on your phone and bookmark it.

Updating the script later: use **Deploy → Manage deployments → edit → New
version** on each. Creating a *new* deployment mints a new URL, which means
editing `secrets.h` again.

## Why two deployments

The terminal cannot sign in to Google, so its deployment must accept anonymous
requests and rely on the token. The management page must not be anonymous — it
edits prices — so its deployment requires a Google account and executes as the
caller, which is what makes `Session.getActiveUser().getEmail()` return a real
address for `requireAdmin_` to check against `ADMIN_EMAILS`.

`doGet` guards the page as well: on the anonymous deployment
`Session.getActiveUser().getEmail()` is empty, so that URL serves "Kein Zugriff"
rather than the management interface.

## What the server validates

The device is on a kitchen wall and its token could leak, so nothing is trusted:

- the token, compared without an early exit on the first differing byte
- EAN-13, EAN-8 and UPC-A **check digits** — the firmware's own seed barcodes had
  to be corrected because of this check
- integer rappen only; floats, negatives and absurd values are rejected, and
  `free` must agree with a zero price
- names trimmed, stripped of control characters, bounded to the terminal's field
- image URLs must be https **and** on `openfoodfacts.org`; a lookalike host such
  as `openfoodfacts.org.evil.com` is rejected
- the resident must exist and be active
- the shelf cap, so the terminal's grid is never sent more than it can show

Responses distinguish a refusal from a fault. A validation failure returns
`{ok: false, error: ...}` and the terminal drops the transaction, because
retrying it unchanged would fail identically. An **exception** inside the script
returns `{ok: false, retry: true, ...}` and the terminal keeps the transaction
queued — a broken or half-deployed script must not cost anyone a drink.

`recordPurchase` checks the transaction id and appends under one `LockService`
lock, and the check reads the sheet rather than a cache. A retry after a timeout
therefore returns `{ok: true, duplicate: true}` instead of a second row.
`voidPurchase` is idempotent for the same reason.

## Management page

Served to your phone by the management deployment. It lists what is in the fridge
and what is archived, changes prices behind a confirmation, archives and restores,
and adds a beer.

Barcodes are read with the browser's own `BarcodeDetector` (Android Chrome) and
require the same code in two consecutive frames before the field is filled.
**iOS Safari does not implement it**, so there the number is typed instead; the
page says so rather than appearing broken. No scanning library is loaded from a
third party — this page is served by your Apps Script and should not pull in
someone else's code.

Product names and images come from Open Food Facts, fetched by the phone rather
than the server, so it costs no Apps Script quota and carries no credentials.
Coverage of Swiss beer is patchy; type the name when nothing comes back.

## Testing without Google

    ../tools/test.sh

Parses every backend source with Node and exercises the validators — check
digits, prices, text sanitising, image-URL host matching and token comparison —
against known-good EAN-13, EAN-8 and UPC-A values. That runs on your Mac; it does
not test deployment, authentication or the sheet itself.
