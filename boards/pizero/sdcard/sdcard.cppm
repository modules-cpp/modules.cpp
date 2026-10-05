// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module platform.pizero.sdcard;

import mm.fs;
import mm.mcu;
import mm.sdcard;
import mm.sdcard.socket;

// A named, non-exported namespace, as docs/modules-c++20.mdy requires of a
// provider's objects in an interface unit.
namespace platform::pizero_sdcard_provider {

mm::sdcard::SdioCard card{{.instance = 0,
                           .clock_gpio = 30,
                           .command_gpio = 31,
                           .data0_gpio = 40,
                           .data_clock_hz = 25'000'000}};

class Socket final : public mm::sdcard::socket::Provider {
public:
    [[nodiscard]] mm::fs::BlockDevice& card() override { return platform::pizero_sdcard_provider::card; }
};

Socket socket;

struct Register {
    Register() { mm::sdcard::socket::set_provider(socket); }
};

const Register registered;

}  // namespace platform::pizero_sdcard_provider
