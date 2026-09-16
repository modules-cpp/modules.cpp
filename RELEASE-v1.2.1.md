# modules.cpp v1.2.1

Hardware device interfaces and a native Linux target. v1.2.0 proved that one
manifest tree produces host tools, emulator targets, and firmware for the
Raspberry Pi Pico. v1.2.1 adds the portable device interfaces that a composite
board binds — touch, inertial sensing, real-time clock, and colour display —
with project-owned controller drivers, a new custom board that exercises all
four at once, a portable console, and a native Linux platform.

```sh
./configure --target arm-none-eabi --compiler arm-none-eabi-gcc \
            --sdk pico-arm --board rp2350_touch_lcd_28 --build debug
./build --target apps/board-smoke/
```

```sh
./configure --compiler gcc-15 --target-host x86_64-linux-gnu \
            --board sdl-x86_64 --build debug
./build --target apps/display-demo/
./run apps/display-demo/
```

Four new portable interfaces follow the same shape as mm.mcu and mm.display:
a platform-interface module defines the API, a project-owned driver module
implements one controller's protocol over mm.mcu transports, and a board's
provider supplies the wiring and registers the implementation. An application
names only the interface; the selected board binds the rest.

`mm.touch` reports panel contacts in the panel's own coordinate space.
`mm.imu` reports signed counts with a scale, so a caller converts to whatever
units it wants. `mm.rtc` reads and sets a calendar date and time and says
whether that reading can be trusted — a clock that lost power answers with
well-formed and meaningless fields. `mm.lcd.st7789` drives a colour TFT over
SPI, sixteen bits per pixel, and answers `Ok` to refresh because the panel
shows what was written when it was written.

The four drivers are modelled on Waveshare's reference drivers for the
RP2350-Touch-LCD-2.8 but contain no vendor header, no board pin number, no
unit conversion, no interrupt handler, and no rotation transform. The
`rp2350_touch_lcd_28` board is the first custom board in `boards/`, binding
all four providers in one executable. `board-smoke` names the four interfaces
and no board, proving the four-provider closure links.

`mm.stdio` is a portable console: a platform-selected role for moving bytes
to and from wherever the platform's console goes. `write` reports how much it
accepted; `read` with nothing available is `Ok` with count zero. No
formatting, no line discipline, no baud. The Pico SDK provider uses USB CDC;
the Linux provider uses file descriptors.

The Linux platform adds two hosted SDKs — `x86_64-linux-gnu` and
`aarch64-linux-gnu` — implemented directly over libc and the Linux userspace
kernel ABI. Two boards per architecture: a generic board with DRM/KMS
display, evdev touch, IIO sensors, and file-descriptor console; and an SDL2
board that moves the screen and pointer into a window. The SDL2 backend is the
one optional package in the platform, and selecting the board is the only way
anything pays for it. Injection then follows the application: a provider the
board binds but the application never reaches is not in the executable.

`mm.mcu:i2c` is the new transport facility the device drivers use:
`I2cConfiguration`, `i2c_configure`, `i2c_write`, `i2c_read`, and
`i2c_write_read`. The write-read pair is one transaction, not two, so a
device's address pointer is not exposed between command and reply.

**Nothing automated asserts that a board image ran.** Hardware execution is an
opt-in script whose result a person reads. The four device drivers are
protocol-complete against a recording platform without hardware, but visual
touch, IMU, clock, and display behavior on the named Waveshare board remain
hardware-qualification requirements. The Linux platform builds and runs on
the architecture it names; it does not cross-compile. Tuning options other
than `buildable-host`, `buildable-target`, and `core` are still validated and
recorded but not applied to compilation, and builds remain full rather than
incremental.

`mm: 1.0`, `mm: 1.1`, and `mm: 1.2` manifests are unchanged and still valid.
No new manifest keys are introduced; all new modules use existing keys under
`mm: 1.2`.

Requires a C++20 compiler with module support, GCC 15 or newer, and a POSIX
shell. The Pico platforms require the prebuilt tools that
`platforms/pico/install-sdk-tools.sh` provisions. The Linux SDL2 board
additionally requires the SDL2 development libraries. See
[CHANGELOG.md](CHANGELOG.md) for the full list, `docs/modules-platform-linux.mdy`
for the Linux platform specification, `docs/modules-stdio.mdy` for the
console, `docs/modules-imu.mdy`, `docs/modules-lcd.mdy`, `docs/modules-rtc.mdy`,
and `docs/modules-touch.mdy` for the device interfaces.
