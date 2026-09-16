// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A virtual SSD1680. It is not a display provider and it owns no window: it
// is the chip the driver's SPI bytes land in. The real
// mm.epaper.ssd1680::Controller therefore runs unmodified against it, through
// the mm.mcu seam, and "same behaviour as the driver" is the same code with
// only the transport answer changed.
//
// Time is caller-driven: nothing here sleeps or reads a clock. The adapter
// feeds real milliseconds through advance, so busy release falls out of the
// driver's own wait_until_ready loop exactly as it does on hardware.
module;

#include <cstddef>
#include <span>
#include <vector>

export module platform.linux.epaper.chip;

export namespace platform::linux::epaper {

struct EmulationOptions {
    unsigned int width = 152;
    unsigned int height = 296;
    unsigned int chip_select_gpio = 9;
    unsigned int data_command_gpio = 8;
    unsigned int reset_gpio = 12;
    unsigned int busy_gpio = 13;
    unsigned long soft_reset_busy_ms = 20;
    unsigned long refresh_ms = 500;
};

class EmulatedSsd1680 {
public:
    explicit EmulatedSsd1680(EmulationOptions options);

    // GPIO edges the driver drives. Unknown pins are ignored, as a chip
    // ignores lines it does not own.
    void gpio_write(unsigned int pin, bool high);
    // Only the busy pin reads back a level. A false answer means the line is
    // not the chip's, which the adapter maps to its honest Status.
    [[nodiscard]] bool gpio_read(unsigned int pin, bool& high);

    // Bytes are consumed only while chip select is low and the chip is
    // awake; data-mode bytes stream to the last command.
    void spi_write(std::span<const std::byte> data);

    void advance(unsigned long milliseconds);
    [[nodiscard]] unsigned long ticks_ms() const;

    // Set once per released master activation, until the adapter shows the
    // frame.
    [[nodiscard]] bool refresh_due() const { return refresh_due_; }
    void refresh_presented();

    [[nodiscard]] std::span<const std::byte> black_white_ram() const;
    [[nodiscard]] std::span<const std::byte> chromatic_ram() const;

private:
    void pump();
    void reset_chip();
    void command(std::byte value);
    void data_byte(std::byte value);
    void ram(std::vector<std::byte>& plane, std::byte value);

    EmulationOptions options_;
    std::size_t row_bytes_;
    std::vector<std::byte> black_white_;
    std::vector<std::byte> chromatic_;

    bool chip_select_low_ = false;
    bool data_command_high_ = false;
    bool reset_low_ = false;
    bool sleeping_ = false;

    std::byte last_command_{};
    std::size_t argument_ = 0;
    std::byte argument_bytes_[25]{};

    unsigned int x_window_start_ = 0;
    unsigned int x_window_end_ = 0;
    unsigned int y_window_start_ = 0;
    unsigned int y_window_end_ = 0;
    unsigned int x_counter_ = 0;
    unsigned int y_counter_ = 0;

    unsigned long now_ = 0;
    unsigned long release_at_ = 0;
    bool release_presents_ = false;
    bool refresh_due_ = false;
};

}

namespace platform::linux::epaper {

EmulatedSsd1680::EmulatedSsd1680(EmulationOptions options)
    : options_(options),
      row_bytes_((static_cast<std::size_t>(options_.width) + 7) / 8),
      black_white_(row_bytes_ * options_.height, std::byte{0xff}),
      chromatic_(row_bytes_ * options_.height, std::byte{0xff}) {}

void EmulatedSsd1680::pump() {
    if (release_at_ != 0 && now_ >= release_at_) {
        release_at_ = 0;
        if (release_presents_) {
            release_presents_ = false;
            refresh_due_ = true;
        }
    }
}

void EmulatedSsd1680::reset_chip() {
    // A real panel's RAM is garbage after power-on; deterministic white keeps
    // host tests assertable. The portable sequence clears the panel before
    // writing, so nothing portable can see the difference. White means the
    // black/white plane set and the chromatic pigment *inactive*: a chromatic
    // byte of 0xff drives red, so 0x00 is the pigment-free default.
    black_white_.assign(black_white_.size(), std::byte{0xff});
    chromatic_.assign(chromatic_.size(), std::byte{0x00});
    sleeping_ = false;
    last_command_ = std::byte{};
    argument_ = 0;
    x_window_start_ = 0;
    x_window_end_ = 0;
    y_window_start_ = 0;
    y_window_end_ = 0;
    x_counter_ = 0;
    y_counter_ = 0;
    release_at_ = 0;
    release_presents_ = false;
    refresh_due_ = false;
}

void EmulatedSsd1680::gpio_write(unsigned int pin, bool high) {
    if (pin == options_.chip_select_gpio) chip_select_low_ = !high;
    if (pin == options_.data_command_gpio) data_command_high_ = high;
    if (pin == options_.reset_gpio && high != reset_low_) {
        reset_low_ = high;
        if (high) reset_chip();  // active-low: release is the reset edge
    }
}

bool EmulatedSsd1680::gpio_read(unsigned int pin, bool& high) {
    if (pin != options_.busy_gpio) return false;
    pump();
    high = release_at_ != 0;
    return true;
}

void EmulatedSsd1680::spi_write(std::span<const std::byte> data) {
    for (const auto value : data) {
        if (!chip_select_low_ || sleeping_) continue;
        if (!data_command_high_) command(value);
        else data_byte(value);
    }
}

void EmulatedSsd1680::advance(unsigned long milliseconds) {
    now_ += milliseconds;
    pump();
}

unsigned long EmulatedSsd1680::ticks_ms() const { return now_; }

void EmulatedSsd1680::refresh_presented() { refresh_due_ = false; }

std::span<const std::byte> EmulatedSsd1680::black_white_ram() const {
    return black_white_;
}

std::span<const std::byte> EmulatedSsd1680::chromatic_ram() const {
    return chromatic_;
}

void EmulatedSsd1680::command(std::byte value) {
    last_command_ = value;
    argument_ = 0;
    if (value == std::byte{0x12} && release_at_ == 0) {
        release_at_ = now_ + options_.soft_reset_busy_ms;
        release_presents_ = false;
    }
    if (value == std::byte{0x20} && release_at_ == 0) {
        release_at_ = now_ + options_.refresh_ms;
        release_presents_ = true;
    }
}

void EmulatedSsd1680::data_byte(std::byte value) {
    switch (last_command_) {
        case std::byte{0x24}:
            ram(black_white_, value);
            return;
        case std::byte{0x25}:
            ram(chromatic_, value);
            return;
        case std::byte{0x10}:
            if (argument_ == 0 && value == std::byte{0x01}) sleeping_ = true;
            return;
        case std::byte{0x44}:
            argument_bytes_[argument_++] = value;
            if (argument_ == 2) {
                x_window_start_ = static_cast<unsigned int>(
                    static_cast<unsigned char>(argument_bytes_[0]));
                x_window_end_ = static_cast<unsigned int>(
                    static_cast<unsigned char>(argument_bytes_[1]));
            }
            return;
        case std::byte{0x45}:
            argument_bytes_[argument_++] = value;
            if (argument_ == 4) {
                y_window_start_ =
                    static_cast<unsigned int>(static_cast<unsigned char>(
                        argument_bytes_[0])) |
                    (static_cast<unsigned int>(static_cast<unsigned char>(
                        argument_bytes_[1]))
                        << 8);
                y_window_end_ =
                    static_cast<unsigned int>(static_cast<unsigned char>(
                        argument_bytes_[2])) |
                    (static_cast<unsigned int>(static_cast<unsigned char>(
                        argument_bytes_[3]))
                        << 8);
            }
            return;
        case std::byte{0x4e}:
            if (argument_++ == 0)
                x_counter_ = static_cast<unsigned int>(
                    static_cast<unsigned char>(value));
            return;
        case std::byte{0x4f}:
            argument_bytes_[argument_++] = value;
            if (argument_ == 2)
                y_counter_ = static_cast<unsigned int>(static_cast<unsigned char>(
                                 argument_bytes_[0])) |
                             (static_cast<unsigned int>(static_cast<unsigned char>(
                                 argument_bytes_[1]))
                                 << 8);
            return;
        case std::byte{0x11}:
        case std::byte{0x22}:
        case std::byte{0x3c}:
            // Store-and-ignore: the emulation carries no waveform state.
            ++argument_;
            return;
        case std::byte{0x2c}:
        case std::byte{0x2d}:
        case std::byte{0x2e}:
        case std::byte{0x2f}:
            ++argument_;
            return;
        default:
            return;
    }
}

void EmulatedSsd1680::ram(std::vector<std::byte>& plane, std::byte value) {
    // The portable driver addresses RAM in byte columns and rows: one data
    // byte is one whole byte of one row, and the window and counters count
    // bytes, not pixels.
    plane[static_cast<std::size_t>(y_counter_) * row_bytes_ + x_counter_] = value;

    // X increments per byte and wraps to the window start; Y advances per
    // row. The counters stay inside the window, so an overlong stream cannot
    // escape it.
    if (++x_counter_ > x_window_end_) {
        x_counter_ = x_window_start_;
        if (++y_counter_ > y_window_end_) y_counter_ = y_window_end_;
    }
}

}
