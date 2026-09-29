# modules.cpp v1.2.4

Something to listen with. v1.2.3 gave a program the analog world; v1.2.4
gives it sound: a portable audio interface of sources, sinks, and the stream
between them, I2S in `mm.mcu` as a continuous transport over PIO and DMA on
every Pico SDK lane, an ES8311 codec driver for both directions, and audio
providers for the RP2350 LCD 1.54 family and the RP2350-Touch-LCD-2.8. It
adds critical sections to `mm.mcu` and the transport model to the execution
model, and rebuilds the build scripts around two platform scripts whose
checks are read from the manifests.

```sh
scripts/build-audio.sh --board rp2350_touch_lcd_154 --flash
scripts/build-audio.sh --board rp2350_touch_lcd_28
scripts/build-gfx.sh --board rp2350_lcd_154
scripts/build-aarch64-linux-gnu.sh --board sdl-linux-aarch64 --app gfx-demo --run
```

`mm.audio` is three interface classes and knows no pin, transport, or
board. `In` is a source of sampled sound, `Out` a sink, and `Stream` the
connection with one producer and one consumer that carries samples between
either one and an application, or between the two directly. A board's
platform provider derives a `Microphone` from `In` and a `Speaker` from
`Out` and registers one of each; a board without one registers a device that
answers `Unsupported`. `Ring` is the shipped `Stream`: a ring over
caller-owned storage that never allocates, with two indices each written by
one side, zero-copy regions bounded by the wrap, and underrun and overrun
counts. Samples are sixteen-bit and mono, `Format` carries the nominal rate
and `Rate` the exact one, because two devices asked for one rate generally
run at two, and `service` never waits.

I2S in `mm.mcu` became a continuous transport: its clock runs from
configure to release, and `i2s_write` and `i2s_read` queue into a buffer
the platform owns, beside `i2s_rate`, `i2s_start`, `i2s_progress`,
`i2s_stop`, and `i2s_release`. Paced ADC capture and a DAC facility share
that lifecycle and are specified with it, though no provider implements
them yet. On the Pico SDK lanes, `platform.pico.mcu` implements I2S instance
zero over PIO and DMA on RP2040 and on RP2350 Arm and RISC-V: side-set clocks,
ping-pong DMA over blocks the adapter owns, and a DMA interrupt that refills
or empties them from 256-frame rings, sending zeros on underrun and counting
what it drops. `interrupts_disable`, `interrupts_enable`, and
`InterruptGuard` give critical sections that nest, and
docs/modules-execution.mdy adds model 3, the transport: what a transport's
handler may do, which is acknowledge, count, re-arm, switch to silence, and
drop, and nothing above the seam.

`mm.audio.es8311` drives the Everest ES8311 over `mm.mcu` I2C and I2S:
`Output`, its DAC as an `Out`, and `Input`, its ADC recording the analog
microphone as an `In`, sharing one `Codec`. The review that brought it over
corrected its I2C address, its serial-port register, and its missing clock
and power-up programming. `rp2350_lcd_154` and `rp2350_touch_lcd_154` are
the RP2350 LCD 1.54 family, with and without touch; their audio provider
registers the ES8311 as Speaker, with the NS4150B amplifier enable, and as
Microphone. The RP2350-Touch-LCD-2.8 gains a Speaker straight over I2S for
its PCM5101A, and no Microphone, having none. `apps/audio-smoke` plays a
tone while recording through whichever devices a board registers.

The build scripts are two platform scripts and their wrappers.
`scripts/build-pico.sh` and `scripts/build-linux.sh` build one application
for any board of their platform, and `scripts/build-target.sh` for any
target lane configure accepts. None of them lists what an image must
contain: they walk the application's closure through the lane's bindings as
the build does, and require one initializer of every reached provider, none
of any other, the symbols of every driver, and every library link. Feature
wrappers -- `build-gfx.sh`, `build-font.sh`, `build-epaper.sh`,
`build-audio.sh`, and the rest -- work on any board of either platform, and
one wrapper per target platform, from `build-aarch64-linux-gnu.sh` to
`build-m68k-linux-external.sh`, names a lane and a default application.
Every script has a `--dry-run` that `test.sh` pins, and every script
restores the configuration it found. **The per-board and per-lane scripts
of earlier releases are removed**; each maps to a wrapper and a board, as
`build-linux-epaper-font-demo.sh` does to
`build-epaper.sh --app font --board epaper`.

**None of the audio is qualified on hardware.** The PIO program, the DMA
ping-pong, the ES8311 transcripts, and both boards' wiring are built for
every Pico SDK lane and checked against datasheets and Waveshare's
schematics, and the host suite pins the ring, the conversions, the codec
contracts against a recording platform, and the critical sections; a tone
from the speaker and a recording from the microphone are physical runs a
person hears. Paced ADC capture and the DAC answer `Unsupported` everywhere,
and audio is mono.

`mm: 1.0`, `mm: 1.1`, and `mm: 1.2` manifests are unchanged and still valid.
No manifest key or value is introduced.

Requires a C++20 compiler with module support, GCC 14 or newer or a recent
Clang, with GCC 15 or newer recommended, and a POSIX shell. The Pico
platforms require the prebuilt tools that
`platforms/pico/install-sdk-tools.sh` provisions. The Linux SDL2 and e-paper
boards additionally require the SDL2 development libraries, and the target
wrappers their cross toolchains and, to run, qemu. See
[CHANGELOG.md](CHANGELOG.md) for the full list, `docs/modules-audio.mdy` for
the audio interface, `docs/modules-platform-mcu.mdy` for I2S and critical
sections, and `docs/modules-execution.mdy` for the transport model.
