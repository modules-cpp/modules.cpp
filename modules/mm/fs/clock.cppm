// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstdint>

export module mm.fs:clock;

export namespace mm::fs {

// Seconds since 1970-01-01 for the timestamps a volume writes, or 0 for
// unknown. A function pointer rather than a dependency, so mm.fs names no
// clock module: a program installs one, from mm.rtc or anywhere else.
using Clock = std::uint64_t (*)();

// Installs the clock; nullptr removes it.
void set_clock(Clock clock);

// What a driver stamps: the installed clock's answer, or 0 with none.
[[nodiscard]] std::uint64_t now();

}
