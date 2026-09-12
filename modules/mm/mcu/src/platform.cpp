// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module mm.mcu;

namespace mm::mcu {

namespace {

// The fallback is a Platform with nothing overridden, so an unserved lane answers
// Unsupported rather than failing to link or dereferencing a null pointer. A
// missing implementation is a runtime answer here; making it a link-time or
// configure-time one is the responsibility work the design document describes.
Platform unserved;
Platform* current = &unserved;

}  // namespace

void set_platform(Platform& platform) { current = &platform; }

Platform& platform() { return *current; }

}  // namespace mm::mcu
