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
| `product_catalog.h/.cpp` | Fixed-capacity catalog with archive/restore: up to 8 active on the grid, 32 stored. Integer rappen, price formatting, deterministic no-photo tile colour. |
| `resident_directory.h/.cpp` | The resident list. Seeded from `settings::default_residents`, replaced wholesale by the backend in milestone 7. A malformed list is rejected rather than leaving the device unable to record a purchase. |
| `purchase_log.h/.cpp` | Per-resident drink and rappen tally behind the summary screen. |
| `display_ui.h/.cpp` | LVGL port: PSRAM draw buffers, flush through `M5.Display.pushImage`, touch through `M5.Touch`, `millis` as the tick source. |
| `ui_screens.h/.cpp` | One builder per state. Screens are rebuilt on transition only. |
| `font_de_*.c`, `ui_fonts.h`, `tools/generate-fonts.sh` | Montserrat subsets carrying the German and Swiss-French letters. LVGL's built-in fonts are ASCII-only, which is why umlauts were missing. |
| `ui_keyboard.h/.cpp` | German QWERTZ layout with umlaut keys, so a name like "Feldschlösschen" can be typed as well as displayed. |
| `ui_lvgl.h` | Includes LVGL and asserts major version 9, so an LVGL 8 sketchbook copy reports itself instead of producing dozens of rename errors. |
| `tests/test_catalog_layout.cpp`, `tools/test-layout.sh` | Host test for the grid rule. |

`settings.h` gained the screen metrics, the resident list, the theme and the
prototype's simulated latencies. Residents are placeholders and must be replaced
before deployment.

## Household changes

Seven residents, no guest entry — the host pays. The resident-selection screen
shows those seven plus an eighth **Bier archivieren** button, which is why the
grid rule lands on exactly 2 x 4 there. That layout follows the resident count,
so a backend list of a different size still lays out correctly; the host test
checks every count from 1 to `max_residents` for bounds and minimum tap size.

**Nicht gelistet** now offers the archived beers first and only falls through to
the new-product form when there is nothing to restore, or when you press
**Neues Bier anlegen**. The mock catalog therefore starts with two active beers,
matching the usual stock, and six archived ones so the restore flow has content.

The confirmation screen carries **Rückgängig** and **Übersicht anzeigen**.
Ignoring both returns to the catalog on its own; the summary table also returns
after 15 seconds, so the terminal never sits lit.

Undo is the whole reason the confirmation now dwells for 6 seconds rather than
2.5: that dwell *is* the undo window, and it has to be long enough to notice a
mis-tap and react. The reversal runs through its own UNDOING state so milestone 8
can put a real `voidPurchase` call where the simulated delay is, keeping the
original `transaction_id` — the backend matches the reversal to the row it
reverses. A failed undo retries the undo, never the submission before it, which
is what `undo_in_flight` in the context is for. The acknowledgement afterwards is
grey rather than green, offers nothing further, and clears in 2 seconds.

Undo reverses the purchase, not the catalog: a beer registered through the ad hoc
form stays on the grid, and is removed by archiving it.

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
| `./tools/build.sh` (C++17) | PASS | 906,794 bytes | 36,496 bytes |
| Board defaults, no compiler override (Arduino IDE C++20 equivalent) | PASS | 976,484 bytes | 30,240 bytes |
| `./tools/test-layout.sh` | PASS | all checks | host binary |

Static RAM excludes the two 1280 x 40 PSRAM draw buffers (102,400 bytes each),
LVGL's runtime heap and stacks. No inference about final memory fit should be
drawn from these figures.

## Fonts

LVGL's built-in Montserrat fonts contain ASCII plus a handful of symbols and no
Latin-1, so every umlaut rendered blank. `tools/generate-fonts.sh` builds four
subsets with `lv_font_conv` from the Montserrat TTF that ships inside the LVGL
library, adding `ÄÖÜäöüßÉéÈèÀàÂâÊêÎîÔôÛûÇç`, German quotes and an en dash.
The generated `font_de_*.c` files are committed because Arduino IDE has no build
step that could produce them.

Only `font_de_20` merges the LVGL symbol glyphs, because it is `LV_FONT_DEFAULT`
and widget internals such as the keyboard's control keys draw with it. All five
built-in Montserrat fonts are disabled in `lv_conf.h`. That is a net **saving**:
flash dropped from 977,398 to 905,578 bytes, since five full fonts with
FontAwesome merged at every size cost more than four targeted subsets.

Rendering the letters is only half of it — LVGL's default keyboard is a QWERTY
map with no umlaut keys, so `ui_keyboard` installs a QWERTZ layout with
`ü ö ä ß` on the letter rows. Its control array is length-checked against the
key maps with `static_assert`, because LVGL indexes the two in lockstep and a
mismatch would read past the end of the array at runtime.

## Arduino IDE library folder

Arduino IDE reads `~/Documents/Arduino/libraries`; `tools/build.sh` reads the
project-local `.arduino/user/libraries`. They are independent, and the sketchbook
held **lvgl 8.3.2**, whose API differs from 9.2.2 across most of this code.
Install lvgl 9.2.2 through the IDE Library Manager as well; `ui_lvgl.h` reports
the mismatch explicitly if that is missed.

Two things were checked rather than assumed, by pointing CLI builds at a
reproduction of the IDE's layout:

- No sketch in the sketchbook uses LVGL — the only matches are inside the
  library's own `examples/`. Upgrading it breaks nothing else there.
- A stale `libraries/lv_conf.h` for v8.3.2 sits beside the lvgl folder, the
  location LVGL 8 expected. It is **not** on the include path: builds with and
  without it produce byte-identical firmware (976,484 bytes), and the sketch-local
  `lv_conf.h` wins through `-DLV_CONF_INCLUDE_SIMPLE`. It can be deleted as
  tidy-up, but it changes nothing.

## Verification procedure

Host, no hardware:

    ./tools/test-layout.sh      # grid rule and bounds
    ./tools/build.sh            # firmware, pinned C++17

On the Tab5, after uploading (see README for the port-independent upload command):

1. The catalog shows two tiles side by side, each with a coloured placeholder,
   name and price.
2. Tap a tile **on its photo square**, not just its text. The whole tile is one
   target: `lv_obj_create` sets `LV_OBJ_FLAG_CLICKABLE` on every base object, so
   the photo panel used to swallow the press while the labels, which clear that
   flag themselves, passed it through. `make_panel` now clears it too. The header shows the product and price; seven resident buttons and
   `Bier archivieren` appear as a 2 x 4 grid.
3. Tap a name. A spinner shows for about 0.8 s, then a green `Gebucht` screen names
   the resident and product.
4. On that screen, press `Übersicht anzeigen`: the table lists each resident with
   a drink count and total. It returns on its own after 15 s, or on `Zurück`.
   Ignoring both buttons returns to the catalog after 6 s.
5. Book another drink and press `Rückgängig`. A spinner shows briefly, then a grey
   `Rückgängig gemacht` screen, which clears after 2 s. Open the summary again:
   that resident's count and total must be back to what they were. Pressing undo
   twice is impossible — the reversal screen has no buttons.
6. `Zurück` from the resident screen returns to the catalog without recording.
7. Tap a tile, then `Bier archivieren`. Confirm. The beer leaves the grid and the
   layout re-flows to a single full-width tile. `Abbrechen` on the confirmation
   must leave it in place.
8. `Nicht gelistet` now lists the archived beers. Tapping one restores it to the
   grid. `Neues Bier anlegen` reaches the ad hoc form: the name field raises the
   keyboard, the numeric pad composes a price capped at CHF 99.99, `C` clears,
   `Gratis` overrides the price, and `Weiter` is inert until a price or Gratis is set.
9. Restore beers until eight are active. The archived screen must then refuse
   further restores and say the fridge is full.
10. Long-press the header to open Admin. It reports the active and archived counts
   and whether residents came from the backend or the local fallback. Enable
   `Nachsten Fehler simulieren`, go back, and complete a purchase: it must land on
   the error screen with a retry that keeps the same transaction ID (serial log).
11. Serial at 115200 prints every transition as `[state] FROM -> TO`.

## Requires the physical Tab5

- **Colour order.** The flush callback treats LVGL's RGB565 as `lgfx::rgb565_t`.
  If colours appear inverted, that cast is the single line to change to `swap565_t`.
- **Touch accuracy against LVGL's hit testing**, especially near tile edges and on
  the keyboard, which milestone 2 validated only for raw coordinates.
- **Whether the tiles are genuinely thumb-sized** at arm's length in front of a
  fridge. With the usual two beers each tile is 616 x 552, but the resident grid
  is always 300 x 254 buttons; that is the tighter case to judge.
- **Whether the archive button belongs on the resident screen.** It sits beside
  seven names, so a mis-tap costs a confirmation screen rather than a purchase.
  Worth watching in real use.
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
- The host toolchain on this machine has a stale `CommandLineTools/usr/include/c++/v1`
  containing three files, which shadows the SDK's libc++ and breaks any host C++
  build. `tools/test-layout.sh` detects this and falls back to the SDK copy.
