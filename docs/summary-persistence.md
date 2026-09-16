# Consumption summary

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

## Verification

    ./tools/test.sh

The protocol suite asserts that a sync carrying only a summary parses, that the
summary arrives even when the catalog is unchanged, that drinks and totals
survive, that a negative drink count is clamped rather than wrapping an unsigned
tally into a huge number, that a row without a resident id is dropped, and that
more rows than this build can hold is rejected.

| Build | Result | Flash | Static RAM |
|---|---|---:|---:|
| `./tools/build.sh` (C++17), clean | PASS | 1,751,900 bytes | 78,728 bytes |

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
