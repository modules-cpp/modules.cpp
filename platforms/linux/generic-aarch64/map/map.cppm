// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <new>

export module platform.linux.generic_aarch64.map;

import platform.linux.map;

namespace {

platform::linux::Map make_map() {
    platform::linux::Map map;
    map.board_name = "generic-linux-aarch64";
    return map;
}

const auto defaults = make_map();
struct Register { Register() { platform::linux::set_map(defaults); } };
const Register registered;

}
