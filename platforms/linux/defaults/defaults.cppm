// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module platform.linux.defaults;

import platform.linux.map;

// A named, non-exported namespace rather than an unnamed one: Clang emits an
// interface unit's unnamed-namespace objects again in every importer, and a
// provider object must exist exactly once.
namespace platform::linux::defaults_provider {

const platform::linux::Map defaults{};

struct Register {
    Register() { platform::linux::set_map(defaults); }
};

const Register registered;

}
