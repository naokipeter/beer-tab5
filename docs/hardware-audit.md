# Hardware and framework audit — 2026-09-16

## Inspection and dependency baseline

The supplied working directory was a collection of projects, not a Git repository.
No existing Tab5 project was found in its filename/directory scan. A separate
`beer-tab5` repository was created. No applicable parent AGENTS.md was found.
Git and Homebrew are installed; `arduino-cli` is absent from PATH, but Arduino IDE
bundles CLI 1.1.1. Arduino15 contains the official M5Stack 3.3.9 core/toolchain and
cached M5Unified 0.2.22 / M5GFX 0.2.29 ZIPs. The Documents/Arduino sketchbook was
initially unreadable under macOS permissions; it has since been inspected and
contains **lvgl 8.3.2**, which Arduino IDE uses in preference to the project-local
9.2.2 that the CLI build uses. The two library folders are independent. Since
LVGL 8 and 9 differ across most of the API, `firmware/beer_terminal/ui_lvgl.h`
asserts the major version so the mismatch reports itself directly.

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
| Camera | Official UserDemo `hal_camera.cpp`: `esp_video_init`, `ESP_VIDEO_MIPI_CSI_DEVICE_NAME`, V4L2 `open/ioctl/mmap`, `VIDIOC_DQBUF` / `VIDIOC_QBUF` | Camera is MIPI CSI/ISP, not the generic ESP32 parallel-camera interface. **Confirmed unavailable under Arduino** — see the camera gate section below. |

[Official Arduino setup](https://docs.m5stack.com/en/arduino/m5tab5/program),
[touch](https://docs.m5stack.com/en/arduino/m5tab5/touch),
[Wi-Fi](https://docs.m5stack.com/en/arduino/m5tab5/wifi),
[power](https://docs.m5stack.com/en/arduino/m5tab5/power),
[wakeup](https://docs.m5stack.com/en/arduino/m5tab5/wakeup).
Local inspected sources: pinned libraries' `src/utility/Power_Class.cpp/.hpp`,
M5GFX `src/M5GFX.cpp`, and core board/variant files.

## Camera gate: not passed — verified by compilation, 2026-09-16

Re-verified against the pinned toolchain (`m5stack:esp32@3.3.9`, Arduino CLI 1.1.1,
both `esp32p4-libs` and `esp32p4_es-libs` SDK variants) with throwaway probe sketches
built through `tools/build.sh`.

| Probe | Includes / calls | Result |
|---|---|---|
| A | `esp_video_init.h`, `esp_cam_sensor.h` | `fatal error: esp_video_init.h: No such file or directory` |
| B | `esp_cam_ctlr_csi.h`, `driver/isp.h`; `esp_cam_new_csi_ctlr()`, `esp_isp_new_processor()` | **Compiles and links** (479574 B flash, 27292 B RAM) |
| C | `usb/usb_host.h`; `usb_host_install()`, `usb_host_client_register()` | **Compiles and links** (511144 B flash) |

The precise situation: the P4 **peripheral** drivers ship with the core
(`esp_driver_cam` with CSI/DVP, `esp_driver_isp`, `esp_driver_jpeg`, `esp_driver_ppa`;
`libesp_driver_cam.a`, `libesp_driver_isp.a` present). The **sensor stack does not**:
`esp_video`, `esp_cam_sensor`, `esp_sccb_intf` and `esp_ipa` are absent from
`include/` and `lib/` in both SDK variants, and the SDK's frozen `sdkconfig`
contains no `ESP_VIDEO`, `CAM_SENSOR` or `CAMERA_SC*` keys at all. So the MIPI
receiver can be allocated, but nothing knows how to bring up, address or configure
the SC2356/SC2336 over SCCB, and no ISP tuning parameters exist for it.

This is not fixable with `arduino-cli lib install`. `esp_video` and `esp_cam_sensor`
are ESP-IDF *managed components* selected through Kconfig and compiled into the SDK.
The Arduino core ships prebuilt static libraries against a fixed `sdkconfig`;
adding a component means rebuilding that SDK (`esp32-arduino-lib-builder`) or using
Arduino as an ESP-IDF component. Nor is there an Arduino library to install:
the Library Manager index contains no Tab5 or SC2356/SC2336 entry, and neither
M5Unified 0.2.22 nor M5GFX 0.2.29 exposes any camera API for this board.

Every working Tab5 camera reference found is ESP-IDF, not Arduino: M5Stack's own
[M5Tab5-UserDemo](https://github.com/m5stack/M5Tab5-UserDemo) (IDF 5.4.2, vendors
`esp_video`/`esp_cam_sensor` under `platforms/tab5/components/`), Espressif's
[esp-bsp `m5stack_tab5`](https://github.com/espressif/esp-bsp/tree/master/bsp/m5stack_tab5),
the [`espp/m5stack-tab5`](https://components.espressif.com/components/espp/m5stack-tab5)
registry component, and community projects such as
[M5Tab5-VideoLink](https://github.com/amcchord/M5Tab5-VideoLink).

Paths that would unblock the camera later, in increasing cost:
rebuild the Arduino P4 SDK with the camera components enabled; build Arduino as an
ESP-IDF component; or port the application to native ESP-IDF. All three also carry
the unresolved SC2356-vs-SC2336 naming question and the untested grayscale/ISP
format negotiation. None is on the critical path if barcode capture moves off-device.

The naming mismatch still stands: product documentation says SC2356, the official
reference contains `sensors/sc2336` and `CAMERA_SC2336`. Confirm the detected sensor
ID before attempting any camera milestone.

The C6 must also run firmware compatible with the core's ESP-Hosted transport.
Linking `WiFi.h` is not an association test. Validate the existing C6 firmware,
SDIO startup, reconnect behaviour and HTTP/TLS operation on hardware in milestone 7;
use M5Stack's official C6 recovery instructions if a firmware mismatch is found.

## USB Host as a camera-free scanner option

Probe C shows the ESP-IDF USB Host stack links from Arduino on this target
(`libusb.a`, `usb/usb_host.h`, `CONFIG_SOC_USB_OTG_SUPPORTED=y`). The managed
`usb_host_hid` component is **not** in the SDK, so a HID boot-keyboard client would
have to be written on top of `usb_host.h` or that component vendored (Apache-2.0).

Unverified and hardware-dependent: whether the Tab5 USB-A port supplies host VBUS
on battery, and how that rail is controlled (`M5.Power.setUsbOutput` is a candidate,
unconfirmed for this board). Linking the stack is not evidence a device enumerates.

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
