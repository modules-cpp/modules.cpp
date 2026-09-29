# modules.cpp v1.2.3

Something to measure with. v1.2.2 put pictures on the panels; v1.2.3 gives a
program the analog world and the wire beneath it: an ADC and a PWM in
`mm.mcu` beside GPIO, SPI, and I2C, implemented on the Pico SDK lanes and on
Linux; a GPIO edge latch that reports what happened on a pin without ever
running application code in a handler; the execution model that says why it
is a latch, written down once for every interface; and a core JSON module
that the build itself is the first consumer of. It also fixes the Clang link
failure the Linux tests met on a Raspberry Pi, and documents the Linux
device map in full.

```sh
./configure --target arm-none-eabi --compiler arm-none-eabi-gcc \
            --sdk pico-arm --board pico --build debug
./build --target apps/analog-smoke/
./build --target apps/gpio-edge-smoke/
```

```sh
./json --check tests/mm/json/fixtures/y_object.json
./json --indent compile_commands.json
```

`mm.mcu` gains two facility partitions, `:adc` and `:pwm`, in the shape of
its bus partitions: inventories the platform answers per channel and per
output, seven operations on the `Platform` seam with `Unsupported` defaults,
and free functions over them. `adc_configure`, `adc_read`, and `adc_release`
work a channel that carries its own width and reference, so a Linux IIO
device whose channels scale differently is described truthfully and a board
that has not measured its reference reports zero rather than a guess;
`adc_millivolts` converts a count in integer arithmetic. `pwm_configure`
claims an output at the nearest period the hardware holds, `pwm_period`
says what that is, `pwm_write` sets a duty in nanoseconds of it, and
`pwm_release` returns the pad; outputs that share a counter must share a
period, and two pads that alias one compare register are one signal, which
the description says in advance and a second claim is refused for.
`pwm_plan` turns a period into a divider and a top for any
prescaler-and-top counter, reserving one count so that a full duty is a
level the compare register can hold on every part — the cost is stated
exactly, down to the half-count interval at each divider's limit, and the
test table pins it against an independent rational model. A pad is one
thing at a time: a plain GPIO configuration yields to an analog claim, a
watched pin and an analog claim yield only to their own release, and the
asymmetry is written down with its reason.

The Pico SDK provider implements both through its adapter, which now keeps
one owner per pad across GPIO, the edge latch, ADC, and PWM; the ADC
reference is the selected board's own row in the bridge's
`resolve-board.cmake`, never its vendor ancestor's. The Linux provider
implements both over IIO and PWM sysfs from new `adc.*` and `pwm.*` device
map keys, exporting an output only if nobody else has and never unexporting
another's. `analog-smoke` is the wired fixture: a PWM output through an RC
filter into an ADC channel, read as raw ratios at a quarter, a half, and
three quarters duty, with the sibling, alias, and ownership rules checked
on the way.

The GPIO edge latch — `gpio_watch`, `gpio_take`, `gpio_unwatch`, and a
bounded `gpio_wait` — reports selected physical edges as a flag the program
asks for, on the Pico through the SDK's GPIO callback on core zero and on
Linux through GPIO-v2 line events. `gpio-edge-smoke` is its fixture.
docs/modules-execution.mdy is the contract behind it: one thread of control
from `main` to return, every call returning, nothing running application
code on the program's behalf, and hardware that acts alone recorded in a
latch. The Linux console and device map were brought to that contract — a
console write is bounded everywhere but a regular file, and the map
override is read through a descriptor checked to be a regular file and
capped at 64 KiB, so the first board query returns.

`mm.json` reads and writes RFC 8259 JSON with no exceptions, no templates
of its own, and no allocation in its scanner, so the same module serves the
host tools and a target holding a document in a fixed buffer. The scanner
answers one token per call with strict grammar, UTF-8, and surrogate checks;
the value layer parses over an explicit stack, writes compact or indented,
and refuses duplicate keys and integers beyond `long long` unless told
otherwise. Every output changes only on Ok. The JSON Parsing Test Suite is
vendored under `tests/mm/json/fixtures` and every file's outcome is pinned.
`mm.build` is the first consumer, reading a bridge's `compile_commands.json`
through it — which fixed a `\u` escape in a path being copied through as
text — and `json` is the front end, with `--check`, `--indent`,
`--compact`, and `--scan`.

The Linux providers' classes, objects, and registration objects live in
named non-exported namespaces now. Clang emits an interface unit's
unnamed-namespace objects again in every importer, so `tests/mm/linux`,
which imports the DRM provider for its testing seam, failed to link under
Clang 19 on a Raspberry Pi with `undefined reference to vtable for
(anonymous namespace)::LinuxDisplay`; docs/modules-c++20.mdy states the
rule. docs/modules-platform-linux.mdy now documents the device map in full:
its two layers, the override grammar, every key with its default and
meaning, the validation rules, and a complete example for a Pi.

**The analog and edge fixtures have not been run on hardware in this
release.** `analog-smoke` builds for the RP2040 and both RP2350
architectures with the adapter symbols present, and the host suite pins the
contracts, the planner, and the Linux parsers; the ratios on a real RC
filter, the steady ends of a PWM, and edge delivery on a real pin are
physical runs a person reads from the USB console. **No Pico board other
than the six vendor boards has an ADC reference row**, so on
rp2350_touch_lcd_28 and the e-paper carriers `adc_millivolts` answers zero
until someone measures one and adds it. The Linux ADC and PWM have been
exercised only against a map that names neither. Backlight dimming through
the new PWM, a battery-sense channel, and the sketch layer's `analogRead`
and `analogWrite` are the consumers this release makes possible and does
not yet contain.

`mm: 1.0`, `mm: 1.1`, and `mm: 1.2` manifests are unchanged and still valid.
No manifest key or value is introduced; the device map gains `adc.*` and
`pwm.*` keys, which are runtime configuration and not manifest.

Requires a C++20 compiler with module support, GCC 14 or newer or a recent
Clang, with GCC 15 or newer recommended, and a POSIX shell. The Pico
platforms require the prebuilt tools that
`platforms/pico/install-sdk-tools.sh` provisions. The Linux SDL2 and e-paper
boards additionally require the SDL2 development libraries. See
[CHANGELOG.md](CHANGELOG.md) for the full list, `docs/modules-platform-mcu.mdy`
for the analog facilities and the edge latch, `docs/modules-execution.mdy`
for the execution model, and `docs/modules-json.mdy` for the JSON module.
