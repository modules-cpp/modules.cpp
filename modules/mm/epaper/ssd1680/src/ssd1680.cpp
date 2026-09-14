// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <array>
#include <cstddef>
#include <optional>
#include <span>

module mm.epaper.ssd1680;

namespace mm::epaper::ssd1680 {
namespace {

constexpr std::byte software_reset{0x12};
constexpr std::byte deep_sleep{0x10};
constexpr std::byte data_entry_mode{0x11};
constexpr std::byte display_update_control_2{0x22};
constexpr std::byte master_activation{0x20};
constexpr std::byte write_black_white_ram{0x24};
constexpr std::byte set_ram_x_window{0x44};
constexpr std::byte set_ram_y_window{0x45};
constexpr std::byte set_ram_x_counter{0x4e};
constexpr std::byte set_ram_y_counter{0x4f};

bool contains_gpio(const mm::mcu::Board& board, unsigned int pin) {
    if (board.gpios.empty()) return true;
    for (const auto& gpio : board.gpios)
        if (gpio.number == pin) return true;
    return false;
}

}

Controller::Controller(Wiring wiring, Panel panel) : wiring_(wiring), panel_(panel) {}

mm::display::Geometry Controller::geometry() const {
    return {panel_.width, panel_.height, 1};
}

mm::display::Status Controller::from_mcu(mm::mcu::Status status) const {
    switch (status) {
        case mm::mcu::Status::Ok: return mm::display::Status::Ok;
        case mm::mcu::Status::BadArgument: return mm::display::Status::BadArgument;
        case mm::mcu::Status::Unsupported: return mm::display::Status::Unsupported;
        case mm::mcu::Status::Busy: return mm::display::Status::Busy;
        case mm::mcu::Status::Timeout: return mm::display::Status::Timeout;
    }
    return mm::display::Status::TransportError;
}

mm::display::Status Controller::fail(mm::display::Status status) {
    if (status != mm::display::Status::Ok) state_ = State::Failed;
    return status;
}

mm::display::Status Controller::validate() const {
    if (panel_.width == 0 || panel_.height == 0 || panel_.width > 176 ||
        panel_.height > 296 || wiring_.spi.baud == 0 || wiring_.busy_timeout_ms == 0 ||
        wiring_.spi.mode != mm::mcu::SpiMode::Mode0 ||
        wiring_.spi.bit_order != mm::mcu::BitOrder::MostSignificantFirst)
        return mm::display::Status::BadArgument;

    const std::array pins{
        wiring_.spi.clock_gpio, wiring_.spi.transmit_gpio,
        wiring_.chip_select_gpio, wiring_.data_command_gpio,
        wiring_.reset_gpio, wiring_.busy_gpio,
    };
    for (std::size_t i = 0; i < pins.size(); ++i)
        for (std::size_t j = i + 1; j < pins.size(); ++j)
            if (pins[i] == pins[j]) return mm::display::Status::BadArgument;
    if (wiring_.spi.receive_gpio)
        for (const auto pin : pins)
            if (pin == *wiring_.spi.receive_gpio)
                return mm::display::Status::BadArgument;

    const auto board = mm::mcu::board();
    for (const auto pin : pins)
        if (!contains_gpio(board, pin)) return mm::display::Status::BadArgument;
    if (wiring_.spi.receive_gpio && !contains_gpio(board, *wiring_.spi.receive_gpio))
        return mm::display::Status::BadArgument;
    return mm::display::Status::Ok;
}

mm::display::Status Controller::command(std::byte value,
                                        std::span<const std::byte> data) {
    auto status = from_mcu(mm::mcu::gpio_write(wiring_.data_command_gpio, false));
    if (status != mm::display::Status::Ok) return fail(status);
    status = from_mcu(mm::mcu::gpio_write(wiring_.chip_select_gpio, false));
    if (status != mm::display::Status::Ok) return fail(status);
    const std::array command_byte{value};
    status = from_mcu(mm::mcu::spi_write(wiring_.spi.instance, command_byte));
    const auto deselect = from_mcu(mm::mcu::gpio_write(wiring_.chip_select_gpio, true));
    if (status != mm::display::Status::Ok) return fail(status);
    if (deselect != mm::display::Status::Ok) return fail(deselect);
    if (data.empty()) return mm::display::Status::Ok;

    status = from_mcu(mm::mcu::gpio_write(wiring_.data_command_gpio, true));
    if (status != mm::display::Status::Ok) return fail(status);
    status = from_mcu(mm::mcu::gpio_write(wiring_.chip_select_gpio, false));
    if (status != mm::display::Status::Ok) return fail(status);
    status = from_mcu(mm::mcu::spi_write(wiring_.spi.instance, data));
    const auto data_deselect =
        from_mcu(mm::mcu::gpio_write(wiring_.chip_select_gpio, true));
    if (status != mm::display::Status::Ok) return fail(status);
    return data_deselect == mm::display::Status::Ok ? data_deselect : fail(data_deselect);
}

mm::display::Status Controller::stream(std::byte command_value,
                                       std::span<const std::byte> data) {
    return command(command_value, data);
}

mm::display::Status Controller::wait_until_ready() {
    unsigned long start = 0;
    auto status = from_mcu(mm::mcu::ticks_ms(start));
    if (status != mm::display::Status::Ok) return fail(status);
    for (;;) {
        bool high = false;
        status = from_mcu(mm::mcu::gpio_read(wiring_.busy_gpio, high));
        if (status != mm::display::Status::Ok) return fail(status);
        if (high != wiring_.busy_active_high) return mm::display::Status::Ok;

        unsigned long now = 0;
        status = from_mcu(mm::mcu::ticks_ms(now));
        if (status != mm::display::Status::Ok) return fail(status);
        if (now - start >= wiring_.busy_timeout_ms)
            return fail(mm::display::Status::Timeout);
        status = from_mcu(mm::mcu::delay_ms(1));
        if (status != mm::display::Status::Ok) return fail(status);
    }
}

mm::display::Status Controller::initialize() {
    auto status = validate();
    if (status != mm::display::Status::Ok) return fail(status);
    status = from_mcu(mm::mcu::spi_configure(wiring_.spi));
    if (status != mm::display::Status::Ok) return fail(status);
    for (const auto pin : {wiring_.chip_select_gpio, wiring_.data_command_gpio,
                           wiring_.reset_gpio}) {
        status = from_mcu(mm::mcu::gpio_configure(
            pin, mm::mcu::Direction::Out, mm::mcu::Pull::None));
        if (status != mm::display::Status::Ok) return fail(status);
    }
    status = from_mcu(mm::mcu::gpio_configure(
        wiring_.busy_gpio, mm::mcu::Direction::In, mm::mcu::Pull::None));
    if (status != mm::display::Status::Ok) return fail(status);
    status = from_mcu(mm::mcu::gpio_write(wiring_.chip_select_gpio, true));
    if (status != mm::display::Status::Ok) return fail(status);

    status = from_mcu(mm::mcu::gpio_write(wiring_.reset_gpio,
                                          !wiring_.reset_active_low));
    if (status != mm::display::Status::Ok) return fail(status);
    status = from_mcu(mm::mcu::delay_ms(wiring_.reset_hold_ms));
    if (status != mm::display::Status::Ok) return fail(status);
    status = from_mcu(mm::mcu::gpio_write(wiring_.reset_gpio,
                                          wiring_.reset_active_low));
    if (status != mm::display::Status::Ok) return fail(status);
    status = from_mcu(mm::mcu::delay_ms(wiring_.reset_release_ms));
    if (status != mm::display::Status::Ok) return fail(status);

    if ((status = command(software_reset)) != mm::display::Status::Ok) return status;
    if ((status = wait_until_ready()) != mm::display::Status::Ok) return status;
    for (const auto& entry : panel_.initialization)
        if ((status = command(entry.command, entry.data)) != mm::display::Status::Ok)
            return status;

    // X then Y increment is the portable packed-row convention this driver
    // exposes. Panel-specific scan direction remains in initialization data.
    const std::array entry_mode{std::byte{0x03}};
    if ((status = command(data_entry_mode, entry_mode)) != mm::display::Status::Ok)
        return status;
    state_ = State::Ready;
    return mm::display::Status::Ok;
}

mm::display::Status Controller::set_window(mm::display::Rectangle rectangle) {
    const unsigned int x_start = rectangle.x / 8;
    const unsigned int x_end = (rectangle.x + rectangle.width - 1) / 8;
    const unsigned int y_end = rectangle.y + rectangle.height - 1;
    const std::array x_window{std::byte(x_start), std::byte(x_end)};
    const std::array y_window{
        std::byte(rectangle.y & 0xff), std::byte((rectangle.y >> 8) & 0x01),
        std::byte(y_end & 0xff), std::byte((y_end >> 8) & 0x01),
    };
    const std::array x_counter{std::byte(x_start)};
    const std::array y_counter{
        std::byte(rectangle.y & 0xff), std::byte((rectangle.y >> 8) & 0x01),
    };
    auto status = command(set_ram_x_window, x_window);
    if (status == mm::display::Status::Ok) status = command(set_ram_y_window, y_window);
    if (status == mm::display::Status::Ok) status = command(set_ram_x_counter, x_counter);
    if (status == mm::display::Status::Ok) status = command(set_ram_y_counter, y_counter);
    return status;
}

mm::display::Status Controller::write(mm::display::Rectangle rectangle,
                                      std::span<const std::byte> bytes) {
    if (state_ != State::Ready) return mm::display::Status::NotInitialized;
    if (rectangle.width == 0 || rectangle.height == 0 || rectangle.x >= panel_.width ||
        rectangle.y >= panel_.height || rectangle.width > panel_.width - rectangle.x ||
        rectangle.height > panel_.height - rectangle.y || rectangle.x % 8 != 0 ||
        (rectangle.width % 8 != 0 && rectangle.x + rectangle.width != panel_.width) ||
        bytes.size() != static_cast<std::size_t>((rectangle.width + 7) / 8) * rectangle.height)
        return mm::display::Status::BadArgument;
    auto status = set_window(rectangle);
    return status == mm::display::Status::Ok ? stream(write_black_white_ram, bytes)
                                             : status;
}

mm::display::Status Controller::clear(mm::display::Color color) {
    if (state_ != State::Ready) return mm::display::Status::NotInitialized;
    if (color == mm::display::Color::Red) return mm::display::Status::Unsupported;
    const auto fill = color == mm::display::Color::White ? std::byte{0xff}
                                                         : std::byte{0x00};
    std::array<std::byte, 32> chunk;
    chunk.fill(fill);
    const mm::display::Rectangle full{0, 0, panel_.width, panel_.height};
    auto status = set_window(full);
    if (status != mm::display::Status::Ok) return status;

    status = command(write_black_white_ram);
    if (status != mm::display::Status::Ok) return status;
    status = from_mcu(mm::mcu::gpio_write(wiring_.data_command_gpio, true));
    if (status != mm::display::Status::Ok) return fail(status);
    status = from_mcu(mm::mcu::gpio_write(wiring_.chip_select_gpio, false));
    if (status != mm::display::Status::Ok) return fail(status);
    std::size_t remaining =
        static_cast<std::size_t>((panel_.width + 7) / 8) * panel_.height;
    while (remaining != 0 && status == mm::display::Status::Ok) {
        const auto count = remaining < chunk.size() ? remaining : chunk.size();
        status = from_mcu(mm::mcu::spi_write(
            wiring_.spi.instance, std::span<const std::byte>{chunk.data(), count}));
        remaining -= count;
    }
    const auto deselect = from_mcu(mm::mcu::gpio_write(wiring_.chip_select_gpio, true));
    if (status != mm::display::Status::Ok) return fail(status);
    return deselect == mm::display::Status::Ok ? deselect : fail(deselect);
}

mm::display::Status Controller::refresh(mm::display::Refresh mode) {
    if (state_ != State::Ready) return mm::display::Status::NotInitialized;
    const auto control = mode == mm::display::Refresh::Full
                             ? std::optional{panel_.full_update_control}
                             : panel_.partial_update_control;
    if (!control) return mm::display::Status::Unsupported;
    state_ = State::Refreshing;
    const std::array data{*control};
    auto status = command(display_update_control_2, data);
    if (status == mm::display::Status::Ok) status = command(master_activation);
    if (status == mm::display::Status::Ok) status = wait_until_ready();
    if (status != mm::display::Status::Ok) return status;
    state_ = State::Ready;
    return mm::display::Status::Ok;
}

mm::display::Status Controller::sleep() {
    if (state_ != State::Ready) return mm::display::Status::NotInitialized;
    const std::array data{panel_.deep_sleep_control};
    const auto status = command(deep_sleep, data);
    if (status == mm::display::Status::Ok) state_ = State::Sleeping;
    return status;
}

}
