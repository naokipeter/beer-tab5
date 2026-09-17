# Milestone 11 — managing beers from the terminal

## Step 1: archiving reaches the backend

**The archive button never worked.** `product_catalog::archive()` flipped the flag
on the device and persisted it locally, and nothing was ever sent. The next sync
called `replace_all()`, which replaces the catalog with the spreadsheet's copy,
and the beer came back — within ten minutes at the latest.

It went unnoticed because archiving was done from the phone page instead. This
was a plain omission: the state machine was built without the server round trip
it needs.

Archiving and restoring now travel through the **transaction queue**, the same
path as purchases: written to flash first, retried until acknowledged, so a
change made without Wi-Fi is not lost. `Kind` gains `Archive` and `Restore`;
older queue files still decode, since they only ever contain `Purchase` or `Void`.

### Creating a beer had the same gap

`product_catalog::add_product()` was local-only too, so a beer added through
**Nicht gelistet** never reached `Products`. No money was lost — `recordPurchase`
does not require the product to exist, so the drink was booked correctly — but
the beer lived on that one terminal: invisible on the phone page, impossible to
re-price or archive there, and gone if the flash was cleared.

It now queues a `createProduct` before the purchase that follows it, so the sheet
gains the product first. The server treats a repeat as an update of the row it
already made, so a retry cannot produce a duplicate.

### A sync must not undo a queued change

While a product change is waiting, the server's catalog is stale by definition —
it has not seen the change. Applying it would take the beer off the grid, put it
back at the next sync, then remove it again once the queue drained: the terminal
visibly fighting itself.

`apply_sync()` therefore keeps the local catalog whenever
`transaction_queue::has_product_changes()` is true. The summary is still applied,
since it is unaffected.

### Who may do it

`setActive` and `changePrice` are open to anyone holding the **device token**,
not restricted to an admin. This is the fridge changing, several times a week,
by whoever is standing in front of it. Every change lands in the sheet with a
timestamp, which is where it can be reviewed.

That is a deliberate trade, made explicitly: a household of seven that knows each
other pays more for a lock than it gains.

### Prices and history

`Purchases` records `price_rappen` at the time of purchase, so changing a price —
for a promotion, say — never rewrites what anyone already owes. Past drinks keep
the price they were bought at. That is what makes a temporary price sane.

`setProductActive_` and `setProductPrice_` are shared by the device API and the
management page, so the two cannot enforce different rules.

## Verification

    ./tools/test.sh

The queue suite asserts that `Archive` and `Restore` survive encoding as
themselves, and that an unrecognised kind byte still decodes as a `Purchase` —
never as a reversal.

| Build | Result | Flash | Static RAM |
|---|---|---:|---:|
| `./tools/build.sh` (C++17) | PASS | 1,756,148 bytes | 79,352 bytes |

## Requires the physical Tab5

1. Archive a beer. It leaves the grid **and** `active` goes FALSE in the sheet.
1b. Add a beer through `Nicht gelistet`. A row must appear in `Products` with the
   price entered, and the beer must be visible on the phone page.
2. Wait past a sync. It must stay archived — this is what was broken.
3. Restore it from `Nicht gelistet`. `active` goes TRUE again.
4. Archive with the router off: it leaves the grid, the admin screen shows one
   waiting, and the sheet updates once the network returns.
5. During step 4, confirm the beer does **not** reappear when a sync runs while
   the change is still queued.

## Still to come in this milestone

- The **Bier verwalten** screen replacing the archive button: edit the price
  and/or archive, from the same place.
- Manual EAN entry.
- A resident editing UI; `adminSaveResident` exists but nothing calls it, so
  residents are still maintained directly in the sheet.
