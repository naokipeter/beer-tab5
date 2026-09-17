# Redraw speed

A screen change rebuilds its whole object tree and repaints 1280x720. Three
things made that slower than it needed to be.

## The draw buffer was in PSRAM

LVGL fills the draw buffer pixel by pixel — blending, font rasterising, one small
access after another — and PSRAM costs several times more per access than
internal RAM. That rendering, not the blit, dominated the time a screen took to
appear.

The buffer is now allocated with `MALLOC_CAP_INTERNAL`, falling back to PSRAM
with a warning on serial if it does not fit. Rendering happens internally and the
finished area is copied once into the panel's PSRAM framebuffer, which is the
cheaper way round: one bulk copy instead of tens of thousands of scattered
writes.

**A single buffer, not two.** The flush is synchronous — `pushImage` returns
before `lv_display_flush_ready` — so LVGL never renders into one buffer while the
other is in flight. The second buffer was 80 KB of internal RAM doing nothing.
32 lines at 1280 wide is 81,920 bytes.

## The loop slept a fixed 5 ms

Every iteration waited 5 ms whether or not there was work, which is latency added
to every touch. `lv_timer_handler` returns how long LVGL is content to wait; the
loop now sleeps that long, capped at 10 ms so the network and queue still get
their turn promptly, and at least 1 ms so it never spins.

## The refresh period was 33 ms

`LV_DEF_REFR_PERIOD` is now 16 ms. A rebuilt screen waited up to a full period
before anything was drawn.

## Measuring rather than guessing

`ui_screens::show` logs when building a screen takes more than 20 ms:

    [ui] SELECTING_USER built in 34 ms

That number is object construction only — the pixels appear one refresh later. If
it stays small and the screen still feels slow, the cost is in rendering, and the
next levers are a taller draw buffer, `LV_DRAW_SW_DRAW_UNIT_CNT` above 1 with
`LV_USE_OS`, or the P4's 2D acceleration.

## Verification

| Build | Result | Flash | Static RAM |
|---|---|---:|---:|
| `./tools/build.sh` (C++17) | PASS | 1,754,612 bytes | 79,360 bytes |

Static RAM is unchanged: the buffer is allocated at runtime either way.

On the device, serial should say

    [ui] draw buffer in internal RAM (81920 bytes)

If it says it fell back to PSRAM, internal RAM ran out and redraws will be as
slow as before — that is the line to report.
