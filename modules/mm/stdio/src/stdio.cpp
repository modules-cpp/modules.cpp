// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module mm.stdio;

namespace mm::stdio {

namespace {

Console unserved;
Console* current = &unserved;

}

void set_console(Console& console) { current = &console; }

Console& selected_console() { return *current; }

}
