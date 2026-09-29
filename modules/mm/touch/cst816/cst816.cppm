// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.touch.cst816;

import mm.mcu;
import mm.touch;

export namespace mm::touch::cst816 {

// Everything the controller needs to know about the board it is soldered to.
// A provider fills this in; the driver reads it and never guesses.
struct Wiring {
    mm::mcu::I2cConfiguration i2c;
    unsigned int address = 0x15;
    unsigned int reset_gpio = 0;
    unsigned int interrupt_gpio = 0;
    // The reference driver drives reset low for 100ms and waits 100ms after
    // releasing it. Both are panel timings rather than bus timings, so a board
    // that measured different ones may say so.
    unsigned long reset_hold_ms = 100;
    unsigned long reset_release_ms = 100;
};

struct Panel {
    unsigned int width = 0;
    unsigned int height = 0;
    // The reference driver for this controller reports one point, so a panel
    // this driver can describe reports at most one contact at a time.
    unsigned int points = 1;
};

class Controller : public mm::touch::Touch {
public:
    Controller(const Wiring& wiring, const Panel& panel)
        : wiring_(wiring), panel_(panel) {}

    [[nodiscard]] mm::touch::Geometry geometry() const override;
    [[nodiscard]] mm::touch::Status initialize() override;
    [[nodiscard]] mm::touch::Status read(std::span<mm::touch::Point> points,
                                         std::size_t& count) override;
    [[nodiscard]] mm::touch::Status sleep() override;

private:
    enum class State { Idle, Ready, Sleeping };

    [[nodiscard]] mm::touch::Status write_register(unsigned int register_address,
                                                   std::byte value);
    [[nodiscard]] mm::touch::Status read_register(unsigned int register_address,
                                                  std::span<std::byte> data);
    [[nodiscard]] mm::touch::Status reset();
    [[nodiscard]] mm::touch::Status from_mcu(mm::mcu::Status status) const;

    Wiring wiring_;
    Panel panel_;
    State state_ = State::Idle;
};

}
