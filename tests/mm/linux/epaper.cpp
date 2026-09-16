// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

import mm.display;
import mm.epaper.ssd1680;
import mm.mcu;
import mm.test;
import platform.linux.epaper.chip;

namespace {

using mm::test::expect;

// The real controller runs against the emulated chip through the same seam
// it uses on hardware: this test swaps the platform in for its duration and
// hands the chip real millisecond counts without sleeping, so a refresh that
// costs 500 ms on the wall clock costs five hundred virtual ones here.
class ChipPlatform final : public mm::mcu::Platform {
public:
    explicit ChipPlatform(platform::linux::epaper::EmulatedSsd1680& chip)
        : chip_(chip) {}

    [[nodiscard]] mm::mcu::Board board() const override {
        static const mm::mcu::Gpio gpios[]{{8, "DC"},
                                            {9, "CS"},
                                            {10, "SCLK"},
                                            {11, "SDIN"},
                                            {12, "RST"},
                                            {13, "BUSY"}};
        return {"epaper-test", gpios, std::nullopt};
    }

    [[nodiscard]] mm::mcu::Status gpio_configure(unsigned int,
                                                 mm::mcu::Direction,
                                                 mm::mcu::Pull) override {
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_write(unsigned int pin,
                                             bool high) override {
        chip_.gpio_write(pin, high);
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_read(unsigned int pin,
                                            bool& high) override {
        if (!chip_.gpio_read(pin, high)) return mm::mcu::Status::Unsupported;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status spi_configure(
        const mm::mcu::SpiConfiguration& value) override {
        return value.instance == 0 && value.baud != 0
                   ? mm::mcu::Status::Ok
                   : mm::mcu::Status::BadArgument;
    }

    [[nodiscard]] mm::mcu::Status spi_write(
        unsigned int instance, std::span<const std::byte> data) override {
        if (instance != 0) return mm::mcu::Status::Unsupported;
        chip_.spi_write(data);
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status delay_ms(unsigned long milliseconds) override {
        chip_.advance(milliseconds);
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status ticks_ms(unsigned long& ticks) override {
        ticks = chip_.ticks_ms();
        return mm::mcu::Status::Ok;
    }

private:
    platform::linux::epaper::EmulatedSsd1680& chip_;
};

// Puts the real platform back when the case leaves scope, even on failure.
class RestorePlatform {
public:
    explicit RestorePlatform(mm::mcu::Platform& saved) : saved_(saved) {}
    ~RestorePlatform() { mm::mcu::set_platform(saved_); }
    RestorePlatform(const RestorePlatform&) = delete;
    RestorePlatform& operator=(const RestorePlatform&) = delete;

private:
    mm::mcu::Platform& saved_;
};

mm::epaper::ssd1680::Wiring wiring(unsigned long busy_timeout_ms = 10'000) {
    return {
        .spi = {.instance = 0,
                .clock_gpio = 10,
                .transmit_gpio = 11,
                .receive_gpio = std::nullopt,
                .baud = 4'000'000,
                .mode = mm::mcu::SpiMode::Mode0,
                .bit_order = mm::mcu::BitOrder::MostSignificantFirst},
        .chip_select_gpio = 9,
        .data_command_gpio = 8,
        .reset_gpio = 12,
        .busy_gpio = 13,
        .reset_active_low = true,
        .busy_active_high = true,
        .reset_hold_ms = 2,
        .reset_release_ms = 200,
        .busy_timeout_ms = busy_timeout_ms,
    };
}

mm::epaper::ssd1680::Panel panel(std::optional<std::byte> chromatic = std::nullopt) {
    static const std::array border_data{std::byte{0x01}};
    static const std::array initialization{
        mm::epaper::ssd1680::InitializationCommand{std::byte{0x3c}, border_data}};
    return {
        .width = 152,
        .height = 296,
        .initialization = initialization,
        .full_update_control = std::byte{0xf7},
        .partial_update_control = std::nullopt,
        .chromatic_ram_command = chromatic,
        .deep_sleep_control = std::byte{0x01},
    };
}

bool all_bytes(std::span<const std::byte> ram, std::byte value) {
    for (const auto cell : ram)
        if (cell != value) return false;
    return true;
}

// Raw chip-level bytes, the way a transcript test would send them.
void chip_command(platform::linux::epaper::EmulatedSsd1680& chip,
                  std::span<const std::byte> bytes) {
    chip.gpio_write(9, false);
    chip.gpio_write(8, false);
    chip.spi_write(bytes);
}

void chip_data(platform::linux::epaper::EmulatedSsd1680& chip,
               std::byte command, std::span<const std::byte> bytes) {
    chip_command(chip, std::span{&command, 1});
    chip.gpio_write(8, true);
    chip.spi_write(bytes);
}

void full_sequence() {
    platform::linux::epaper::EmulatedSsd1680 chip{
        platform::linux::epaper::EmulationOptions{}};
    ChipPlatform platform{chip};
    mm::mcu::Platform& original = mm::mcu::platform();
    mm::mcu::set_platform(platform);
    RestorePlatform restore{original};

    mm::epaper::ssd1680::Controller controller{wiring(), panel()};
    const auto geometry = controller.geometry();
    expect(geometry.width == 152 && geometry.height == 296 &&
               geometry.bits_per_pixel == 1,
           "the portable geometry is the panel's");
    expect(controller.initialize() == mm::display::Status::Ok,
           "initialize completes over the emulated chip");
    expect(all_bytes(chip.black_white_ram(), std::byte{0xff}),
           "the reset pulse leaves RAM white");
    expect(all_bytes(chip.chromatic_ram(), std::byte{0x00}),
           "the reset pulse leaves the chromatic pigment inactive");

    expect(controller.clear(mm::display::Color::White) == mm::display::Status::Ok,
           "clearing to white");
    expect(all_bytes(chip.black_white_ram(), std::byte{0xff}),
           "a white clear lands in the black/white RAM");

    // Even rows white, odd rows black.
    constexpr std::size_t row_bytes = 19;
    std::vector<std::byte> frame;
    frame.reserve(row_bytes * 296);
    for (unsigned int y = 0; y < 296; ++y)
        for (std::size_t x = 0; x < row_bytes; ++x)
            frame.push_back(y & 1 ? std::byte{0x00} : std::byte{0xff});
    expect(controller.write({0, 0, 152, 296}, frame) == mm::display::Status::Ok,
           "a full frame write");
    expect(chip.black_white_ram().size() == frame.size() &&
               std::equal(chip.black_white_ram().begin(),
                          chip.black_white_ram().end(), frame.begin()),
           "the RAM holds exactly the frame written");
    expect(controller.refresh(mm::display::Refresh::Full) ==
               mm::display::Status::Ok,
           "a full refresh completes once the emulated busy releases");
    expect(chip.refresh_due(), "the finished refresh is flagged for display");

    expect(controller.sleep() == mm::display::Status::Ok, "sleep");
    expect(controller.write({0, 0, 8, 8}, frame) ==
               mm::display::Status::NotInitialized,
           "a write after sleep is refused");
    expect(controller.initialize() == mm::display::Status::Ok,
           "re-initializing wakes the chip");
    expect(all_bytes(chip.black_white_ram(), std::byte{0xff}),
           "waking clears the RAM white again");

}

void refresh_timeout() {
    platform::linux::epaper::EmulationOptions options;
    options.refresh_ms = 200;
    platform::linux::epaper::EmulatedSsd1680 chip{options};
    ChipPlatform platform{chip};
    mm::mcu::Platform& original = mm::mcu::platform();
    mm::mcu::set_platform(platform);
    RestorePlatform restore{original};

    mm::epaper::ssd1680::Controller controller{wiring(100), panel()};
    expect(controller.initialize() == mm::display::Status::Ok,
           "initializing before a too-short busy timeout");
    expect(controller.refresh(mm::display::Refresh::Full) ==
               mm::display::Status::Timeout,
           "a refresh slower than the wiring budget times out");
    expect(!chip.refresh_due(), "the never-released refresh is not displayable");

}

void windowing() {
    platform::linux::epaper::EmulatedSsd1680 chip{
        platform::linux::epaper::EmulationOptions{}};
    ChipPlatform platform{chip};
    mm::mcu::Platform& original = mm::mcu::platform();
    mm::mcu::set_platform(platform);
    RestorePlatform restore{original};

    mm::epaper::ssd1680::Controller controller{wiring(), panel()};
    expect(controller.initialize() == mm::display::Status::Ok, "initializing");
    expect(controller.clear(mm::display::Color::Black) == mm::display::Status::Ok,
           "filling black");
    expect(all_bytes(chip.black_white_ram(), std::byte{0x00}),
           "the black fill covers the whole RAM");

    // Two byte columns by two rows, top-left.
    chip_data(chip, std::byte{0x44}, std::array{std::byte{0x00}, std::byte{0x01}});
    chip_data(chip, std::byte{0x45}, std::array{std::byte{0x04}, std::byte{0x00},
                                                std::byte{0x05}, std::byte{0x00}});
    chip_data(chip, std::byte{0x4e}, std::array{std::byte{0x00}});
    chip_data(chip, std::byte{0x4f}, std::array{std::byte{0x04}, std::byte{0x00}});
    chip_data(chip, std::byte{0x24}, std::array{std::byte{0xff}, std::byte{0xff},
                                                std::byte{0xff}, std::byte{0xff}});

    const auto ram = chip.black_white_ram();
    const auto white = [&ram](std::size_t y, std::size_t x) {
        return ram[y * 19 + x] == std::byte{0xff};
    };
    expect(white(4, 0) && white(4, 1) && white(5, 0) && white(5, 1),
           "the windowed stream writes only its window");
    std::size_t white_bytes = 0;
    for (const auto value : ram)
        if (value == std::byte{0xff}) ++white_bytes;
    expect(white_bytes == 4, "no byte outside the window changed");

}

constexpr std::byte one_24{0x24};

void deep_sleep() {
    platform::linux::epaper::EmulatedSsd1680 chip{
        platform::linux::epaper::EmulationOptions{}};
    ChipPlatform platform{chip};
    mm::mcu::Platform& original = mm::mcu::platform();
    mm::mcu::set_platform(platform);
    RestorePlatform restore{original};

    mm::epaper::ssd1680::Controller controller{wiring(), panel()};
    expect(controller.initialize() == mm::display::Status::Ok, "initializing");
    expect(controller.clear(mm::display::Color::Black) == mm::display::Status::Ok,
           "filling black before sleep");
    chip_command(chip, std::span{&one_24, 1});
    chip.gpio_write(8, true);
    expect(controller.sleep() == mm::display::Status::Ok, "sleeping");
    chip.spi_write(std::array{std::byte{0xff}});
    expect(all_bytes(chip.black_white_ram(), std::byte{0x00}),
           "bytes sent while the chip sleeps are ignored");

    chip.gpio_write(12, false);
    chip.gpio_write(12, true);
    expect(all_bytes(chip.black_white_ram(), std::byte{0xff}),
           "a reset pulse wakes the chip and clears it white");

}

void chromatic_plane() {
    platform::linux::epaper::EmulatedSsd1680 chip{
        platform::linux::epaper::EmulationOptions{}};
    ChipPlatform platform{chip};
    mm::mcu::Platform& original = mm::mcu::platform();
    mm::mcu::set_platform(platform);
    RestorePlatform restore{original};

    // The same panel without the pigment command is the black/white board:
    // it must not claim red and must never touch the pigment RAM.
    mm::epaper::ssd1680::Controller mono{wiring(), panel()};
    expect(mono.initialize() == mm::display::Status::Ok, "monochrome init");
    expect(mono.clear(mm::display::Color::Red) == mm::display::Status::Unsupported,
           "a black/white panel rejects red");
    expect(all_bytes(chip.chromatic_ram(), std::byte{0x00}),
           "the black/white panel never touches the pigment RAM");

    mm::epaper::ssd1680::Controller controller{
        wiring(), panel(std::byte{0x26})};
    expect(controller.initialize() == mm::display::Status::Ok, "initializing");
    expect(controller.clear(mm::display::Color::Black) == mm::display::Status::Ok,
           "filling black");
    expect(all_bytes(chip.black_white_ram(), std::byte{0x00}) &&
               all_bytes(chip.chromatic_ram(), std::byte{0x00}),
           "a black clear writes both planes");
    expect(controller.clear(mm::display::Color::White) == mm::display::Status::Ok,
           "filling white");
    expect(all_bytes(chip.black_white_ram(), std::byte{0xff}) &&
               all_bytes(chip.chromatic_ram(), std::byte{0x00}),
           "a white clear leaves the chromatic pigment inactive");
    expect(controller.clear(mm::display::Color::Red) == mm::display::Status::Ok,
           "filling red");
    expect(all_bytes(chip.black_white_ram(), std::byte{0xff}) &&
               all_bytes(chip.chromatic_ram(), std::byte{0xff}),
           "a red clear drives the pigment over white");

    // A frame write restores the patterned black/white plane and
    // deactivates the pigment across the rectangle.
    std::array<std::byte, 19 * 296> frame{};
    for (auto& cell : frame) cell = std::byte{0x55};
    expect(controller.write({0, 0, 152, 296},
                           std::span<const std::byte>{frame}) ==
               mm::display::Status::Ok,
           "writing a frame over the red panel");
    expect(all_bytes(chip.black_white_ram(), std::byte{0x55}) &&
               all_bytes(chip.chromatic_ram(), std::byte{0x00}),
           "the frame write clears the pigment and lands in the black/white");

    // The datasheet's other pigment address reaches the same RAM.
    constexpr std::array x_window{std::byte{0x00}, std::byte{0x03}};
    constexpr std::array y_window{std::byte{0x00}, std::byte{0x00},
                                  std::byte{0x00}, std::byte{0x01}};
    constexpr std::array single_zero{std::byte{0x00}};
    constexpr std::array pair_zero{std::byte{0x00}, std::byte{0x00}};
    constexpr std::array pigment_byte{std::byte{0xff}};
    chip_data(chip, std::byte{0x44}, x_window);
    chip_data(chip, std::byte{0x45}, y_window);
    chip_data(chip, std::byte{0x4E}, single_zero);
    chip_data(chip, std::byte{0x4F}, pair_zero);
    chip_data(chip, std::byte{0x25}, pigment_byte);
    chip_data(chip, std::byte{0x26}, pair_zero);
    const auto chromatic = chip.chromatic_ram();
    expect(chromatic[0] == std::byte{0xff} && chromatic[1] == std::byte{0x00},
           "0x25 and 0x26 stream into the same pigment RAM");
}

const mm::test::case_ cases[]{
    {"platform.linux.epaper.chip full sequence", full_sequence},
    {"platform.linux.epaper.chip refresh timeout", refresh_timeout},
    {"platform.linux.epaper.chip windowing", windowing},
    {"platform.linux.epaper.chip deep sleep", deep_sleep},
    {"platform.linux.epaper.chip chromatic plane", chromatic_plane},
};
const mm::test::registrar registrar{"platform.linux.epaper.chip", cases};

}  // namespace
