// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The emulated ST7789 board's MCU: platform.linux.lcd.panel with the board's
// pins and glass. The pins are chosen so RF24's scannerGraphic and Adafruit's
// ST7789 examples need no edit: DC on 6 and CS on 9, the SPI bus on 10, 11,
// and 12, and 7 and 8 free for scannerGraphic's radio. The glass is
// Adafruit's ST7789 module: mounted a half turn round, an IPS panel needing
// INVON, subpixels red first.
module;

#include <span>

export module platform.linux.lcd.device;

import mm.mcu;
import platform.linux.lcd.chip;
import platform.linux.lcd.panel;

// A named, non-exported namespace rather than an unnamed one: Clang emits an
// interface unit's unnamed-namespace objects again in every importer, and a
// provider object must exist exactly once.
namespace platform::linux::lcd_device_provider {

constexpr mm::mcu::Gpio gpios[]{{5, "RST"},  {6, "DC"},    {7, "GPIO7"},
                                {8, "GPIO8"}, {9, "CS"},    {10, "SCLK"},
                                {11, "MOSI"}, {12, "MISO"}};

const platform::linux::lcd::PanelBoard board{
    .name = "lcd-linux",
    .title = "ST7789 emulation",
    .gpios = gpios,
    .spi = {.instance = 0, .clock_gpio = 10, .transmit_gpio = 11, .receive_gpio = 12},
    .chip = {.chip_select_gpio = 9,
             .data_command_gpio = 6,
             .reset_gpio = 5,
             .mirror_columns = true,
             .mirror_rows = true,
             .inverted = true,
             .bgr = false},
};

platform::linux::lcd::EmulatedPanel emulated_platform{board};
struct Register {
    Register() { mm::mcu::set_platform(emulated_platform); }
};
const Register registered;

}  // namespace platform::linux::lcd_device_provider
