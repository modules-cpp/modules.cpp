// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The emulated ILI9341 board's MCU: platform.linux.lcd.panel with an ILI9341
// module's glass and the Arduino Uno pins Adafruit's ILI9341 examples use, so
// they need no edit: DC on 9, CS on 10, reset on 8, and the SPI bus on 13
// (SCK), 11 (MOSI), and 12 (MISO). The ILI9341 takes the commands the
// emulated controller models exactly as the ST7789 does; what differs is the
// glass. Adafruit's rotation 0 sets MX and BGR, so the glass mirrors the
// columns and has its subpixels blue first, and it is not an IPS panel, so
// true colours need no inversion.
module;

#include <span>

export module platform.linux.lcd.ili9341.device;

import mm.mcu;
import platform.linux.lcd.chip;
import platform.linux.lcd.panel;

// A named, non-exported namespace rather than an unnamed one: Clang emits an
// interface unit's unnamed-namespace objects again in every importer, and a
// provider object must exist exactly once.
namespace platform::linux::lcd_ili9341_device_provider {

constexpr mm::mcu::Gpio gpios[]{{8, "RST"},   {9, "DC"},    {10, "CS"},
                                {11, "MOSI"}, {12, "MISO"}, {13, "SCK"}};

const platform::linux::lcd::PanelBoard board{
    .name = "ili9341-linux",
    .title = "ILI9341 emulation",
    .gpios = gpios,
    .spi = {.instance = 0, .clock_gpio = 13, .transmit_gpio = 11, .receive_gpio = 12},
    .chip = {.chip_select_gpio = 10,
             .data_command_gpio = 9,
             .reset_gpio = 8,
             .mirror_columns = true,
             .mirror_rows = false,
             .inverted = false,
             .bgr = true},
};

platform::linux::lcd::EmulatedPanel emulated_platform{board};
struct Register {
    Register() { mm::mcu::set_platform(emulated_platform); }
};
const Register registered;

}  // namespace platform::linux::lcd_ili9341_device_provider
