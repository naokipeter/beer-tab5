# Tab5 beer terminal

Milestone 3: the LVGL interface and the explicit state machine, driven by a mock
catalog of eight beers. Milestones 1 and 2 are confirmed working on the device.
There is no camera, networking, backend deployment, offline queue or sleep
implementation yet.

The Tab5 camera is not reachable from the Arduino framework — verified by
compilation, see [the hardware audit](docs/hardware-audit.md). Barcode capture
therefore moves to a phone management page, and the terminal itself is
catalog-first: residents tap a product tile rather than scanning. See
[the architecture proposal](docs/architecture.md) and
[milestone 3](docs/milestone-3.md).

## Build

### Arduino IDE

Open `firmware/beer_terminal/beer_terminal.ino`. Install M5Stack board package
3.3.9, M5Unified 0.2.22, M5GFX 0.2.29 and lvgl 9.2.2 using the IDE's
Boards/Library Managers. LVGL reads the sketch-local `lv_conf.h`; the
`build_opt.h` beside the sketch supplies `-DLV_CONF_INCLUDE_SIMPLE` for that.

**The IDE and the CLI use different library folders.** Arduino IDE reads your
sketchbook (`~/Documents/Arduino/libraries`), while `tools/build.sh` reads the
project-local `.arduino/user/libraries`. Installing or upgrading a library for
one does not affect the other. LVGL in particular renamed most of its API
between 8 and 9, so an older sketchbook copy produces dozens of
"not declared in this scope" errors; `ui_lvgl.h` turns that into a single
explicit message. In the IDE, open Library Manager, find lvgl, and select
version 9.2.2 from the version dropdown.
Select M5Tab5 and the connected port under Tools; enable PSRAM and USB CDC On
Boot, select Hardware CDC and JTAG for USB Mode, and select the chip variant
matching the device. Click Verify or Upload. Serial Monitor uses 115200 baud.
The sketch accepts C++17 or newer, including the board package's default C++20;
no custom language flags are needed in Arduino IDE.

### Arduino CLI

Tested on Apple Silicon macOS with Arduino CLI **1.1.1**, official
**m5stack:esp32@3.3.9**, **M5Unified@0.2.22**, **M5GFX@0.2.29**, **lvgl@9.2.2**.
Application and library compilation explicitly uses GNU C++17. Core prebuilt
ESP-IDF libraries retain their upstream compilation settings.

```sh
cd /Users/nlpeter/Sites/tab5-beer-terminal
./tools/setup.sh
./tools/build.sh
./tools/verify-examples.sh
```

On another computer, change the `cd` path. Install Arduino CLI 1.1.1 from
[Arduino's official release](https://github.com/arduino/arduino-cli/releases/tag/v1.1.1)
and place it on PATH, or set `ARDUINO_CLI` to its executable. The wrapper also
finds the CLI bundled with Arduino IDE on macOS. It prints the version during setup.
Setup installs pinned dependencies through the official package/library indexes;
initial installation requires internet and several GB of free disk space.

All writable CLI data, libraries and downloads live under ignored `.arduino/`;
build products live under ignored `build/`. On this machine only, existing board
packages are reused through `.arduino/data/packages`, a symlink to the installed
Arduino15 packages. Cached official library ZIPs were extracted locally. A fresh
checkout uses ordinary project-local package installation instead. Setup refuses
to install a missing core through the shared symlink.

The full board selection is:

```text
m5stack:esp32:m5stack_tab5:ChipVariant=prev3,PSRAM=enabled,USBMode=hwcdc,CDCOnBoot=cdc,PartitionScheme=default
```

`prev3` is the official board default, **not a claim about your device's silicon**.
Before uploading, determine chip revision from the device/flash tool. For silicon
v3.00 or newer, build with `CHIP_VARIANT=postv3 ./tools/build.sh` and use the matching
variant when uploading. Do not force-flash a binary rejected for chip revision.
No serial port is stored in configuration.

## Fonts

The UI draws with Montserrat subsets that include German and Swiss-French
letters; LVGL's built-in fonts are ASCII-only. They are committed as
`firmware/beer_terminal/font_de_*.c`. Regenerate with `./tools/generate-fonts.sh`
after changing the glyph set — it needs Node and the installed lvgl library.

## Host checks

`./tools/test-layout.sh` builds and runs the catalog grid geometry on the Mac —
no Tab5 and no Arduino toolchain needed. It asserts the layout for one to eight
products, that tiles stay inside the margins and clear of the header and footer,
and that a partial last row is centred.

## Physical verification — milestone 3

Upload as below, then walk the procedure in
[docs/milestone-3.md](docs/milestone-3.md): the eight-tile catalog, a purchase
through to the green confirmation, the cancel paths, the ad hoc product screen,
and the simulated-failure retry reached by long-pressing the header. Serial at
115200 prints every transition as `[state] FROM -> TO`.

The open hardware questions are colour order in the flush callback, touch
accuracy against LVGL hit testing, redraw latency, and whether a 300 x 268 tile
is genuinely thumb-sized in front of a fridge.

## Physical verification — milestone 2

In Arduino IDE, reopen `firmware/beer_terminal/beer_terminal.ino` and click Upload
using the same settings that worked for milestone 1. No new libraries are needed.
Turn the device 180 degrees from its previous position. The text should now be
upright. Tap the four blue corner buttons: only the touched corner should turn
green and show OK. After all four, the status should read "All corners OK".
Drag a finger inside the central outlined area: the cyan dot should follow it
without mirroring or offset. Tap "Start again" to clear the results and repeat.
Serial Monitor at 115200 baud reports startup dimensions and touch availability.
The on-screen sample counter should keep increasing. Drag for at least 10 seconds;
if tracking stops, note whether Samples still increases, whether Contact says yes,
and whether x/y change. Report orientation, corner response and dot alignment before the
LVGL milestone; this is the requested hardware-observation gate.

The rotation is centralized in `settings.h` (`display_rotation = 3`, previously 1).
M5GFX automatically transforms touch coordinates with the display rotation.
This test remains awake; it does not test sleep or wake from touch.

### Optional CLI upload

1. Connect Tab5 over a USB data cable. Run `./tools/arduino.sh board list`.
2. Set `TAB5_PORT` to its reported port, and `CHIP_VARIANT` to `prev3` or `postv3`.
3. Build and upload explicitly:

```sh
CHIP_VARIANT="$CHIP_VARIANT" ./tools/build.sh
./tools/arduino.sh upload \
  --fqbn "m5stack:esp32:m5stack_tab5:ChipVariant=$CHIP_VARIANT,PSRAM=enabled,USBMode=hwcdc,CDCOnBoot=cdc,PartitionScheme=default" \
  --port "$TAB5_PORT" --input-dir "build/$CHIP_VARIANT/beer_terminal" \
  firmware/beer_terminal
./tools/arduino.sh monitor --port "$TAB5_PORT" --config baudrate=115200
```

4. Verify the touch test described above and PSRAM detection. If the initial
   serial message is missed while USB enumerates, reset with the monitor open.
5. Report screen output, PSRAM, chip revision and any resets. Display and touch
   examples have separate build directories; compile success does not verify
   real display revisions or touch calibration.

Stop here for confirmation of milestone 2 before further hardware-dependent work.
The firmware stays awake: battery current and wake latency have not been measured.

## Files added

- `firmware/beer_terminal/`: sketch, state machine, catalog, LVGL port and screens,
  `lv_conf.h`, centralized settings, secrets example.
- `tests/`, `tools/test-layout.sh`: host test for the adaptive catalog grid.
- `arduino-cli.yaml`, `tools/`: official package URL, pinned setup, build and example checks.
- `docs/`: hardware/API findings, proposed architecture, compilation evidence.
- `backend/README.md`: reserved backend scope for milestone 8.
- `.gitignore`: excludes credentials, dependencies and build products.

Never commit `secrets.h` or `wifi_secrets.h`. No credentials are needed to build
milestone 1. Future endpoint/token configuration belongs in the ignored secrets
file; Wi-Fi provisioning is separate. No service-account credentials will be used.
