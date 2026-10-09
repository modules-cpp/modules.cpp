# modules.cpp v1.3.4

Something to share and connect with. v1.3.4 introduces configure-managed
external C++20 modules shared across applications, backports the high-speed
4-bit SDIO driver for RP2350 with SD/FAT hardware diagnostics, adds an uptime
RTC provider and display gamma corrections for RP2350 LCD boards, suppresses
GCC psabi diagnostics on 32-bit ARM, and provides absent storage fallbacks for
Linux SDL environments.

```sh
./build.sh
./test.sh
# Check shared external modules:
./out/bin/shell --run tests/scripts/external-modules.sh
# Build SD/FAT test firmware for RP2350 Touch LCD 2.8:
./configure.sh --target arm-none-eabi --compiler arm-none-eabi-gcc --sdk pico-arm --board rp2350_touch_lcd_28 --runner none --build debug
./test.sh --target --compile-only tests/boards/rp2350_touch_lcd_28
```

Configure-managed external `kind: module` children can now live alongside
applications in connected `kind: dir` trees using manifest 1.3 schema.
External applications import these shared modules and partitions without
handwritten header workarounds. Configure validates interfaces, rejects
collisions with installed module names, checks dependency ordering, and
detects cycles before publishing bindings, while build keeps objects and
BMIs in the external tree.

The 4-bit SDIO driver provides fast SD card transfers over PIO and DMA on
RP2350, integrated into the Pico SDK bridge. The `rp2350_touch_lcd_28` profile
binds the driver to its TF card socket as `mm.sdcard.socket`. A dedicated
read-only on-board test in `tests/boards/rp2350_touch_lcd_28` validates sector
reading, timeouts, multi-block reads, and FAT mounting on hardware.

`platform.rp2350_touch_lcd_154.rtc` provides an uptime-based software `mm.rtc`
clock for RP2350 1.54-inch LCD boards, enabling calendar and clock applications
such as clockcal without requiring external RTC hardware. The ST7789 display
driver updates positive and negative gamma curves and inverted display mode for
`rp2350_touch_lcd_154`. For Linux SDL builds, `platform.linux.storage.absent`
supplies an explicit unavailable SD socket, FAT, and LittleFS provider returning
`Unsupported` so applications referencing storage build cleanly without local
storage emulation.

**SDIO operation and software RTC are verified on RP2350 development boards;
write operations and high-load wear-leveling are not formally qualified.**
**External named module declarations remain restricted to kind: module manifests
and cannot be declared directly in app sources.**

Source manifests `mm: 1.0` through `1.3` remain supported; the persisted
configuration schema is unchanged.

Requires a C++20 compiler with module support (GCC 14/15 or Clang 18+). Pico
builds require the Arm toolchain, Pico SDK, picotool, and its tools. See
[CHANGELOG.md](CHANGELOG.md), `docs/modules-external-apps.mdy`,
`docs/modules-sdcard.mdy`, `docs/modules-configure.mdy`, and
`docs/modules-display.mdy`.
