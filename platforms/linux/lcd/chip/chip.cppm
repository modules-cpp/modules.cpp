// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A virtual ST7789. It is not a display provider and it owns no window: it is
// the controller the SPI bytes land in, so mm.lcd.st7789::Controller and
// Adafruit's ST7789 library both run unmodified against it through the
// mm.mcu seam. It keeps the controller's frame memory and the few registers
// that decide what the glass shows, and render turns them into that image.
//
// Nothing here reads a clock or sleeps: the controller has no busy line, and
// the settling times a driver waits after reset or sleep-out cost nothing.
module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module platform.linux.lcd.chip;

export namespace platform::linux::lcd {

// The controller's frame memory is 240 by 320 whatever the glass.
inline constexpr unsigned int frame_width = 240;
inline constexpr unsigned int frame_height = 320;

struct EmulationOptions {
    unsigned int chip_select_gpio = 9;
    unsigned int data_command_gpio = 6;
    unsigned int reset_gpio = 5;
    // How the glass shows the frame, which belongs to the module rather than
    // the controller. rotated_180 is the half turn Adafruit's modules are
    // mounted with; inverted says true colours need INVON, as on IPS panels.
    bool rotated_180 = true;
    bool inverted = true;
    // The part of the frame the glass covers, in frame coordinates before
    // the panel's rotation.
    unsigned int visible_x = 0;
    unsigned int visible_y = 0;
    unsigned int visible_width = frame_width;
    unsigned int visible_height = frame_height;
};

class EmulatedSt7789 {
public:
    explicit EmulatedSt7789(EmulationOptions options);

    // Pin edges the driver drives. Lines that are not the chip's are ignored,
    // as the chip ignores lines it does not own.
    void gpio_write(unsigned int pin, bool high);

    // Bytes are taken only while chip select is low and reset is high: a
    // command while data/command is low, arguments and pixels while high.
    void spi_write(std::span<const std::byte> data);

    // The image the glass shows, visible_width by visible_height RGB565
    // pixels, row by row; out must hold that many. Black while asleep or with
    // the display off.
    void render(std::span<std::uint16_t> out) const;

    // Whether anything that render depends on changed since presented.
    [[nodiscard]] bool changed() const { return changed_; }
    void presented() { changed_ = false; }

    // The frame memory as written, row by row in frame coordinates, and the
    // registers, for tests.
    [[nodiscard]] std::span<const std::uint16_t> frame() const { return frame_; }
    [[nodiscard]] bool sleeping() const { return sleeping_; }
    [[nodiscard]] bool display_on() const { return display_on_; }
    [[nodiscard]] bool inversion_on() const { return inversion_on_; }
    [[nodiscard]] std::byte memory_access() const { return memory_access_; }
    [[nodiscard]] std::byte pixel_format() const { return pixel_format_; }
    [[nodiscard]] const EmulationOptions& options() const { return options_; }

private:
    void reset_registers();
    void command(std::byte value);
    void argument(std::byte value);
    void pixel_byte(std::byte value);
    void store(std::uint16_t pixel);

    EmulationOptions options_;
    std::vector<std::uint16_t> frame_;

    bool chip_select_low_ = false;
    bool data_command_high_ = false;
    bool reset_low_ = false;

    bool sleeping_ = true;
    bool display_on_ = false;
    bool inversion_on_ = false;
    std::byte memory_access_{};
    std::byte pixel_format_{0x66};

    std::byte last_command_{};
    unsigned int arguments_ = 0;
    std::byte argument_bytes_[4]{};

    unsigned int column_start_ = 0;
    unsigned int column_end_ = frame_width - 1;
    unsigned int row_start_ = 0;
    unsigned int row_end_ = frame_height - 1;
    unsigned int column_ = 0;
    unsigned int row_ = 0;
    bool high_byte_pending_ = false;
    std::byte high_byte_{};

    bool changed_ = true;
};

}

namespace platform::linux::lcd {

namespace {

constexpr std::byte software_reset{0x01};
constexpr std::byte sleep_in_command{0x10};
constexpr std::byte sleep_out_command{0x11};
constexpr std::byte inversion_off_command{0x20};
constexpr std::byte inversion_on_command{0x21};
constexpr std::byte display_off_command{0x28};
constexpr std::byte display_on_command{0x29};
constexpr std::byte column_address{0x2a};
constexpr std::byte row_address{0x2b};
constexpr std::byte memory_write{0x2c};
constexpr std::byte memory_access_control{0x36};
constexpr std::byte pixel_format_set{0x3a};

constexpr std::byte row_mirror{0x80};
constexpr std::byte column_mirror{0x40};
constexpr std::byte exchange{0x20};
constexpr std::byte blue_first{0x08};

unsigned int from_pair(std::byte high, std::byte low) {
    return (static_cast<unsigned int>(static_cast<unsigned char>(high)) << 8) |
           static_cast<unsigned int>(static_cast<unsigned char>(low));
}

bool has(std::byte value, std::byte bit) { return (value & bit) != std::byte{}; }

}  // namespace

EmulatedSt7789::EmulatedSt7789(EmulationOptions options)
    : options_(options),
      frame_(static_cast<std::size_t>(frame_width) * frame_height, 0) {
    if (options_.visible_x >= frame_width) options_.visible_x = 0;
    if (options_.visible_y >= frame_height) options_.visible_y = 0;
    if (options_.visible_width == 0 ||
        options_.visible_x + options_.visible_width > frame_width)
        options_.visible_width = frame_width - options_.visible_x;
    if (options_.visible_height == 0 ||
        options_.visible_y + options_.visible_height > frame_height)
        options_.visible_height = frame_height - options_.visible_y;
}

void EmulatedSt7789::reset_registers() {
    // The datasheet's reset values. Frame memory is not reset: a reset
    // changes what the glass shows, not what was written.
    sleeping_ = true;
    display_on_ = false;
    inversion_on_ = false;
    memory_access_ = std::byte{};
    pixel_format_ = std::byte{0x66};
    last_command_ = std::byte{};
    arguments_ = 0;
    column_start_ = 0;
    column_end_ = frame_width - 1;
    row_start_ = 0;
    row_end_ = frame_height - 1;
    column_ = 0;
    row_ = 0;
    high_byte_pending_ = false;
    changed_ = true;
}

void EmulatedSt7789::gpio_write(unsigned int pin, bool high) {
    if (pin == options_.chip_select_gpio) {
        chip_select_low_ = !high;
        // A deselect ends the transaction; a pixel left half-sent is lost.
        if (high) high_byte_pending_ = false;
    }
    if (pin == options_.data_command_gpio) data_command_high_ = high;
    if (pin == options_.reset_gpio && reset_low_ == high) {
        reset_low_ = !high;
        if (reset_low_) reset_registers();
    }
}

void EmulatedSt7789::spi_write(std::span<const std::byte> data) {
    if (!chip_select_low_ || reset_low_) return;
    for (const auto value : data) {
        if (!data_command_high_) {
            command(value);
        } else if (last_command_ == memory_write) {
            pixel_byte(value);
        } else {
            argument(value);
        }
    }
}

void EmulatedSt7789::command(std::byte value) {
    last_command_ = value;
    arguments_ = 0;
    high_byte_pending_ = false;
    if (value == software_reset) {
        reset_registers();
        return;
    }
    if (value == sleep_in_command) sleeping_ = true;
    if (value == sleep_out_command) sleeping_ = false;
    if (value == inversion_off_command) inversion_on_ = false;
    if (value == inversion_on_command) inversion_on_ = true;
    if (value == display_off_command) display_on_ = false;
    if (value == display_on_command) display_on_ = true;
    if (value == memory_write) {
        column_ = column_start_;
        row_ = row_start_;
    }
    changed_ = true;
}

void EmulatedSt7789::argument(std::byte value) {
    if (arguments_ < 4) argument_bytes_[arguments_] = value;
    ++arguments_;
    if (last_command_ == column_address && arguments_ == 4) {
        column_start_ = from_pair(argument_bytes_[0], argument_bytes_[1]);
        column_end_ = from_pair(argument_bytes_[2], argument_bytes_[3]);
    } else if (last_command_ == row_address && arguments_ == 4) {
        row_start_ = from_pair(argument_bytes_[0], argument_bytes_[1]);
        row_end_ = from_pair(argument_bytes_[2], argument_bytes_[3]);
    } else if (last_command_ == memory_access_control && arguments_ == 1) {
        memory_access_ = value;
        changed_ = true;
    } else if (last_command_ == pixel_format_set && arguments_ == 1) {
        pixel_format_ = value;
    }
}

void EmulatedSt7789::pixel_byte(std::byte value) {
    // Only the 16-bit format is modelled; any other drops the pixels rather
    // than guessing at their packing.
    if ((pixel_format_ & std::byte{0x0f}) != std::byte{0x05}) return;
    if (!high_byte_pending_) {
        high_byte_ = value;
        high_byte_pending_ = true;
        return;
    }
    high_byte_pending_ = false;
    store(static_cast<std::uint16_t>(from_pair(high_byte_, value)));
}

void EmulatedSt7789::store(std::uint16_t pixel) {
    // The logical column and row become a frame address as the datasheet
    // orders it: exchange first, then mirror each within the frame.
    unsigned int column = column_;
    unsigned int row = row_;
    if (has(memory_access_, exchange)) {
        const auto swapped = column;
        column = row;
        row = swapped;
    }
    if (has(memory_access_, column_mirror) && column < frame_width)
        column = frame_width - 1 - column;
    if (has(memory_access_, row_mirror) && row < frame_height)
        row = frame_height - 1 - row;
    if (column < frame_width && row < frame_height) {
        frame_[static_cast<std::size_t>(row) * frame_width + column] = pixel;
        changed_ = true;
    }

    // The column advances and wraps to the next row at the window's end,
    // and the rows wrap back to the window's start after the last.
    if (column_ >= column_end_) {
        column_ = column_start_;
        row_ = row_ >= row_end_ ? row_start_ : row_ + 1;
    } else {
        ++column_;
    }
}

void EmulatedSt7789::render(std::span<std::uint16_t> out) const {
    const auto width = options_.visible_width;
    const auto height = options_.visible_height;
    const auto count = static_cast<std::size_t>(width) * height;
    if (out.size() < count) return;
    if (sleeping_ || !display_on_) {
        for (std::size_t i = 0; i < count; ++i) out[i] = 0;
        return;
    }
    const bool invert = inversion_on_ != options_.inverted;
    const bool swap_red_blue = has(memory_access_, blue_first);
    for (unsigned int y = 0; y < height; ++y) {
        for (unsigned int x = 0; x < width; ++x) {
            unsigned int column = options_.visible_x + x;
            unsigned int row = options_.visible_y + y;
            if (options_.rotated_180) {
                column = options_.visible_x + (width - 1 - x);
                row = options_.visible_y + (height - 1 - y);
            }
            auto pixel = frame_[static_cast<std::size_t>(row) * frame_width + column];
            if (invert) pixel = static_cast<std::uint16_t>(~pixel);
            if (swap_red_blue) {
                pixel = static_cast<std::uint16_t>(((pixel & 0x001f) << 11) |
                                                   (pixel & 0x07e0) |
                                                   ((pixel & 0xf800) >> 11));
            }
            out[static_cast<std::size_t>(y) * width + x] = pixel;
        }
    }
}

}
