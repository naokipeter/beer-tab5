# Milestone 9 — offline transaction queue and retry

A purchase is written to flash **before** anything is sent, and stays there until
the backend acknowledges it. Wi-Fi being down, the router being off or Apps
Script being slow no longer loses a drink, and no retry can record one twice.

## What was added

| File | Role |
|---|---|
| `transaction_queue.h/.cpp` | Bounded, persisted FIFO of purchases and reversals. |
| `catalog_codec` | `encode_queue` / `decode_queue`, same versioned CRC-checked header. |

`backend` drains the queue instead of taking purchases as arguments, and now
distinguishes two kinds of failure. `app_state` enqueues and flushes before the
first attempt. The confirmation says when a booking is stored but not yet sent;
the admin screen shows the depth.

## Unreachable is not a failure

The important distinction:

| Outcome | Meaning | Queue | What the user sees |
|---|---|---|---|
| `Success` | recorded, or a `duplicate` replay | popped | **Gebucht** |
| `Unreachable` | never reached the backend | **kept** | **Gebucht**, plus "wird nachgetragen" |
| `ServerError` | the backend broke: `ok:false` with `retry:true` | **kept** | **Gebucht**, "wird nachgetragen" |
| `Rejected` | the backend answered and refused | popped | the error, with retry |

A fault in the Apps Script — a missing function, a quota, a transient Sheets
failure — is not a verdict on the request, so it must not discard a drink. The
backend marks its own exceptions `retry: true`; only a refusal it actually means
comes back without that flag. Both look like `ok:false` on the wire, and treating
them alike cost a booking the first time a deployment went out incomplete.

Telling someone their beer failed because the router is down would be wrong:
it *is* booked, it is on flash, and it will go out. Only an outright rejection —
an unknown person, an invalid barcode — is something a human has to act on, and
retrying those bytes unchanged would fail identically, so the entry is dropped
rather than carried forever.

## Four outcomes of enqueueing, not two

A queued transaction is not the same as a request in flight, and conflating the
two produced a real bug: a purchase enqueued while a sync held the request slot
was reported as stored, the state machine then polled a result nobody had set to
`Pending`, read the *previous* attempt's error text and showed the error screen.

`enqueue_purchase` therefore returns which of four things happened:

| Outcome | Stored | Resolves by | Screen |
|---|---|---|---|
| `Watching` | yes | the backend's real answer | whatever it says |
| `Deferred` | yes | timer | **Gebucht**, "wird nachgetragen" |
| `Refused` | **no** | timer, forced to fail | the error |
| `NoBackend` | n/a | timer | **Gebucht** (prototype) |

`Refused` covers a full queue: nothing was stored, so claiming a booking would be
a lie. The previous code returned the same "false" for a full queue as for an
unconfigured device, which would have confirmed a drink that was never recorded
anywhere.

A stale message is also cleared when a resident is picked, so one attempt's error
can never be attributed to the next.

## Undo of something never sent

Undoing a purchase still sitting in the queue **removes that entry**. Nothing was
recorded, so there is nothing to reverse and no round trip to make. Only a
purchase that already reached the backend produces a `voidPurchase`.

That falls out of keeping the queue keyed by transaction id, and it is the case
that would otherwise leave a reversal for a purchase the server never saw.

## Capacity

64 entries. Seven people would have to drink about nine each during one outage to
fill it. When it does fill the terminal **refuses further purchases** with a
message to get it back on Wi-Fi.

Dropping the oldest entry was the alternative and is worse: every entry is a
drink somebody was already told was booked, so discarding one silently turns a
confirmation into a lie. Refusing is visible and recoverable.

## Ordering and durability

The queue is a plain array drained from the front, not a ring buffer, so the
on-disk order is the send order and stays readable after a crash. `flush()` runs
from the loop, so a flash write never delays a touch — except on enqueue, which
flushes immediately and deliberately: that write is the whole point.

A queue file that fails its CRC is rejected whole and reported loudly on serial.
It is the one file where losing data costs money, so it does not disappear
quietly.

## Verification

    ./tools/test.sh

The queue suite round-trips purchases, a free drink and a reversal; asserts that
the transaction id and price survive (which is what makes a retry safe), that
order is preserved, that a purchase cannot decode as a reversal, that a corrupted
queue is rejected whole rather than partly replayed, and that a queue blob is not
accepted as a catalog.

    ./tools/build.sh

| Build | Result | Flash | Static RAM |
|---|---|---:|---:|
| `./tools/build.sh` (C++17), clean | PASS | 1,750,178 bytes | 77,272 bytes |

Static RAM grew by about 15 KB: the queue plus its encode buffer, both static
rather than on the stack.

## Requires the physical Tab5

1. **Offline purchase.** Switch the router off. Book a beer: it must confirm with
   "wird nachgetragen", not an error. Admin shows one waiting.
2. **Automatic drain.** Switch the router back on. Within the backoff window the
   entry must reach the sheet without anyone touching the terminal.
3. **Power cut while queued.** Book offline, pull the power, restore it. Serial
   must report the entry still waiting, and it must arrive once the network does.
   This is the milestone's actual acceptance test.
4. **No double recording.** Book offline, restore the network, and while it drains
   pull the power again. Exactly one row must exist.
5. **Undo while queued.** Book offline, then undo. The entry must vanish from the
   queue and nothing must ever reach the sheet.
6. **Queue full.** Hard to reach honestly; lower `max_queued_transactions` to 2 in
   a test build to see the refusal.
