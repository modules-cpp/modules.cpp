// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module mm.usb.device;

namespace mm::usb::device {

namespace {

Device unserved;
Device* current = &unserved;

}

void set_device(Device& device) { current = &device; }

Device& selected_device() { return *current; }

}
