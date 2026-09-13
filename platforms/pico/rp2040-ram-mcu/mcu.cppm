// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// GPIO only. The interface's other facilities are left to Platform's defaults,
// which answer Unsupported, because this board drives no UART and keeps no
// millisecond clock, and saying so is better than pretending.
export module platform.rp2040_ram.mcu;

import mm.mcu;

namespace {

// RP2040 addresses from the datasheet's register map. Single-cycle IO holds pin
// state; the bank selects which peripheral owns a pin; the pad controls the input
// buffer and the pulls; the reset block must release both banks first.
constexpr unsigned long sio_base = 0xd0000000UL;
constexpr unsigned long io_bank0_base = 0x40014000UL;
constexpr unsigned long pads_bank0_base = 0x4001c000UL;
constexpr unsigned long resets_base = 0x4000c000UL;

constexpr unsigned int pin_count = 30;
constexpr unsigned long function_sio = 5;

volatile unsigned long& reg(unsigned long address) {
    return *reinterpret_cast<volatile unsigned long*>(address);
}

bool banks_released = false;

void release_banks() {
    if (banks_released) return;
    constexpr unsigned long wanted = (1UL << 5) | (1UL << 8);  // io_bank0, pads_bank0
    reg(resets_base) &= ~wanted;
    while ((reg(resets_base + 0x08) & wanted) != wanted) {
    }
    banks_released = true;
}

class Rp2040RamPlatform : public mm::mcu::Platform {
public:
    [[nodiscard]] mm::mcu::Status gpio_configure(unsigned int pin, mm::mcu::Direction direction,
                                                 mm::mcu::Pull pull) override {
        if (pin >= pin_count) return mm::mcu::Status::BadArgument;
        release_banks();

        constexpr unsigned long pad_ie = 1UL << 6;
        constexpr unsigned long pad_od = 1UL << 7;
        constexpr unsigned long pad_pue = 1UL << 3;
        constexpr unsigned long pad_pde = 1UL << 2;

        unsigned long pad = reg(pads_bank0_base + 0x04 + 4 * pin);
        pad |= pad_ie;
        pad &= ~(pad_od | pad_pue | pad_pde);
        switch (pull) {
            case mm::mcu::Pull::None: break;
            case mm::mcu::Pull::Up: pad |= pad_pue; break;
            case mm::mcu::Pull::Down: pad |= pad_pde; break;
        }
        reg(pads_bank0_base + 0x04 + 4 * pin) = pad;
        reg(io_bank0_base + 0x04 + 8 * pin) = function_sio;

        const unsigned long mask = 1UL << pin;
        // OE_SET / OE_CLR
        reg(sio_base + (direction == mm::mcu::Direction::Out ? 0x24 : 0x28)) = mask;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_write(unsigned int pin, bool high) override {
        if (pin >= pin_count) return mm::mcu::Status::BadArgument;
        reg(sio_base + (high ? 0x14 : 0x18)) = 1UL << pin;  // OUT_SET / OUT_CLR
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_read(unsigned int pin, bool& high) override {
        if (pin >= pin_count) return mm::mcu::Status::BadArgument;
        high = (reg(sio_base + 0x04) & (1UL << pin)) != 0;  // GPIO_IN
        return mm::mcu::Status::Ok;
    }
};

Rp2040RamPlatform rp2040_ram_platform;

// Registration at static initialisation. Linking this module's object is what
// makes mm.mcu answer for this board; nothing has to reference the object,
// because objects are linked directly rather than through an archive.
struct Register {
    Register() { mm::mcu::set_platform(rp2040_ram_platform); }
};

const Register registered;

}  // namespace
