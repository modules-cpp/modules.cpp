// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <optional>
#include <span>

export module mm.epaper.ssd1680;

import mm.display;
import mm.mcu;

export namespace mm::epaper::ssd1680 {

struct Wiring {
    mm::mcu::SpiConfiguration spi;
    unsigned int chip_select_gpio = 0;
    unsigned int data_command_gpio = 0;
    unsigned int reset_gpio = 0;
    unsigned int busy_gpio = 0;
    bool reset_active_low = true;
    bool busy_active_high = true;
    unsigned long reset_hold_ms = 10;
    unsigned long reset_release_ms = 10;
    unsigned long busy_timeout_ms = 5'000;
};

// Command data is provider-owned static storage and must outlive Controller.
struct InitializationCommand {
    std::byte command;
    std::span<const std::byte> data;
};

struct Panel {
    unsigned int width = 0;
    unsigned int height = 0;
    std::span<const InitializationCommand> initialization;
    std::byte full_update_control = std::byte{0xf7};
    std::optional<std::byte> partial_update_control;
    std::byte deep_sleep_control = std::byte{0x01};
};

class Controller final : public mm::display::Display {
public:
    Controller(Wiring wiring, Panel panel);

    [[nodiscard]] mm::display::Geometry geometry() const override;
    [[nodiscard]] mm::display::Status initialize() override;
    [[nodiscard]] mm::display::Status clear(mm::display::Color color) override;
    [[nodiscard]] mm::display::Status write(
        mm::display::Rectangle rectangle,
        std::span<const std::byte> bytes) override;
    [[nodiscard]] mm::display::Status refresh(mm::display::Refresh mode) override;
    [[nodiscard]] mm::display::Status sleep() override;

private:
    enum class State { Uninitialized, Ready, Refreshing, Sleeping, Failed };

    [[nodiscard]] mm::display::Status validate() const;
    [[nodiscard]] mm::display::Status command(
        std::byte value, std::span<const std::byte> data = {});
    [[nodiscard]] mm::display::Status stream(
        std::byte command_value, std::span<const std::byte> data);
    [[nodiscard]] mm::display::Status set_window(mm::display::Rectangle rectangle);
    [[nodiscard]] mm::display::Status wait_until_ready();
    [[nodiscard]] mm::display::Status from_mcu(mm::mcu::Status status) const;
    [[nodiscard]] mm::display::Status fail(mm::display::Status status);

    Wiring wiring_;
    Panel panel_;
    State state_ = State::Uninitialized;
};

}
