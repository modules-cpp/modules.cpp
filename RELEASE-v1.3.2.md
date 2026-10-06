# modules.cpp v1.3.2

Something to capture and keep. v1.3.1 added USB connectivity; v1.3.2 gives
programs a portable file system with littlefs and FAT on Pico, SD-card and
SPI-flash drivers with Linux emulation, addressable RGB LEDs, and live
HM01B0 camera preview on PICO-Cam-A. New Waveshare board profiles, standalone
UF2 flashing, and USB console fixes complete the release.

```sh
./build.sh
./test.sh
./scripts/build-pico.sh --board pico_cam_a --app camera-demo --dry-run
./scripts/build-rgb-led.sh --board rp2040_zero --dry-run
./scripts/build-fs.sh --board generic --dry-run
```

`mm.fs` adds allocation-free paths, mounts, files and directories, with
move-only handles and a reusable 25-check volume contract. Pico applications
can mount littlefs on reserved program-flash storage, or FAT12/16/32 on block
devices. Linux mounts ordinary directories through `mm.fs.local` and
`mm.fs.native`. `mm.sdcard` drives SD cards in SPI mode, board socket
providers supply wiring, and `mm.spiflash` drives W25Q-family NOR flash.
The `storage-linux` profiles emulate both chips in persistent image files,
with CRC checks and fault injection for host-side driver tests.

`mm.led` and `mm.led.ws2812b` provide addressable RGB LEDs over the new
`mm.mcu` pulse facility. Waveshare RP2040/RP2350 Zero profiles bind the
onboard WS2812B; GEEK profiles bind their LCDs and SD sockets; RP2350-PiZero
profiles select the RP2350B SDK header and offer a USB-host variant.
**These new LED and storage board paths are not yet qualified on hardware.**

PICO-Cam-A binds an HM01B0 camera and a 240x135 ST7789V LCD. `camera-demo`
starts live capture in CROP after reset, with K3 stop/restart and K4 cycling
crop, nearest, area and temporal sampling. `mm.camera.preview` implements
processing and debounce as C++20 module code; the display receives complete
RGB565 frames. Sensor settings and initialization delays follow Waveshare's
working C demo. Camera support adapts ArduCAM/RPI-Pico-Cam through Waveshare;
original attribution and local changes are recorded in
`boards/pico_cam_a/mm.mdy`. Basic preview has been confirmed on the board.
**The 250 MHz capture clock exceeds the RP2040's documented 200 MHz operating
point. Frame rate and image quality are not formally qualified.**

`flash --image <file.uf2>` installs independently built firmware through the
existing Pico backend, including `--auto-flash`. USB console connection and
flush behavior now handle terminals without DTR, deconfiguration, debugger
halts and disconnected hosts with bounded waits. `scripts/install-linux-deps.sh`
provides an Ubuntu/Debian dependency installer.

Source manifests `mm: 1.0`, `1.1`, `1.2` and `1.3` remain supported; the
persisted configuration schema is unchanged. Linux littlefs/FAT providers,
4-bit SDIO, file commands in the shell, and manifest version 1.4 remain
unreleased development work on main.

Requires a C++20 compiler with module support (GCC 15 recommended), a POSIX
shell, and the development libraries required by the selected hosted
providers. Pico builds require an Arm or RISC-V toolchain, the Pico SDK and
picotool; vendored littlefs and FatFs are provisioned by their vendor scripts.

See [CHANGELOG.md](CHANGELOG.md), `docs/modules-fs.mdy`,
`docs/modules-sdcard.mdy`, `docs/modules-spiflash.mdy`,
`docs/modules-linux-storage.mdy`, `docs/modules-led.mdy`,
`docs/modules-pico-cam-a.mdy`, `docs/modules-stdio.mdy`, and
`docs/modules-flash.mdy` for the released behavior and its limits.
