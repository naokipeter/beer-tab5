# Milestone 2 — rotated display and touch validation

The user confirmed milestone 1 uploaded successfully. Their requested orientation
is 180 degrees from the original landscape screen: rotation 1 becomes rotation 3.

Changes: `settings.h` centralizes rotation; the sketch calls the new
`display_ui.h/.cpp` diagnostic module; README gives Arduino IDE-first checks.
Four 240x112 corner targets turn green on a touch-down inside them. A status
counter tracks progress. The centre area displays a finger-following dot, with
repainting limited to roughly 30 Hz. Start again clears the test. Event processing
runs each loop and does not allocate application objects or strings dynamically.
M5Unified's `Touch_Class.cpp` calls the display's `convertRawXY`, so no manual
coordinate mirroring is applied. LVGL remains milestone 3.

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
