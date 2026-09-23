// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

module mm.shell;

import :status;
import :capability;
import :io;
import :state;
import :command;

namespace mm::shell {

namespace {

[[nodiscard]] bool is_valid_name(std::string_view name) {
    if (name.empty()) {
        return false;
    }
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
            .status = Status::CapacityExceeded,
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

InstallResult Registry::install_special(const CommandDescriptor& descriptor) {
    if (count_ >= storage_.size()) {
        return InstallResult{
            .status = Status::CapacityExceeded,
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
    if (find(descriptor.name) != nullptr) {
        return InstallResult{.status = Status::Duplicate};
    }

    storage_[count_++] = descriptor;
    return InstallResult{.status = Status::Ok};
}

InstallResult Registry::install_pack(std::span<const CommandDescriptor> pack) {
    if (count_ + pack.size() > storage_.size()) {
        return InstallResult{
            .status = Status::CapacityExceeded,
            .overflow = OverflowInfo{
                .storage_class = StorageClass::CustomCommands,
                .required = count_ + pack.size(),
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
        if (desc.command_class == CommandClass::SpecialBuiltin) {
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
