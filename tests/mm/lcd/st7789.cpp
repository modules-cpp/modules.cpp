// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>
#include <optional>
#include <span>

import mm.mcu;
import mm.display;
import mm.lcd.st7789;
import mm.test;

void mm_test_lcd_reset();
void mm_test_lcd_force(mm::mcu::Status status);
std::size_t mm_test_lcd_transcript_size();
bool mm_test_lcd_is_data(std::size_t index);
std::size_t mm_test_lcd_transfer_size(std::size_t index);
unsigned int mm_test_lcd_byte(std::size_t index, std::size_t offset);
std::size_t mm_test_lcd_find_command(unsigned int opcode);
std::size_t mm_test_lcd_largest_transfer();
std::size_t mm_test_lcd_pixel_bytes();
unsigned long mm_test_lcd_ticks();
std::size_t mm_test_lcd_gpio_writes();
unsigned int mm_test_lcd_gpio_pin(std::size_t index);
bool mm_test_lcd_gpio_level(std::size_t index);

namespace {

using mm::test::expect;
using mm::display::Status;

constexpr std::array porch{std::byte{0x0c}, std::byte{0x0c}, std::byte{0x00},
                           std::byte{0x33}, std::byte{0x33}};
constexpr std::array gate{std::byte{0x75}};
constexpr std::array initialization{
    mm::lcd::st7789::InitializationCommand{std::byte{0xb2}, porch},
    mm::lcd::st7789::InitializationCommand{std::byte{0xb7}, gate},
};

mm::lcd::st7789::Wiring wiring() {
    return {.spi = {.instance = 1,
                    .clock_gpio = 10,
                    .transmit_gpio = 11,
                    .receive_gpio = std::nullopt,
                    .baud = 62'500'000,
                    .mode = mm::mcu::SpiMode::Mode0,
                    .bit_order = mm::mcu::BitOrder::MostSignificantFirst},
            .chip_select_gpio = 13,
            .data_command_gpio = 8,
            .reset_gpio = 15,
            .backlight_gpio = 16,
            .reset_step_ms = 100,
            .sleep_out_ms = 120};
}

mm::lcd::st7789::Panel panel() {
    return {.width = 240,
            .height = 320,
            .initialization = initialization,
            .memory_access = std::byte{0x00},
            .inverted = true,
            .column_offset = 0,
            .row_offset = 0};
}

void initializes_with_the_documented_sequence() {
    mm_test_lcd_reset();
    mm::lcd::st7789::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Ok, "the controller initializes");

    expect(mm_test_lcd_ticks() == 420,
           "three reset steps and the sleep-out wait are all observed");

    const auto sleep_out = mm_test_lcd_find_command(0x11);
    const auto format = mm_test_lcd_find_command(0x3a);
    const auto access = mm_test_lcd_find_command(0x36);
    const auto on = mm_test_lcd_find_command(0x29);
    expect(sleep_out < mm_test_lcd_transcript_size(), "sleep out is sent");
    expect(access < mm_test_lcd_transcript_size() && sleep_out < access,
           "memory access control follows sleep out");
    expect(format < mm_test_lcd_transcript_size() &&
               mm_test_lcd_byte(format + 1, 0) == 0x05,
           "the pixel format is set to sixteen bits");
    expect(mm_test_lcd_find_command(0x21) < mm_test_lcd_transcript_size(),
           "an inverted panel has inversion turned on");
    expect(on < mm_test_lcd_transcript_size(), "the display is turned on");

    // The provider's porch and gate settings reached the panel unchanged.
    const auto board_porch = mm_test_lcd_find_command(0xb2);
    expect(board_porch < mm_test_lcd_transcript_size() &&
               mm_test_lcd_transfer_size(board_porch + 1) == 5 &&
               mm_test_lcd_byte(board_porch + 1, 4) == 0x33,
           "the board's initialization commands are sent with their data");

    const auto geometry = controller.geometry();
    expect(geometry.width == 240 && geometry.height == 320 &&
               geometry.bits_per_pixel == 16,
           "geometry reports a sixteen-bit panel");
}

// The backlight must come on after the panel is initialised, or the first thing
// a user sees is whatever the controller's frame memory held.
void the_backlight_comes_on_last_and_off_first() {
    mm_test_lcd_reset();
    mm::lcd::st7789::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Ok, "the controller initializes");

    std::size_t backlight = mm_test_lcd_gpio_writes();
    for (std::size_t index = 0; index < mm_test_lcd_gpio_writes(); ++index)
        if (mm_test_lcd_gpio_pin(index) == 16 && mm_test_lcd_gpio_level(index))
            backlight = index;
    expect(backlight == mm_test_lcd_gpio_writes() - 1,
           "the backlight is the last thing initialize does");

    expect(controller.sleep() == Status::Ok, "the controller sleeps");
    expect(!mm_test_lcd_gpio_level(mm_test_lcd_gpio_writes() - 1) ||
               mm_test_lcd_gpio_pin(mm_test_lcd_gpio_writes() - 1) != 16,
           "the backlight is not left on after sleep");
}

void a_window_write_addresses_rgb565_pixels() {
    mm_test_lcd_reset();
    mm::lcd::st7789::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Ok, "the controller initializes");

    const auto before = mm_test_lcd_pixel_bytes();
    // Two pixels: red then blue, most significant byte first.
    const std::array pixels{std::byte{0xf8}, std::byte{0x00},
                            std::byte{0x00}, std::byte{0x1f}};
    expect(controller.write({10, 20, 2, 1}, pixels) == Status::Ok,
           "a packed region is accepted");

    const auto columns = mm_test_lcd_find_command(0x2a);
    expect(columns < mm_test_lcd_transcript_size() &&
               mm_test_lcd_byte(columns + 1, 1) == 10 &&
               mm_test_lcd_byte(columns + 1, 3) == 11,
           "the column window is inclusive of its last pixel");
    const auto rows = mm_test_lcd_find_command(0x2b);
    expect(rows < mm_test_lcd_transcript_size() &&
               mm_test_lcd_byte(rows + 1, 1) == 20 &&
               mm_test_lcd_byte(rows + 1, 3) == 20,
           "a one-row window addresses one row");
    expect(mm_test_lcd_find_command(0x2c) < mm_test_lcd_transcript_size(),
           "memory write is entered before pixels");
    expect(mm_test_lcd_pixel_bytes() == before + 4,
           "two RGB565 pixels are four bytes of data");
}

void a_panel_offset_shifts_the_window() {
    mm_test_lcd_reset();
    auto offset = panel();
    offset.column_offset = 35;
    offset.row_offset = 2;
    mm::lcd::st7789::Controller controller{wiring(), offset};
    expect(controller.initialize() == Status::Ok, "the controller initializes");

    const std::array pixel{std::byte{0x00}, std::byte{0x00}};
    expect(controller.write({0, 0, 1, 1}, pixel) == Status::Ok, "a single pixel writes");
    const auto columns = mm_test_lcd_find_command(0x2a);
    const auto rows = mm_test_lcd_find_command(0x2b);
    expect(mm_test_lcd_byte(columns + 1, 1) == 35 && mm_test_lcd_byte(columns + 1, 3) == 35,
           "the column offset reaches the controller");
    expect(mm_test_lcd_byte(rows + 1, 1) == 2 && mm_test_lcd_byte(rows + 1, 3) == 2,
           "so does the row offset");
}

void clear_streams_bounded_chunks_without_a_framebuffer() {
    mm_test_lcd_reset();
    mm::lcd::st7789::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Ok, "the controller initializes");

    const auto before = mm_test_lcd_pixel_bytes();
    expect(controller.clear(mm::display::Color::Red) == Status::Ok,
           "a full-screen clear succeeds");
    expect(mm_test_lcd_pixel_bytes() - before == 240u * 320u * 2u,
           "every pixel on the panel is written once");
    expect(mm_test_lcd_largest_transfer() <= 32,
           "the fill is streamed in bounded chunks rather than one framebuffer");

    // Red is 0xf800, and a swapped pair is 0x00f8, which is a different colour
    // the panel would happily show. Nothing else here would notice.
    const auto pixels = mm_test_lcd_find_command(0x2c) + 1;
    expect(mm_test_lcd_is_data(pixels), "pixels follow the memory-write command");
    expect(mm_test_lcd_byte(pixels, 0) == 0xf8 && mm_test_lcd_byte(pixels, 1) == 0x00,
           "a filled pixel is RGB565 most significant byte first");
}

void a_region_must_match_its_bytes_and_fit_the_panel() {
    mm_test_lcd_reset();
    mm::lcd::st7789::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Ok, "the controller initializes");

    const std::array two_pixels{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
    expect(controller.write({0, 0, 3, 1}, two_pixels) == Status::BadArgument,
           "a region wider than its bytes is rejected");
    expect(controller.write({239, 0, 2, 1}, two_pixels) == Status::BadArgument,
           "a region running off the panel is rejected");
    expect(controller.write({0, 0, 0, 0}, std::span<const std::byte>{}) ==
               Status::BadArgument,
           "an empty region is rejected");
}

void refresh_answers_without_sending_anything() {
    mm_test_lcd_reset();
    mm::lcd::st7789::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Ok, "the controller initializes");

    const auto before = mm_test_lcd_transcript_size();
    expect(controller.refresh(mm::display::Refresh::Full) == Status::Ok,
           "a full refresh is satisfied");
    expect(controller.refresh(mm::display::Refresh::Partial) == Status::Ok,
           "so is a partial one");
    expect(mm_test_lcd_transcript_size() == before,
           "a panel already showing its pixels sends nothing to refresh them");
}

void the_lifecycle_is_explicit() {
    mm_test_lcd_reset();
    mm::lcd::st7789::Controller controller{wiring(), panel()};

    expect(controller.clear(mm::display::Color::White) == Status::NotInitialized,
           "clearing before initialization reports so");
    expect(controller.refresh(mm::display::Refresh::Full) == Status::NotInitialized,
           "so does refreshing");

    expect(controller.initialize() == Status::Ok, "the controller initializes");
    expect(controller.sleep() == Status::Ok, "the controller sleeps");
    expect(controller.clear(mm::display::Color::White) == Status::NotInitialized,
           "a sleeping controller requires reinitialization");
}

void a_transport_failure_stops_the_controller() {
    mm_test_lcd_reset();
    mm::lcd::st7789::Controller controller{wiring(), panel()};
    expect(controller.initialize() == Status::Ok, "the controller initializes");

    mm_test_lcd_force(mm::mcu::Status::Busy);
    const std::array pixel{std::byte{0}, std::byte{0}};
    expect(controller.write({0, 0, 1, 1}, pixel) == Status::Busy,
           "a failing transport is reported");
    mm_test_lcd_force(mm::mcu::Status::Ok);
    expect(controller.write({0, 0, 1, 1}, pixel) == Status::NotInitialized,
           "a controller whose panel state is unknown stops claiming to be ready");
}

const mm::test::case_ cases[] = {
    {"initializes with the sequence", &initializes_with_the_documented_sequence},
    {"backlight comes on last", &the_backlight_comes_on_last_and_off_first},
    {"a window write addresses pixels", &a_window_write_addresses_rgb565_pixels},
    {"a panel offset shifts the window", &a_panel_offset_shifts_the_window},
    {"clear streams bounded chunks", &clear_streams_bounded_chunks_without_a_framebuffer},
    {"a region must match its bytes", &a_region_must_match_its_bytes_and_fit_the_panel},
    {"refresh sends nothing", &refresh_answers_without_sending_anything},
    {"the lifecycle is explicit", &the_lifecycle_is_explicit},
    {"transport failure stops it", &a_transport_failure_stops_the_controller},
};

const mm::test::registrar reg{"mm.lcd st7789", cases};

}  // namespace
