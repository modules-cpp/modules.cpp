// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

module mm.led.ws2812b;

namespace mm::led::ws2812b {

namespace {

constexpr std::size_t bytes_per_led = 3;

}  // namespace

Controller::Controller(Wiring wiring, Chain chain) : wiring_(wiring), chain_(chain) {}

mm::led::Status Controller::from_mcu(mm::mcu::Status status) const {
    switch (status) {
        case mm::mcu::Status::Ok: return mm::led::Status::Ok;
        case mm::mcu::Status::BadArgument: return mm::led::Status::BadArgument;
        case mm::mcu::Status::Unsupported: return mm::led::Status::Unsupported;
        case mm::mcu::Status::Busy: return mm::led::Status::Busy;
        case mm::mcu::Status::Timeout: return mm::led::Status::Timeout;
        case mm::mcu::Status::TransportError: return mm::led::Status::TransportError;
    }
    return mm::led::Status::TransportError;
}

// A transport failure mid-frame leaves the LEDs showing something unknown, so
// the controller stops claiming to be ready rather than accepting the next call.
mm::led::Status Controller::fail(mm::led::Status status) {
    state_ = State::Idle;
    return status;
}

unsigned int Controller::count() const { return chain_.count; }

// The part of the provider's frame the chain uses; initialize has checked it
// is long enough.
std::span<std::byte> Controller::frame() const {
    return chain_.frame.first(static_cast<std::size_t>(chain_.count) * bytes_per_led);
}

void Controller::fill_dark() {
    for (auto& byte : frame()) byte = std::byte{0};
}

mm::led::Status Controller::initialize() {
    if (chain_.count == 0 ||
        chain_.frame.size() < static_cast<std::size_t>(chain_.count) * bytes_per_led)
        return mm::led::Status::BadArgument;

    const mm::mcu::PulseConfiguration configuration{
        .instance = wiring_.instance,
        .gpio = wiring_.gpio,
        .bit_period_ns = bit_period_ns,
        .zero_high_ns = zero_high_ns,
        .one_high_ns = one_high_ns,
        .reset_ns = reset_ns,
    };
    const auto status = from_mcu(mm::mcu::pulse_configure(configuration));
    if (status != mm::led::Status::Ok) return status;

    // The LEDs power up showing whatever they latch from the noise on their
    // input, so initialize sends the chain dark before anything is asked of it.
    state_ = State::Ready;
    fill_dark();
    return refresh();
}

mm::led::Status Controller::write(unsigned int first, std::span<const mm::led::Color> colors) {
    if (state_ != State::Ready) return mm::led::Status::NotInitialized;
    if (first > chain_.count || colors.size() > chain_.count - first)
        return mm::led::Status::BadArgument;

    auto bytes = frame().subspan(static_cast<std::size_t>(first) * bytes_per_led);
    std::size_t at = 0;
    for (const auto& color : colors) {
        const auto red = static_cast<std::byte>(color.red);
        const auto green = static_cast<std::byte>(color.green);
        const auto blue = static_cast<std::byte>(color.blue);
        bytes[at++] = chain_.order == Order::Grb ? green : red;
        bytes[at++] = chain_.order == Order::Grb ? red : green;
        bytes[at++] = blue;
    }
    return mm::led::Status::Ok;
}

mm::led::Status Controller::refresh() {
    if (state_ != State::Ready) return mm::led::Status::NotInitialized;
    const auto status = from_mcu(mm::mcu::pulse_write(wiring_.instance, frame()));
    if (status != mm::led::Status::Ok) return fail(status);
    return mm::led::Status::Ok;
}

mm::led::Status Controller::clear() {
    if (state_ != State::Ready) return mm::led::Status::NotInitialized;
    fill_dark();
    return refresh();
}

mm::led::Status Controller::sleep() {
    if (state_ == State::Sleeping) return mm::led::Status::Ok;
    if (state_ != State::Ready) return mm::led::Status::NotInitialized;
    auto status = clear();
    if (status != mm::led::Status::Ok) return status;
    status = from_mcu(mm::mcu::pulse_release(wiring_.instance));
    if (status != mm::led::Status::Ok) return fail(status);
    state_ = State::Sleeping;
    return mm::led::Status::Ok;
}

}  // namespace mm::led::ws2812b
