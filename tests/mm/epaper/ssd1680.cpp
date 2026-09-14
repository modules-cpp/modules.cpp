// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>
#include <optional>

import mm.display;
import mm.epaper.ssd1680;
import mm.mcu;
import mm.test;

void mm_test_epaper_reset();
void mm_test_epaper_busy(bool busy);
void mm_test_epaper_force(mm::mcu::Status status);
bool mm_test_epaper_contains(unsigned int value);
unsigned int mm_test_epaper_writes();
std::size_t mm_test_epaper_transcript_size();
bool mm_test_epaper_transfer_is_data(std::size_t index);
std::size_t mm_test_epaper_transfer_size(std::size_t index);
unsigned int mm_test_epaper_transfer_byte(std::size_t index, std::size_t byte);
unsigned int mm_test_epaper_command_count(unsigned int command);
std::size_t mm_test_epaper_largest_transfer();
std::size_t mm_test_epaper_gpio_write_count(unsigned int pin);
bool mm_test_epaper_gpio_write_level(unsigned int pin, std::size_t index);

namespace {

using mm::test::expect;

constexpr std::array border_data{std::byte{0x05}};
constexpr std::array initialization{
    mm::epaper::ssd1680::InitializationCommand{std::byte{0x3c}, border_data},
};

mm::epaper::ssd1680::Wiring wiring(unsigned long timeout = 10) {
    return {
        .spi = {.instance = 0,
                .clock_gpio = 2,
                .transmit_gpio = 3,
                .receive_gpio = std::nullopt,
                .baud = 4'000'000,
                .mode = mm::mcu::SpiMode::Mode0,
                .bit_order = mm::mcu::BitOrder::MostSignificantFirst},
        .chip_select_gpio = 4,
        .data_command_gpio = 5,
        .reset_gpio = 6,
        .busy_gpio = 7,
        .reset_active_low = true,
        .busy_active_high = true,
        .reset_hold_ms = 1,
        .reset_release_ms = 1,
        .busy_timeout_ms = timeout,
    };
}

mm::epaper::ssd1680::Panel panel() {
    return {
        .width = 128,
        .height = 296,
        .initialization = initialization,
        .full_update_control = std::byte{0xf7},
        .partial_update_control = std::nullopt,
        .deep_sleep_control = std::byte{0x01},
    };
}

void initializes_from_provider_owned_panel_data() {
    mm_test_epaper_reset();
    mm::epaper::ssd1680::Controller controller{wiring(), panel()};
    expect(controller.initialize() == mm::display::Status::Ok,
           "valid wiring and panel initialization succeed");
    expect(controller.geometry().width == 128 && controller.geometry().height == 296,
           "geometry comes from the panel descriptor");
    expect(mm_test_epaper_gpio_write_count(6) == 2 &&
               !mm_test_epaper_gpio_write_level(6, 0) &&
               mm_test_epaper_gpio_write_level(6, 1),
           "the active-low hardware reset is asserted and then released once");
    expect(mm_test_epaper_transcript_size() == 5,
           "initialization emits the exact command and data transcript");
    expect(!mm_test_epaper_transfer_is_data(0) &&
               mm_test_epaper_transfer_byte(0, 0) == 0x12 &&
               !mm_test_epaper_transfer_is_data(1) &&
               mm_test_epaper_transfer_byte(1, 0) == 0x3c &&
               mm_test_epaper_transfer_is_data(2) &&
               mm_test_epaper_transfer_byte(2, 0) == 0x05 &&
               !mm_test_epaper_transfer_is_data(3) &&
               mm_test_epaper_transfer_byte(3, 0) == 0x11 &&
               mm_test_epaper_transfer_is_data(4) &&
               mm_test_epaper_transfer_byte(4, 0) == 0x03,
           "software reset, provider initialization, and data mode stay distinct");
}

void writes_a_packed_region_and_refreshes() {
    mm_test_epaper_reset();
    mm::epaper::ssd1680::Controller controller{wiring(), panel()};
    expect(controller.initialize() == mm::display::Status::Ok, "controller initializes");
    const std::array pixels{
        std::byte{0xaa}, std::byte{0x55}, std::byte{0x00}, std::byte{0xff},
    };
    expect(controller.write({0, 4, 16, 2}, pixels) == mm::display::Status::Ok,
           "an aligned packed region is accepted");
    expect(mm_test_epaper_contains(0x44) && mm_test_epaper_contains(0x45) &&
               mm_test_epaper_contains(0x4e) && mm_test_epaper_contains(0x4f) &&
               mm_test_epaper_contains(0x24),
           "RAM window, counters, and black-white write commands are emitted");
    expect(controller.refresh(mm::display::Refresh::Full) == mm::display::Status::Ok &&
               mm_test_epaper_contains(0x22) && mm_test_epaper_contains(0x20),
           "full update control and master activation are emitted");
    expect(controller.refresh(mm::display::Refresh::Partial) ==
               mm::display::Status::Unsupported,
           "partial refresh is not invented when the descriptor omits it");
    expect(controller.sleep() == mm::display::Status::Ok &&
               mm_test_epaper_contains(0x10),
           "deep sleep is explicit");
    expect(controller.clear(mm::display::Color::White) ==
               mm::display::Status::NotInitialized,
           "a sleeping controller requires reinitialization");
    expect(controller.initialize() == mm::display::Status::Ok,
           "the documented wake path is full reinitialization");
}

void rejects_bad_regions_and_times_out_busy() {
    mm_test_epaper_reset();
    mm::epaper::ssd1680::Controller controller{wiring(), panel()};
    expect(controller.initialize() == mm::display::Status::Ok, "controller initializes");
    const std::array one_byte{std::byte{0}};
    expect(controller.write({1, 0, 8, 1}, one_byte) ==
               mm::display::Status::BadArgument,
           "a non-byte-aligned X coordinate is rejected");
    expect(controller.write({0, 0, 16, 1}, one_byte) ==
               mm::display::Status::BadArgument,
           "a payload whose packed size is wrong is rejected");

    mm_test_epaper_reset();
    auto duplicate = wiring();
    duplicate.busy_gpio = duplicate.reset_gpio;
    mm::epaper::ssd1680::Controller duplicate_controller{duplicate, panel()};
    expect(duplicate_controller.initialize() == mm::display::Status::BadArgument,
           "one GPIO cannot silently carry two control signals");

    mm_test_epaper_reset();
    mm_test_epaper_busy(true);
    mm::epaper::ssd1680::Controller timeout_controller{wiring(2), panel()};
    expect(timeout_controller.initialize() == mm::display::Status::Timeout,
           "a stuck busy input reaches a bounded timeout");
}

void clear_streams_without_a_full_framebuffer() {
    mm_test_epaper_reset();
    mm::epaper::ssd1680::Controller controller{wiring(), panel()};
    expect(controller.initialize() == mm::display::Status::Ok, "controller initializes");
    const auto before = mm_test_epaper_writes();
    expect(controller.clear(mm::display::Color::White) == mm::display::Status::Ok,
           "monochrome clear succeeds");
    expect(mm_test_epaper_writes() > before + 2,
           "clear streams bounded chunks rather than allocating one framebuffer");
    expect(mm_test_epaper_largest_transfer() <= 32,
           "no clear transfer exceeds the fixed stack chunk");
    expect(controller.clear(mm::display::Color::Red) ==
               mm::display::Status::Unsupported,
           "a monochrome controller does not invent red pixels");
}

void supports_a_visible_width_with_padded_ram_rows() {
    mm_test_epaper_reset();
    auto narrow = panel();
    narrow.width = 122;
    mm::epaper::ssd1680::Controller controller{wiring(), narrow};
    expect(controller.initialize() == mm::display::Status::Ok,
           "a panel width need not be a multiple of eight");
    std::array<std::byte, 16> final_row{};
    expect(controller.write({0, 0, 122, 1}, final_row) == mm::display::Status::Ok,
           "the final visible row includes its padded RAM byte");
}

void supports_descriptor_qualified_partial_refresh() {
    mm_test_epaper_reset();
    auto partial_panel = panel();
    partial_panel.partial_update_control = std::byte{0xfc};
    mm::epaper::ssd1680::Controller controller{wiring(), partial_panel};
    expect(controller.initialize() == mm::display::Status::Ok,
           "the controller with qualified partial control initializes");
    expect(controller.refresh(mm::display::Refresh::Partial) ==
               mm::display::Status::Ok &&
               mm_test_epaper_command_count(0x22) == 1 &&
               mm_test_epaper_command_count(0x20) == 1,
           "partial refresh uses update control and master activation");
}

void stops_after_a_transport_error() {
    mm_test_epaper_reset();
    mm_test_epaper_force(mm::mcu::Status::Busy);
    mm::epaper::ssd1680::Controller controller{wiring(), panel()};
    expect(controller.initialize() == mm::display::Status::Busy,
           "the MCU transport status reaches the display caller");
    expect(mm_test_epaper_transcript_size() == 0,
           "initialization emits no command after configuration fails");
}

const mm::test::case_ cases[] = {
    {"initializes from panel data", &initializes_from_provider_owned_panel_data},
    {"writes and refreshes", &writes_a_packed_region_and_refreshes},
    {"rejects regions and times out", &rejects_bad_regions_and_times_out_busy},
    {"clear streams bounded chunks", &clear_streams_without_a_full_framebuffer},
    {"supports padded visible rows", &supports_a_visible_width_with_padded_ram_rows},
    {"supports qualified partial refresh", &supports_descriptor_qualified_partial_refresh},
    {"stops after transport error", &stops_after_a_transport_error},
};

const mm::test::registrar reg{"mm.epaper.ssd1680", cases};

}
