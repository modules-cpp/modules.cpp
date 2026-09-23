// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

export module mm.shell:capability;

export namespace mm::shell {

enum class ScriptProfile {
    Embedded,
    Full,
};

enum class Capability : unsigned int {
    Scripts = 0,
    CustomCommands = 1,
    Console = 2,
    Variables = 3,
    Functions = 4,
    CommandSubstitution = 5,
    Board = 6,
    Gpio = 7,
    Spi = 8,
    I2c = 9,
    Uart = 10,
    Timer = 11,
    Adc = 12,
    Pwm = 13,
    Pathname = 14,
    Files = 15,
    Processes = 16,
    Pipelines = 17,
    Redirections = 18,
    Signals = 19,
    Environment = 20,
};

constexpr std::size_t capability_count = 21;

[[nodiscard]] constexpr std::string_view name_of(Capability cap) {
    switch (cap) {
        case Capability::Scripts: return "scripts";
        case Capability::CustomCommands: return "custom_commands";
        case Capability::Console: return "console";
        case Capability::Variables: return "variables";
        case Capability::Functions: return "functions";
        case Capability::CommandSubstitution: return "command_substitution";
        case Capability::Board: return "board";
        case Capability::Gpio: return "gpio";
        case Capability::Spi: return "spi";
        case Capability::I2c: return "i2c";
        case Capability::Uart: return "uart";
        case Capability::Timer: return "timer";
        case Capability::Adc: return "adc";
        case Capability::Pwm: return "pwm";
        case Capability::Pathname: return "pathname";
        case Capability::Files: return "files";
        case Capability::Processes: return "processes";
        case Capability::Pipelines: return "pipelines";
        case Capability::Redirections: return "redirections";
        case Capability::Signals: return "signals";
        case Capability::Environment: return "environment";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::optional<Capability> lookup_capability(
    std::string_view name) {
    if (name == "scripts") return Capability::Scripts;
    if (name == "custom_commands") return Capability::CustomCommands;
    if (name == "console") return Capability::Console;
    if (name == "variables") return Capability::Variables;
    if (name == "functions") return Capability::Functions;
    if (name == "command_substitution") return Capability::CommandSubstitution;
    if (name == "board") return Capability::Board;
    if (name == "gpio") return Capability::Gpio;
    if (name == "spi") return Capability::Spi;
    if (name == "i2c") return Capability::I2c;
    if (name == "uart") return Capability::Uart;
    if (name == "timer") return Capability::Timer;
    if (name == "adc") return Capability::Adc;
    if (name == "pwm") return Capability::Pwm;
    if (name == "pathname") return Capability::Pathname;
    if (name == "files") return Capability::Files;
    if (name == "processes") return Capability::Processes;
    if (name == "pipelines") return Capability::Pipelines;
    if (name == "redirections") return Capability::Redirections;
    if (name == "signals") return Capability::Signals;
    if (name == "environment") return Capability::Environment;
    return std::nullopt;
}

class CapabilitySet {
public:
    constexpr CapabilitySet() = default;

    constexpr void set(Capability cap) {
        mask_ |= (std::uint32_t{1} << static_cast<unsigned int>(cap));
    }

    constexpr void clear(Capability cap) {
        mask_ &= ~(std::uint32_t{1} << static_cast<unsigned int>(cap));
    }

    [[nodiscard]] constexpr bool has(Capability cap) const {
        const auto bit =
            std::uint32_t{1} << static_cast<unsigned int>(cap);
        return (mask_ & bit) != 0;
    }

    [[nodiscard]] constexpr bool contains(const CapabilitySet& other) const {
        return (mask_ & other.mask_) == other.mask_;
    }

    constexpr void merge(const CapabilitySet& other) {
        mask_ |= other.mask_;
    }

    [[nodiscard]] constexpr bool empty() const {
        return mask_ == 0;
    }

    [[nodiscard]] static constexpr CapabilitySet none() {
        return {};
    }

    [[nodiscard]] static constexpr CapabilitySet all() {
        CapabilitySet set;
        set.mask_ = (std::uint32_t{1} << capability_count) - 1;
        return set;
    }

    [[nodiscard]] static constexpr CapabilitySet level1() {
        CapabilitySet set;
        set.set(Capability::Scripts);
        set.set(Capability::CustomCommands);
        set.set(Capability::Variables);
        set.set(Capability::Functions);
        set.set(Capability::CommandSubstitution);
        return set;
    }

    [[nodiscard]] static constexpr CapabilitySet level2() {
        // Mandatory software capabilities for Level 2.
        // Peripheral capabilities (Board, Gpio, Spi, I2c, Uart, Timer,
        // Adc, Pwm, and Console) are optional, provider-derived, and
        // merged at runtime by mm.mcu capability discovery.
        return level1();
    }

    [[nodiscard]] static constexpr CapabilitySet level3() {
        // Mandatory software capabilities for Level 3.
        CapabilitySet set = level2();
        set.set(Capability::Pathname);
        set.set(Capability::Files);
        set.set(Capability::Processes);
        set.set(Capability::Pipelines);
        set.set(Capability::Redirections);
        set.set(Capability::Signals);
        set.set(Capability::Environment);
        return set;
    }

    [[nodiscard]] constexpr bool operator==(
        const CapabilitySet& other) const = default;

private:
    std::uint32_t mask_ = 0;
};

}  // namespace mm::shell
