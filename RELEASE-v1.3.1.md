# modules.cpp v1.3.1

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

```sh
out/bin/sketch apps/ino/blink && ./build.sh apps/ino/blink/
./scripts/build-pico.sh --board pico_usb_device --app apps/shell-smoke --dry-run
./scripts/build-linux.sh --board lcd --app apps/gfx-demo --dry-run
./test.sh
```

The USB subsystem introduces standard USB vocabularies and platform interfaces.
`mm.usb` defines status, speed, transfer types, endpoint addresses, and setup
packets. `mm.usb.device` provides nonblocking asynchronous transfers, device
descriptors, and setup request dispatching. `mm.usb.host` handles device
enumeration, pipe allocation, and interface claiming. On top of these,
`mm.usb.cdc` supplies CDC ACM device functions and a `CdcConsole` provider for
`mm.stdio`, `mm.usb.msc` implements Bulk-Only Transport (BOT) backing block
storage via transparent SCSI commands, and `mm.usb.vendor` provides echo
devices and hosts for loopback qualification.

Sketches expand their hardware reach and library compatibility. `Wire1` and
`Wire2` add second and third I2C buses with independent buffers, pins, and clock
configuration. `Serial1` and `Serial2` expose hardware UART streams with pin
assignment, timeouts, and nonblocking reads. The legacy profile
(`sketch --legacy`) enables seamless compilation of third-party Arduino
libraries by providing `using namespace mm::sketch::legacy`, defining
`ARDUINO=10819`, redirecting `SD.h` to SdFat, exporting standard SPI pin names
(`SS`, `MOSI`, `MISO`, `SCK`), providing flash helper macros, and adding
`interrupts()` and `noInterrupts()` control.

On hardware and platforms, Raspberry Pi Pico gains native USB port ownership
options (`console` vs `application`) with dedicated `pico_usb_device` and
`pico2_usb_device` boards, while `pico_usb_host` and `pico2_usb_host` support
mass storage drives via Pico-PIO-USB. On Linux, `platforms/linux/lcd` provides
emulated ST7789 and ILI9341 displays rendered in SDL2 windows for desktop
graphics development, and `platforms/linux/libusb` provides the Linux USB host
provider.

`mm.mdy` now natively parses multi-line fenced code blocks with language tags,
preserving indentation and blank lines, and renders them into HTML `<pre><code
class="language-...">` tags. `tools/flash` adds `--auto-flash` for automatic
Pico device discovery and bootloader reset, `configure` adds `--check` to audit
manifest trees, and `bootstrap.sh` supports custom compiler overrides via
`--compiler`.

**USB device and host hardware operations have been qualified on Raspberry Pi
Pico and Linux host.** **Emulated displays require SDL2 development libraries on
the host.**

`mm: 1.0`, `mm: 1.1`, `mm: 1.2`, and `mm: 1.3` manifests remain fully
supported.

Requires a C++20 compiler with module support (GCC 15 recommended, Clang 16+),
a POSIX shell, and libusb / SDL2 for hosted providers. See
[CHANGELOG.md](CHANGELOG.md) for the full change list,
`docs/modules-usb.mdy` for the USB subsystem, `docs/modules-sketch.mdy` and
`docs/modules-ino.mdy` for sketches, and `docs/modules-platform-pico.mdy` for
Pico platform details.
