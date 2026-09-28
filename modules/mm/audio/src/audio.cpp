// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module mm.audio;

namespace mm::audio {

namespace {

In unserved_in;
Out unserved_out;
In* current_in = &unserved_in;
Out* current_out = &unserved_out;

}

void set_in(In& in) { current_in = &in; }

In& selected_in() { return *current_in; }

void set_out(Out& out) { current_out = &out; }

Out& selected_out() { return *current_out; }

}
