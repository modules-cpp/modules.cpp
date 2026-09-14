// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <optional>
#include <span>

export module mm.lcd.st7789;

import mm.display;
import mm.mcu;

export namespace mm::lcd::st7789 {

struct Wiring {
    mm::mcu::SpiConfiguration spi;
    unsigned int chip_select_gpio = 0;
    unsigned int data_command_gpio = 0;
    unsigned int reset_gpio = 0;
    // A panel with no controllable backlight leaves this unset; one with a
    // plain on-off line names it and the controller raises it at initialize.
    std::optional<unsigned int> backlight_gpio;
    // The reference driver holds the line high, low, then high again, waiting
    // this long at each step.
    unsigned long reset_step_ms = 100;
    unsigned long sleep_out_ms = 120;
};

// Command data is provider-owned static storage and must outlive Controller.
struct InitializationCommand {
    std::byte command;
    std::span<const std::byte> data;
};

struct Panel {
    unsigned int width = 0;
    unsigned int height = 0;
    // Porch, power, and gamma settings differ between panels carrying the same
    // controller, so the board supplies them rather than this module guessing.
    std::span<const InitializationCommand> initialization;
    // Memory access control: which way the panel is wired to the controller's
    // frame memory. The board knows; this module only sends it.
    std::byte memory_access = std::byte{0x00};
    // Some panels are wired such that the controller's inversion must be on for
    // colours to come out right.
    bool inverted = true;
    // The visible area may sit at an offset inside the controller's frame
    // memory, which is what makes a 240-wide panel on a 240-wide controller
    // still need a column offset on some boards.
    unsigned int column_offset = 0;
    unsigned int row_offset = 0;
};

class Controller final : public mm::display::Display {
public:
    Controller(Wiring wiring, Panel panel);

    [[nodiscard]] mm::display::Geometry geometry() const override;
    [[nodiscard]] mm::display::Status initialize() override;
    [[nodiscard]] mm::display::Status clear(mm::display::Color color) override;
    [[nodiscard]] mm::display::Status write(mm::display::Rectangle rectangle,
                                            std::span<const std::byte> bytes) override;
    [[nodiscard]] mm::display::Status refresh(mm::display::Refresh mode) override;
    [[nodiscard]] mm::display::Status sleep() override;

private:
    enum class State { Idle, Ready, Sleeping };

    [[nodiscard]] mm::display::Status command(std::byte value,
                                              std::span<const std::byte> data = {});
    [[nodiscard]] mm::display::Status stream(std::span<const std::byte> data);
    [[nodiscard]] mm::display::Status set_window(mm::display::Rectangle rectangle);
    [[nodiscard]] mm::display::Status fill(mm::display::Rectangle rectangle,
                                           unsigned int pixel);
    [[nodiscard]] mm::display::Status reset();
    [[nodiscard]] mm::display::Status select(bool data);
    [[nodiscard]] mm::display::Status from_mcu(mm::mcu::Status status) const;
    [[nodiscard]] mm::display::Status fail(mm::display::Status status);

    Wiring wiring_;
    Panel panel_;
    State state_ = State::Idle;
};

}
