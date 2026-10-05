// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module platform.geek.sdcard;

import mm.fs;
import mm.mcu;
import mm.sdcard;
import mm.sdcard.socket;

// A named, non-exported namespace, as docs/modules-c++20.mdy requires of a
// provider's objects in an interface unit.
namespace platform::geek_sdcard_provider {

mm::sdcard::SpiCard card{{.spi = {.instance = 0,
                                    .clock_gpio = 18,
                                    .transmit_gpio = 19,
                                    .receive_gpio = 20,
                                    .baud = 0,
                                    .mode = mm::mcu::SpiMode::Mode0,
                                    .bit_order = mm::mcu::BitOrder::MostSignificantFirst},
                            .chip_select_gpio = 23,
                            .data_baud = 25'000'000}};

class Socket final : public mm::sdcard::socket::Provider {
public:
    [[nodiscard]] mm::fs::BlockDevice& card() override { return platform::geek_sdcard_provider::card; }
};

Socket socket;

struct Register {
    Register() { mm::sdcard::socket::set_provider(socket); }
};

const Register registered;

}  // namespace platform::geek_sdcard_provider
