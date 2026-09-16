# Milestone 1 verification

Run date: 2026-09-16, Apple Silicon macOS. Arduino CLI 1.1.1, M5Stack core 3.3.9,
M5Unified 0.2.22, M5GFX 0.2.29. Board options are recorded in README and build.sh.
Tested target: `ChipVariant=prev3`. `postv3` is available as a build option but
not yet validated in this milestone.

| Check | Result |
|---|---|
| `./tools/setup.sh` | PASS; official indexes refreshed, pinned core already present, both pinned libraries already installed |
| Official M5GFX `Basic/TextLogScroll` | PASS; 472,832 bytes flash, 25,804 bytes static RAM |
| Official M5Unified `Basic/Touch/DragDrop` | PASS; 536,334 bytes flash, 27,728 bytes static RAM |
| `./tools/build.sh` | PASS; 535,756 bytes flash, 27,624 bytes static RAM |
| C++17 | PASS; CLI explicitly selects GNU C++17; sketch accepts C++17 or newer for IDE compatibility |
| Shell scripts / ignore rules | PASS; all scripts pass `bash -n`; secrets and generated directories are ignored |
| Official Tab5 Arduino camera | BLOCKED; missing official Arduino example and video/sensor components; not falsely counted as a pass |
| Upload / boot / touch / camera / networking / battery | NOT RUN; physical observations required |

Static RAM reported by the linker does not include runtime display buffers,
PSRAM, stacks or decoder allocations. No inference about final-product memory
fit or speed should be made from this minimal build.

The library ZIP SHA-256 values match the installed official Arduino library index:

```text
M5Unified 0.2.22 a183ca78d7c0c07148b77310f40f028e195fe01315af982e151157fa03eaaa55
M5GFX     0.2.29 0b7ed248f858a940403eab2cab5d2ade82a8c8cc2f09dc2d2a802d9688f9ab70
```

Reproduce with setup/build/verify-examples from README. Official example sources
are compiled unchanged from pinned library archives, not copied or rewritten.
The build wrapper preserves the core's C++ compiler flags and places GNU C++17
after its response-file defaults. The first exploratory builds used the board's
default C++ setting; the final builds use the corrected explicit C++17 setting.

Compiler commands, ELF/map and binaries are under `build/prev3/<sketch>/`.
Local logs are `/private/tmp/tab5-m1-build.log`,
`/private/tmp/tab5-examples-build.log` and `/private/tmp/tab5-setup.log`.
Temporary logs may be cleaned by the OS; this document records the outcomes.

Next gate: follow README's physical verification and confirm before milestone 2.
No upload or device firmware replacement was attempted.

## Arduino IDE compatibility fix

The sketch now requires C++17 or newer instead of exactly C++17. Rebuilt
successfully both with `tools/build.sh` (C++17) and with the same board/options
using the unmodified board compiler defaults (C++20), matching Arduino IDE.
The latter was verified through Arduino CLI without any build-property override;
no GUI upload was performed. Logs: `/private/tmp/tab5-cpp17-fix-build.log` and
`/private/tmp/tab5-ide-default-build.log`.
