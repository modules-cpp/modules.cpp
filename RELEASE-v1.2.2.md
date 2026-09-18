# modules.cpp v1.2.2

Something to draw with. v1.2.1 bound colour and e-paper panels to portable
interfaces and proved the closure links. v1.2.2 puts pictures on them: a
bitmap font module with committed IBM Plex Mono tables, a graphics module of
caller-owned packed surfaces, drawing primitives, quarter-turn rotation, and
palette expansion, and two demonstration applications that run unchanged on
the one-bit e-paper, the RP2350 colour LCD, and both Linux emulations. It
also opens the Linux lanes to Clang and the whole build to GCC 14.

```sh
./configure --target arm-none-eabi --compiler arm-none-eabi-gcc \
            --sdk pico-arm --board rp2350_touch_lcd_28 --build debug
./build --target apps/gfx-demo/
```

```sh
scripts/build-linux-sdl-font-demo.sh --run
scripts/build-linux-epaper-gfx-demo.sh --run
```

`mm.gfx` sits above `mm.display` and below everything that draws. A
`Surface` is a caller-owned packed frame in the display's own layout: one bit
per pixel with one as white, or big-endian RGB565. `fill`, `pixel`, `line`,
`rectangle`, and `fill_rectangle` clip to it; `rotate` turns a one-bit frame
by quarter turns and turns a rectangle with it; and two `write` overloads
carry a surface to a panel — at the panel's own depth, sending exactly the
surface's pixel bytes and enforcing the e-paper controller's byte-aligned
window rule before the provider sees it, or one-bit onto a sixteen-bit panel,
expanded a row at a time through a base palette and rectangular regions with
palettes of their own. That second path is the point: a 240 by 320 frame is
9,600 bytes one bit deep and 153,600 in RGB565, so one-bit is the composition
every board here can afford, and colour is a property of the write.

`mm.fonts` is monospace bitmap text on that surface. `render` is a
transparent one-bit compositor: glyph pixels take the ink and every other bit
is left alone, so text composes over whatever is already there. `measure`
counts well-formed UTF-8 code points and stops at a malformed one. The two
committed tables, `kMono12` and `kMono16`, are rasterized from IBM Plex Mono
under the SIL Open Font License over ASCII and the Polish letters, and the
tests hold golden bitmaps for them.

`font-demo` and `gfx-demo` are the pictures. The first shows both tables,
the Polish charset, and every glyph of the twelve pixel face, upright and
then turned a quarter clockwise three times, each line in its own colour on a
colour panel. The second is every drawing primitive in one scene — a
dithered glow, nested frames, a haloed porthole with a starburst — turned
the same way, with hue-cycling regions and a plasma written through the
porthole in RGB565 on a colour panel. Each has a build script for Linux SDL,
Linux e-paper, Pico e-paper, and the RP2350 LCD, asserting the provider
closure of the image and, where it can, running it. The e-paper emulation
gains an AArch64 board so the lane runs on an ARM development machine.

A hosted SDK may now declare `compiler-family: any`, and the two Linux SDKs
do: they own none of the link, so they constrain none of the toolchain, and
a lane on them may name GCC or Clang. Two expressions that tripped an
internal compiler error in GCC 14 are spelled around it; GCC 14 bootstraps,
builds, and passes the host suite. And the target-option probe no longer
leaves `help-dummy.o` behind.

**The font table generator is not in the repository.** The generated file
names `tools/fonts/gen_font.py` and the pinned face, and the specification
describes regenerating from them, but the script is not committed; the
tables can be used and verified, not yet rebuilt. **Text and graphics on the
physical panels have not been looked at**: the compositions are pinned by
host tests and were viewed through the SDL and emulated e-paper lanes, and
the RP2350 and Pico e-paper builds are inspected images. `mm.fonts` composes
one-bit only; colour text goes through the expanding write, and a
transparent glyph over an RGB565 surface, a stored-bitmap `blit`, a
sixteen-bit `rotate`, and a second plane for the tri-colour panel's red are
future work. Tuning options other than `buildable-host`, `buildable-target`,
and `core` are still validated and recorded but not applied to compilation,
and builds remain full rather than incremental.

`mm: 1.0`, `mm: 1.1`, and `mm: 1.2` manifests are unchanged and still valid.
`compiler-family: any` is a new value for an existing key on hosted SDK
manifests; nothing else is introduced.

Requires a C++20 compiler with module support, GCC 14 or newer or a recent
Clang, with GCC 15 or newer recommended, and a POSIX shell. The Pico
platforms require the prebuilt tools that
`platforms/pico/install-sdk-tools.sh` provisions. The Linux SDL2 and e-paper
boards additionally require the SDL2 development libraries. See
[CHANGELOG.md](CHANGELOG.md) for the full list, `docs/modules-gfx.mdy` for
the graphics layer, and `docs/modules-fonts.mdy` for the font module.
