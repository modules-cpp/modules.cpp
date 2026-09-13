// Abstract platform selected for one configured lane.
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

export module models.platform;

export namespace models {

enum class PlatformSystem { Posix, Linux, BareMetal, Unknown };
enum class PlatformRuntime { Unknown, Glibc, Newlib, Picolibc, None };
enum class LinkOwnership { Project, External };
enum class PlatformResponsibility {
    ResetVector,
    InitialStack,
    MemoryLayout,
    RuntimeInit,
    Syscalls
};

struct ResponsibilityOwner {
    PlatformResponsibility responsibility;
    std::string_view owner;
};

// One interface this platform resolves, after SDK defaults and board overrides
// have been applied. owner names the SDK or board that supplied the binding,
// and from_board distinguishes a board's specialisation from its SDK's default.
struct EffectivePlatformProvider {
    std::string_view interface_module;
    std::string_view provider_module;
    std::string_view owner;
    bool from_board = false;
};

class Platform {
public:
    virtual ~Platform() = default;

    [[nodiscard]] virtual std::string_view target() const = 0;
    [[nodiscard]] virtual PlatformSystem system() const = 0;
    [[nodiscard]] virtual PlatformRuntime runtime() const = 0;
    [[nodiscard]] virtual LinkOwnership link_ownership() const = 0;
    [[nodiscard]] virtual std::optional<std::string_view> sdk() const = 0;
    [[nodiscard]] virtual std::optional<std::filesystem::path> sdk_manifest() const = 0;
    [[nodiscard]] virtual std::optional<std::string_view> board() const = 0;
    [[nodiscard]] virtual std::optional<std::filesystem::path> board_manifest() const = 0;
    [[nodiscard]] virtual bool models_responsibilities() const = 0;
    [[nodiscard]] virtual std::vector<ResponsibilityOwner> responsibility_owners() const = 0;
    [[nodiscard]] virtual std::vector<PlatformResponsibility> unresolved() const = 0;

    // The providers this platform supplies, which are selected rather than
    // authored: they are never use: edges, and never appear in a Module's
    // imports().
    [[nodiscard]] virtual std::vector<EffectivePlatformProvider> platform_providers() const = 0;
};

}  // namespace models
