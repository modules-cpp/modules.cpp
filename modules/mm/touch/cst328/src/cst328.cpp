// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <array>
#include <cstddef>
#include <span>

module mm.touch.cst328;

namespace mm::touch::cst328 {

namespace {

// Sixteen-bit register addresses, big-endian on the wire. Named for what the
// driver uses rather than reproducing the controller's whole map.
constexpr unsigned int first_touch_id = 0xd000;
constexpr unsigned int touch_flag_and_count = 0xd005;
constexpr unsigned int mode_debug_info = 0xd101;
constexpr unsigned int mode_normal = 0xd109;
constexpr unsigned int deep_sleep = 0xd105;
constexpr unsigned int ic_info = 0xd1f4;

// The controller reports this in its information block when it is the part the
// driver expects. Anything else is a different device answering the address.
constexpr unsigned int check_code = 0xcaca;

constexpr std::size_t ic_info_size = 24;
constexpr std::size_t check_code_offset = 10;

// The first point sits at 0xd000 and the flag register at 0xd005 lies between
// it and the second point, so every point after the first is two bytes further
// along than a flat five-byte stride would put it.
constexpr std::size_t point_stride = 5;
constexpr std::size_t flag_gap = 2;
constexpr std::size_t report_size = 27;

// The controller stamps its first point with this identifier. The reference
// driver treats any other value as a report that has not settled yet.
constexpr unsigned int first_point_identifier = 0x06;

constexpr unsigned int max_points = 5;

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

// A mode selection is a register address with no payload: the write ends after
// the two address bytes.
mm::touch::Status Controller::command(unsigned int register_address) {
    const std::array data{static_cast<std::byte>((register_address >> 8) & 0xff),
                          static_cast<std::byte>(register_address & 0xff)};
    return from_mcu(mm::mcu::i2c_write(wiring_.i2c.instance, wiring_.address, data));
}

mm::touch::Status Controller::read_register(unsigned int register_address,
                                            std::span<std::byte> data) {
    const std::array command_bytes{
        static_cast<std::byte>((register_address >> 8) & 0xff),
        static_cast<std::byte>(register_address & 0xff)};
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

    // Debug-information mode is what makes the identification block readable;
    // normal reporting mode is entered again before returning.
    status = command(mode_debug_info);
    if (status != mm::touch::Status::Ok) return status;

    std::array<std::byte, ic_info_size> information{};
    status = read_register(ic_info, information);
    if (status != mm::touch::Status::Ok) return status;

    const auto reported = (byte_value(information[check_code_offset + 1]) << 8) |
                          byte_value(information[check_code_offset]);

    status = command(mode_normal);
    if (status != mm::touch::Status::Ok) return status;

    // Checked after normal mode is restored, so a wrong part is not left in
    // debug mode by a driver that gave up early.
    if (reported != check_code) return mm::touch::Status::Unsupported;

    state_ = State::Ready;
    return mm::touch::Status::Ok;
}

mm::touch::Status Controller::read(std::span<mm::touch::Point> points, std::size_t& count) {
    if (state_ != State::Ready) return mm::touch::Status::NotInitialized;

    std::array<std::byte, 1> flag{};
    auto status = read_register(touch_flag_and_count, flag);
    if (status != mm::touch::Status::Ok) return status;

    const auto reported = byte_value(flag[0]) & 0x0f;
    if (reported == 0) {
        count = 0;
        return mm::touch::Status::Ok;
    }

    std::array<std::byte, report_size> report{};
    status = read_register(first_touch_id, report);
    if (status != mm::touch::Status::Ok) return status;

    // An unsettled report is not an error: the panel is being touched and the
    // controller has not finished saying where, so the honest answer is that no
    // position is available yet.
    if ((byte_value(report[0]) & 0x0f) != first_point_identifier) {
        count = 0;
        return mm::touch::Status::Ok;
    }

    auto available = reported;
    if (available > panel_.points) available = panel_.points;

    // A caller never learns about points it had no room for, which is what
    // makes a span of one a valid single-touch client of a five-point panel.
    count = available < points.size() ? available : points.size();

    for (std::size_t index = 0; index < count; ++index) {
        const auto base = index * point_stride + (index > 0 ? flag_gap : 0);
        const auto high_x = byte_value(report[base + 1]);
        const auto high_y = byte_value(report[base + 2]);
        const auto low = byte_value(report[base + 3]);
        points[index].x = (high_x << 4) | ((low & 0xf0) >> 4);
        points[index].y = (high_y << 4) | (low & 0x0f);
    }

    return mm::touch::Status::Ok;
}

mm::touch::Status Controller::sleep() {
    if (state_ != State::Ready) return mm::touch::Status::NotInitialized;
    const auto status = command(deep_sleep);
    if (status != mm::touch::Status::Ok) return status;
    state_ = State::Sleeping;
    return mm::touch::Status::Ok;
}

}  // namespace mm::touch::cst328
