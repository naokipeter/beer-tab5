# Consumption summary

## What the overview shows

**One person's own consumption, broken down by beer** — name, count and total per
beer, plus a grand total. Not everybody's figures: someone standing at the fridge
is answering "what do I owe", and the person is already known, because the screen
is reached from the confirmation of their own purchase.

The residents-by-products matrix belongs in the spreadsheet, where settling up
happens. A `QUERY` over `Purchases` keeps it a view of the truth rather than a
second copy of it.

## Fetched on demand, not on every sync

The breakdown is requested when the screen opens (`mySummary` with the
resident id), not carried on every sync. The full matrix would be
`residents x products` — up to 352 rows in the worst case, comfortably past the
8 KB response buffer — while one person's breakdown is at most a dozen rows.

It is also fresher: the screen opens seconds after a purchase, so the figures
include it.

Offline the backend's history is out of reach. The screen then reports what this
device still owes — the purchases for that person still in the queue — rather
than an empty table, since that number is known exactly.

The sync-level summary stays for the admin screen and as the cheap per-resident
total; the two come from the same `Purchases` sheet, so they cannot disagree.


The summary survives a reboot and shows what the **spreadsheet** holds, not only
what this device happened to see since it was switched on.

Milestone 3 built it as a local tally and flagged the limitation; this closes it.

## Baseline plus queue

    shown = the backend's figures + purchases still in the local queue

That formulation is what makes double counting impossible. A purchase is in
exactly one of two places: acknowledged by the backend, and therefore already in
its figures, or still in the queue, and therefore added on top. It can never be
in both, because it leaves the queue only when the backend confirms it.

The device keeps no independent tally at all any more. `record_locally()` is
empty: enqueueing a purchase already makes it visible, because the queue is part
of the view.

The baseline is persisted to `/summary.bin`, so a restart shows the last known
figures immediately rather than an empty table while the first sync runs.

## The summary is sent on every sync

Unlike the catalog, which is skipped when the revision matches, `summary` is
included in every response. It changes with every purchase, while the revision
only moves when a product is edited, so gating it on `changed` would leave the
terminal showing stale totals indefinitely.

After a purchase is acknowledged, the device clears its sync timer so fresh
figures are fetched right away. Without that the drink would leave the queue and
not yet be in the baseline, briefly under-reporting the total.

Server-side, `consumptionSummary_()` is shared by the device sync and the
management page, so the two cannot disagree. Voided purchases are excluded by
both.

## Absent is not empty

A backend that has not been redeployed answers without a `summary` field at all.
Reading that as an empty summary would replace the stored figures with nothing
and persist it — the terminal would then show only what is still in its own
queue, which looks exactly like the local-tally behaviour this change replaced.

`SyncResult::has_summary` records whether the field was present. When it is
absent the stored baseline is kept and serial says to redeploy the Apps Script.

The sync log line reports the summary row count, so whether the backend is
sending it can be read directly:

    [sync] applied revision 7: 3 products, 4 residents, 4 summary
    [sync] unchanged at revision 7, 4 summary row(s)
    [sync] response carries no summary; keeping the stored one. Redeploy the Apps Script.

## Verification

    ./tools/test.sh

The protocol suite asserts that a sync carrying only a summary parses, that the
summary arrives even when the catalog is unchanged, that drinks and totals
survive, that a negative drink count is clamped rather than wrapping an unsigned
tally into a huge number, that a row without a resident id is dropped, and that
more rows than this build can hold is rejected.

| Build | Result | Flash | Static RAM |
|---|---|---:|---:|
| `./tools/build.sh` (C++17), clean | PASS | 1,752,068 bytes | 78,728 bytes |

## Requires the physical Tab5

1. Book a beer, open the summary: the count must include it immediately.
2. Power-cycle and open the summary: the figures must still be there, before any
   sync has had time to run.
3. Book offline, then open the summary: the queued drink must be counted.
4. Bring the network back. Once the queue drains, the total must stay the **same** —
   the drink moves from the queue into the baseline, and must not be counted twice.
   That is the case worth watching.
5. Add a purchase row by hand in the sheet: it must appear on the terminal after
   the next sync, which is the point of taking the figures from the backend.
