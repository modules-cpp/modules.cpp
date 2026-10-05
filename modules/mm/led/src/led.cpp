// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module mm.led;

namespace mm::led {

namespace {

Led unserved;
Led* current = &unserved;

}

void set_led(Led& led) { current = &led; }

Led& selected_led() { return *current; }

}
