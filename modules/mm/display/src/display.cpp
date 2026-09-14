// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module mm.display;

namespace mm::display {

namespace {

Display unserved;
Display* current = &unserved;

}

void set_display(Display& display) { current = &display; }

Display& selected_display() { return *current; }

}
