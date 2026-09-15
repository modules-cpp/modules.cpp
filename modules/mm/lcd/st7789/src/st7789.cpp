// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <array>
#include <cstddef>
#include <span>

module mm.lcd.st7789;

namespace mm::lcd::st7789 {

namespace {

constexpr std::byte sleep_out{0x11};
constexpr std::byte inversion_off{0x20};
constexpr std::byte inversion_on{0x21};
constexpr std::byte display_off{0x28};
constexpr std::byte display_on{0x29};
constexpr std::byte column_address{0x2a};
constexpr std::byte row_address{0x2b};
constexpr std::byte memory_write{0x2c};
constexpr std::byte memory_access_control{0x36};
constexpr std::byte pixel_format{0x3a};

// Sixteen bits per pixel, which is what makes a pixel two bytes of RGB565.
constexpr std::byte pixel_format_16bit{0x05};

constexpr unsigned int bits_per_pixel = 16;
constexpr unsigned int bytes_per_pixel = 2;

// A fill is streamed in bounded chunks so a full-screen clear needs no
// framebuffer, exactly as the ePaper controller does it.
constexpr std::size_t chunk_pixels = 16;

// mm.display's three named colours as RGB565. A caller that wants any other
// colour composes pixels and calls write.
[[nodiscard]] unsigned int rgb565(mm::display::Color color) {
    switch (color) {
        case mm::display::Color::White: return 0xffff;
        case mm::display::Color::Black: return 0x0000;
        case mm::display::Color::Red: return 0xf800;
    }
    return 0x0000;
}

}  // namespace

Controller::Controller(Wiring wiring, Panel panel)
    : wiring_(wiring), panel_(panel) {}

mm::display::Status Controller::from_mcu(mm::mcu::Status status) const {
    switch (status) {
        case mm::mcu::Status::Ok: return mm::display::Status::Ok;
        case mm::mcu::Status::BadArgument: return mm::display::Status::BadArgument;
        case mm::mcu::Status::Unsupported: return mm::display::Status::Unsupported;
        case mm::mcu::Status::Busy: return mm::display::Status::Busy;
        case mm::mcu::Status::Timeout: return mm::display::Status::Timeout;
        case mm::mcu::Status::TransportError: return mm::display::Status::TransportError;
    }
    return mm::display::Status::TransportError;
}

// A transport failure mid-transaction leaves the panel's state unknown, so the
// controller stops claiming to be ready rather than accepting the next call.
mm::display::Status Controller::fail(mm::display::Status status) {
    state_ = State::Idle;
    return status;
}

mm::display::Geometry Controller::geometry() const {
    return {panel_.width, panel_.height, bits_per_pixel};
}

mm::display::Status Controller::select(bool data) {
    auto status = from_mcu(mm::mcu::gpio_write(wiring_.data_command_gpio, data));
    if (status != mm::display::Status::Ok) return fail(status);
    return mm::display::Status::Ok;
}

mm::display::Status Controller::command(std::byte value, std::span<const std::byte> data) {
    auto status = select(false);
    if (status != mm::display::Status::Ok) return status;
    status = from_mcu(mm::mcu::gpio_write(wiring_.chip_select_gpio, false));
    if (status != mm::display::Status::Ok) return fail(status);

    const std::array command_bytes{value};
    status = from_mcu(mm::mcu::spi_write(wiring_.spi.instance, command_bytes));
    if (status != mm::display::Status::Ok) {
        const auto ignored = mm::mcu::gpio_write(wiring_.chip_select_gpio, true);
        static_cast<void>(ignored);
        return fail(status);
    }

    if (!data.empty()) {
        status = select(true);
        if (status != mm::display::Status::Ok) return status;
        status = from_mcu(mm::mcu::spi_write(wiring_.spi.instance, data));
        if (status != mm::display::Status::Ok) {
            const auto ignored = mm::mcu::gpio_write(wiring_.chip_select_gpio, true);
            static_cast<void>(ignored);
            return fail(status);
        }
    }

    return from_mcu(mm::mcu::gpio_write(wiring_.chip_select_gpio, true));
}

// Pixels only: the caller has already been put in data mode and chip select is
// already low, because a window write is one transaction from the panel's view.
mm::display::Status Controller::stream(std::span<const std::byte> data) {
    return from_mcu(mm::mcu::spi_write(wiring_.spi.instance, data));
}

mm::display::Status Controller::set_window(mm::display::Rectangle rectangle) {
    if (rectangle.width == 0 || rectangle.height == 0 ||
        rectangle.x + rectangle.width > panel_.width ||
        rectangle.y + rectangle.height > panel_.height)
        return mm::display::Status::BadArgument;

    const auto first_column = rectangle.x + panel_.column_offset;
    const auto last_column = first_column + rectangle.width - 1;
    const auto first_row = rectangle.y + panel_.row_offset;
    const auto last_row = first_row + rectangle.height - 1;

    const std::array columns{static_cast<std::byte>((first_column >> 8) & 0xff),
                             static_cast<std::byte>(first_column & 0xff),
                             static_cast<std::byte>((last_column >> 8) & 0xff),
                             static_cast<std::byte>(last_column & 0xff)};
    auto status = command(column_address, columns);
    if (status != mm::display::Status::Ok) return status;

    const std::array rows{static_cast<std::byte>((first_row >> 8) & 0xff),
                          static_cast<std::byte>(first_row & 0xff),
                          static_cast<std::byte>((last_row >> 8) & 0xff),
                          static_cast<std::byte>(last_row & 0xff)};
    return command(row_address, rows);
}

mm::display::Status Controller::reset() {
    auto status = from_mcu(mm::mcu::gpio_configure(wiring_.chip_select_gpio,
                                                   mm::mcu::Direction::Out,
                                                   mm::mcu::Pull::None));
    if (status != mm::display::Status::Ok) return status;
    status = from_mcu(mm::mcu::gpio_configure(wiring_.data_command_gpio,
                                              mm::mcu::Direction::Out,
                                              mm::mcu::Pull::None));
    if (status != mm::display::Status::Ok) return status;
    status = from_mcu(mm::mcu::gpio_configure(wiring_.reset_gpio, mm::mcu::Direction::Out,
                                              mm::mcu::Pull::None));
    if (status != mm::display::Status::Ok) return status;
    status = from_mcu(mm::mcu::gpio_write(wiring_.chip_select_gpio, true));
    if (status != mm::display::Status::Ok) return status;

    // High, low, high, waiting at each step: the controller latches its reset
    // on the falling edge and needs time either side of it.
    for (const bool level : {true, false, true}) {
        status = from_mcu(mm::mcu::gpio_write(wiring_.reset_gpio, level));
        if (status != mm::display::Status::Ok) return status;
        status = from_mcu(mm::mcu::delay_ms(wiring_.reset_step_ms));
        if (status != mm::display::Status::Ok) return status;
    }
    return mm::display::Status::Ok;
}

mm::display::Status Controller::initialize() {
    if (panel_.width == 0 || panel_.height == 0) return mm::display::Status::BadArgument;

    auto status = from_mcu(mm::mcu::spi_configure(wiring_.spi));
    if (status != mm::display::Status::Ok) return status;

    status = reset();
    if (status != mm::display::Status::Ok) return status;

    status = command(sleep_out);
    if (status != mm::display::Status::Ok) return status;
    status = from_mcu(mm::mcu::delay_ms(wiring_.sleep_out_ms));
    if (status != mm::display::Status::Ok) return status;

    const std::array access{panel_.memory_access};
    status = command(memory_access_control, access);
    if (status != mm::display::Status::Ok) return status;

    const std::array format{pixel_format_16bit};
    status = command(pixel_format, format);
    if (status != mm::display::Status::Ok) return status;

    for (const auto& entry : panel_.initialization) {
        status = command(entry.command, entry.data);
        if (status != mm::display::Status::Ok) return status;
    }

    status = command(panel_.inverted ? inversion_on : inversion_off);
    if (status != mm::display::Status::Ok) return status;
    status = command(display_on);
    if (status != mm::display::Status::Ok) return status;

    // The backlight comes on last, so the panel is initialised before anything
    // is visible rather than showing whatever its frame memory held.
    if (wiring_.backlight_gpio) {
        status = from_mcu(mm::mcu::gpio_configure(*wiring_.backlight_gpio,
                                                  mm::mcu::Direction::Out,
                                                  mm::mcu::Pull::None));
        if (status != mm::display::Status::Ok) return status;
        status = from_mcu(mm::mcu::gpio_write(*wiring_.backlight_gpio, true));
        if (status != mm::display::Status::Ok) return status;
    }

    state_ = State::Ready;
    return mm::display::Status::Ok;
}

mm::display::Status Controller::fill(mm::display::Rectangle rectangle, unsigned int pixel) {
    auto status = set_window(rectangle);
    if (status != mm::display::Status::Ok) return status;
    status = command(memory_write);
    if (status != mm::display::Status::Ok) return status;

    status = select(true);
    if (status != mm::display::Status::Ok) return status;
    status = from_mcu(mm::mcu::gpio_write(wiring_.chip_select_gpio, false));
    if (status != mm::display::Status::Ok) return fail(status);

    std::array<std::byte, chunk_pixels * bytes_per_pixel> chunk{};
    for (std::size_t index = 0; index < chunk.size(); index += bytes_per_pixel) {
        chunk[index] = static_cast<std::byte>((pixel >> 8) & 0xff);
        chunk[index + 1] = static_cast<std::byte>(pixel & 0xff);
    }

    auto remaining = static_cast<std::size_t>(rectangle.width) * rectangle.height *
                     bytes_per_pixel;
    while (remaining != 0 && status == mm::display::Status::Ok) {
        const auto count = remaining < chunk.size() ? remaining : chunk.size();
        status = stream(std::span<const std::byte>{chunk.data(), count});
        remaining -= count;
    }

    const auto deselect = from_mcu(mm::mcu::gpio_write(wiring_.chip_select_gpio, true));
    if (status != mm::display::Status::Ok) return fail(status);
    return deselect == mm::display::Status::Ok ? deselect : fail(deselect);
}

mm::display::Status Controller::clear(mm::display::Color color) {
    if (state_ != State::Ready) return mm::display::Status::NotInitialized;
    return fill({0, 0, panel_.width, panel_.height}, rgb565(color));
}

mm::display::Status Controller::write(mm::display::Rectangle rectangle,
                                      std::span<const std::byte> bytes) {
    if (state_ != State::Ready) return mm::display::Status::NotInitialized;
    if (bytes.size() != static_cast<std::size_t>(rectangle.width) * rectangle.height *
                            bytes_per_pixel)
        return mm::display::Status::BadArgument;

    auto status = set_window(rectangle);
    if (status != mm::display::Status::Ok) return status;
    status = command(memory_write);
    if (status != mm::display::Status::Ok) return status;

    status = select(true);
    if (status != mm::display::Status::Ok) return status;
    status = from_mcu(mm::mcu::gpio_write(wiring_.chip_select_gpio, false));
    if (status != mm::display::Status::Ok) return fail(status);

    status = stream(bytes);
    const auto deselect = from_mcu(mm::mcu::gpio_write(wiring_.chip_select_gpio, true));
    if (status != mm::display::Status::Ok) return fail(status);
    return deselect == mm::display::Status::Ok ? deselect : fail(deselect);
}

// Nothing to do: the panel showed the pixels as they arrived. Both modes are
// satisfied, so one portable sequence works across this panel and an ePaper one.
mm::display::Status Controller::refresh(mm::display::Refresh) {
    if (state_ != State::Ready) return mm::display::Status::NotInitialized;
    return mm::display::Status::Ok;
}

mm::display::Status Controller::sleep() {
    if (state_ != State::Ready) return mm::display::Status::NotInitialized;

    // Dark before asleep, so the panel does not show a frozen frame.
    if (wiring_.backlight_gpio) {
        const auto status = from_mcu(mm::mcu::gpio_write(*wiring_.backlight_gpio, false));
        if (status != mm::display::Status::Ok) return fail(status);
    }

    auto status = command(display_off);
    if (status != mm::display::Status::Ok) return status;
    status = command(std::byte{0x10});  // sleep in
    if (status != mm::display::Status::Ok) return status;

    state_ = State::Sleeping;
    return mm::display::Status::Ok;
}

}  // namespace mm::lcd::st7789
