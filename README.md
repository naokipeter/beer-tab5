# Tab5 beer terminal

Milestone 2: display and touchscreen verification, rotated 180 degrees from the
original landscape orientation. Milestone 1 upload worked on the user's device.
There is no scanner, purchasing UI, networking, backend deployment or sleep implementation yet.
See [the hardware audit](docs/hardware-audit.md) for the camera blocker and
[the architecture proposal](docs/architecture.md) for subsequent milestones.

## Build

### Arduino IDE

Open `firmware/beer_terminal/beer_terminal.ino`. Install M5Stack board package
3.3.9, M5Unified 0.2.22 and M5GFX 0.2.29 using the IDE's Boards/Library Managers.
Select M5Tab5 and the connected port under Tools; enable PSRAM and USB CDC On
Boot, select Hardware CDC and JTAG for USB Mode, and select the chip variant
matching the device. Click Verify or Upload. Serial Monitor uses 115200 baud.
The sketch accepts C++17 or newer, including the board package's default C++20;
no custom language flags are needed in Arduino IDE.

### Arduino CLI

Tested on Apple Silicon macOS with Arduino CLI **1.1.1**, official
**m5stack:esp32@3.3.9**, **M5Unified@0.2.22**, **M5GFX@0.2.29**.
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

## Physical verification — milestone 2

In Arduino IDE, reopen `firmware/beer_terminal/beer_terminal.ino` and click Upload
using the same settings that worked for milestone 1. No new libraries are needed.
Turn the device 180 degrees from its previous position. The text should now be
upright. Tap the four blue corner buttons: only the touched corner should turn
green and show OK. After all four, the status should read "All corners OK".
Drag a finger inside the central outlined area: the cyan dot should follow it
without mirroring or offset. Tap "Start again" to clear the results and repeat.
Serial Monitor at 115200 baud reports dimensions, touch availability and each
corner hit. Report orientation, corner response and dot alignment before the
LVGL milestone; this is the requested hardware-observation gate.

The rotation is centralized in `settings.h` (`display_rotation = 3`, previously 1).
M5Unified automatically transforms touch coordinates with the display rotation.
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

4. Verify the touch test described above, serial heartbeat every five seconds,
   and PSRAM detection. If the initial
   serial message is missed while USB enumerates, reset with the monitor open.
5. Report screen output, PSRAM, chip revision and any resets. Display and touch
   examples have separate build directories; compile success does not verify
   real display revisions or touch calibration.

Stop here for confirmation of milestone 2 before further hardware-dependent work.
The firmware stays awake: battery current and wake latency have not been measured.

## Files added

- `firmware/beer_terminal/`: minimal sketch, centralized settings, secrets example.
- `arduino-cli.yaml`, `tools/`: official package URL, pinned setup, build and example checks.
- `docs/`: hardware/API findings, proposed architecture, compilation evidence.
- `backend/README.md`: reserved backend scope for milestone 8.
- `.gitignore`: excludes credentials, dependencies and build products.

Never commit `secrets.h` or `wifi_secrets.h`. No credentials are needed to build
milestone 1. Future endpoint/token configuration belongs in the ignored secrets
file; Wi-Fi provisioning is separate. No service-account credentials will be used.
