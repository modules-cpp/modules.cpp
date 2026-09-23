// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

export module mm.shell:capability;

export namespace mm::shell {

enum class Level {
    BareMetal = 1,
    Mcu = 2,
    Posix = 3,
};

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

}  // namespace mm::shell

namespace mm::shell::detail {

struct CapabilityName {
    Capability capability;
    std::string_view name;
};

inline constexpr std::array<CapabilityName, capability_count> capability_names{{
    {Capability::Scripts, "scripts"},
    {Capability::CustomCommands, "custom_commands"},
    {Capability::Console, "console"},
    {Capability::Variables, "variables"},
    {Capability::Functions, "functions"},
    {Capability::CommandSubstitution, "command_substitution"},
    {Capability::Board, "board"},
    {Capability::Gpio, "gpio"},
    {Capability::Spi, "spi"},
    {Capability::I2c, "i2c"},
    {Capability::Uart, "uart"},
    {Capability::Timer, "timer"},
    {Capability::Adc, "adc"},
    {Capability::Pwm, "pwm"},
    {Capability::Pathname, "pathname"},
    {Capability::Files, "files"},
    {Capability::Processes, "processes"},
    {Capability::Pipelines, "pipelines"},
    {Capability::Redirections, "redirections"},
    {Capability::Signals, "signals"},
    {Capability::Environment, "environment"},
}};

}  // namespace mm::shell::detail

export namespace mm::shell {

[[nodiscard]] constexpr std::string_view name_of(Capability cap) {
    for (const auto& entry : detail::capability_names) {
        if (entry.capability == cap) return entry.name;
    }
    return "unknown";
}

[[nodiscard]] constexpr std::optional<Capability> lookup_capability(
    std::string_view name) {
    for (const auto& entry : detail::capability_names) {
        if (entry.name == name) return entry.capability;
    }
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
