// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module platform.linux.defaults;

import platform.linux.map;

namespace {

const platform::linux::Map defaults{};

struct Register {
    Register() { platform::linux::set_map(defaults); }
};

const Register registered;

}
