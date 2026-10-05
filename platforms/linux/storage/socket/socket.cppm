// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module platform.linux.storage.socket;

import mm.fs;
import mm.mcu;
import mm.sdcard;
import mm.sdcard.socket;
import platform.linux.storage.bus;

// A named, non-exported namespace, as docs/modules-c++20.mdy requires of a
// provider's objects in an interface unit.
namespace platform::linux::storage_socket_provider {

using platform::linux::storage::BusWiring;

mm::sdcard::SpiCard card{{.spi = {.instance = BusWiring::instance,
                                    .clock_gpio = BusWiring::clock_gpio,
                                    .transmit_gpio = BusWiring::transmit_gpio,
                                    .receive_gpio = BusWiring::receive_gpio,
                                    .baud = 0,
                                    .mode = mm::mcu::SpiMode::Mode0,
                                    .bit_order = mm::mcu::BitOrder::MostSignificantFirst},
                            .chip_select_gpio = BusWiring::sdcard_chip_select_gpio,
                            .data_baud = 25'000'000}};

class Socket final : public mm::sdcard::socket::Provider {
public:
    [[nodiscard]] mm::fs::BlockDevice& card() override {
        return platform::linux::storage_socket_provider::card;
    }
};

Socket socket;

struct Register {
    Register() { mm::sdcard::socket::set_provider(socket); }
};

const Register registered;

}  // namespace platform::linux::storage_socket_provider
