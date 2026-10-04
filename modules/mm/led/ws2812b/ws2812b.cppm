// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <span>

export module mm.led.ws2812b;

import mm.led;
import mm.mcu;

export namespace mm::led::ws2812b {

// Which mm.mcu pulse instance drives the chain, and from which GPIO.
struct Wiring {
    unsigned int instance = 0;
    unsigned int gpio = 0;
};

// The order of a pixel's three bytes on the wire. The WS2812B takes green,
// red, blue; some clones take red, green, blue.
enum class Order { Grb, Rgb };

// The chain: how many LEDs, in what byte order, and the provider-owned frame
// the controller composes into, three bytes an LED. The frame must outlive
// the Controller.
struct Chain {
    unsigned int count = 0;
    Order order = Order::Grb;
    std::span<std::byte> frame;
};

// The datasheet's timing, in nanoseconds.
inline constexpr std::uint32_t bit_period_ns = 1'250;
inline constexpr std::uint32_t zero_high_ns = 400;
inline constexpr std::uint32_t one_high_ns = 800;
inline constexpr std::uint32_t reset_ns = 280'000;

class Controller final : public mm::led::Led {
public:
    Controller(Wiring wiring, Chain chain);

    [[nodiscard]] unsigned int count() const override;
    [[nodiscard]] mm::led::Status initialize() override;
    [[nodiscard]] mm::led::Status write(unsigned int first,
                                        std::span<const mm::led::Color> colors) override;
    [[nodiscard]] mm::led::Status refresh() override;
    [[nodiscard]] mm::led::Status clear() override;
    [[nodiscard]] mm::led::Status sleep() override;

private:
    enum class State { Idle, Ready, Sleeping };

    [[nodiscard]] std::span<std::byte> frame() const;
    void fill_dark();
    [[nodiscard]] mm::led::Status from_mcu(mm::mcu::Status status) const;
    [[nodiscard]] mm::led::Status fail(mm::led::Status status);

    Wiring wiring_;
    Chain chain_;
    State state_ = State::Idle;
};

}
