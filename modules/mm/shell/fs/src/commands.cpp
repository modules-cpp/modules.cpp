// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

module mm.shell.fs;

import mm.shell;
import mm.fs;

namespace mm::shell::fs {
namespace {

using Args = std::span<const std::string_view>;

struct Text {
    char* data = nullptr;
    std::size_t capacity = 0;
    std::size_t used = 0;

    [[nodiscard]] bool append(std::string_view value) {
        if (used > capacity || value.size() > capacity - used) return false;
        for (char c : value) data[used++] = c;
        return true;
    }

    [[nodiscard]] bool decimal(std::uint64_t value) {
        char digits[20]{};
        std::size_t count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        if (used > capacity || count > capacity - used) return false;
        while (count != 0) data[used++] = digits[--count];
        return true;
    }

    [[nodiscard]] std::string_view view() const { return {data, used}; }
};

[[nodiscard]] Text text_buffer(CommandContext& context) {
    return {reinterpret_cast<char*>(context.scratch.data()), context.scratch.size(), 0};
}

[[nodiscard]] bool number(std::string_view spelling, std::uint64_t& value) {
    if (spelling.empty()) return false;
    std::uint64_t parsed = 0;
    for (char c : spelling) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    value = parsed;
    return true;
}

void diagnostic(CommandContext& context, std::string_view message) {
    (void)context.io.err.write(message);
}

void usage(CommandContext& context, CommandResult& result, std::string_view command) {
    result.status = 2;
    result.error = Status::BadArgument;
    diagnostic(context, command);
    diagnostic(context, ": bad argument\n");
}

void overflow(CommandContext& context, CommandResult& result, std::string_view command,
              std::size_t required) {
    result.status = 1;
    result.error = Status::Overflow;
    result.overflow = {StorageClass::TransactionScratch, required};
    diagnostic(context, command);
    diagnostic(context, ": transaction scratch overflow\n");
}

// mm.fs's answer as the shell reports it: 1 for what a file system says
// about the files, 2 for a malformed path, and the mcu pack's codes for the
// device beneath.
void file_status(CommandContext& context, CommandResult& result, std::string_view command,
                 mm::fs::Status status) {
    std::string_view message;
    int code = 1;
    Status error = Status::Unavailable;
    switch (status) {
        case mm::fs::Status::Ok: return;
        case mm::fs::Status::BadArgument:
            usage(context, result, command);
            return;
        case mm::fs::Status::NotFound:
            message = "no such file or directory";
            error = Status::NotFound;
            break;
        case mm::fs::Status::Exists: message = "already exists"; break;
        case mm::fs::Status::NotDirectory: message = "not a directory"; break;
        case mm::fs::Status::IsDirectory: message = "is a directory"; break;
        case mm::fs::Status::NotEmpty: message = "directory not empty"; break;
        case mm::fs::Status::NoSpace:
            message = "no space left";
            error = Status::WriteError;
            break;
        case mm::fs::Status::ReadOnly:
            message = "read-only volume";
            error = Status::WriteError;
            break;
        case mm::fs::Status::NameTooLong: message = "name too long"; break;
        case mm::fs::Status::TooMany: message = "too many open files"; break;
        case mm::fs::Status::Busy:
            message = "busy";
            code = 75;
            break;
        case mm::fs::Status::CrossVolume: message = "across volumes"; break;
        case mm::fs::Status::Unsupported:
            message = "unsupported";
            code = 70;
            error = Status::Unsupported;
            break;
        case mm::fs::Status::Corrupt:
            message = "corrupt volume";
            code = 74;
            error = Status::ReadError;
            break;
        case mm::fs::Status::Timeout:
            message = "timeout";
            code = 124;
            break;
        case mm::fs::Status::TransportError:
            message = "transport error";
            code = 74;
            error = Status::ReadError;
            break;
    }
    result.status = code;
    result.error = error;
    diagnostic(context, command);
    diagnostic(context, ": ");
    diagnostic(context, message);
    diagnostic(context, "\n");
}

void output(CommandContext& context, CommandResult& result, std::string_view value) {
    const auto written = context.io.out.write(value);
    if (written == SinkResult::Accepted) return;
    result.status = 1;
    result.error = written == SinkResult::WouldBlock ? Status::Unavailable : Status::WriteError;
    result.overflow = context.io.out.failure().overflow;
}

// "ls PATH [SKIP]": a name a line, a directory's with a trailing slash,
// skipping the first SKIP entries; as many as fit, and a note of where to
// go on when more remain.
void ls_handler(void*, Args args, CommandContext& context, CommandResult& result) {
    std::uint64_t skip = 0;
    if (args.size() < 2 || args.size() > 3 || (args.size() == 3 && !number(args[2], skip))) {
        usage(context, result, "ls");
        return;
    }
    mm::fs::Directory directory;
    auto status = mm::fs::open_directory(args[1], directory);
    if (status != mm::fs::Status::Ok) {
        file_status(context, result, "ls", status);
        return;
    }
    auto text = text_buffer(context);
    char name[mm::fs::max_name + 1]{};
    std::uint64_t seen = 0;
    bool more = false;
    while (true) {
        std::size_t length = 0;
        mm::fs::Stat stat;
        bool done = false;
        status = directory.next(name, length, stat, done);
        if (status != mm::fs::Status::Ok) {
            file_status(context, result, "ls", status);
            return;
        }
        if (done) break;
        if (seen++ < skip) continue;
        const std::string_view entry{name, length};
        const std::size_t line = length + (stat.kind == mm::fs::Kind::Directory ? 2 : 1);
        if (text.used + line > text.capacity) {
            if (text.used == 0) {
                overflow(context, result, "ls", line);
                return;
            }
            more = true;
            --seen;
            break;
        }
        (void)text.append(entry);
        if (stat.kind == mm::fs::Kind::Directory) (void)text.append("/");
        (void)text.append("\n");
    }
    output(context, result, text.view());
    if (more && result.status == 0) {
        char note[24]{};
        Text tail{note, sizeof(note), 0};
        (void)tail.append(" ");
        (void)tail.decimal(seen);
        (void)tail.append("\n");
        diagnostic(context, "ls: more; ls ");
        diagnostic(context, args[1]);
        diagnostic(context, tail.view());
    }
}

// "cat PATH [OFFSET]": the file's bytes from OFFSET, as many as the scratch
// holds, and a note of the next offset when more remain.
void cat_handler(void*, Args args, CommandContext& context, CommandResult& result) {
    std::uint64_t offset = 0;
    if (args.size() < 2 || args.size() > 3 || (args.size() == 3 && !number(args[2], offset))) {
        usage(context, result, "cat");
        return;
    }
    mm::fs::Stat stat;
    auto status = mm::fs::stat(args[1], stat);
    if (status == mm::fs::Status::Ok && stat.kind == mm::fs::Kind::Directory)
        status = mm::fs::Status::IsDirectory;
    if (status != mm::fs::Status::Ok) {
        file_status(context, result, "cat", status);
        return;
    }
    if (offset >= stat.size) return;    // nothing past the end
    if (context.scratch.empty()) {
        overflow(context, result, "cat", 1);
        return;
    }
    mm::fs::File file;
    status = mm::fs::open(args[1], mm::fs::Access::Read, mm::fs::Disposition::OpenExisting, file);
    if (status == mm::fs::Status::Ok) status = file.seek(offset);
    std::size_t count = 0;
    if (status == mm::fs::Status::Ok) status = file.read(context.scratch, count);
    if (status != mm::fs::Status::Ok) {
        file_status(context, result, "cat", status);
        return;
    }
    output(context, result,
           {reinterpret_cast<const char*>(context.scratch.data()), count});
    if (result.status == 0 && offset + count < stat.size) {
        char note[24]{};
        Text tail{note, sizeof(note), 0};
        (void)tail.append(" ");
        (void)tail.decimal(offset + count);
        (void)tail.append("\n");
        diagnostic(context, "cat: more; cat ");
        diagnostic(context, args[1]);
        diagnostic(context, tail.view());
    }
}

// "stat PATH": "file SIZE MODIFIED" or "directory 0 MODIFIED", then " ro"
// for a read-only entry.
void stat_handler(void*, Args args, CommandContext& context, CommandResult& result) {
    if (args.size() != 2) {
        usage(context, result, "stat");
        return;
    }
    mm::fs::Stat stat;
    const auto status = mm::fs::stat(args[1], stat);
    if (status != mm::fs::Status::Ok) {
        file_status(context, result, "stat", status);
        return;
    }
    char line[64]{};
    Text text{line, sizeof(line), 0};
    (void)text.append(stat.kind == mm::fs::Kind::Directory ? "directory " : "file ");
    (void)text.decimal(stat.size);
    (void)text.append(" ");
    (void)text.decimal(stat.modified);
    if (stat.read_only) (void)text.append(" ro");
    (void)text.append("\n");
    output(context, result, text.view());
}

// "write PATH TEXT..." and "append PATH TEXT...": the words joined by
// spaces and a newline, replacing the file or added to its end.
void put(Args args, CommandContext& context, CommandResult& result, std::string_view command,
         bool append) {
    if (args.size() < 2) {
        usage(context, result, command);
        return;
    }
    std::size_t required = 1;
    for (std::size_t i = 2; i < args.size(); ++i) required += args[i].size() + (i == 2 ? 0 : 1);
    if (required > context.scratch.size()) {
        overflow(context, result, command, required);
        return;
    }
    auto text = text_buffer(context);
    for (std::size_t i = 2; i < args.size(); ++i) {
        if (i != 2) (void)text.append(" ");
        (void)text.append(args[i]);
    }
    (void)text.append("\n");
    mm::fs::File file;
    auto status = mm::fs::open(
        args[1], append ? mm::fs::Access::Append : mm::fs::Access::Write,
        append ? mm::fs::Disposition::OpenOrCreate : mm::fs::Disposition::CreateOrTruncate,
        file);
    std::size_t count = 0;
    if (status == mm::fs::Status::Ok)
        status = file.write(std::as_bytes(std::span{text.data, text.used}), count);
    if (status == mm::fs::Status::Ok && count != text.used) status = mm::fs::Status::NoSpace;
    if (status == mm::fs::Status::Ok) status = file.close();
    file_status(context, result, command, status);
}

void write_handler(void*, Args args, CommandContext& context, CommandResult& result) {
    put(args, context, result, "write", false);
}

void append_handler(void*, Args args, CommandContext& context, CommandResult& result) {
    put(args, context, result, "append", true);
}

void rm_handler(void*, Args args, CommandContext& context, CommandResult& result) {
    if (args.size() != 2) {
        usage(context, result, "rm");
        return;
    }
    file_status(context, result, "rm", mm::fs::remove(args[1]));
}

void mkdir_handler(void*, Args args, CommandContext& context, CommandResult& result) {
    if (args.size() != 2) {
        usage(context, result, "mkdir");
        return;
    }
    file_status(context, result, "mkdir", mm::fs::make_directory(args[1]));
}

void mv_handler(void*, Args args, CommandContext& context, CommandResult& result) {
    if (args.size() != 3) {
        usage(context, result, "mv");
        return;
    }
    file_status(context, result, "mv", mm::fs::rename(args[1], args[2]));
}

[[nodiscard]] bool is_mounted(std::string_view prefix) {
    mm::fs::Volume* volume = nullptr;
    return mm::fs::mounted(prefix, volume) == mm::fs::Status::Ok;
}

// "df [PREFIX]": "PREFIX TOTAL FREE" in bytes, for the one named or every
// mounted prefix the application declared.
void df_handler(void* context_pointer, Args args, CommandContext& context, CommandResult& result) {
    const auto& binding = *static_cast<const FsBinding*>(context_pointer);
    if (args.size() > 2) {
        usage(context, result, "df");
        return;
    }
    auto text = text_buffer(context);
    const auto line = [&](std::string_view prefix) {
        mm::fs::Space space;
        const auto status = mm::fs::space(prefix, space);
        if (status != mm::fs::Status::Ok) {
            file_status(context, result, "df", status);
            return false;
        }
        if (!text.append(prefix) || !text.append(" ") || !text.decimal(space.total) ||
            !text.append(" ") || !text.decimal(space.free) || !text.append("\n")) {
            overflow(context, result, "df", text.used + prefix.size() + 44);
            return false;
        }
        return true;
    };
    if (args.size() == 2) {
        if (!line(args[1])) return;
    } else {
        for (const auto prefix : binding.prefixes)
            if (is_mounted(prefix) && !line(prefix)) return;
    }
    output(context, result, text.view());
}

// "mounts": the declared prefixes that are mounted, a line each.
void mounts_handler(void* context_pointer, Args args, CommandContext& context,
                    CommandResult& result) {
    const auto& binding = *static_cast<const FsBinding*>(context_pointer);
    if (args.size() != 1) {
        usage(context, result, "mounts");
        return;
    }
    auto text = text_buffer(context);
    for (const auto prefix : binding.prefixes) {
        if (!is_mounted(prefix)) continue;
        if (!text.append(prefix) || !text.append("\n")) {
            overflow(context, result, "mounts", text.used + prefix.size() + 1);
            return;
        }
    }
    output(context, result, text.view());
}

}  // namespace

void enable(CapabilitySet& capabilities) { capabilities.set(Capability::Files); }

InstallResult install_fs(Registry& registry, FsBinding& binding) {
    const struct Entry {
        std::string_view name;
        std::string_view summary;
        CommandHandler handler;
    } entries[fs_builtin_count]{
        {"ls", "list a directory: ls PATH [SKIP]", &ls_handler},
        {"cat", "print a file: cat PATH [OFFSET]", &cat_handler},
        {"stat", "describe a file or directory", &stat_handler},
        {"write", "replace a file with a line: write PATH TEXT...", &write_handler},
        {"append", "add a line to a file: append PATH TEXT...", &append_handler},
        {"rm", "remove a file or an empty directory", &rm_handler},
        {"mkdir", "make a directory", &mkdir_handler},
        {"mv", "rename within a volume: mv FROM TO", &mv_handler},
        {"df", "volume space in bytes: df [PREFIX]", &df_handler},
        {"mounts", "the mounted volumes", &mounts_handler},
    };
    CapabilitySet required;
    required.set(Capability::Files);
    CommandDescriptor pack[fs_builtin_count]{};
    for (std::size_t i = 0; i < fs_builtin_count; ++i) {
        pack[i] = {
            .name = entries[i].name,
            .summary = entries[i].summary,
            .command_class = CommandClass::Builtin,
            .required_capabilities = required,
            .handler = entries[i].handler,
            .context = &binding,
        };
    }
    return registry.install_pack(pack);
}

}  // namespace mm::shell::fs
