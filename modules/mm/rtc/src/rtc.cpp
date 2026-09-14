// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module mm.rtc;

namespace mm::rtc {

namespace {

Clock unserved;
Clock* current = &unserved;

}

void set_clock(Clock& clock) { current = &clock; }

Clock& selected_clock() { return *current; }

}
