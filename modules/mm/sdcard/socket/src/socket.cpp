// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module mm.sdcard.socket;

import mm.fs;

namespace mm::sdcard::socket {

namespace {

mm::fs::BlockDevice no_socket;
Provider unserved;
Provider* current = &unserved;

}  // namespace

mm::fs::BlockDevice& Provider::card() { return no_socket; }

void set_provider(Provider& provider) { current = &provider; }

Provider& selected_provider() { return *current; }

mm::fs::BlockDevice& card() { return current->card(); }

}  // namespace mm::sdcard::socket
