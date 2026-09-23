// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.shell.mcu;

import mm.shell;
import mm.mcu;
import mm.stdio;

export namespace mm::shell::mcu {

class McuConsole {
public:
    [[nodiscard]] mm::stdio::Status attach(
        mm::stdio::Console& driver, std::span<char> pending);
    [[nodiscard]] bool attached() const { return driver_ != nullptr; }
    [[nodiscard]] std::size_t pending_bytes() const { return used_; }
    [[nodiscard]] ByteSink sink();
    [[nodiscard]] mm::stdio::Status pump();
    [[nodiscard]] mm::stdio::Status flush();
    [[nodiscard]] mm::stdio::Status connected(bool& value) const;
    [[nodiscard]] mm::stdio::Status read(
        std::span<std::byte> bytes, std::size_t& count) const;

private:
    static SinkResult write_callback(
        void* self, std::span<const char> bytes);
    static SinkResult flush_callback(void* self);
    static SinkFailure failure_callback(void* self);

    mm::stdio::Console* driver_ = nullptr;
    std::span<char> pending_{};
    std::size_t used_ = 0;
    SinkFailure failure_{};
};

struct Level2Binding {
    McuConsole* console = nullptr;
};

constexpr std::size_t mcu_builtin_count = 10;

[[nodiscard]] CapabilitySet capabilities(const McuConsole* console =
                                              nullptr);

// The level-1 and MCU descriptors are installed as one transaction.
// intro and binding must outlive the registry entries.
[[nodiscard]] InstallResult install_level2(
    Registry& registry, Introspection& intro, Level2Binding& binding);

}  // namespace mm::shell::mcu
