// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

export module mm.shell:command;

import :status;
import :capability;
import :io;
import :state;

export namespace mm::shell {

struct CommandContext {
    IoServices& io;
    ShellState& state;
    const CapabilitySet& capabilities;
    std::span<std::byte> scratch;
};

struct CommandDescriptor;

using CommandHandler = void (*)(
    void* context,
    std::span<const std::string_view> args,
    CommandContext& context_view,
    CommandResult& result);

struct CommandDescriptor {
    std::string_view name;
    std::string_view summary;
    CommandClass command_class = CommandClass::Custom;
    CapabilitySet required_capabilities;
    CommandHandler handler = nullptr;
    void* context = nullptr;
};

class Registry {
public:
    constexpr Registry() = default;
    explicit Registry(std::span<CommandDescriptor> storage);

    [[nodiscard]] InstallResult install(const CommandDescriptor& descriptor);
    [[nodiscard]] InstallResult install_special(
        const CommandDescriptor& descriptor);
    [[nodiscard]] InstallResult install_pack(
        std::span<const CommandDescriptor> pack);

    [[nodiscard]] const CommandDescriptor* find(std::string_view name) const;
    [[nodiscard]] std::size_t count() const { return count_; }
    [[nodiscard]] std::size_t capacity() const { return storage_.size(); }
    [[nodiscard]] std::span<const CommandDescriptor> descriptors() const {
        return storage_.subspan(0, count_);
    }

    void reset() { count_ = 0; }

private:
    std::span<CommandDescriptor> storage_;
    std::size_t count_ = 0;
};

[[nodiscard]] CommandResult dispatch(
    const CommandDescriptor& descriptor,
    std::span<const std::string_view> args,
    CommandContext& context);

}  // namespace mm::shell
