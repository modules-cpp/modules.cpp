// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <limits>
#include <span>
#include <string_view>

module mm.shell;

import :status;
import :capability;
import :io;
import :state;
import :command;
import :command_internal;

namespace mm::shell {

namespace {

[[nodiscard]] bool is_valid_name(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    if (name == "[") return true;

    for (char c : name) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc <= 0x20 || uc >= 0x7f) {
            return false;
        }
        switch (c) {
            case '/':
            case '|':
            case '&':
            case ';':
            case '(':
            case ')':
            case '<':
            case '>':
            case '$':
            case '`':
            case '\\':
            case '"':
            case '\'':
            case '*':
            case '?':
            case '[':
            case ']':
            case '#':
            case '~':
            case '=':
            case '%':
            case '!':
            case '{':
            case '}':
            case '^':
            case ',':
                return false;
            default:
                break;
        }
    }
    return true;
}

}  // namespace

Registry::Registry(std::span<CommandDescriptor> storage)
    : storage_(storage), count_(0) {}

InstallResult Registry::install(const CommandDescriptor& descriptor) {
    if (count_ >= storage_.size()) {
        return InstallResult{
            .status = Status::Overflow,
            .overflow = OverflowInfo{
                .storage_class = StorageClass::CustomCommands,
                .required = count_ + 1,
            },
        };
    }
    if (!is_valid_name(descriptor.name)) {
        return InstallResult{.status = Status::BadArgument};
    }
    if (descriptor.handler == nullptr) {
        return InstallResult{.status = Status::BadArgument};
    }
    if (descriptor.command_class == CommandClass::SpecialBuiltin) {
        return InstallResult{.status = Status::BadArgument};
    }
    if (find(descriptor.name) != nullptr) {
        return InstallResult{.status = Status::Duplicate};
    }

    storage_[count_++] = descriptor;
    return InstallResult{.status = Status::Ok};
}

InstallResult Registry::install_pack(std::span<const CommandDescriptor> pack) {
    return install_pack_impl(pack, false);
}

InstallResult Registry::install_pack_impl(
    std::span<const CommandDescriptor> pack,
    bool permit_special_builtins) {
    if (pack.size() > storage_.size() - count_) {
        std::size_t required = 0;
        if (pack.size() <=
            std::numeric_limits<std::size_t>::max() - count_) {
            required = count_ + pack.size();
        }
        return InstallResult{
            .status = Status::Overflow,
            .overflow = OverflowInfo{
                .storage_class = StorageClass::CustomCommands,
                .required = required,
            },
        };
    }

    for (std::size_t i = 0; i < pack.size(); ++i) {
        const auto& desc = pack[i];
        if (!is_valid_name(desc.name)) {
            return InstallResult{.status = Status::BadArgument};
        }
        if (desc.handler == nullptr) {
            return InstallResult{.status = Status::BadArgument};
        }
        if (!permit_special_builtins &&
            desc.command_class == CommandClass::SpecialBuiltin) {
            return InstallResult{.status = Status::BadArgument};
        }
        if (find(desc.name) != nullptr) {
            return InstallResult{.status = Status::Duplicate};
        }

        for (std::size_t j = i + 1; j < pack.size(); ++j) {
            if (desc.name == pack[j].name) {
                return InstallResult{.status = Status::Duplicate};
            }
        }
    }

    for (const auto& desc : pack) {
        storage_[count_++] = desc;
    }
    return InstallResult{.status = Status::Ok};
}

InstallResult detail::StandardCommandInstaller::install(
    Registry& registry,
    std::span<const CommandDescriptor> pack) {
    return registry.install_pack_impl(pack, true);
}

const CommandDescriptor* Registry::find(std::string_view name) const {
    for (std::size_t i = 0; i < count_; ++i) {
        if (storage_[i].name == name) {
            return &storage_[i];
        }
    }
    return nullptr;
}

CommandResult dispatch(
    const CommandDescriptor& descriptor,
    std::span<const std::string_view> args,
    CommandContext& context) {
    if (args.empty() || args[0] != descriptor.name) {
        return {
            .flow = Flow::Normal,
            .status = static_cast<int>(CommandStatus::Usage),
            .error = Status::BadArgument,
        };
    }
    if (!context.capabilities.contains(descriptor.required_capabilities)) {
        return {
            .flow = Flow::Normal,
            .status = static_cast<int>(CommandStatus::Unavailable),
            .error = Status::Unavailable,
        };
    }
    if (descriptor.handler == nullptr) {
        return {
            .flow = Flow::Normal,
            .status = static_cast<int>(CommandStatus::CannotExecute),
            .error = Status::BadArgument,
        };
    }
    CommandResult result{
        .flow = Flow::Normal,
        .status = 0,
        .error = Status::Ok,
    };
    descriptor.handler(descriptor.context, args, context, result);
    return result;
}

}  // namespace mm::shell
