# Hardware and framework audit — 2026-09-16

## Inspection and dependency baseline

The supplied working directory was a collection of projects, not a Git repository.
No existing Tab5 project was found in its filename/directory scan. A separate
`tab5-beer-terminal` repository was created. No applicable parent AGENTS.md was found.
Git and Homebrew are installed; `arduino-cli` is absent from PATH, but Arduino IDE
bundles CLI 1.1.1. Arduino15 contains the official M5Stack 3.3.9 core/toolchain and
cached M5Unified 0.2.22 / M5GFX 0.2.29 ZIPs. The Documents/Arduino directory could
not be inspected because macOS denied access; no claim is made about its contents.

The cached official libraries contain display and touch examples. They were
extracted unchanged into the project's ignored library directory. M5Unified's
manifest requires M5GFX >=0.2.29. Only these two libraries are linked in milestone 1.
LVGL is deferred to milestone 3; propose 9.2.2, matching the official demo's
`repos.json`, then pin after compiling its port. No barcode library is installed yet.

## Verified APIs and limitations

| Subsystem | Official API/source | Implication |
|---|---|---|
| Board | `m5stack:esp32:m5stack_tab5`, `boards.txt`, `variants/m5stack_tab5/pins_arduino.h` | Real Tab5 target; PSRAM enabled. Core distinguishes pre-v3 and v3+ silicon. |
| Display | `M5.config()`, `M5.begin(cfg)`, `M5.Display.setRotation`, `setBrightness`, `fillScreen`, `println`; M5GFX Tab5 detection | Use library panel detection; do not write panel initialization or GPIO assignments. |
| Touch | `M5.update()`, `M5.Touch.getCount()`, `getDetail(i)` with position/event methods | M5GFX configures internal I2C and interrupt itself; actual panel revision needs hardware validation. |
| Wireless | `<WiFi.h>`, `WiFi.begin`, `WiFi.status`, `WiFi.disconnect`, `WiFi.mode(WIFI_OFF)`; `WiFi.setPins` available | Board variant already provides ESP-Hosted SDIO pins for C6. Driver stop is not proof the C6 power rail is off. |
| Battery | `M5.Power.getBatteryVoltage()`, `getBatteryLevel()`, `isCharging()`, INA226 initialization | Measurements and charge behaviour require the physical battery. |
| Power | `M5.Display.sleep()/wakeup()`, `M5.Power.lightSleep(us, touch_wakeup)`, `deepSleep(us, touch_wakeup)`, `timerSleep`, `powerOff`, `setExtOutput`, `setUsbOutput` | APIs exist but their board-specific implementation matters; no sleep mode selected yet. |
| Camera | Official UserDemo `hal_camera.cpp`: `esp_video_init`, `ESP_VIDEO_MIPI_CSI_DEVICE_NAME`, V4L2 `open/ioctl/mmap`, `VIDIOC_DQBUF` / `VIDIOC_QBUF` | Camera is MIPI CSI/ISP, not the generic ESP32 parallel-camera interface. Arduino camera support is unverified and currently blocked. |

[Official Arduino setup](https://docs.m5stack.com/en/arduino/m5tab5/program),
[touch](https://docs.m5stack.com/en/arduino/m5tab5/touch),
[Wi-Fi](https://docs.m5stack.com/en/arduino/m5tab5/wifi),
[power](https://docs.m5stack.com/en/arduino/m5tab5/power),
[wakeup](https://docs.m5stack.com/en/arduino/m5tab5/wakeup).
Local inspected sources: pinned libraries' `src/utility/Power_Class.cpp/.hpp`,
M5GFX `src/M5GFX.cpp`, and core board/variant files.

## Camera gate: not passed

No official Tab5 Arduino camera sketch was found locally or in the official Tab5
Arduino documentation inspected. The generic CameraWebServer sketch is not a Tab5
SC2356 example. The installed P4 and P4_ES SDK libraries do not contain
`esp_video_init.h`, `esp_cam_sensor` or the SC2356 driver. Merely including an
ESP-IDF header will not supply missing compiled components or Kconfig settings.

The correct official reference is
[M5Stack/M5Tab5-UserDemo](https://github.com/m5stack/M5Tab5-UserDemo), inspected at
commit `b4e356bc491ca070d54004718dad789c07d5fc93` (temporary read-only reference checkout
at `/private/tmp/tab5-official-userdemo`). Its documented embedded build uses
**ESP-IDF 5.4.2**, not Arduino CLI. It includes camera components under
`platforms/tab5/components/`, including `esp_video`, `esp_cam_sensor`, `esp_ipa`
and `esp_sccb_intf`, plus board initialization. Its camera HAL uses the BSP's
existing I2C handle, two mapped buffers and RGB565 output. A GREY format enum exists,
but that does not establish SC2356 grayscale output works on this configuration.
Sensor output/ISP format negotiation must be tested.

There is also a naming mismatch to resolve: the product documentation says
SC2356, while this reference checkout contains `sensors/sc2336` and
`CAMERA_SC2336` configuration. Do not infer electrical or register compatibility
from those names. Confirm the detected sensor ID and the matching official
driver/revision before attempting the camera milestone.

To reproduce that official reference separately, install ESP-IDF 5.4.2, clone the
repository at the commit above, run `python ./fetch_repos.py`, then follow its
`platforms/tab5` build instructions (`idf.py build`). This is an investigation
option, **not an application framework migration performed in milestone 1**.
Before milestone 4, assess whether these official components can be built into
the Arduino distribution with matching IDF configuration. If not, explain the
tradeoff and propose Arduino-as-IDF-component or native ESP-IDF for approval.
Camera compilation cannot honestly be confirmed with the present Arduino package.
No guessed GPIO mappings or replacement-board code were introduced.

The C6 must also run firmware compatible with the core's ESP-Hosted transport.
Linking `WiFi.h` is not an association test. Validate the existing C6 firmware,
SDIO startup, reconnect behaviour and HTTP/TLS operation on hardware in milestone 7;
use M5Stack's official C6 recovery instructions if a firmware mismatch is found.

## Barcode decoder risk and next experiment

| Candidate | Formats / image input / licence | ESP32-P4 risk |
|---|---|---|
| [ZXing-C++](https://github.com/zxing-cpp/zxing-cpp) | EAN-13, EAN-8, UPC-A; `ImageView` supports luminance and row stride/cropping; Apache-2.0 | Current source requires C++20 internally (public API C++17). Core reader has no external library dependency, but CMake/Arduino integration and allocations need a real cross-build. Disable writers, examples and unused formats; do not assume Arduino Library Manager compatibility. |
| [ZBar](https://github.com/mchehab/zbar) | EAN/UPC; grayscale Y800 input; LGPL-2.1 family (inspect component notices before distributing) | C scanner/decoder could be ported separately from desktop video/UI dependencies; no P4 build or performance validated. Autotools defaults are unsuitable as-is for an Arduino sketch. |

Neither decoder is selected as proven. At milestone 5 build the reader behind a
bounded grayscale-frame interface, then measure decoder heap peak, stack, flash,
latency and recognition rate with real bottle/can images. A 640x240 8-bit ROI
alone is 153,600 bytes; double buffering doubles that, excluding camera, decoder
and LVGL allocations. These are buffer arithmetic, not measured memory usage.
Use a borrowed frame/stride while dequeued, requeue after decoding, and avoid
full RGB copies. Require a valid EAN/UPC check digit plus the same code in two
consecutive frames, resetting confidence on no result or a changed code. Stop
capture after acceptance; benchmark glare, curved labels and poor focus.

## Wake/power risk

In M5Unified 0.2.22 the Tab5 branch initializes power expanders but does **not**
assign `_wakeupPin` (default 255). Consequently `touch_wakeup=true` alone does
not enable Tab5 touchscreen wake in this version. M5GFX's Tab5 branch configures
the touch interrupt on GPIO23; that source observation is not a verified deep-sleep
wake circuit. Do not copy the generic ESP32 capacitive-touch wake API.

- **Display-off idle:** processor remains running, touch can be polled. Candidate
  fallback with backlight off; camera/driver and wireless rails require separate
  shutdown checks. A 10–20 ms polling interval is a design target, not measured latency.
- **Light sleep:** CPU context resumes, but a supported wake source and peripheral
  restoration must be established. Neither touch wake nor current is verified.
- **Deep sleep:** restarts the program. Official docs demonstrate timed wake;
  this does not demonstrate touch wake. RTC/power-button wake may be practical,
  but exact behaviour and latency require hardware observations.
- **Power-off:** the Tab5 implementation pulses its power-control expander;
  this differs from either sleep mode. Do not call it during initial tests.

Choose the lowest measured mode that meets the wake interaction only in milestone 10.
Measure battery current (USB unplugged) and wake-to-usable-screen latency for each
viable mode. Test camera stream stop versus actual sensor power/reset, C6 driver
stop versus rail shutdown, backlight, USB/external rails, and repeated wake cycles.
M5.begin can enable rails even when no Wi-Fi/camera application is running.
No battery-life estimate or deep-sleep touch claim is justified yet.
