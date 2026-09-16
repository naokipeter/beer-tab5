# Milestone 3 — LVGL screens and the state machine

Implements the explicit state machine and every screen it can reach, driven by a
compiled-in mock catalog of eight beers. No camera, networking, backend, queue or
sleep implementation yet; those stay in their later milestones.

## What was added

| File | Role |
|---|---|
| `firmware/beer_terminal/lv_conf.h` | Generated from lvgl 9.2.2 `lv_conf_template.h`. Differences: CLIB malloc/string/sprintf, logging on, Montserrat 20/28/32/48 enabled, default font 20. |
| `firmware/beer_terminal/build_opt.h` | `-DLV_CONF_INCLUDE_SIMPLE`, so the sketch-local `lv_conf.h` is the one LVGL uses for both the sketch and the library. |
| `app_state.h/.cpp` | The eleven states, the events between them, the purchase context and a transaction ID generated once per attempt. Timed transitions are non-blocking. |
| `catalog_layout.h/.cpp` | Adaptive grid geometry. No LVGL or Arduino dependency, integer-only, so it is testable on the host. |
| `product_catalog.h/.cpp` | Fixed-capacity catalog (max 8), integer rappen, price formatting, and the deterministic no-photo tile colour. |
| `display_ui.h/.cpp` | LVGL port: PSRAM draw buffers, flush through `M5.Display.pushImage`, touch through `M5.Touch`, `millis` as the tick source. |
| `ui_screens.h/.cpp` | One builder per state. Screens are rebuilt on transition only. |
| `ui_lvgl.h` | Includes LVGL and asserts major version 9, so an LVGL 8 sketchbook copy reports itself instead of producing dozens of rename errors. |
| `tests/test_catalog_layout.cpp`, `tools/test-layout.sh` | Host test for the grid rule. |

`settings.h` gained the screen metrics, the resident list, the theme and the
prototype's simulated latencies. Residents are placeholders and must be replaced
before deployment.

## Grid rule, as tested

`rows = max(1, floor(sqrt(n)))`, `cols = ceil(n / rows)`:

| Products | Grid | Tile | Card |
|---:|---|---|---|
| 1 | 1 x 1 | 1248 x 552 | horizontal |
| 2 | 1 x 2 | 616 x 552 | vertical |
| 3 | 1 x 3 | 405 x 552 | vertical |
| 4 | 2 x 2 | 616 x 268 | horizontal |
| 5 | 2 x 3 | 405 x 268 | horizontal |
| 6 | 2 x 3 | 405 x 268 | horizontal |
| 7 | 2 x 4 | 300 x 268 | vertical |
| 8 | 2 x 4 | 300 x 268 | vertical |

The host test also asserts that every tile stays inside the margins and clear of
the header and footer, that a partial last row is centred to within a pixel, and
that an empty catalog does not produce degenerate geometry.

## Compilation

2026-09-16, pinned dependencies, `ChipVariant=prev3`:

| Build | Result | Flash | Static RAM |
|---|---|---:|---:|
| `./tools/build.sh` (C++17) | PASS | 972,760 bytes | 30,240 bytes |
| Board defaults, no compiler override (Arduino IDE C++20 equivalent) | PASS | 972,796 bytes | not captured |
| `./tools/test-layout.sh` | PASS | all checks | host binary |

Static RAM excludes the two 1280 x 40 PSRAM draw buffers (102,400 bytes each),
LVGL's runtime heap and stacks. No inference about final memory fit should be
drawn from these figures.

## Verification procedure

Host, no hardware:

    ./tools/test-layout.sh      # grid rule and bounds
    ./tools/build.sh            # firmware, pinned C++17

On the Tab5, after uploading (see README for the port-independent upload command):

1. The catalog shows eight tiles as 2 x 4, each with a coloured placeholder, name
   and price. `Turbinenbrau Gassenhauer` reads `Gratis` in green.
2. Tap a tile. The header shows the product and price; six resident buttons appear.
3. Tap a name. A spinner shows for about 0.8 s, then a green `Gebucht` screen names
   the resident and product, and after 2 s the catalog returns.
4. `Zuruck` from the resident screen returns to the catalog without recording.
5. `Nicht gelistet` opens the ad hoc product screen. Tapping the name field raises
   the keyboard; the numeric pad composes a price capped at CHF 99.99; `C` clears;
   `Gratis` overrides the price. `Weiter` is inert until a price or Gratis is set.
6. Long-press the header to open Admin. Enable `Nachsten Fehler simulieren`, go back,
   and complete a purchase: it must land on the error screen with a retry that keeps
   the same transaction ID (visible in the serial log).
7. Serial at 115200 prints every transition as `[state] FROM -> TO`.

## Requires the physical Tab5

- **Colour order.** The flush callback treats LVGL's RGB565 as `lgfx::rgb565_t`.
  If colours appear inverted, that cast is the single line to change to `swap565_t`.
- **Touch accuracy against LVGL's hit testing**, especially near tile edges and on
  the keyboard, which milestone 2 validated only for raw coordinates.
- **Whether the tiles are genuinely thumb-sized** at arm's length in front of a
  fridge. The 2 x 4 tile is 300 x 268; this is the layout's real acceptance test.
- **Redraw latency.** Partial render with two 1280 x 40 PSRAM buffers is a starting
  point, not a measured choice. If screen rebuilds feel slow, the levers are buffer
  size, buffer placement in internal RAM, `LV_USE_OS`/`LV_DRAW_SW_DRAW_UNIT_CNT`,
  and DMA in the flush callback.
- **PSRAM draw-buffer allocation succeeding at boot.** The sketch reports failure on
  serial and on the panel rather than continuing with a broken display.

## Notes and limitations

- Screens allocate LVGL objects when built. This happens on state transitions only,
  never inside the main loop, and `lv_obj_clean` releases the previous screen first.
- The submission is simulated by a timer in `app_state`. The real HTTPS client and
  the offline queue arrive in milestones 7 and 9; the transaction ID and the retry
  path are already shaped for them.
- Admin lists prices read-only. Editing, archive and restore are milestone 11.
- Umlauts are avoided in UI strings for now; the Montserrat subsets and the text
  encoding for German labels are worth settling before the strings multiply.
- The host toolchain on this machine has a stale `CommandLineTools/usr/include/c++/v1`
  containing three files, which shadows the SDK's libc++ and breaks any host C++
  build. `tools/test-layout.sh` detects this and falls back to the SDK copy.
