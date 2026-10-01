// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module mm.usb.host;

namespace mm::usb::host {

namespace {

Host unserved;
Host* current = &unserved;

}

void set_host(Host& host) { current = &host; }

Host& selected_host() { return *current; }

}
