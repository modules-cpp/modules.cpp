// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

module mm.touch.cst816;

namespace mm::touch::cst816 {

namespace {

// Eight-bit register addresses, one byte on the wire. Named for what the
// driver uses rather than reproducing the controller's whole map.
constexpr unsigned int finger_count = 0x02;
constexpr unsigned int x_coordinate_high = 0x03;
constexpr unsigned int chip_id_register = 0xa7;
constexpr unsigned int chip_id_expected = 0xb6;
constexpr unsigned int auto_sleep = 0xf6;
constexpr unsigned int irq_control = 0xfe;

// Point mode: the controller interrupts and holds coordinates, which is the
// mode mm.touch can report. Gesture and all modes report events this interface
// has no words for.
constexpr std::byte auto_sleep_disabled = std::byte{0x01};
constexpr std::byte auto_sleep_enabled = std::byte{0x00};
constexpr std::byte point_mode = std::byte{0x41};

// The coordinate pair the reference driver decodes: the high nibbles carry the
// top bits, the low bytes the rest, four bytes for one point.
constexpr std::size_t point_size = 4;
constexpr std::size_t chip_id_size = 1;

constexpr unsigned int max_points = 1;

[[nodiscard]] unsigned int byte_value(std::byte value) {
    return static_cast<unsigned int>(value);
}

}  // namespace

mm::touch::Status Controller::from_mcu(mm::mcu::Status status) const {
    switch (status) {
        case mm::mcu::Status::Ok: return mm::touch::Status::Ok;
        case mm::mcu::Status::BadArgument: return mm::touch::Status::BadArgument;
        case mm::mcu::Status::Unsupported: return mm::touch::Status::Unsupported;
        case mm::mcu::Status::Busy: return mm::touch::Status::Busy;
        case mm::mcu::Status::Timeout: return mm::touch::Status::Timeout;
        case mm::mcu::Status::TransportError: return mm::touch::Status::TransportError;
    }
    return mm::touch::Status::TransportError;
}

mm::touch::Geometry Controller::geometry() const {
    return {panel_.width, panel_.height, panel_.points};
}

mm::touch::Status Controller::write_register(unsigned int register_address, std::byte value) {
    const std::array data{static_cast<std::byte>(register_address), value};
    return from_mcu(mm::mcu::i2c_write(wiring_.i2c.instance, wiring_.address, data));
}

mm::touch::Status Controller::read_register(unsigned int register_address,
                                            std::span<std::byte> data) {
    const std::array command_bytes{static_cast<std::byte>(register_address)};
    return from_mcu(mm::mcu::i2c_write_read(wiring_.i2c.instance, wiring_.address,
                                            command_bytes, data));
}

mm::touch::Status Controller::reset() {
    auto status = from_mcu(mm::mcu::gpio_configure(wiring_.reset_gpio,
                                                   mm::mcu::Direction::Out,
                                                   mm::mcu::Pull::None));
    if (status != mm::touch::Status::Ok) return status;

    // The interrupt line is configured as a pulled-up input and then left
    // alone. This driver polls; the pin is set up because leaving a controller
    // output floating against an unconfigured pin is worse than reading it.
    status = from_mcu(mm::mcu::gpio_configure(wiring_.interrupt_gpio,
                                              mm::mcu::Direction::In,
                                              mm::mcu::Pull::Up));
    if (status != mm::touch::Status::Ok) return status;

    status = from_mcu(mm::mcu::gpio_write(wiring_.reset_gpio, false));
    if (status != mm::touch::Status::Ok) return status;
    status = from_mcu(mm::mcu::delay_ms(wiring_.reset_hold_ms));
    if (status != mm::touch::Status::Ok) return status;
    status = from_mcu(mm::mcu::gpio_write(wiring_.reset_gpio, true));
    if (status != mm::touch::Status::Ok) return status;
    return from_mcu(mm::mcu::delay_ms(wiring_.reset_release_ms));
}

mm::touch::Status Controller::initialize() {
    if (panel_.width == 0 || panel_.height == 0 || panel_.points == 0 ||
        panel_.points > max_points)
        return mm::touch::Status::BadArgument;

    auto status = from_mcu(mm::mcu::i2c_configure(wiring_.i2c));
    if (status != mm::touch::Status::Ok) return status;

    status = reset();
    if (status != mm::touch::Status::Ok) return status;

    // The controller answers its chip identifier register with a fixed byte.
    // Anything else is a different device at this address.
    std::array<std::byte, chip_id_size> identification{};
    status = read_register(chip_id_register, identification);
    if (status != mm::touch::Status::Ok) return status;
    if (byte_value(identification[0]) != chip_id_expected)
        return mm::touch::Status::Unsupported;

    // Auto-sleep stays off while the driver is in use, and point mode is what
    // makes coordinates available on request.
    status = write_register(auto_sleep, auto_sleep_disabled);
    if (status != mm::touch::Status::Ok) return status;
    status = write_register(irq_control, point_mode);
    if (status != mm::touch::Status::Ok) return status;

    state_ = State::Ready;
    return mm::touch::Status::Ok;
}

mm::touch::Status Controller::read(std::span<mm::touch::Point> points, std::size_t& count) {
    if (state_ != State::Ready) return mm::touch::Status::NotInitialized;

    std::array<std::byte, 1> count_byte{};
    auto status = read_register(finger_count, count_byte);
    if (status != mm::touch::Status::Ok) return status;

    const auto reported = byte_value(count_byte[0]) & 0x0f;
    if (reported == 0) {
        count = 0;
        return mm::touch::Status::Ok;
    }

    std::array<std::byte, point_size> coordinate{};
    status = read_register(x_coordinate_high, coordinate);
    if (status != mm::touch::Status::Ok) return status;

    // A caller never learns about points it had no room for, which is what
    // makes a span of one a valid single-touch client.
    const auto available = std::min<unsigned int>(reported, panel_.points);
    count = available < points.size() ? available : points.size();
    if (count != 0) {
        points[0].x = (byte_value(coordinate[0]) & 0x0f) << 8 | byte_value(coordinate[1]);
        points[0].y = (byte_value(coordinate[2]) & 0x0f) << 8 | byte_value(coordinate[3]);
    }

    return mm::touch::Status::Ok;
}

mm::touch::Status Controller::sleep() {
    if (state_ != State::Ready) return mm::touch::Status::NotInitialized;
    const auto status = write_register(auto_sleep, auto_sleep_enabled);
    if (status != mm::touch::Status::Ok) return status;
    state_ = State::Sleeping;
    return mm::touch::Status::Ok;
}

}  // namespace mm::touch::cst816
