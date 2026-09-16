# Milestone 7 — Wi-Fi and the backend client

The device now talks HTTPS to the Apps Script endpoint over the ESP32-C6: it
syncs the catalog and resident list, records purchases, and reverses them. The
server side is milestone 8; this milestone fixes the contract both ends implement.

Without `secrets.h` the firmware still builds and runs. Purchases are then
simulated locally exactly as in milestone 3, so the whole flow stays
demonstrable on an unconfigured device.

## What was added

| File | Role |
|---|---|
| `config.h` | Resolves `secrets.h` through `__has_include`, with empty fallbacks. |
| `api_protocol.h/.cpp` | The wire contract. No I/O, so it is exercised on the host. |
| `api_client.h/.cpp` | One HTTPS request at a time on its own FreeRTOS task. |
| `wifi_manager.h/.cpp` | Non-blocking association, backoff, radio powered down when idle. |
| `backend.h/.cpp` | Sits between the state machine and the network; applies syncs. |
| `tests/test_api_protocol.cpp` | Host test for the contract. |

`product_catalog` gained `replace_all`; `app_state` now drives real requests and
falls back to the simulated path only when no backend is configured. The admin
screen reports Wi-Fi state, signal and time since the last sync.

Dependency added: **ArduinoJson 7.4.3**, pinned in `tools/setup.sh` and checked
by `tools/build.sh`. Arduino IDE reads a different library folder, so
`tools/setup-ide.sh` installs the same pinned set into the sketchbook; without it
the IDE fails with a missing `ArduinoJson.h` while the CLI build succeeds. A hand-rolled encoder was the alternative and was rejected:
product names contain quotes and umlauts, which is exactly where hand-rolled
JSON escaping goes wrong.

## The contract

One endpoint, POST, JSON in and out, distinguished by `action`. Every response
is an object with a boolean `ok`, and carries `error` when false.

| Action | Request | Response |
|---|---|---|
| `sync` | `token`, `device`, `since` (known revision) | `revision`, `changed`, `products[]`, `residents[]` |
| `recordPurchase` | `token`, `device`, `transaction_id`, `barcode`, `name`, `price_rappen`, `free`, `resident_id` | `duplicate` |
| `voidPurchase` | `token`, `device`, `transaction_id` | — |

Products carry `barcode`, `name`, `price_rappen`, `free`, `image_url`, `active`;
residents carry `id` and `name`. Both lists are authoritative and replace what
the device holds.

`duplicate: true` on a purchase is a **success**, not an error: it means the
backend already had that transaction id. That is what makes a retry after a
timeout safe, and it is why the id is generated once and kept across retries.

The catalog and residents arrive in one round trip rather than two, because on a
battery device the association is the expensive part, not the payload.

## Security

The TLS chain is verified against the full Mozilla root store compiled into the
SDK (`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`), attached through the linker symbols
`_binary_x509_crt_bundle_start`/`_end`. Pinning a single Google root was the
alternative and was rejected: certificate rotation would brick the fridge.

`config.h` refuses a non-https endpoint with a `static_assert`, so a plaintext
URL fails the build rather than sending the device token in clear.

Apps Script answers a POST with a 302 to `script.googleusercontent.com`. The
token therefore lives in the **request body**, which a 302 does not resend, and
not in a header, which `HTTPClient` would resend to the redirect target.

Server-supplied strings are bounded to their fields and stripped of control
characters before they reach a label or the serial log.

## Concurrency

`api_client` runs the request on its own FreeRTOS task, pinned to the core the
Arduino loop does not use, with a 12 KB stack for mbedTLS. The loop polls a
status flag; status is published with a release store after the buffers are
written and read with an acquire load, so the UI never observes a half-written
response. The response buffer is 8 KB in PSRAM.

Nothing in the loop blocks: a slow or dead backend costs a spinner, not a frozen
screen.

## Verification

Host:

    ./tools/test.sh

The protocol suite round-trips a purchase whose product name contains a quote, a
newline and an umlaut, and asserts that each of these behaves: a request that
does not fit refuses rather than truncating (`serializeJson` silently truncates,
which would have posted invalid JSON — the test caught that); a missing `active`
flag defaults to visible rather than emptying the fridge; a nameless product is
dropped; an over-long name is truncated to its field; a response with more
products than this build can hold is rejected and leaves the result untouched;
control characters in a server message are stripped; `duplicate` reports success.

    ./tools/build.sh

| Build | Result | Flash | Static RAM |
|---|---|---:|---:|
| `./tools/build.sh` (C++17) | PASS | 1,724,860 bytes | 53,328 bytes |

Flash grew by 767 KB: TLS, the Wi-Fi stack and the 69 KB certificate bundle.
Still 26 % of the partition.

## Requires the physical Tab5, and milestone 8

Nothing here is verified end to end yet, because the endpoint does not exist.
What must be checked once it does:

1. **The C6 link itself.** This is the milestone's real risk and has never been
   exercised: the hardware audit flagged that the ESP-Hosted transport and the
   C6's own firmware are unverified. Serial should show `[wifi] connected` with
   an IP. If it does not, the C6 firmware is the first suspect — use M5Stack's
   official recovery instructions before assuming a code fault.
2. **TLS against the real endpoint.** A handshake failure appears as
   `[api] transport error`. Confirm the bundle is attached and the heap survives
   a handshake.
3. **The redirect.** Apps Script's 302 must be followed and the body read from
   the second host.
4. **Idempotency.** Pull the power mid-submission, then repeat the purchase. The
   sheet must hold exactly one row, and the device must show the duplicate path
   as success.
5. **Radio power-down.** The radio should drop about 30 s after the last request.
   Whether that actually reduces current draw is a milestone 10 measurement.
6. **Offline behaviour.** With Wi-Fi unplugged, a purchase must wait for the
   radio and then fail with `Kein WLAN` and a working retry — never a silent
   success. Milestone 9 replaces that failure with a queue.

## Known limitations

- A failed purchase is lost unless the user retries. That is milestone 9.
- `replace_all` carries over locally created products that have no barcode, so a
  sync cannot delete a beer somebody added at the fridge. Anything else local is
  overwritten by the server's view.
- The periodic sync is a 10-minute timer. Milestone 10 replaces it with a sync on
  wake, which is what a sleeping device actually needs.
