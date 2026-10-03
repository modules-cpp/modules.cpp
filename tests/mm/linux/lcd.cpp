// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <vector>

import mm.display;
import mm.lcd.st7789;
import mm.mcu;
import mm.test;
import platform.linux.lcd.chip;

namespace {

using mm::test::expect;
using platform::linux::lcd::EmulatedSt7789;
using platform::linux::lcd::EmulationOptions;
using platform::linux::lcd::frame_height;
using platform::linux::lcd::frame_width;

constexpr unsigned int cs = 9;
constexpr unsigned int dc = 6;
constexpr unsigned int rst = 5;

std::byte b(unsigned int value) { return static_cast<std::byte>(value); }

// Raw chip-level bytes, the way a transcript test would send them: one
// command, then its data, in one selected transaction.
void send(EmulatedSt7789& chip, unsigned int command, std::initializer_list<unsigned int> data = {}) {
    chip.gpio_write(cs, false);
    chip.gpio_write(dc, false);
    const std::array command_byte{b(command)};
    chip.spi_write(command_byte);
    chip.gpio_write(dc, true);
    std::vector<std::byte> bytes;
    for (const auto value : data) bytes.push_back(b(value));
    chip.spi_write(bytes);
    chip.gpio_write(cs, true);
}

void window(EmulatedSt7789& chip, unsigned int x0, unsigned int x1, unsigned int y0,
            unsigned int y1) {
    send(chip, 0x2a, {x0 >> 8, x0 & 0xff, x1 >> 8, x1 & 0xff});
    send(chip, 0x2b, {y0 >> 8, y0 & 0xff, y1 >> 8, y1 & 0xff});
}

void pixels(EmulatedSt7789& chip, std::initializer_list<std::uint16_t> values) {
    std::vector<unsigned int> data;
    for (const auto value : values) {
        data.push_back(value >> 8);
        data.push_back(value & 0xff);
    }
    chip.gpio_write(cs, false);
    chip.gpio_write(dc, false);
    const std::array command_byte{b(0x2c)};
    chip.spi_write(command_byte);
    chip.gpio_write(dc, true);
    std::vector<std::byte> bytes;
    for (const auto value : data) bytes.push_back(b(value));
    chip.spi_write(bytes);
    chip.gpio_write(cs, true);
}

// Adafruit_ST7789's generic_st7789 list, as its displayInit sends it.
void adafruit_init(EmulatedSt7789& chip) {
    send(chip, 0x01);
    send(chip, 0x11);
    send(chip, 0x3a, {0x55});
    send(chip, 0x36, {0x08});
    window(chip, 0, 240, 0, 320);
    send(chip, 0x21);
    send(chip, 0x13);
    send(chip, 0x29);
}

std::uint16_t at(const EmulatedSt7789& chip, unsigned int column, unsigned int row) {
    return chip.frame()[static_cast<std::size_t>(row) * frame_width + column];
}

void power_on_and_reset() {
    EmulatedSt7789 chip{EmulationOptions{}};
    expect(chip.sleeping() && !chip.display_on() && chip.pixel_format() == b(0x66),
           "power-on leaves the chip asleep, off, and in 18-bit colour");

    std::vector<std::uint16_t> image(frame_width * frame_height, 0x1234);
    chip.render(image);
    expect(image.front() == 0 && image.back() == 0, "an asleep chip shows black");

    // 18-bit colour, the reset value, drops 16-bit pixels.
    send(chip, 0x11);
    window(chip, 0, 0, 0, 0);
    pixels(chip, {0xf800});
    expect(at(chip, 0, 0) == 0, "pixels are dropped until the format is 16-bit");

    adafruit_init(chip);
    expect(!chip.sleeping() && chip.display_on() && chip.inversion_on() &&
               chip.pixel_format() == b(0x55),
           "Adafruit's initialisation wakes the chip in 16-bit colour");

    window(chip, 3, 3, 4, 4);
    pixels(chip, {0x07e0});
    chip.gpio_write(rst, false);
    expect(chip.sleeping() && !chip.display_on(),
           "holding reset low resets the registers");
    send(chip, 0x29);
    expect(!chip.display_on(), "nothing is taken while reset is low");
    chip.gpio_write(rst, true);
    adafruit_init(chip);
    expect(at(chip, 3, 4) == 0x07e0, "a reset keeps the frame memory");
}

void windowing_and_gating() {
    EmulatedSt7789 chip{EmulationOptions{}};
    adafruit_init(chip);
    send(chip, 0x36, {0x00});

    window(chip, 10, 11, 20, 21);
    pixels(chip, {0x0001, 0x0002, 0x0003, 0x0004, 0x0005});
    expect(at(chip, 10, 20) == 0x0005 && at(chip, 11, 20) == 0x0002 &&
               at(chip, 10, 21) == 0x0003 && at(chip, 11, 21) == 0x0004,
           "pixels fill the window row by row and wrap to its start");

    // Bytes while chip select is high belong to another device on the bus.
    chip.gpio_write(dc, false);
    const std::array stray{b(0x28)};
    chip.spi_write(stray);
    expect(chip.display_on(), "bytes with chip select high are ignored");

    // A half-sent pixel is lost when the transaction ends.
    window(chip, 0, 0, 0, 0);
    chip.gpio_write(cs, false);
    chip.gpio_write(dc, false);
    const std::array ramwr{b(0x2c)};
    chip.spi_write(ramwr);
    chip.gpio_write(dc, true);
    const std::array half{b(0xab)};
    chip.spi_write(half);
    chip.gpio_write(cs, true);
    // Selected again in data mode, the write continues without the half.
    chip.gpio_write(cs, false);
    const std::array whole{b(0x12), b(0x34)};
    chip.spi_write(whole);
    chip.gpio_write(cs, true);
    expect(at(chip, 0, 0) == 0x1234, "a deselect drops a half-sent pixel");
}

void memory_access_orientation() {
    EmulatedSt7789 chip{EmulationOptions{}};
    adafruit_init(chip);

    send(chip, 0x36, {0x20});
    window(chip, 5, 5, 7, 7);
    pixels(chip, {0x0101});
    expect(at(chip, 7, 5) == 0x0101, "MV exchanges columns and rows");

    send(chip, 0x36, {0xc0});
    window(chip, 0, 0, 0, 0);
    pixels(chip, {0x0202});
    expect(at(chip, frame_width - 1, frame_height - 1) == 0x0202,
           "MX and MY mirror the column and the row");

    // Adafruit's rotation 1 on a 135 by 240 glass: MY|MV, x from row 40 and
    // y from column 52, lands inside the glass's part of the frame.
    send(chip, 0x36, {0xa0});
    window(chip, 40, 40, 52, 52);
    pixels(chip, {0x0303});
    expect(at(chip, 52, frame_height - 1 - 40) == 0x0303,
           "MY|MV puts rotation 1's origin at the glass's corner");
}

void rendering() {
    EmulationOptions options{};
    options.mirror_columns = true;
    options.mirror_rows = true;
    options.inverted = true;
    EmulatedSt7789 chip{options};
    adafruit_init(chip);

    // Adafruit's rotation 0 sets MX|MY; its (0, 0) is the top left of the
    // glass the module is mounted with.
    send(chip, 0x36, {0xc0});
    window(chip, 0, 0, 0, 0);
    pixels(chip, {0xf800});
    std::vector<std::uint16_t> image(frame_width * frame_height);
    chip.render(image);
    expect(image[0] == 0xf800, "rotation 0 draws at the glass's top left in true colour");

    send(chip, 0x20);
    chip.render(image);
    expect(image[0] == static_cast<std::uint16_t>(~0xf800u),
           "an IPS glass without INVON shows inverted colour");
    send(chip, 0x21);

    send(chip, 0x36, {0xc8});
    chip.render(image);
    expect(image[0] == 0x001f, "BGR order swaps red and blue");

    send(chip, 0x28);
    chip.render(image);
    expect(image[0] == 0, "display off shows black");

    EmulationOptions cropped{};
    cropped.mirror_columns = false;
    cropped.mirror_rows = false;
    cropped.inverted = false;
    cropped.visible_x = 52;
    cropped.visible_y = 40;
    cropped.visible_width = 135;
    cropped.visible_height = 240;
    EmulatedSt7789 small{cropped};
    adafruit_init(small);
    send(small, 0x20);
    send(small, 0x36, {0x00});
    window(small, 52, 52, 40, 40);
    pixels(small, {0x07e0});
    std::vector<std::uint16_t> glass(135 * 240);
    small.render(glass);
    expect(glass[0] == 0x07e0, "the visible rectangle crops the frame to the glass");

    expect(small.changed(), "a change is reported");
    small.presented();
    expect(!small.changed(), "and cleared once presented");
}

// The ILI9341 board's glass: columns mirrored, subpixels blue first, not IPS,
// on the Uno pins. Adafruit_ILI9341's begin, without a reset line, sends
// SWRESET and then its initcmd list, and its rotation 0 is MX|BGR.
void ili9341_glass() {
    EmulationOptions options{};
    options.chip_select_gpio = 10;
    options.data_command_gpio = 9;
    options.reset_gpio = 8;
    options.mirror_columns = true;
    options.mirror_rows = false;
    options.inverted = false;
    options.bgr = true;
    EmulatedSt7789 chip{options};

    const auto command = [&](unsigned int value, std::initializer_list<unsigned int> data) {
        chip.gpio_write(10, false);
        chip.gpio_write(9, false);
        const std::array command_byte{b(value)};
        chip.spi_write(command_byte);
        chip.gpio_write(9, true);
        std::vector<std::byte> bytes;
        for (const auto item : data) bytes.push_back(b(item));
        chip.spi_write(bytes);
        chip.gpio_write(10, true);
    };
    command(0x01, {});
    command(0xef, {0x03, 0x80, 0x02});
    command(0xcf, {0x00, 0xc1, 0x30});
    command(0xed, {0x64, 0x03, 0x12, 0x81});
    command(0xe8, {0x85, 0x00, 0x78});
    command(0xcb, {0x39, 0x2c, 0x00, 0x34, 0x02});
    command(0xf7, {0x20});
    command(0xea, {0x00, 0x00});
    command(0xc0, {0x23});
    command(0xc1, {0x10});
    command(0xc5, {0x3e, 0x28});
    command(0xc7, {0x86});
    command(0x36, {0x48});
    command(0x37, {0x00});
    command(0x3a, {0x55});
    command(0xb1, {0x00, 0x18});
    command(0xb6, {0x08, 0x82, 0x27});
    command(0xf2, {0x00});
    command(0x26, {0x01});
    command(0xe0, {0x0f, 0x31, 0x2b, 0x0c, 0x0e, 0x08, 0x4e, 0xf1, 0x37, 0x07, 0x10, 0x03,
                   0x0e, 0x09, 0x00});
    command(0xe1, {0x00, 0x0e, 0x14, 0x03, 0x11, 0x07, 0x31, 0xc1, 0x48, 0x08, 0x0f, 0x0c,
                   0x31, 0x36, 0x0f});
    command(0x11, {});
    command(0x29, {});
    expect(!chip.sleeping() && chip.display_on() && chip.memory_access() == b(0x48) &&
               chip.pixel_format() == b(0x55),
           "Adafruit's ILI9341 initialisation wakes the chip, the vendor commands ignored");

    // setRotation(0), then one red pixel at (0, 0) and one blue at (0, 1).
    command(0x36, {0x48});
    command(0x2a, {0x00, 0x00, 0x00, 0x00});
    command(0x2b, {0x00, 0x00, 0x00, 0x01});
    command(0x2c, {0xf8, 0x00, 0x00, 0x1f});
    std::vector<std::uint16_t> image(frame_width * frame_height);
    chip.render(image);
    expect(image[0] == 0xf800 && image[frame_width] == 0x001f,
           "rotation 0 draws at the glass's top left in true colour");
    expect(at(chip, frame_width - 1, 0) == 0xf800, "MX puts it in the frame's last column");
}

// The real controller runs against the emulated chip through the same seam it
// uses on hardware, with this test's platform swapped in for its duration.
class ChipPlatform final : public mm::mcu::Platform {
public:
    explicit ChipPlatform(EmulatedSt7789& chip) : chip_(chip) {}

    [[nodiscard]] mm::mcu::Status gpio_configure(unsigned int, mm::mcu::Direction,
                                                 mm::mcu::Pull) override {
        return mm::mcu::Status::Ok;
    }
    [[nodiscard]] mm::mcu::Status gpio_write(unsigned int pin, bool high) override {
        chip_.gpio_write(pin, high);
        return mm::mcu::Status::Ok;
    }
    [[nodiscard]] mm::mcu::Status spi_configure(const mm::mcu::SpiConfiguration& value) override {
        return value.instance == 0 ? mm::mcu::Status::Ok : mm::mcu::Status::BadArgument;
    }
    [[nodiscard]] mm::mcu::Status spi_write(unsigned int instance,
                                            std::span<const std::byte> data) override {
        if (instance != 0) return mm::mcu::Status::Unsupported;
        chip_.spi_write(data);
        return mm::mcu::Status::Ok;
    }
    [[nodiscard]] mm::mcu::Status delay_ms(unsigned long) override {
        return mm::mcu::Status::Ok;
    }

private:
    EmulatedSt7789& chip_;
};

class RestorePlatform {
public:
    explicit RestorePlatform(mm::mcu::Platform& saved) : saved_(saved) {}
    ~RestorePlatform() { mm::mcu::set_platform(saved_); }
    RestorePlatform(const RestorePlatform&) = delete;
    RestorePlatform& operator=(const RestorePlatform&) = delete;

private:
    mm::mcu::Platform& saved_;
};

void controller_through_the_seam() {
    EmulatedSt7789 chip{EmulationOptions{}};
    ChipPlatform platform{chip};
    RestorePlatform restore{mm::mcu::platform()};
    mm::mcu::set_platform(platform);

    // The board's descriptor, as platform.linux.lcd.display declares it.
    const mm::lcd::st7789::Wiring wiring{
        .spi = {.instance = 0,
                .clock_gpio = 10,
                .transmit_gpio = 11,
                .receive_gpio = std::nullopt,
                .baud = 32'000'000,
                .mode = mm::mcu::SpiMode::Mode0,
                .bit_order = mm::mcu::BitOrder::MostSignificantFirst},
        .chip_select_gpio = cs,
        .data_command_gpio = dc,
        .reset_gpio = rst,
        .backlight_gpio = std::nullopt,
        .reset_step_ms = 10,
        .sleep_out_ms = 10,
    };
    const mm::lcd::st7789::Panel panel{
        .width = 240,
        .height = 320,
        .initialization = {},
        .memory_access = std::byte{0xc0},
        .inverted = true,
        .column_offset = 0,
        .row_offset = 0,
    };
    mm::lcd::st7789::Controller controller{wiring, panel};

    expect(controller.initialize() == mm::display::Status::Ok, "the controller initialises");
    expect(!chip.sleeping() && chip.display_on() && chip.inversion_on() &&
               chip.memory_access() == b(0xc0) &&
               (chip.pixel_format() & b(0x0f)) == b(0x05),
           "initialisation leaves the chip awake, on, and in 16-bit colour");

    expect(controller.clear(mm::display::Color::White) == mm::display::Status::Ok,
           "clearing white");
    std::vector<std::uint16_t> image(frame_width * frame_height);
    chip.render(image);
    expect(image.front() == 0xffff && image.back() == 0xffff, "the glass shows white");

    const std::array red{b(0xf8), b(0x00)};
    expect(controller.write({2, 3, 1, 1}, red) == mm::display::Status::Ok,
           "writing one red pixel");
    chip.render(image);
    expect(image[3 * frame_width + 2] == 0xf800,
           "the pixel shows where the portable layer put it, upright");
}

const mm::test::case_ cases[]{
    {"platform.linux.lcd.chip power-on and reset", power_on_and_reset},
    {"platform.linux.lcd.chip windowing and gating", windowing_and_gating},
    {"platform.linux.lcd.chip memory access orientation", memory_access_orientation},
    {"platform.linux.lcd.chip rendering", rendering},
    {"platform.linux.lcd.chip controller through the seam", controller_through_the_seam},
    {"platform.linux.lcd.chip ILI9341 glass", ili9341_glass},
};
const mm::test::registrar registrar{"platform.linux.lcd.chip", cases};

}  // namespace
