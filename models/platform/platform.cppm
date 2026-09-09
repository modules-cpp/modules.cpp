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

class Platform {
public:
    virtual ~Platform() = default;

    [[nodiscard]] virtual std::string_view target() const = 0;
    [[nodiscard]] virtual PlatformSystem system() const = 0;
    [[nodiscard]] virtual PlatformRuntime runtime() const = 0;
    [[nodiscard]] virtual std::optional<std::string_view> sdk() const = 0;
    [[nodiscard]] virtual std::optional<std::filesystem::path> sdk_manifest() const = 0;
    [[nodiscard]] virtual std::optional<std::string_view> board() const = 0;
    [[nodiscard]] virtual std::optional<std::filesystem::path> board_manifest() const = 0;
    [[nodiscard]] virtual bool models_responsibilities() const = 0;
    [[nodiscard]] virtual std::vector<ResponsibilityOwner> responsibility_owners() const = 0;
    [[nodiscard]] virtual std::vector<PlatformResponsibility> unresolved() const = 0;
};

}  // namespace models
