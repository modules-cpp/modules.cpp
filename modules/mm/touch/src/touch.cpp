// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module mm.touch;

namespace mm::touch {

namespace {

Touch unserved;
Touch* current = &unserved;

}

void set_touch(Touch& touch) { current = &touch; }

Touch& selected_touch() { return *current; }

}
