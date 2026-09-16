# Milestone 8 — Apps Script, spreadsheet and the phone page

The backend the terminal has been talking to since milestone 7 now exists. It is
a single Apps Script web app over one spreadsheet, plus a management page for a
phone. Full setup is in [`../backend/README.md`](../backend/README.md).

Nothing in this milestone touches the firmware except one correction, below.

## What was added

| File | Role |
|---|---|
| `backend/apps-script/Config.gs` | Sheet names, column order, device limits, revision counter. |
| `backend/apps-script/Validate.gs` | Every input check. |
| `backend/apps-script/Data.gs` | All sheet reads and writes, so column order lives in one place. |
| `backend/apps-script/Api.gs` | `doPost`: sync, recordPurchase, voidPurchase. |
| `backend/apps-script/Admin.gs` | `doGet` and the functions the phone page calls. |
| `backend/apps-script/manage.html` | The phone page. |
| `tests/test_backend_validation.js` | Host test for the validators. |

`tools/test.sh` now parses every backend source with Node and runs those tests.

## A firmware correction

All eight barcodes in the compiled-in mock catalog had **invalid EAN-13 check
digits**. The backend validates check digits, so every purchase made against the
seed catalog would have been rejected with "Barcode ungueltig" — and the cause
would have looked like a networking fault. The seeds now carry correct check
digits, and the validator test asserts one of them alongside known-good EAN-13,
EAN-8 and UPC-A values.

## Two deployments, not one

The terminal has no Google login, so its deployment must accept anonymous
requests, execute as the owner and rely on the device token. The management page
edits prices, so its deployment requires a Google account **and executes as the
caller**: under "execute as me", `Session.getActiveUser().getEmail()` returns
empty for ordinary Gmail accounts, and `requireAdmin_` would then refuse
everyone. The cost is that each admin needs edit access to the spreadsheet.

One deployment cannot be both, because access is a per-deployment setting. So the
same project is deployed twice. `doGet` additionally refuses to serve the page
when there is no signed-in user, so the anonymous URL cannot reach it.

This revises what `docs/architecture.md` originally proposed — a single
deployment restricted to Google accounts — which would have locked the terminal
out of its own backend.

## Idempotency

`recordPurchase` looks up the transaction id and appends the row under one
`LockService` lock, reading the sheet rather than a cache. A device that retries
after a timeout that actually succeeded gets `{ok: true, duplicate: true}` and
exactly one row survives. `voidPurchase` is idempotent the same way: voiding an
already-voided purchase succeeds rather than erroring.

Undo marks `voided_at` instead of deleting, so the sheet keeps a trail of what
happened rather than quietly losing it.

## Verification

Host, no Google account needed:

    ./tools/test.sh

Parses all backend sources and exercises the validators: check digits against
known-good EAN-13, EAN-8 and UPC-A plus deliberately wrong ones; integer-only
prices; control-character stripping; image URLs restricted to `openfoodfacts.org`
including the lookalike `openfoodfacts.org.evil.com`; and token comparison
including empty and missing tokens.

This does **not** test deployment, Google authentication, the spreadsheet, or the
device-to-backend path. Those need the real thing.

## Firmware build after the correction

| Build | Result | Flash | Static RAM |
|---|---|---:|---:|
| `./tools/build.sh` (C++17), clean | PASS | 1,744,834 bytes | 61,568 bytes |

## Requires deployment, and then the Tab5

1. Follow `backend/README.md` and deploy both. Put the terminal URL and token
   into `secrets.h`.
2. **Management page**: open the management URL on a phone. Adding a beer,
   changing a price behind its confirmation, archiving and restoring must all
   work, and the shelf cap must refuse a ninth active beer.
3. **Anonymous guard**: open the *terminal* deployment URL in a browser. It must
   say "Kein Zugriff", not show the page.
4. **Sync**: the terminal should log `[sync] applied revision N`. Change a price
   on the phone and confirm the terminal picks it up within the sync interval.
5. **Purchase**: book a beer and confirm a row appears with the right rappen
   amount and resident.
6. **Idempotency**: this is the one worth doing deliberately. Cut the terminal's
   power mid-submission, then book the same beer again. The sheet must hold
   exactly one row.
7. **Undo**: confirm `voided_at` is set and the row is not deleted, and that the
   phone page's summary excludes it.

## Known limitations

- A failed purchase is still lost unless the user retries. Milestone 9.
- The management page has no resident editing UI yet, though `adminSaveResident`
  exists and is validated. Residents can be edited in the sheet directly.
- `lookupProduct`, `createProduct` as separate device-facing actions are not
  implemented: the terminal does not need them, and the phone page uses the
  admin functions instead. Milestone 11 adds manual EAN entry on the device.
