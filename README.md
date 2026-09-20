# beer-tab5

A beer tally for a fridge door, on an M5Stack Tab5. Tap the beer, tap your name,
done. Purchases go to a Google Sheet through a small Apps Script backend.

## How it works

The fridge holds a handful of beers, so the terminal shows them as large tiles
rather than asking you to scan anything. The grid adapts to how many there are:
one beer fills the screen, two split it, eight make a 2×4 grid.

There is **no barcode scanning on the device**. The Tab5 camera is not reachable
from the Arduino framework — the SDK ships the MIPI-CSI and ISP drivers but no
sensor stack, verified by compilation in [the hardware audit](docs/hardware-audit.md).
Barcodes are scanned on a phone instead, through a management page served by the
same Apps Script, which also looks the product up in Open Food Facts. Tapping one
of a handful of tiles turns out to be faster than aiming a camera anyway.

Every purchase is written to flash **before** it is sent and keeps its
transaction id across retries, so a dead router or a backend outage cannot lose a
drink and a retry cannot record one twice.

## Setup

**Hardware.** M5Stack Tab5 (ESP32-P4, with the ESP32-C6 for Wi-Fi).

**Libraries**, pinned. For the CLI build:

```sh
./tools/setup.sh
```

Arduino IDE reads a different library folder, so if you build from the IDE:

```sh
./tools/setup-ide.sh
```

Then restart the IDE so it re-reads its library index.

**Backend.** Follow [backend/README.md](backend/README.md): create the
spreadsheet, paste in the Apps Script sources, set the script properties and
deploy twice — once anonymously for the terminal, once Google-restricted for the
management page.

**Secrets.** Copy `firmware/beer_terminal/secrets.example.h` to `secrets.h` and
fill in the Wi-Fi credentials, the Apps Script URL and the device token. The file
is git-ignored. Without it the firmware still builds and runs, with purchases
simulated locally; the admin screen says so.

## Build

```sh
./tools/build.sh
```

Tested with Arduino CLI 1.1.1 and **m5stack:esp32@3.3.9**, **M5Unified@0.2.22**,
**M5GFX@0.2.29**, **lvgl@9.2.2**, **ArduinoJson@7.4.3**. Application and library
code is compiled as GNU C++17; the core's prebuilt ESP-IDF libraries keep their
upstream settings. The sketch also accepts C++20, so Arduino IDE needs no custom
flags — open `firmware/beer_terminal/beer_terminal.ino` and press Verify.

To upload from the CLI:

```sh
./tools/arduino.sh board list
```

```sh
TAB5_PORT=/dev/cu.usbmodemXXXX CHIP_VARIANT=prev3 ./tools/upload.sh
```

Serial runs at 115200.

## Tests

```sh
./tools/test.sh
```

Runs on the Mac, no board required. Several modules are deliberately free of
Arduino and LVGL so they can be exercised here: the adaptive grid geometry, the
on-flash format for the catalog, queue and summary, and the wire protocol
including its host allow-lists. The Apps Script validators run under Node in the
same pass.

## Layout

| | |
|---|---|
| `firmware/beer_terminal/` | the sketch: state machine, catalog, queue, LVGL screens, networking |
| `backend/apps-script/` | the web app and the phone management page |
| `tests/`, `tools/test.sh` | host tests |
| `tools/` | pinned setup, build, upload, font generation |
| `docs/` | design decisions and what each milestone changed |

## Status

Working on the device: the interface and state machine, a catalog that survives
reboots, Wi-Fi and the backend client, the Apps Script and spreadsheet, the
offline queue, per-person consumption, managing prices and archiving from the
terminal, and the backlight switching off when idle.

Not done: product photos on the device, manual barcode entry, a resident editor
(residents are maintained in the sheet), and real sleep. The device currently
turns its screen and radio off when idle but the SoC stays awake; choosing a
sleep mode needs current measurements on real hardware. Touch cannot wake the
P4 from deep sleep on this board — the touch interrupt is on GPIO23, outside the
sixteen RTC-capable pins — so light sleep is the realistic option.

[docs/](docs/) has the reasoning, including the things that were tried and
reverted.
