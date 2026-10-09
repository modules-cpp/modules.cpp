// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module platform.rp2350_touch_lcd_28.sdcard;

import mm.fs;
import mm.mcu;
import mm.sdcard;
import mm.sdcard.socket;

// A named, non-exported namespace, as docs/modules-c++20.mdy requires of a
// provider's objects in an interface unit.
namespace platform::rp2350_touch_lcd_28_sdcard_provider {

mm::sdcard::SdioCard card{{.instance = 0,
                           .clock_gpio = 19,
                           .command_gpio = 20,
                           .data0_gpio = 21,
                           .data_clock_hz = 100'000}};

class Socket final : public mm::sdcard::socket::Provider {
public:
    [[nodiscard]] mm::fs::BlockDevice& card() override {
        return platform::rp2350_touch_lcd_28_sdcard_provider::card;
    }
};

Socket socket;

struct Register {
    Register() { mm::sdcard::socket::set_provider(socket); }
};

const Register registered;

}  // namespace platform::rp2350_touch_lcd_28_sdcard_provider
