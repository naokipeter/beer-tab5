# Milestone 2 — rotated display and touch validation

The user confirmed milestone 1 uploaded successfully. Their requested orientation
is 180 degrees from the original landscape screen: rotation 1 becomes rotation 3.

Changes: `settings.h` centralizes rotation; the sketch calls the new
`display_ui.h/.cpp` diagnostic module; README gives Arduino IDE-first checks.
Four 240x112 corner targets turn green on a valid contact inside them. A status
counter tracks progress. The centre area displays a finger-following dot, with
repainting limited to 20 Hz. Start again clears the test. The official M5GFX
`getTouch` API is sampled every 16 ms; it includes display coordinate rotation.
No application objects or strings are dynamically allocated in the input loop.
LVGL remains milestone 3.

Compilation on 2026-09-16, pinned dependencies unchanged, `ChipVariant=prev3`:

| Build | Result | Flash | Static RAM |
|---|---|---:|---:|
| `./tools/build.sh` (C++17) | PASS | 539,524 bytes | 27,712 bytes |
| Board defaults, no compiler override (Arduino IDE C++20 equivalent) | PASS | 539,560 bytes | 27,712 bytes |

Local logs: `/private/tmp/tab5-m2-cpp17.log`, `/private/tmp/tab5-m2-ide.log`.
No upload was performed by the agent. No claim of touch calibration or physical
orientation correctness is made from compilation alone.

## Device check before milestone 3

1. Reopen the same sketch in Arduino IDE and upload with the settings that worked.
2. Turn the device 180 degrees: all text should be upright.
3. Tap each corner: only that corner should change; all four should yield All corners OK.
4. Drag in the centre: the cyan dot should follow the finger without offset/mirroring.
5. Tap Start again and repeat. Check for resets or flicker.

Report these observations and confirm continuation. This pause follows the
original requirement to confirm hardware-dependent milestones. Camera compilation
remains separately blocked as documented in the hardware audit; no scanner,
network, purchase logging or sleep functionality is implied by this diagnostic.

## Touch reliability revision

User observed missed taps and dragging stopping after about one second. The
exact hardware-level cause is not yet confirmed. The diagnostic now uses
M5GFX's official rotation-aware `M5.Display.getTouch` directly, at 16 ms intervals,
instead of M5Unified gesture states. `M5.update()` is omitted in this diagnostic
to avoid a second consumer reading the same controller. Corner tests accept
ongoing contact, reset is latched until release, and release requires 64 ms
without a contact. This tolerates brief missing reports but cannot repair a
driver that stops returning coordinates. No GPIO, controller registers, panel
timings or vendor library sources were changed.

The screen shows Samples, Contact and coordinates. Samples must advance even
with no touch. If dragging fails, record whether Samples advances, Contact is
yes/no, and coordinates change. Keep the drag inside the outlined centre area
(the dot deliberately disappears outside it). Test slow drags and a held finger
followed by movement for at least 10 seconds. Test with Serial Monitor closed
as well as open; periodic serial output has been removed from the input loop.

Both builds passed on 2026-09-16: C++17 539,438 bytes flash; IDE-default C++20
539,476 bytes flash; both 27,720 bytes static RAM. Logs:
`/private/tmp/tab5-touch-fix17.log`, `/private/tmp/tab5-touch-fix20.log`.
Physical reliability remains pending user verification before further milestones.
