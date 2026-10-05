# Changelog

All notable changes to modules.cpp. Versions follow [semantic versioning](https://semver.org/).

## [Unreleased]

### Added

- **`rp2040_geek` and `rp2350_geek` boards.** The Waveshare RP2040-GEEK and
  RP2350-GEEK, under `boards/geek`, deriving from `pico` and `pico2-arm` and
  sharing `platform.geek.display`, the 1.14 inch 240×135 ST7789 panel on SPI1.
  The Pico bridge's board table reports no LED (GP25 is the backlight), a
  3300 mV ADC reference, and UART1 on GP4/GP5 as the default UART. Wiring is
  from Waveshare's schematics; not yet qualified on hardware.
- **`rp2040_zero` and `rp2350_zero` boards.** The Waveshare RP2040-Zero and
  RP2350-Zero mini boards, under `boards/zero`, deriving from `pico` and
  `pico2-arm` and binding `platform.zero.led` as `mm.led`. The board table
  reports no LED (the only LED is a WS2812B on GP16, which a GPIO write cannot
  light), a 3300 mV ADC reference, and the second I2C and UART wirings on free
  header pins. Wiring is from Waveshare's
  schematics; not yet qualified on hardware.
- **`rp2350_pizero` board.** The Waveshare RP2350-PiZero, under
  `boards/pizero`, deriving from `pico2-arm`. It is an RP2350B, so the Pico
  bridge's board table gains `MM_PICO_BOARD_HEADER`, which selects the SDK's
  `waveshare_rp2350_pizero` header in place of `pico2` for it: 48 GPIOs, 16MB
  of flash, UART1 on GP4/GP5, no LED. 3300 mV ADC reference. Wiring is from
  Waveshare's schematic; not yet qualified on hardware.
- **`rp2350_pizero_usb_host` board.** `rp2350_pizero` with its PIO-USB socket
  as a USB host on GP28/GP29, binding `platform.pico.usb.host`, as
  `pico2_usb_host` does on GP2/GP3.
- **`mm.led` and `mm.led.ws2812b`.** A platform interface for chains of
  addressable RGB LEDs, with mm.display's lifecycle (`initialize`, `write`,
  `refresh`, `clear`, `sleep`), and a portable WS2812B controller that owns the
  protocol's timing and GRB byte order.
- **`mm.mcu` pulse facility.** `pulse_configure`, `pulse_write`, and
  `pulse_release`: a one-wire pulse-width-coded output, the WS2812's
  transport. The Pico bridge implements it with a four-instruction PIO program
  whose ticks per bit and divider it plans from the requested timing;
  every other platform answers Unsupported.
- **`mm.fs` and `mm.fs.conformance`.** A portable file interface: path
  normalisation, a mount table of up to eight volumes resolved by longest
  prefix, move-only `File` and `Directory` values, operations by path, a
  clock hook, and the `Volume`, `BlockDevice`, and `FlashDevice` seams drivers
  implement, with `McuStorage` over `mm.mcu` block storage. No allocation.
  `mm.fs.conformance` runs the contract as 25 checks against any mounted
  volume. See `drafts/plan-mm-fs-5.mdy` for the drivers still to come.
- **`mm.fs.local`, `mm.fs.native`, and `platform.linux.fs`.** Mount
  interfaces for the board's own storage and for a directory of the
  platform's file system, bound on the Linux SDKs to a provider over
  `std::filesystem` and `std::filebuf`. The device map gains
  `directory.N.path` and `directory.N.writable`; with none, `mm.fs.local`
  mounts the working directory. `apps/fs-smoke` and `scripts/build-fs.sh`
  run the conformance checks on the board's storage: 25 of 25 pass on Linux.
- **`mm.mcu` flash region and `mm.fs::McuFlash`.** Raw flash for data: the top
  of a Pico's program flash, sized by a new board-table column
  (`MM_BOARD_FLASH_REGION_BYTES`: 256 KiB on pico builds, 512 KiB on pico2,
  4 MiB on the PiZero), programmed and erased through `flash_safe_execute`;
  and on Linux an image file named by `flash.*` device-map keys, which refuses
  to program unerased bytes. The Pico bridge's UF2 validation now refuses an
  image that reaches the region, skipping picotool's RP2350-E10 block.
- **`mm.fs.littlefs` and littlefs on every Pico board.** littlefs v2.11.3,
  vendored by `platforms/pico/sdk/pico-sdk/littlefs/vendor.sh` and compiled by
  the Pico bridge for programs that use it, behind
  `platform.pico.fs.littlefs`, which the Pico SDKs bind for `mm.fs.littlefs`
  and `mm.fs.local`: `mm.fs.local` mounts littlefs on the flash region,
  formatting a region never written and leaving a damaged one alone.
  Timestamps live in a littlefs attribute. `scripts/test-littlefs.sh` runs the
  adapter's 77-check native harness; `scripts/build-fs.sh` now defaults to
  `pico`.
- **`mm.fs.fat` and FAT on every Pico board.** FatFs R0.16, vendored by
  `platforms/pico/sdk/pico-sdk/fatfs/vendor.sh` from ChaN's checksummed
  archive and configured by a project `ffconf.h` passed with `-include`,
  behind `platform.pico.fs.fat`, which the Pico SDKs bind for `mm.fs.fat`:
  FAT12/16/32 with long names on 512-byte block devices, never written when it
  holds no FAT volume, with zero-filled extension and true appends.
  `scripts/test-fatfs.sh` runs the adapter's 84-check native harness;
  `apps/fat-smoke` and `scripts/build-fat.sh` check FAT on a host-port board's
  USB drive.
- **`mm.sdcard` and `mm.sdcard.socket`: FAT on TF sockets.** An SD card in SPI
  mode over `mm.mcu` SPI as an `mm.fs` block device -- SDSC, SDHC, and SDXC,
  CRC7 and CRC16 throughout, re-identified after any failure -- and a
  board-bound socket interface, provided by `platform.geek.sdcard`,
  `platform.rp2350_touch_lcd_154.sdcard`, and `platform.pizero.sdcard` for the
  GEEK, LCD 1.54, and PiZero boards. `apps/sd-smoke` and `scripts/build-sd.sh`
  check FAT on the socket's card.
- **`storage-linux`: SD card and SPI flash emulation.** A virtual SD card in SPI mode and a virtual W25Q-family NOR
  flash on one emulated SPI bus, each kept in an image file -- `sdcard.img`
  and `spiflash.img` in the working directory, or wherever the new
  `sdcard.*` and `spiflash.*` device-map keys say -- behind `mm.mcu` on a
  board that also publishes the card as `mm.sdcard.socket`. The card follows
  the SD specification's SPI mode, SDHC or SDSC, with CRC checking and
  injectable faults; the flash ANDs on program, wraps pages, needs write
  enable, reports BUSY, and counts a driver's mistakes. Images are ordinary
  files that `mkfs.fat`, `fsck.fat`, `mtools`, and `littlefs-python` make and
  read. `apps/socket-smoke` and `scripts/build-socket.sh` round-trip the
  socket's last block; `--board storage-linux` selects it on any Linux machine. See
  `docs/modules-linux-storage.mdy`.
- **Library C sources, manifest version 1.4.** A library may declare
  `c-source`, `c-strict`, `c-include`, and `c-option`: C files `mm.build`
  compiles with the lane's C compiler and links into every executable that
  reaches a module naming the library, once per library, project-owned glue
  with warnings as errors. A lane an external bridge links refuses them.
  `libraries/c-demo` is the fixture and a new suite. See
  `docs/modules-libraries.mdy`.
- **littlefs and FAT on Linux, independent of Pico.** Linux's own littlefs
  v2.11.3 and FatFs R0.16 under `platforms/linux/littlefs` and
  `platforms/linux/fatfs` -- their own `vendor.sh`, adapters, `ffconf.h`,
  and providers, `platform.linux.fs.littlefs` and `platform.linux.fs.fat`,
  which the Linux SDKs bind for `mm.fs.littlefs` and `mm.fs.fat`. Pico's
  trees, providers, and bridge blocks are unchanged. Both pass the 25
  conformance checks on Linux, littlefs through `mm.spiflash` on the emulated
  flash and FAT through `mm.sdcard` on the emulated card, and `apps/sd-smoke`
  runs unchanged on `storage-linux` against a `mkfs.fat` image.
  `scripts/test-littlefs.sh` and `scripts/test-fatfs.sh` take
  `--tree pico|linux|both`.
- **SD cards in 4-bit SD mode: the `mm.mcu` sdio facility and
  `mm.sdcard::SdioCard`.** A portable SD bus facility -- configure, clock,
  idle clocks, command, read, write, release, with the platform framing
  commands, checking response and per-line data CRCs, and waiting out busy --
  implemented on Pico over PIO and DMA at 25 MHz, and a second card class
  speaking the SD-mode protocol over it. The RP2350-Touch-LCD-2.8's socket,
  which hardware SPI cannot reach, gets `platform.rp2350_touch_lcd_28.sdcard`,
  and both PiZero boards switch from SPI mode to SDIO. The GEEK and LCD 1.54
  sockets stay in SPI mode. The Pico transport's design learned from carlk3's
  Apache-2.0 no-OS-FatFS-SD-SDIO-SPI-RPi-Pico, acknowledged in the README and
  `docs/modules-sdcard.mdy`; no code is copied. Not yet qualified on
  hardware.
- **Files in the shells: `mm.shell.fs`, `mm.fs.shell`, and `mm.shell.board`.**
  The embedded shell gains ten file commands over `mm.fs` -- `ls`, `cat`,
  `stat`, `write`, `append`, `rm`, `mkdir`, `mv`, `df`, `mounts` -- bounded
  and allocation-free, with `cat` and `ls` paging long output. The full
  shell's redirections, globbing, and file tests reach `mm.fs` volumes
  through `mm.fs.shell`. `apps/mcu-shell` now runs on `mm.shell.board` and
  always mounts the board's own storage at `/data` (littlefs on a Pico);
  `apps/mcu-shell-sd`, selected by `scripts/build-shell.sh --sd`, adds the
  socket's FAT card at `/sd`. With littlefs, the RP2040 shell exceeds the
  shell specification's size ceilings; `docs/modules-shell.mdy` records the
  measurements and leaves the ceilings as a decision.
- **littlefs's pools are sized per board.** Three board-table columns,
  `MM_BOARD_LFS_VOLUMES`, `MM_BOARD_LFS_FILES`, and
  `MM_BOARD_LFS_DIRECTORIES`, size the Pico littlefs adapter's static pools,
  overridable from CMake or the environment and range-checked. The default
  is one volume, four files, and two directories, about 4.5 KB instead of
  9 KB; the PiZero boards keep the old 2, 8, and 4. `mm_pico_lfs_limits`
  reports them, and `scripts/test-littlefs.sh` runs the harness at both the
  default and the PiZero's sizes.
- **The SDIO facility no longer costs every Pico image 6.7 KB of RAM.** Reads
  go by DMA straight into the caller's buffer, byte-swapped by the DMA engine,
  with a chained channel collecting CRC words; the CRC table is sixteen
  entries; write words are computed as they are fed. A read buffer must now
  start on a four-byte boundary, and `SdioCard` bounces any other.
- **`mm.spiflash`.** A W25Q-family SPI NOR flash chip over `mm.mcu` SPI as an
  `mm.fs` flash device: identified by JEDEC ID, 64 KiB to 16 MiB, page
  programs and 4 KiB or 64 KiB erases behind write enable and BUSY polling,
  re-identified after any failure. Tested against the emulated chip; see
  `docs/modules-spiflash.mdy`.
- **`apps/rgb-led-smoke` and `scripts/build-rgb-led.sh`.** Red, green, blue,
  white, and a colour wheel on any board that binds `mm.led`; the default board
  is `rp2040_zero`.

## [v1.3.1] — 2026-10-02

Something to connect and store with. v1.3.0 gave the project a language of its
own with sketches and a native shell; v1.3.1 gives programs standard USB
connectivity and mass storage: the USB subsystem (`mm.usb`) introduces device
and host abstractions, CDC ACM console communication for `mm.stdio`, and SCSI
Mass Storage Class (`mm.usb.msc`) for block storage; sketches gain secondary
peripheral buses (`Wire1`, `Wire2`, `Serial1`, `Serial2`), interrupts, SD card
redirection, and a legacy profile (`sketch --legacy`) for seamless third-party
Arduino library compatibility; native USB port ownership is unlocked on
Raspberry Pi Pico; Linux adds emulated ST7789 and ILI9341 display support over
SDL2; and fenced code blocks arrive in `mm.mdy`.

### Added

- **USB Subsystem (`mm.usb`, `mm.usb.device`, `mm.usb.host`).** Shared vocabulary
  for USB data transfers (`Status`, `Speed`, `TransferType`, `Direction`,
  `EndpointAddress`, `SetupPacket`), platform-interfaces for USB device
  (`mm.usb.device`, nonblocking transfers, descriptors, setup handling) and
  USB host (`mm.usb.host`, bounded transfers, device enumeration, interface claim).
- **USB CDC ACM and Stdio Console Provider (`mm.usb.cdc`).** CDC ACM device class
  function (`CdcDevice`) and `CdcConsole` providing console I/O for `mm.stdio`
  when the application owns the native USB port.
- **USB Mass Storage Class (`mm.usb.msc`).** Bulk-Only Transport (BOT / BBB)
  device class exposing `mm.mcu` block storage to USB hosts via SCSI Transparent
  commands (Inquiry, Test Unit Ready, Request Sense, Read Capacity 10, Read 10, Write 10).
- **USB Vendor Class (`mm.usb.vendor`).** Bulk echo device (`DeviceEcho`) and host
  (`HostEcho`) implementations for qualification and loopback testing.
- **Native USB Port Ownership on Raspberry Pi Pico.** Support for native USB port
  ownership configuration (`console` vs `application`) and dedicated boards
  `pico_usb_device` and `pico2_usb_device`.
- **Fenced code block support in `mm.mdy`.** Native multi-line fenced code block
  parsing (`BlockType::CodeBlock`), language tag preservation, indentation and blank
  line preservation, HTML `<pre><code class="language-...">` generation in `apps/mdy`,
  and updated developer documentation.
- **Flash tool `--auto-flash` flag.** Automatic device discovery, bootloader reset,
  and flashing for Raspberry Pi Pico boards.
- **`Wire1` in `mm.sketch`.** A second I2C bus beside `Wire`, each with its
  own buffers, pending write, clock, and pins, so a library handed `&Wire1`
  talks to the second bus. `Wire1` runs on `mm.mcu`'s new `Board::second_i2c`,
  which the Pico SDK vendor boards give as instance 1 on GP26 and GP27, the
  Arduino cores' `Wire1` pins; a composite board has one only when the bridge's
  board table says so. `setSDA` and `setSCL`, the RP2040 and RP2350 cores'
  spelling, choose a bus's pins before `begin`. Diagnostics name the bus
  (`Wire1.begin`).
- **The legacy sketch profile.** `mm.sketch` exports a namespace
  `mm::sketch::legacy` with the Arduino core's looser signatures:
  `digitalWrite` with an integer or `bool` level, `pinMode` with an integer
  mode, `pulseIn` and `pulseInLong` with an integer level, `shiftIn` and
  `shiftOut` with an integer bit order, and `analogRead` for `unsigned char`,
  `int`, `long`, and `unsigned long` pins. Each forwards to the core function
  and refuses a value the core has no spelling for with `BadArgument`. Only an
  application generated with `sketch --legacy` sees them: its manifest carries
  the new `sketch-profile: legacy` key, and its `main.cpp` and `Sketch.h` add
  `using namespace mm::sketch::legacy`, and its `main.cpp` includes `Sketch.h`
  ahead of the sketch's own includes, where a sketch toolchain puts
  `Arduino.h`, so a library header that includes nothing itself (RobTillaart's
  `Kurtosis.h`) compiles. The build compiles a legacy application, and the
  sources of its sketch libraries, with `ARDUINO=10819` defined, as a sketch
  toolchain does, so a library that selects its platform by that macro (RF24)
  takes its Arduino branch; no board macro is defined. A legacy application
  also gets `avr/pgmspace.h` as a sixth forwarder and, in its `Sketch.h`,
  `<cctype>`, `_BV`, `SPI_HAS_TRANSACTION`, and `printf_P`, `sprintf_P`, and
  `snprintf_P` with AVR's `%S` read as `%s`, which is what RF24 needs to build
  its examples on the host and the Pico. The core vocabulary is
  unchanged, so
  `digitalWrite(pin, 1)` is still refused without it, and a call the core
  already accepts keeps the core overload with it. The build reports every
  legacy application and the sketch libraries compiled into it. RobTillaart's
  AS5600, which passes a `uint8_t` level, builds all 27 of its examples in the
  legacy profile.
- **Byte UART in `mm.mcu`.** `uart_configure` (instance, transmit and receive
  GPIOs, baud), a nonblocking `uart_write` of a byte span that reports what the
  transmitter took, a nonblocking `uart_read`, and `uart_release`, beside the
  original text `uart_write`. `Board` gains `uart` and `second_uart` wirings.
  The Pico provider serves UART0 and UART1 through the FIFOs, validating the
  pins against the RP2040 and RP2350 UART mux, and describes the board header's
  default UART and, on the six vendor boards, a second, UART1 on GP8 and GP9.
  The Linux provider serves the device map's `uart.N` entries, keeping a
  configured device open so nothing that arrives between reads is lost, and
  describes `uart.0` and `uart.1`.
- **`Serial1`, `Serial2`, and `Wire2` in `mm.sketch`.** `HardwareSerial`, a
  `Stream` over the byte UART: `Serial1` on the board's default UART and
  `Serial2` on its second, with `setTX` and `setRX` for pins, a write that waits
  up to the stream's timeout for room, and reads that never wait. `Wire2` is a
  third I2C bus, instance 2, with no board wiring, so it begins only on named
  pins.
- **`Wire.begin(sda, scl)`.** `TwoWire::begin(int sda, int scl)` and
  `begin(int sda, int scl, unsigned long frequency)` choose a bus's pins,
  and optionally its clock, at begin.
- **`noInterrupts()` and `interrupts()` in `mm.sketch`.** They hold off and
  resume the handlers `attachInterrupt` registered: an edge that arrives in
  between stays latched, and its handler runs at the first dispatch after
  `interrupts()`. They do not mask the platform's interrupts. That spec
  previously declined them; RobTillaart's `PCF8574_interrupt_advanced`
  example uses them.
- **Other libraries an example uses.** `sketch --library` finds the other
  sketch libraries each example needs and records them as further
  `sketch-library:` entries: headers the example's own files include, and
  `depends=` in `library.properties`, transitively. It searches the folder
  holding the library, then the folders `MM_SKETCH_LIBRARIES_PATH` names,
  colon separated. The build compiles each such library into the application
  like the first, and accepts one outside the tree provided it declares itself
  a sketch library. RobTillaart's `ACS712_ESP32_external_ADC` and
  `waveMix_demo_temperature` find `ADS1X15` and `DHTNEW`, and Adafruit GFX
  finds Adafruit BusIO. The C and C++ sources beside a sketch are compiled
  with it, recorded as further `file:` entries, as the Arduino tools compile
  every source in a sketch folder.
- **An emulated ST7789 on Linux.** `platforms/linux/lcd` puts a virtual
  ST7789 behind the `mm.mcu` seam on new `lcd-linux-x86_64` and
  `lcd-linux-aarch64` boards and shows its glass in an SDL2 window, as the
  e-paper boards do for the SSD1680. `mm.lcd.st7789` and Adafruit's ST7789
  library both drive it unmodified, so `apps/gfx-demo` and RF24's
  `scannerGraphic` draw on a desktop. `scripts/build-linux.sh --board lcd`
  selects it; `MM_LCD_SNAPSHOT` writes each shown image to a PPM file. The
  same controller behind an ILI9341 module's glass, on the Uno pins Adafruit's
  ILI9341 examples use, is the `ili9341-linux-x86_64` and
  `ili9341-linux-aarch64` boards (`--board ili9341`), so Adafruit GFX's
  `mock_ili9341` draws in a window too.
- **A USB flash drive on a Pico through a PIO USB port.** New
  `pico_usb_host` and `pico2_usb_host` boards turn GP2 and GP3 into a second
  USB port with Pico-PIO-USB, a USB host for a mass-storage device through
  TinyUSB, while the Pico's own USB port stays the console. `mm.mcu` gains
  block storage (`storage_poll`, `storage_geometry`, `storage_read`,
  `storage_write`), and `mm.sketch` `usbStorageBegin` and its companions in
  512-byte sectors. `platforms/pico/sdk/pico-sdk/pio-usb/vendor.sh` provisions
  Pico-PIO-USB. The `mm-usb-storage` sketch library makes a drive an SdFat
  volume; its `UsbKeyPio` example is SdFat's `UsbKey` without the shield.
- **`sketch-define:`.** A sketch application may declare preprocessor
  definitions its libraries are configured by, `NAME` or `NAME=VALUE`, which
  the build passes to every file of the application after `ARDUINO`.
- **SD.h in the legacy profile is SdFat.** A legacy application gets an
  `SD.h` forwarder that includes SdFat and defines `SD` as an `SdFat`, and
  library mode adds the SdFat library to any example including `SD.h`. An
  include inside `#if`, `#ifdef`, or `#ifndef` is no longer hoisted in the
  legacy profile, so SdFat's `USE_SD_H` examples include only what they chose.
- **SPI pin names.** `mm.sketch` exports `SS`, `MOSI`, `MISO`, and `SCK`,
  read from the board's default SPI wiring when used, and `mm.mcu`'s
  `SpiWiring` gains an optional `chip_select_gpio`, the board's default chip
  select. The Pico SDK boards name GP15, DAT3 in SdFat's SDIO wiring for the
  Pico, so on a Pico `SS` is GP15 beside SCK GP18, MOSI GP19, and MISO GP16.
  With them, the SD.h redirect, and RTClib, 18 of SdFat's portable examples
  build for the Pico.
- **More of the Arduino core in the legacy profile.** Integer `SPI_MODE0` to
  `SPI_MODE3` with an `SPISettings` that takes them, `radians` and `degrees`,
  `__FlashStringHelper`, and `pins_arduino.h` and `wiring_private.h`
  forwarders; `Sketch.h` also defines its flash readers so that a library's
  own `pgm_read_byte` macro does not break them. With these, Adafruit GFX
  (through BusIO) and RF24's `scannerGraphic` (through GFX, SSD1306, and
  ST7735) build.

With `Wire1` and, for `ADS_pointerToFunction`, the legacy profile, all 27
examples of RobTillaart's ADS1X15 build; `ADS_RP2040_WIRE1` and
`ADS_pointerToFunction` did not.

### Fixed

- **`configure` recorded `cross-board-linker-script: .` for a board without
  a linker script**, every hosted Linux board, and the build then refused the
  configuration as unsafe, so `scripts/build-linux.sh` failed for the generic,
  SDL, and e-paper boards. No path is now recorded as no path.
- **`sketch` no longer overwrites a sketch folder's own `main.cpp`.** A
  `main.cpp` whose first line is not sketch's generated header is left alone:
  library mode skips that example, and a single application is refused. It
  had replaced RF24's `pingpair_maple/main.cpp`.
- `mm.ino` copied a function's default arguments into the prototype it
  generates, so a sketch defining `uint16_t f(float &c, bool reset = false)`
  failed with "default argument given for parameter 2": C++ allows a default
  in only one declaration. The generated prototype now drops each default
  whole, including defaults that are calls or literals containing commas, and
  the definition keeps it. docs/modules-ino.mdy states the rule and its limit.

- `mm.parse` read a duration's count into a signed 64-bit value without a
  bound, so `parse_time("99999999999999999999s")` overflowed, which is
  undefined behaviour. The count is read unsigned and checked; one past
  18446744073709551615 sets `overflow` and saturates there.
- `scripts/release.sh` reported "cannot reach origin" whenever fetching
  origin's tags failed, including when a local tag pointed elsewhere than
  origin's tag of the same name. It now names each such tag, says the two
  differ, and gives the commands to keep the local one and adopt origin's;
  any other fetch failure prints git's own message.
  `tests/scripts/release-fetch.sh`, run by `test.sh`, builds a throwaway
  origin and clone to prove it.
- Nothing checked that `test.sh` runs every test suite, which is how four
  suites, one of them with 19 failures, went unrun until v1.3.0. The model
  workflow tests now require a `run_test_target` line for every `kind: test`
  manifest under `tests/` and `libraries/`, and no line for a suite that does
  not exist.
- A fresh checkout of v1.3.0 failed `./check.sh` and `sketch --check` on
  every sketch application with `missing generated Arduino.h`. Both checks
  require the five forwarders the sketch tool writes beside `Sketch.h` --
  `Arduino.h`, `Print.h`, `Printable.h`, `Wire.h`, and `SPI.h` -- but
  `.gitignore` excluded them, a rule left from when the compatibility header
  itself was named `Arduino.h`. The rule is gone, the forwarders of
  `apps/ino/blink`, `button`, and `echo` are committed, and
  docs/modules-ino.mdy says every sketch application commits all six
  generated headers and what the check compares.

## [v1.3.0] — 2026-09-29

Something to run sketches with. v1.2.4 gave a program sound; v1.3.0 gives the
project a language of its own to be driven in and a door for code written for
another platform: `.ino` sketches build as ordinary modules.cpp applications,
inside the tree or outside it, over `mm.sketch`'s Arduino-compatible
vocabulary; a shell of the project's own runs the tracked scripts and the root
launchers without `/bin/sh`, and scales down to an allocation-free embedded
shell on a Pico; and `mm.parse` gives both shells and the JSON and configure
readers one scanner and one number reader. Manifests gain `mm: 1.3`.

### Added

- **External sketches through `project:`.** External sketches and
  application trees located outside the project repository checkout build, run,
  flash, debug, and check using the project's toolchain and modules via the
  `project:` key (introduced in `mm: 1.3`). The loader grafts the external
  application tree under an allowlist admitting only `dir` and `app` with
  `sketch:`. The artifact context scopes compilation and output into the
  external root's `out-<lane>` directory, compiling project modules and board
  objects under `graft/project/` without modifying the project's checkout or
  `out/bin` tools. The `sketch` preprocessor provides 4-case tree detection,
  atomic file generation through `write_guarded` using POSIX `openat` and
  `renameat`, and build-time regeneration when `main.cpp` is missing or stale.
- **Sketches.** `mm.ino` turns `.ino` sources into a standard C++20
  translation unit: a prelude, hoisted includes, generated prototypes, the
  sketch bodies with line markers, and a `main` that calls
  `mm::sketch::run`. `tools/sketch` is its front end, including a library
  mode that registers a sketch library's examples, `--check` for committed
  output, and a source guard that writes through `openat` and `renameat`
  without following symlinks. `mm.sketch` is the vocabulary above `mm.mcu` and
  `mm.stdio`: digital, analog, and advanced I/O, time, math, characters,
  random numbers, bits and bytes, external interrupts, `Serial`, `SPI`,
  `Wire`, `Stream`, `String`, `Print`, and `Printable`, with a generated
  `Sketch.h` compatibility header for vendored libraries. `apps/ino` holds
  the blink, button, and echo sketches. docs/modules-ino.mdy and
  docs/modules-sketch.mdy specify them.
- **The project shell.** `mm.shell` is now a target-safe, allocation-free
  level-1 shell over caller-owned storage, with `apps/shell-smoke` as its
  acceptance application; `mm.shell.mcu` composes it with `mm.mcu` and
  `mm.stdio` at level 2 (`apps/mcu-shell`, measured against
  `apps/mcu-shell-size-control` on RP2040 and RP2350); `mm.shell.full` is the
  level-3 host profile with pipelines, redirections, here-documents,
  subshells, traps, functions, and command substitution over abstract
  services; and `mm.shell.posix` keeps the legacy POSIX services behind their
  own module. `tools/shell` runs scripts natively with `--run`, `-c`,
  `--check`, `--tokens`, `--dump-ast`, `--capabilities`, and `--commands`,
  and the thirteen no-extension root launchers run their `.sh` scripts
  through it. `--legacy-sh`, or `MM_SHELL_LEGACY=1` for the launchers, is a
  one-release rollback to `/bin/sh`. The tracked scripts are compared with
  dash for status, output, files, and the environment handed to child tools.
  docs/modules-shell.mdy specifies all four levels.
- **`mm.parse`.** One shell scanner, `Cursor`, for both shell dialects,
  driven through a function-pointer `Sink`, with the shared `TokenKind`,
  `FragmentKind`, and `SourceSpan`; a number reader for decimal, `0x`, `0o`,
  and `0b` integers with `_` separators, fractions including `.5`, and
  exponents, whose integers reach `INT64_MIN` and whose decimal integers also
  carry their nearest double; and a time reader for durations, epoch seconds,
  times, dates, and date-times. It has no `use:` edge. docs/modules-parse.mdy
  specifies it.
- **`mm.json` extract functions.** `extract_time(text, out)` and
  `extract_number(text, integer, number, is_integer)` in `mm.json:scan`,
  delegating to `mm.parse::parse_time` and `mm.parse::parse_number`.
  `TimeExtracted` uses flat fields mirroring `mm.parse::DateTime` so the
  header does not need to import `mm.parse`. `tests/mm/json/extract.cpp`
  covers extraction from parsed JSON string values including ISO dates,
  datetimes, times, epochs, durations, hex, scientific, binary, octal, and
  invalid inputs.
- **`./check.sh` lists documented exceptions, and `--strict`.** A use the
  Known non-conformance list in docs/modules-c++20.mdy permits is printed as an
  exception on every run and counted in the summary; `--strict` reports each
  as a violation and fails. mm.sketch's unscoped `Mode`, `Level`, and
  `BitOrder` and its `SketchNumber`, `sq`, `constrain`, and `map` templates
  are the entry this release adds, excepted by name so anything else is still
  an error.
- `scripts/unused-includes.py` reports standard headers a translation unit
  includes and never uses.
- `scripts/configure-pico.sh` owns the Pico board table and configures a
  Pico lane; `scripts/build-pico-project.sh` builds an external project for a
  Pico board with it.

### Changed

- `mm.build` is split into partitions -- `:graph`, `:manifest`,
  `:platform`, `:config`, `:compile`, and `:external` -- in place of one
  5,700-line source, and its tests are regrouped to match, with new coverage
  of root resolution, version rules, the link gate, lanes, and library link
  inputs.
- `mm.shell`, `mm.shell.full`, `mm.json`, `mm.configure`, and `mm.sketch` read
  shell tokens and numbers through `mm.parse` instead of their own scanners
  and accumulators. Shell arithmetic still accepts decimal digits alone, and
  JSON still refuses a number too large for a double.
- **`.` in the full-profile shell.** `out/bin/shell --run` runs `. FILE`
  in the current shell, so the build scripts under `scripts/`, which share
  `scripts/lib` that way, parse and run in the project shell as in dash: a
  name without a slash is looked for on `PATH`, return ends the file, operands
  after it are its positional parameters while it runs, and an unreadable file
  ends the shell with status 2. `source` stays excluded. Inside a function
  `$0` is now the script's name rather than the function's, as POSIX has it.
- `scripts/build-pico.sh` is the Pico platform script from v1.2.4; the
  external-project builder `main` had under that name is
  `scripts/build-pico-project.sh`.
- The Linux device-map override file is opened by `platform.linux.defaults`,
  not by the `platform.linux.map` interface, so the interface compiles for
  bare-metal targets whose C library has no file descriptors.
- `test.sh` runs `tests/mm/parse`, `tests/mm/shell/mcu`, `tests/mm/ino`, and
  `tests/mm/sketch`, which were in the tests manifest but never run.

### Fixed

- Configuring a bare-metal board that declares a linker script wrote absolute
  paths the build refused, and the tree could then not be reconfigured; the
  paths are recorded project-relative.
- `bootstrap.sh` adds `-flarge-source-files -fno-ipa-sra` under GCC 14, whose
  module location space and `-O2` pass otherwise abort on this tree.
- The move to `mm.parse` left `main` unbuildable and, once built, broken:
  newlines vanished from the full shell's tokens, here-document bodies ran as
  commands, IO numbers and lowercase `$name` were not recognised, refusals
  lost their Incomplete and Unsupported kinds, fragment counts were wrong,
  `INT64_MIN` was refused, and JSON accepted `1e400`. All are fixed and
  covered.
- `mm.parse`'s own tests had never run and 19 of 131 failed. The code was
  wrong for epoch dates (the calendar conversion is Howard Hinnant's again),
  a `{` before a letter (a word, not an operator), leading-dot floats, and a
  single operator at the end of the text in the full dialect; the tests were
  wrong for a bare `\r`, a NUL inside a word, comments, continuations, and
  three epoch values, and now state what the scanner has always done.

### Compatibility

- `mm: 1.0`, `1.1`, and `1.2` manifests are unchanged and still valid. `mm:
  1.3` adds `project:`, `sketch:`, and `sketch-library:`.
- `mm.shell`, `mm.shell.full`, `mm.json`, `mm.configure`, and `mm.sketch`
  each gain a `use: mm.parse` edge. `mm.shell::TokenKind`,
  `mm.shell::FragmentKind`, and `mm.shell::SourceSpan` are aliases of
  `mm.parse`'s.
- `mm.json::TimeExtracted` is new; existing `mm.json` interfaces are
  unchanged.
- The root launchers no longer run their scripts through `/bin/sh` when
  `out/bin/shell` is installed; set `MM_SHELL_LEGACY=1` to restore it for this
  release.

## [v1.2.4] — 2026-09-28

Something to listen with. v1.2.3 gave a program the analog world; v1.2.4
gives it sound: a portable audio interface of sources, sinks, and the stream
between them, I2S in `mm.mcu` as a continuous transport over PIO and DMA on
every Pico SDK lane, an ES8311 codec driver for both directions, and audio
providers for the RP2350 LCD 1.54 family and the RP2350-Touch-LCD-2.8. It
adds critical sections to `mm.mcu` and the transport model to the execution
model, and rebuilds the build scripts around two platform scripts whose
checks are read from the manifests.

### Added

- **`mm.audio`.** A portable audio interface of three interface classes:
  `In`, a source of sampled sound; `Out`, a sink; and `Stream`, the
  connection with one producer and one consumer that carries samples between
  either one and an application, or between the two directly. It knows no
  pin, transport, or board: a board's platform provider derives a
  `Microphone` from `In` and a `Speaker` from `Out` and registers one of each.
  `Ring` is the shipped `Stream`, a ring over caller-owned storage that never
  allocates, with two indices each written by one side through plain atomic
  loads and stores, zero-copy regions bounded by the wrap, device claims per
  end, and underrun and overrun counts. Samples are sixteen-bit and mono;
  `to_sample`, `to_sample_offset`, and their inverses convert to and from any
  width from one to thirty-two bits. `Format` carries the nominal rate and
  `Rate` the exact one, because two devices asked for one rate generally run
  at two. `Out` adds `pending` for draining; `service` never waits, a device
  keeps what its transport did not accept, and silence on underrun is the
  transport's. The fallbacks answer `Unsupported`. `tests/mm/audio` pins the
  arithmetic at every width, the ring at every offset of capacities one
  through nine, and three million samples through a ring of sixty-one on two
  threads. docs/modules-audio.mdy specifies it, and drafts/plan-audio.mdy
  records the research and the board providers still to come.
- **`mm.audio.es8311`.** A portable driver for the Everest ES8311 codec over
  `mm.mcu` I2C and I2S, brought over from the audio-work branch and reworked
  for this design: `Codec` is the chip, shared by `Output`, its DAC as an
  `mm.audio::Out`, and `Input`, its ADC recording the analog microphone as an
  `mm.audio::In`. The chip is identified by its ID registers, reset once, and
  runs as the I2S slave with its master clock taken from the bit clock; both
  directions share the link's one rate. `Output` keeps a pending run through
  zero and partial acceptance and errors, sends each sample in both slots,
  and reports transmitter silence as underrun; `Input` takes no more than its
  Stream holds, reads the board's slot at the board's PGA gain, and reports
  receiver drops as overrun. The review corrected the earlier version's I2C
  address (0x06 to 0x18), its serial-port register (the ADC's 0x0a to the
  DAC's 0x09 for playback), and its missing clock and power-up programming.
  `tests/mm/audio/es8311` pins the transcripts and both contracts against a
  recording full-duplex platform. Unqualified on hardware.
- **RP2350 LCD 1.54 family.** `rp2350_lcd_154`, the non-touch RP2350-LCD-1.54
  and its -EN option, derives from pico2-arm and binds the ST7789 panel, the
  QMI8658, and the ES8311 audio; `rp2350_touch_lcd_154`, the touch model and
  its -EN option, now derives from it and adds only the CST816. The audio
  provider, `platform.rp2350_touch_lcd_154.audio` in `platforms/pico`,
  registers an ES8311 `Output` that also drives the NS4150B amplifier enable
  on GP0 as the Speaker and an ES8311 `Input` as the Microphone, running the
  codec as the I2S slave on GP1, GP2, GP4, and GP5 with its MCLK input GP3
  held low. The Pico bridge's board table gives both boards a 3.3 V ADC
  reference, no LED -- pico2's GP25 is their volume button -- and a default
  UART of UART1 on GP26 and GP27, because pico2's GP0 and GP1 are the
  amplifier enable and the codec's playback data. `apps/audio-smoke` plays a
  tone while recording through whichever devices a board registers. The
  touch board's IMU provider now registers its sensor, which it never did,
  and its panel is clocked at the ST7789V2's 62.5 MHz limit rather than a
  requested 230 MHz. Wiring and register values were checked against the
  family's schematic and the ES8311 datasheet; none of it is qualified on
  hardware.
- **I2S on the Pico SDK lanes.** `platform.pico.mcu` implements `mm.mcu` I2S
  instance zero over PIO and DMA in the bridge's adapter, on RP2040 and on
  RP2350 Arm and RISC-V. A clock state machine drives the bit and word clocks
  by side-set and shifts the transmit line from configure to release; a
  receive state machine samples the receive line in step with them. Each is
  fed by two DMA channels in ping-pong over blocks the adapter owns, and the
  DMA interrupt refills or empties those blocks from two 256-frame rings,
  sending zeros and counting missed frames when the transmit ring runs dry
  and dropping and counting frames the receive ring cannot hold. The rate is
  reported exactly from the PIO clock divider. The execution model's model 3
  now names that block copy among a transport handler's bounded work. Built
  for every Pico SDK lane; unqualified on hardware.
- **RP2350-Touch-LCD-2.8 audio.** `platform.rp2350_touch_lcd_28.audio`, in
  `platforms/pico`, which the board now binds: a Speaker straight over `mm.mcu`
  I2S on GP2, GP3, and GP4 for the board's PCM5101A, which has no control port,
  and a Microphone that answers `Unsupported`, the board having none. The
  Speaker keeps the `Out` contract, sends each sample in both slots, and
  configures the nearest of the rates the PCM5101A's datasheet lists for its
  PLL from a bit clock of 32 times the rate, keeping the clocks running across
  stop and start. The Pico bridge's board table gives the board a 3.3 V ADC
  reference and no LED, its GP25 being the battery key, and the board's pin
  maps were checked against Waveshare's schematic. `apps/audio-smoke` now
  plays alone on a board whose Microphone answers `Unsupported`.
- **Paced ADC capture and a DAC in `mm.mcu`.** `adc_pace`, `adc_pace_rate`,
  `adc_pace_start`, `adc_take`, `adc_pace_progress`, and `adc_pace_stop`
  capture a channel continuously into a buffer the platform owns;
  `AdcChannel` gains `maximum_pace_hz` and `pace_depth`. A paced channel owns
  the whole converter, so every `adc_read` and every other channel's
  configure or pace answers `Busy` until release. The new DAC facility,
  `dac_description` through `dac_release` over a `DacOutput` inventory,
  queues levels at a rate and holds half scale -- not its last level -- when
  starved. Both report exact rates as a `Frequency` and what they have done
  as a `Progress`. Every provider inherits `Unsupported`, and paced
  capture's limits are zero, until its implementation lands.

- **Critical sections in `mm.mcu`.** `interrupts_disable` and
  `interrupts_enable` over an `InterruptState`, and `InterruptGuard` for a
  scope: between the two no handler of the selected platform runs on the
  calling core. Enable restores what its disable saved, so sections nest; a
  held state cannot begin a second section and an enable without a disable
  is refused before the platform is asked. The Pico provider uses the SDK's
  `save_and_disable_interrupts` and `restore_interrupts`; the Linux,
  rp2040-ram, and widget-rp2040 providers install no handler and answer Ok
  without masking. `tests/mm/mcu/interrupt.cpp` pins nesting, order, and the
  guard.

### Changed

- **Build scripts.** Two platform scripts, three families of wrapper, and
  checks read from the manifests replace the seventeen hand-written
  `scripts/build-*.sh`. `scripts/build-pico.sh` builds one application for
  any Pico board, a composite board taking its vendor ancestor's lane, and
  `scripts/build-linux.sh` for any native Linux lane. What the image must
  contain is no longer written in the scripts: `scripts/lib/manifest.sh`
  walks the application's closure through the lane's bindings -- the board,
  its bases, and its SDK -- to a fixed point, as the build does, and the
  image must carry one initializer of every provider in that closure, none
  of every other provider the lane binds, the symbols of every driver a
  reached provider uses, and a NEEDED entry for every library link input. An
  application reaching an interface the lane does not bind is reported
  unavailable with exit 77.
  - Feature wrappers, each a short declaration that works on every board of
    either platform, `--board` picking the platform from the board's chain:
    `build-display.sh`, `build-gfx.sh`, `build-font.sh`, `build-epaper.sh`
    (`--app gfx|font`, `--panel bw|bwr|b`), `build-analog.sh`,
    `build-gpio-edge.sh`, `build-stdio.sh`, `build-board.sh`,
    `build-audio.sh`, and the two that build twice to assert a difference,
    `build-sdl.sh` and `build-linux-smoke.sh`. On the SDL and e-paper Linux
    boards `--run` must succeed.
  - Target wrappers over `scripts/build-target.sh`, which builds for any
    target lane configure accepts and checks the image's ELF machine as well:
    `build-aarch64-linux-gnu.sh`, `build-x86_64-linux-gnu.sh`,
    `build-arm-linux-gnueabihf.sh`, `build-arm-none-eabi.sh` (mps2-an385
    under qemu-system), `build-m68k-linux-gnu.sh`, and
    `build-m68k-linux-external.sh` (cmake-demo-smoke). `--run` goes through
    `./run` when the lane has a runner, and otherwise runs a hosted image
    directly or under qemu user mode with the SDK's runtime prefix.
  - Every script takes `--dry-run`, which prints the lane, the commands, and
    every check without touching the tree or needing a toolchain;
    `tests/scripts/run.sh`, run by `test.sh`, pins forty-four of them. Every
    script restores the configuration record it found, a cross target or a
    board included, rather than resetting to the host lane, unless given
    `--keep`. Exit-code explanations live beside their applications as
    `exit-codes` files.
  - The per-board and per-lane scripts are removed: `build-gfx-demo-*`,
    `build-font-demo-*`, `build-board-smoke-rp2350_touch_lcd_28`,
    `build-analog-smoke-pico`, `build-gpio-edge-smoke-pico`,
    `build-stdio-smoke-pico-sdk`, and the `build-linux-*` scripts but
    `build-linux-smoke.sh`. Each maps to a wrapper and a board, as
    `build-linux-epaper-font-demo.sh` does to
    `build-epaper.sh --app font --board epaper`. The Pico, Linux, stdio, and
    platforms specifications name the new scripts.
- `tests/mm/touch-cst816` moved to `tests/mm/touch/cst816`, beside the CST328
  tests it shares an interface with, as `tests/mm/audio/es8311` sits under
  `tests/mm/audio`. The suite keeps its name, touch-cst816, and `test.sh` now
  runs it.
- **Execution model, model 3.** docs/modules-execution.mdy adds the
  transport: a continuous transport's handler may acknowledge, count, re-arm
  a DMA channel on the provider's buffer, switch to a buffer of silence, and
  drop a sample it has no room for, and nothing more; no handler touches
  memory above the seam. It also states what a critical section may contain,
  and records masking as the one exception to general interrupt access.
- **I2S in `mm.mcu` is a continuous transport.** Its clock runs from
  configure to release, so the byte-span `i2s_write` and `i2s_read` became
  queueing calls over a buffer the platform owns, with `i2s_rate`,
  `i2s_start`, `i2s_progress`, `i2s_stop`, and `i2s_release` beside them.
  `I2sConfiguration` names bit and word clock GPIOs, optional transmit and
  receive GPIOs, `rate_hz`, and `slot_bits` in place of the data, clock,
  word-select GPIOs and baud; sixteen-bit slots move `std::int16_t` words and
  wider ones `std::int32_t`, a starved transmitter sends zeros by itself, and
  a link claims its pads. Paced ADC capture, the DAC, and I2S share one
  lifecycle, specified once in docs/modules-platform-mcu.mdy. No provider
  implemented the earlier form.

## [v1.2.3] — 2026-09-27

Something to measure with. v1.2.2 put pictures on the panels; v1.2.3 gives a
program the analog world and the wire beneath it: an ADC and a PWM in
`mm.mcu` beside GPIO, SPI, and I2C, implemented on the Pico SDK lanes and on
Linux; a GPIO edge latch that reports what happened on a pin without ever
running application code in a handler; the execution model that says why it
is a latch, written down once for every interface; and a core JSON module
that the build itself is the first consumer of. It also fixes the Clang link
failure the Linux tests met on a Raspberry Pi, and documents the Linux
device map in full.

11 commits since v1.2.2; 460 files changed, 8110 insertions, 277 deletions,
352 of those files the vendored JSON Parsing Test Suite.

### Added

- **Execution model.** docs/modules-execution.mdy specifies how a modules.cpp
  program executes: one thread of control from main to return, every call
  returning, no callback or handler running application code, and hardware
  that acts on its own recorded in a latch the program asks when it chooses.
  It states once the rules the interface documents apply one at a time, and
  the Linux console and device map were brought to it: a console write is
  bounded on a pipe, FIFO, terminal, or socket and plain on a regular file,
  and the map override is read through a nonblocking descriptor checked to
  be a regular file and capped at 64 KiB, so the first board query returns.
- **`mm.json`.** A core module reading and writing RFC 8259 JSON without
  exceptions, templates, or, in its scanner, allocation. The `:status`
  partition names the faults and where they are; the `:scan` partition is a
  pull scanner that answers one token per call with strict grammar, UTF-8,
  and surrogate checks, plus decoding into a caller's span and the two
  numeric conversions; the `:value` partition is a document model with
  `parse` over an explicit stack, `write` in compact and indented layouts,
  and duplicate-key and wide-integer policies. Every output changes only on
  Ok. `tests/mm/json` pins the grammar, the decoder, the numeric boundaries,
  the value model, and the JSON Parsing Test Suite, vendored under
  `tests/mm/json/fixtures` with its notice. docs/modules-json.mdy specifies
  it.
- **`json` tool.** `tools/json` with root launchers `json` and `json.sh`:
  `--check` reports the first fault of each file as
  `FILE:LINE:COLUMN: DESCRIPTION (STATUS)`, `--indent` and `--compact`
  rewrite a document in either layout, and `--scan` lists the scanner's
  tokens.
- **ADC and PWM in `mm.mcu`.** Two facility partitions beside SPI and I2C,
  with inventories the platform answers per channel and per output:
  `adc_configure`, `adc_read`, `adc_release`, and `adc_channel_for_gpio` over
  `AdcChannel` with its width and reference; `pwm_configure`, `pwm_period`,
  `pwm_write`, `pwm_release`, and `pwm_output_for_gpio` over `PwmOutput` with
  its counter group, comparator, and period limits. `adc_millivolts` converts a
  count in integer arithmetic; `pwm_plan` turns a period into a divider and a
  top for a prescaler-and-top counter such as the RP2040's and RP2350's,
  reserving one count so full duty always fits the compare register. A pad is
  one thing at a time: a plain GPIO yields to an analog claim, a watched GPIO
  and an analog claim yield only to their own release. Every provider inherits
  `Unsupported` for both until its implementation lands; the host stand-in
  and `tests/mm/mcu` pin the contracts and the planner against an independent
  rational model. The Pico SDK provider implements both over `hardware_adc`
  and `hardware_pwm` through its adapter, which now keeps one owner per pad
  across GPIO, the edge latch, ADC, and PWM; the ADC reference is the selected
  board's own row in the bridge's `resolve-board.cmake`. `analog-smoke` and
  `scripts/build-analog-smoke-pico.sh` are the wired fixture: PWM through an
  RC filter into the ADC, read as raw ratios. The Linux provider implements
  both over IIO and PWM sysfs from new `adc.*` and `pwm.*` device-map keys,
  with the same one-owner-per-pad rule.
- **GPIO edge latch in `mm.mcu`.** Portable `gpio_watch`, `gpio_take`,
  `gpio_unwatch`, and bounded `gpio_wait` report selected physical edges
  without running application callbacks inside interrupt handlers. Pico SDK
  and Linux GPIO-v2 providers implement the facility; Linux uses a bounded
  event read and monotonic poll, while Pico uses its GPIO callback and an
  event-assisted wait. `gpio-edge-smoke` builds for Pico ARM and RISC-V and
  provides a wired fixture for physical verification.
- **`mm.touch.cst816`.** The Goodtek CST816 capacitive touch controller over
  portable mm.mcu I2C and GPIO, beside `mm.touch.cst328`. Eight-bit register
  map; the chip identifier register is checked at initialization; the
  finger-count poll is followed by the coordinate read only while a contact is
  on the panel; the four-byte point decodes the high nibbles and the low
  bytes. The reference driver's interrupt handler, gesture mode, and mirror
  transform are absent for the same reasons the CST328 ones are: mm.touch is
  polled, reports points in the panel's own coordinates, and mm.mcu has no
  callbacks. `tests/mm/touch-cst816` pins the transcript against a recording
  platform.
- **`rp2350_touch_lcd_154` board.** The Waveshare RP2350-Touch-LCD-1.54
  composite board. It derives from pico2-arm and binds three platform
  providers: an ST7789 panel (240 by 240, RGB565, SPI1, backlight on GP13) on
  `mm.display`, a CST816 touch controller at address 0x15 (reset on GP16,
  interrupt on GP15) on `mm.touch`, and a QMI8658 inertial sensor on
  `mm.imu`. The panel's porch, power, and gamma settings and the three pin
  maps come from Waveshare's public firmware repository for this product,
  which is the same source the vendor's examples run; the wiki remains
  unreachable to automated requests. Its panel settings live with the board,
  not the driver, exactly as the 2.8 board's do.

### Changed

- `mm.build` reads a bridge's `compile_commands.json` through `mm.json`; the
  private reader it carried is gone, and the bootstrap compiles `mm.json`
  before `mm.build`.
- docs/modules-platform-linux.mdy documents the device map in full: its two
  layers, the override grammar, every key of every facility with its default
  and meaning, the validation rules, and a complete example for a Raspberry
  Pi.
- A watched GPIO is owned by its provider: another watch or configuration
  answers Busy, and unwatch leaves the pin unconfigured. Linux maps ENXIO and
  EOPNOTSUPP to Unsupported for edge requests while retaining the existing
  transport-error mapping for other MCU operations.

### Fixed

- The Linux providers' subclasses, registered objects, and registration
  objects moved from unnamed namespaces to named non-exported ones
  (`platform::linux::<name>_provider`). Clang emits an interface unit's
  unnamed-namespace objects again in every importer, so `tests/mm/linux`,
  which imports the DRM provider for its testing seam, failed to link under
  Clang with `undefined reference to vtable for (anonymous
  namespace)::LinuxDisplay`; GCC was unaffected. docs/modules-c++20.mdy
  states the rule. This is the fix `main` carried since before v1.2.2.
- A `\u` escape in a `compile_commands.json` path was copied through as
  text and never matched the ABI probe; it is decoded now. A compile
  database that is not JSON is reported with its line and column instead of
  being read past.

## [v1.2.2] — 2026-09-17

Something to draw with. v1.2.1 bound colour and e-paper panels to portable
interfaces and proved the closure links. v1.2.2 puts pictures on them: a
bitmap font module with committed IBM Plex Mono tables, a graphics module of
caller-owned packed surfaces, drawing primitives, quarter-turn rotation, and
palette expansion, and two demonstration applications that run unchanged on
the one-bit e-paper, the RP2350 colour LCD, and both Linux emulations. It
also opens the Linux lanes to Clang and the whole build to GCC 14.

16 commits since v1.2.1; 55 files changed, 6045 insertions, 45 deletions.

### Added

- **`mm.gfx`.** A software rasterizer above `mm.display` and below everything
  that draws. `Surface` is a caller-owned packed frame in the display's own
  layout — one bit per pixel, MSB left, one is white, or big-endian RGB565 —
  with total `row_bytes`, `size`, and `valid` that return zero or false
  rather than a wrapped number on any dimensions. `fill`, `pixel`, `line`,
  `rectangle`, and `fill_rectangle` clip to the surface, take
  `mm::display::Color` on one-bit surfaces (`Red` is `Unsupported` before any
  pixel changes) and `Rgb565` on sixteen-bit ones, and never touch a one-bit
  row's padding bits. `rotate` turns a one-bit surface clockwise by quarter
  turns, and a `constexpr` overload turns a rectangle with it. `expand_row`
  maps a packed row to RGB565 through a `Palette` of ink and paper. Two
  `write` overloads carry a surface to a display: same-depth, sending exactly
  the surface's pixel bytes and enforcing the one-bit controller's byte-aligned
  window rule before the provider sees it; and one-bit onto a sixteen-bit
  panel, expanded a row at a time through a base palette and a list of
  rectangular `Region`s with palettes of their own, so a one-bit composition
  is the memory-sized path to a colour panel. Neither initializes, clears, or
  refreshes.
- **`mm.fonts`.** Monospace bitmap text on a `Surface`: `Glyph`, `Font`, and
  `TextMetrics`; `measure`, which counts well-formed UTF-8 code points and
  stops at a malformed one; and `render`, a transparent one-bit compositor
  that inks glyph pixels and leaves every other bit alone, rejects a trailing
  partial sequence, clips at the surface's right edge, and refuses text past
  its bottom. Two committed tables, `kMono12` and `kMono16`, rasterized from
  IBM Plex Mono (SIL Open Font License 1.1, `modules/mm/fonts/licenses/OFL.txt`)
  over ASCII and the eighteen Polish letters, 113 code points each, with the
  face's SHA-256 in the generated file's header.
- **`font-demo` app.** Two lines at sixteen pixels and two at twelve, the last
  in the Polish charset, then every glyph of the twelve pixel table wrapped to
  the panel, shown upright and turned a quarter clockwise three times. A
  sixteen-bit panel expands each line through its own palette, turned with
  the frame; a one-bit panel gets the same bits black on white. Build scripts
  for Linux SDL, Linux e-paper, Pico e-paper (both panels), and the RP2350
  LCD, each asserting the provider closure and the presence of both tables.
- **`gfx-demo` app.** Every drawing primitive in one scene: an ordered-dither
  glow, nested frames, a haloed porthole with a starburst from a compile-time
  sine table, and an orientation tick, turned through four orientations. On a
  colour panel the expanding write runs through six hue-cycling regions that
  turn with the scene, and a plasma drawn directly in RGB565 is written
  through the porthole at the panel's depth. The same four build scripts.
- **`epaper-linux-aarch64` board.** The emulated SSD1680 board existed only
  for x86-64; the AArch64 board is the same two rebindings over
  `generic-linux-aarch64`, so the e-paper lane runs on an ARM development
  machine.
- **Specifications.** `docs/modules-fonts.mdy` and `docs/modules-gfx.mdy`,
  each marked with the release it governs.

### Changed

- **A hosted SDK may declare `compiler-family: any`.** The two Linux SDKs do:
  they supply no specs file, linker script, or runtime prefix, so they own
  none of the link and constrain none of the toolchain. A lane on them may
  name GCC or Clang, and `configure` compares target triples by machine rather
  than by spelling, since Clang writes the vendor field where GCC omits it.
  The build's processor table gains the Clang rows for both hosted triples. A
  bare-metal SDK cannot say `any`, and the loader rejects it there. The four
  Linux build scripts take `--compiler` accordingly.
- **The GCC target-option probe no longer passes `-c`.** `--help=target` is
  answered by compiling a synthetic input named `help-dummy`; with `-c` the
  driver also assembled it, leaving `help-dummy.o` in the working directory
  after every target-lane build. The listing is identical without it, for
  ARM, RISC-V, and m68k. `clean.sh` removes a stale one.

### Fixed

- Bootstrap and `configure` under GCC 14: two `const auto` bindings of
  `options.values(...)` triggered an internal compiler error in
  `simplify_aggr_init_expr`; both are spelled as `std::vector<std::string>`
  now. GCC 14 bootstraps, builds, and passes the full host test suite.
- The build refused Clang for Linux applications even after the SDK allowed
  it, because the processor table had no Clang row for a hosted triple.

### Known limitations

- **The font table generator is not in the repository.** `data.cppm` says it
  was produced by `tools/fonts/gen_font.py` from a pinned TTF, and the
  specification describes regenerating it, but the script is not committed.
  The tables can be used and verified against the golden bitmaps in the
  tests; they cannot yet be regenerated from source.
- Text and graphics on the physical panels have not been looked at. The
  compositions are pinned by host tests and were viewed through the SDL and
  emulated e-paper lanes; the RP2350 LCD and the Pico e-paper builds are
  inspected images, not observed pictures.
- `mm.fonts` composes one-bit only. Colour text goes through the expanding
  write; there is no transparent glyph over an RGB565 surface, no stored
  bitmap `blit`, no read-only surface view, no sixteen-bit `rotate`, and no
  second plane for the tri-colour panel's red.
- The gfx demo composes with per-pixel calls through the public API; that is
  what it is for, and on an RP2040 the dithered glow costs tens of
  milliseconds per orientation.
- No tool converts a tuning option into a compiler or linker argument.
  Carried from v1.2.0; unchanged.
- Nothing automated asserts that a Pico image ran. Carried from v1.2.0;
  unchanged.
- RP2350 RISC-V hardware has not been exercised. Carried from v1.2.0;
  unchanged.
- Builds are full rather than incremental. Carried from v1.2.0; unchanged.

### Compatibility

- `mm: 1.0`, `mm: 1.1`, and `mm: 1.2` manifests are unchanged and still valid.
- `compiler-family: any` is a new value for an existing `mm: 1.2` key on
  hosted SDK manifests; the loader rejects it on bare-metal SDKs. No other
  manifest key or value is introduced.
- `mm.fonts` and `mm.gfx` are new modules; no existing module's interface
  changes.
- The configuration record stays `configuration-2`.
- GCC 14 or newer, or a recent Clang, builds the project; GCC 15 or newer is
  recommended.

## [v1.2.1] — 2026-09-15

Hardware device interfaces and a native Linux target. v1.2.0 proved that one
manifest tree produces host tools, emulator targets, and firmware for the
Raspberry Pi Pico. v1.2.1 adds the portable device interfaces that a
composite board binds — touch, inertial sensing, real-time clock, and colour
display — with project-owned controller drivers, a new custom board that
exercises all four at once, a portable console, a native Linux platform, and
the CI fix that keeps the gate honest.

19 commits since v1.2.0; 171 files changed, 12954 insertions, 66 deletions.

### Added

- **`mm.mcu:i2c`.** A portable I2C bus facility in `mm.mcu`: `I2cConfiguration`
  (instance, SDA, SCL, baud), `i2c_configure`, `i2c_write`, `i2c_read`, and
  `i2c_write_read`. The write-read pair is one transaction, not two, so a
  device's address pointer is not exposed to another master between the
  command and the reply. Seven-bit addressing only; ten-bit is a future
  extension when a device that uses it appears.
- **`mm.touch` and `mm.touch.cst328`.** A portable touch input interface and
  the Hynitron CST328 capacitive controller over I2C and GPIO. `read` fills
  as many points as the panel reports into the caller's span; no contact is
  `Ok` with count zero. Coordinates are in the panel's own space; rotation and
  calibration belong to the caller. The driver owns the sixteen-bit
  big-endian register protocol, a bounded reset, and a state machine.
- **`mm.imu` and `mm.imu.qmi8658`.** A portable inertial measurement interface
  and the QST QMI8658 six-axis sensor over I2C. Readings are signed counts in
  the sensor's own axes with a `Scale` that says what a count is worth, so a
  caller converts to whatever units it wants. `read` returns acceleration and
  rotation from one atomic reading. Temperature is hundredths of a degree
  Celsius. The driver owns acceleration and rotation range selection, output
  rate, address auto-detection, and a bounded state machine.
- **`mm.rtc` and `mm.rtc.pcf85063`.** A portable real-time clock interface and
  the NXP PCF85063 I2C clock. `DateTime` is a plain calendar, not an epoch:
  full year, month, day, weekday, hour, minute, second. `read` reports the
  time alongside a `trusted` flag — a clock that lost power still answers
  with well-formed and meaningless fields. The driver owns BCD packing,
  oscillator-stop detection, and a century fix at 2000.
- **`mm.lcd` and `mm.lcd.st7789`.** A colour display controller module for the
  Sitronix ST7789 over SPI and GPIO. Pixels are RGB565 at sixteen bits per
  pixel; `write` places them in the named window and `refresh` answers `Ok`
  because the panel shows what was written when it was written. The board
  supplies the panel's initialisation commands, porch, power, gamma, memory
  access, inversion, and offset. Clear maps the three `mm.display` colours
  onto their RGB565 values.
- **`mm.stdio`.** A portable console: a platform-selected role for moving
  bytes to and from wherever the platform's console goes — USB CDC on Pico,
  descriptors 0 and 1 on Linux. `write` reports how much it accepted; `read`
  with nothing available is `Ok` with count zero; `connected` is a query,
  not a wait. No formatting, no line discipline, no baud. The Pico SDK
  provider uses USB CDC; the Linux provider uses file descriptors with
  zero-timeout input polling, partial output counts, and sticky loss. A
  `stdio-smoke` app exercises the interface.
- **`rp2350_touch_lcd_28` board.** The Waveshare RP2350-Touch-LCD-2.8
  composite board. It binds four platform providers: an ST7789 colour panel
  on `mm.display`, a CST328 touch controller on `mm.touch`, a QMI8658
  inertial sensor on `mm.imu`, and a PCF85063 clock on `mm.rtc`. Each
  provider owns its own wiring; the device drivers are portable modules under
  `modules/mm` and the board is the only place that knows which pin is which.
  A `board-smoke` app names the four interfaces and no board, proving a
  four-provider board links all four in one executable.
- **`display-demo` app.** A portable application that draws three RGB565
  colour bars, holds them, and rotates them twice. It exists so a person can
  tell a working panel from a still one, and so the red bar's encoding
  (0xf800) catches a byte-swap that would draw blue instead.
- **`--runner native`.** A runner profile that executes a target image
  directly through `/usr/bin/env`, accepted only when the selected target
  triple equals the build machine's and only with `--target-host`. It is what
  makes a native lane's tests runnable without `--compile-only`.
- **Linux platform.** Native hosted SDKs for `x86_64-linux-gnu` and
  `aarch64-linux-gnu`, implemented directly over libc and the Linux userspace
  kernel ABI. Two boards per architecture: `generic-linux-<arch>` names the
  defaults map provider, and `sdl-linux-<arch>` derives from it and moves the
  screen and pointer into an SDL2 window. Providers cover stdio (file
  descriptors), MCU (GPIO-v2, spidev, I2C_RDWR, termios UART, CLOCK_MONOTONIC),
  RTC (RTC ioctls or CLOCK_REALTIME), display (DRM/KMS dumb buffers, RGB565),
  evdev touch, and IIO. An optional SDL2 backend provides display and touch
  through a window, selected by the board rather than the SDK, so nothing
  pays for it unless a board binds it.
- **Specifications.** `docs/modules-imu.mdy`, `modules-lcd.mdy`,
  `modules-rtc.mdy`, and `modules-touch.mdy`, each marked with the release it
  governs. `docs/modules-platform-linux.mdy` and `modules-stdio.mdy` were
  added in the same series and are included in this entry.

### Changed

- **A library's `link-input` reaches the link line.** It was declared,
  validated, ordered, and carried since v1.2.0 with no command consuming it.
  The project link now appends the link inputs of every library a closure
  reaches, after the objects, once per library however many wrappers reach it,
  and in reverse walk order so a dependency's library follows the module that
  needed it. A library whose checkout is absent is reported before the linker
  runs rather than discovered as an undefined symbol. `library-directory` and
  `link-archive` are still carried and still consumed by nothing, and the
  external CMake link does not receive the segment; both remain under
  docs/modules-libraries.mdy's Current boundaries.
- **`mm::mcu::Status` gains `TransportError`.** `Unsupported` means the
  platform does not have the facility, which cannot also stand for a device
  that is present and failing. The five controller modules that translate
  `mm::mcu::Status` — SSD1680, ST7789, CST328, QMI8658, PCF85063 — each handle
  the new value explicitly. The Pico provider's private ABI cannot originate
  it and does not need to.
- **A module naming a `library:` may take the `platform.` prefix.** A wrapper
  still takes `lib.`; a platform provider that reaches its library directly
  takes `platform.`, because its public dependency is the project interface
  module rather than a foreign library API. The loader machine-checks that the
  prefix is one of the two.
- **A non-core platform provider named by a hosted SDK or board may include
  POSIX and Linux UAPI headers** and invoke their ioctl request macros, for the
  interface it implements and nothing else. Without it the Linux providers were
  not expressible. docs/modules-c++20.mdy carries the allowance.

### Fixed

- Continuous integration build: `cppcheck` was not installed, causing the
  `test` step to fail because `tests/mm/configure` exercises the installed
  `check` tool, which wraps `cppcheck` and exits 127 without it.

### Known limitations

- **No tool converts a tuning option into a compiler or linker argument.**
  Carried from v1.2.0; unchanged.
- Nothing automated asserts that a Pico image ran. Carried from v1.2.0;
  unchanged.
- RP2350 RISC-V hardware has not been exercised. Carried from v1.2.0;
  unchanged.
- `requires-board` matches exactly; a derived board does not satisfy a
  requirement naming its base. Carried from v1.2.0; unchanged.
- A derived board cannot remove one of its base's sources, and multiple bases
  are not supported. Carried from v1.2.0; unchanged.
- Arbitrary tri-color image composition is not implemented; the B panel
  initializes and clears its red plane. Carried from v1.2.0; unchanged.
- picotool is required, not vendored. Carried from v1.2.0; unchanged.
- Builds are full rather than incremental. Carried from v1.2.0; unchanged.
- The four device drivers are modelled on Waveshare's reference drivers for
  the RP2350-Touch-LCD-2.8. They drive the protocol but do not include
  vendor headers, board pin numbers, unit conversions, interrupt handlers,
  or rotation transforms. Hardware qualification on the named board remains
  pending.
- The Linux platform is a native hosted lane. It builds and runs on the
  architecture it names; it does not cross-compile. The SDL2 backend requires
  the SDL2 development libraries at build time.
- `mm.stdio` on the Pico SDK uses USB CDC and requires the Pico SDK's USB
  stack. A board without USB CDC must provide its own `mm.stdio` provider.

### Compatibility

- `mm: 1.0`, `mm: 1.1`, and `mm: 1.2` manifests are unchanged and still valid.
- No new manifest keys are introduced; all new modules use existing `kind:`,
  `module:`, `platform-interface:`, `platform-provider:`, `use:`, `library:`,
  `link-input:`, and `file:` keys under `mm: 1.2`.
- `mm::mcu::Status` gains one enumerator, `TransportError`. Code switching over
  it exhaustively without a default gains an unhandled case; every such switch
  in this repository was updated.
- The configuration record stays `configuration-2`.
- GCC 15 or newer is required, as in v1.2.0.

## [v1.2.0] — 2026-09-14

Targets that are real hardware. v1.1 could cross compile and run a hosted
target under an emulator; a bare-metal target compiled but could not link.
v1.2 adds the three things that were missing — a description of the platform,
a way to consume code the project does not own, and a portable interface to
the machine — so one manifest tree now produces host tools, emulator targets,
and firmware that runs on a Raspberry Pi Pico.

51 commits since v1.1.0; 232 files changed, 19373 insertions, 899 deletions.

### Added

- **Platforms.** `kind: sdk` and `kind: board` manifests describe a target:
  triple, compiler family, runtime, processor properties, linker script, board
  sources, and machine. `configure --sdk` and `--board` select one. A board
  implies its SDK, and `target` is not valid on a board because a board's
  target is its SDK's.
- **Responsibilities.** Five startup obligations — reset vector, initial stack,
  memory layout, runtime init, syscalls — declared by `provides:` on an SDK or
  board. A bare-metal lane must cover all five exactly once; a gap names the
  missing one and an overlap names both claimants, before the linker runs.
- **Libraries.** `kind: library` describes a vendored tree, its licence, and
  its public include interface. `library:` on a module makes it a wrapper and
  on an SDK names the platform implementation. A checkout may be absent, which
  is a valid unselected state rather than an error.
- **External CMake builder.** `external-build: cmake` delegates the final link
  to a project-owned bridge beside the library manifest. Build generates
  `mm-toolchain.cmake` and `mm-inputs.cmake`, validates the bridge's ABI
  projection against the project's own, fingerprints a cache identity over
  every input, and publishes the artifacts the bridge lists in `mm-result.txt`.
- **The core boundary.** `option: core` with `read-only: core` marks a branch
  that may not reach third-party code, machine-checked across `use:` edges and
  for `library:` declarations.
- **`requires-board`.** Pins an application or test to one board. A root build
  skips a mismatch and counts it; an explicit request exits 77.
- **The Pico platform family.** `pico-arm` and `pico-riscv` SDKs, six boards
  (pico, pico-w, pico2-arm, pico2-riscv, pico2-w-arm, pico2-w-riscv), the
  pinned Pico SDK checkout with its CMake bridge and C adapter, the
  `riscv32-pico-elf` toolchain, UF2 validation through real picotool, and
  OpenOCD support. `platforms/pico/install-sdk-tools.sh` provisions the
  prebuilt tools against pinned SHA-256 digests.
- **`mm.mcu`.** A portable, pure C++ microcontroller interface in partitions by
  facility: GPIO, SPI, UART, timer, board. `Unsupported` is a first-class
  answer, so a platform implements what it has.
- **Platform providers.** `platform-interface:` marks a module as a portable
  interface; `platform-provider:` on an SDK or board binds it to an
  implementation. The build injects that provider only into executables whose
  dependency closure reaches the interface, so an unrelated application
  receives no provider object and no static initialiser.
- **`mm.display` and `mm.epaper.ssd1680`.** A portable display boundary and an
  SSD1680 controller with bounded reset and busy handling, packed writes, full
  and partial refresh, and deep sleep, exercised against a recording platform
  without hardware.
- **`flash` tool.** Installs one built target application on a physical board
  through picotool's UF2 loader.
- **Board derivation.** `derives-from:` builds one board on another, inheriting
  its platform identity and specialising the memory map, sources, machine, and
  provider bindings. `boards/` holds a project's own hardware, separate from
  the reference support in `platforms/`.
- **Specifications.** `docs/modules-mm.mdy`, `modules-platforms.mdy`,
  `modules-libraries.mdy`, `modules-cmake.mdy`, `modules-platform-pico.mdy`,
  `modules-platform-mcu.mdy`, `modules-display.mdy`, `modules-epaper.mdy`, and
  `modules-flash.mdy`, each marked with the release it governs.

### Changed

- **Manifest format version 1.2.** Every platform, library, provider, and
  derivation key requires `mm: 1.2`. An older binary rejects such a manifest by
  version rather than assigning a key an older meaning. `mm: 1.3` and `mm: 1.4`
  are rejected as unsupported.
- Provider resolution runs once, after the manifest tree is loaded and before
  either lane tool builds its filtered target tree, because an interface
  requirement decides whether a node survives filtering. `build` and `test`
  consume one analysis rather than approximating it separately.
- Board sources are carried into externally linked lanes, so a board may
  contribute objects to a bridge-owned link.
- The Pico adapter reports the selected board rather than `PICO_BOARD`, so
  `pico2-arm` and `pico2-riscv` are distinguishable at runtime.
- `models::BoardNode` exposes resolved values, the derivation chain, and the
  origin of each value; `model --boards` prints them.
- Generic external-builder code names no library, board, or controller.

### Fixed

- Bare-metal configuration and the Cortex-M33 security domain.
- Pico coupling removed from the generic external build path.
- A UF2 is accepted only when every block carries its magic words, so a bridge
  that copies an ELF under a `.uf2` name fails before publication.

### Known limitations

- **No tool converts a tuning option into a compiler or linker argument.** Only
  `buildable-host`, `buildable-target` and `core` are consumed; the others are
  validated, recorded and reported as ignored.
- Nothing automated asserts that a Pico image ran. Hardware execution is an
  opt-in script whose result a person reads.
- RP2350 RISC-V hardware has not been exercised, and no OpenOCD machine value
  is claimed for it.
- `requires-board` matches exactly; a derived board does not satisfy a
  requirement naming its base.
- A derived board cannot remove one of its base's sources, and multiple bases
  are not supported.
- Arbitrary tri-color image composition is not implemented; the B panel
  initializes and clears its red plane.
- picotool is required, not vendored.
- Builds are full rather than incremental.

### Compatibility

- `mm: 1.0` and `mm: 1.1` manifests are unchanged and still valid.
- The configuration record stays `configuration-2`; its key set is extended,
  and a reader predating a key rejects it by name rather than ignoring it.
- A tree using any 1.2 key requires a tool that supports 1.2.
- **GCC 15 or newer is required.** GCC 14 fails with an internal compiler error
  in `simplify_aggr_init_expr` (`cp/semantics.cc`) compiling `tools/configure`,
  and no source-level workaround avoids it. Continuous integration builds with
  GCC 15 from the Ubuntu toolchain PPA.

## [v1.1.0] — 2026-09-08

Cross compilation. v1.0 built, tested and documented itself with one compiler.
v1.1 adds a configuration step that selects a compiler, a build type and a
target, and the tools that act on it, so one manifest tree can produce host
tools and target artifacts side by side and run and debug the latter under an
emulator.

30 commits since v1.0.1; 86 files changed, 6779 insertions, 406 deletions.

### Added

- **`configure` tool.** Resolves the manifest tree, writes `out/config.mdy`,
  and records resolved settings per node. `build`, `test`, `run` and `debug`
  all read that one file, so they cannot select different compilers.
  `--compiler` accepts `gcc`, `g++`, `clang`, `clang++` with an optional
  version suffix and target-prefixed drivers such as `m68k-linux-gnu-g++-16`;
  `--build` selects `debug` or `release`; `--target`, `--target-host`,
  `--runner` and `--debugger` configure a target lane.
- **Manifest options.** `option:`, `reset:` and `read-only:` keys, inherited
  down the folder tree. Eight registered names: `warnings`, `warnings-error`,
  `optimize`, `debug-info`, `assertions`, `include-dir`, `buildable-host`,
  `buildable-target`. Values are typed rather than raw arguments, so a manifest
  cannot name a compiler, target or linker.
- **`read-only` locks.** Freezes a value for a branch. Redeclaring a locked
  name is an error even when the value would be identical, which keeps
  manifest validity independent of the selected build.
- **Host and target lanes.** `buildable-host` and `buildable-target` say which
  lanes a node can be built for, validated across `use:` edges so a portable
  application cannot depend on a host-only module.
- **`run` tool.** Executes a target binary through the configured runner.
- **`debug` tool.** Connects the configured debugger to a target binary.
  Profiles for `m68k-linux-gnu`: `qemu-user` and `gdb` over a QEMU remote stub.
- **`models.toolchain`.** A toolchain as five roles — compiler, assembler,
  linker, librarian, debugger — bound to programs, where several roles may
  share one program and `invoked()` separates a role a build runs from one that
  is only named.
- Five specifications: `docs/modules-configure.mdy`, `docs/modules-build.mdy`,
  `docs/modules-test.mdy`, `docs/modules-run.mdy`, `docs/modules-debug.mdy`.
- `scripts/release.sh` and GitHub Actions workflows for a clean build on every
  commit and a manually triggered release build.

### Changed

- **Output layout.** `out/` holds bootstrap products, installed commands,
  `out/config.mdy` and generated documentation. A configured native build
  writes to `out-host/`, a target build to `out-target-<triple>/`. A configured
  build never overwrites what bootstrap produced, and a cross-compiled
  executable never replaces a host tool in `out/bin`.
- **Manifest format version 1.1.** A manifest declaring `option:`, `reset:` or
  `read-only:` must declare `mm: 1.1`. Unknown keys are rejected in a 1.1
  manifest and still ignored in a 1.0 one.
- `bootstrap.sh` stages `build0`, `build1` and `configure1`.
- `clean.sh` removes `out-*/` as well as `out/` and `gcm.cache/`.
- Build type defaults and the baseline compile and link flags now come from one
  typed policy rather than separate literals.
- `models.configuration` describes the lane: `host_toolchain()`,
  `target_toolchain()`, `selection()` and the build directories.
- `mm.model` reports the configuration a build would actually follow rather
  than the unconfigured default.
- `--compile-only` on `test` compiles without linking, so it is usable on a
  target that cannot yet link.
- Common CLI flags unified across the tools: `-v`/`--verbose`, `-h`/`--help`.

### Fixed

- `test.sh` smoke tests ran stale binaries from an unconfigured build; they now
  run the installed commands.
- `mm.shell` and `tests/mm/shell` are declared host-only, since `setenv` and
  `unsetenv` have no freestanding implementation.

### Known limitations

- **No tool converts an option into a compiler or linker argument.** Only
  `buildable-host` and `buildable-target` are consumed; the other six are
  validated, recorded and reported as ignored.
- No command-line overrides for compiler, target, build type, flags or output
  directory. `configure` is the only route.
- Runner and debugger support is limited to profiles compiled into `configure`.
- Builds are full rather than incremental.
- **Hosted cross targets work end to end; freestanding targets compile but
  cannot link.** A bare-metal target such as `arm-none-eabi` needs `--specs=`
  and syscall stubs, and there is no route for either yet.

### Compatibility

- `mm: 1.0` manifests are unchanged and still valid.
- `bootstrap.sh` still uses the literal `c++` and needs no configuration.
- An existing `out/config.mdy` keeps the directories it was written with until
  `configure` runs again.

## [v1.0.1] — 2026-09-05

### Changed

- Renamed the core build types for clarity: `Unit` to `TranslationUnit`,
  `Target` to `BuildableNode`, and `Node` to `ManifestNode`, so one concept
  reads under one name in both the concrete and abstract layers.
- Reconciled the documentation with the implementation.

## [v1.0.0] — 2026-09-02

First release. A self-contained C++20 project that builds, tests and documents
itself from source, with no third-party build system, package manager, test
framework or documentation generator. 77 commits from the initial commit on
2026-08-23.

### Added

- **MDY**, a line-oriented manifest and document format, with the `mm.mdy`
  manifest tree that every tool walks: `kind: project`, `dir`, `module`, `app`,
  `test` and `doc`.
- **Bootstrap.** `bootstrap.sh` compiles enough by hand through the literal
  `c++` to produce `build1`, which then builds the rest, including the real
  build tool. A raw-command fallback runs if that fails.
- **Core modules** under `modules/mm`: `mm.mdy` (parser), `mm.build` (manifest
  walk, dependency graph, compiler and linker driver), `mm.app` (application
  boundary and command line), `mm.test` (test framework), `mm.shell` (shell and
  environment), `mm.model` (adapter onto `models/`).
- **Tools**: `build`, `test`, `check`, `model`, `shell`.
- **Applications**: `main` and `mdy`, the documentation renderer.
- **`models/`**, a second independent description of the project's own
  structure as abstract C++20 interfaces: `models.document`,
  `models.configuration`, `models.manifest`, `models.repository`,
  `models.tool`, `models.modules`, `models.workflow`, `models.artifacts`.
- **Documentation** written in MDY and rendered by the project's own tool:
  `docs/modules.mdy`, `docs/mdy.mdy`, `docs/modules-c++20.mdy`,
  `docs/modules-model.mdy`.
- **Workflow scripts**: `bootstrap.sh`, `build.sh`, `test.sh`, `document.sh`,
  `check.sh`, `model.sh`, `clean.sh`, each with a no-extension counterpart that
  runs through the project's own shell tool.
- Test framework with self-registering suites, expected failures reported as
  `xfail`, and `xpass` failing the run when a known defect starts passing.
- GCC and Clang backends, selected per build.

[Unreleased]: https://github.com/modules-cpp/modules.cpp/compare/v1.3.1...HEAD
[v1.3.1]: https://github.com/modules-cpp/modules.cpp/releases/tag/v1.3.1
[v1.3.0]: https://github.com/modules-cpp/modules.cpp/releases/tag/v1.3.0
[v1.2.4]: https://github.com/modules-cpp/modules.cpp/releases/tag/v1.2.4
[v1.2.3]: https://github.com/modules-cpp/modules.cpp/releases/tag/v1.2.3
[v1.2.2]: https://github.com/modules-cpp/modules.cpp/releases/tag/v1.2.2
[v1.2.1]: https://github.com/modules-cpp/modules.cpp/releases/tag/v1.2.1
[v1.2.0]: https://github.com/modules-cpp/modules.cpp/releases/tag/v1.2.0
[v1.1.0]: https://github.com/modules-cpp/modules.cpp/releases/tag/v1.1.0
[v1.0.1]: https://github.com/modules-cpp/modules.cpp/releases/tag/v1.0.1
[v1.0.0]: https://github.com/modules-cpp/modules.cpp/releases/tag/v1.0.0
