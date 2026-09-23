// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <limits>
#include <span>
#include <string_view>

export module mm.shell:io;

import :status;

export namespace mm::shell {

enum class SinkResult {
    Accepted,
    WouldBlock,
    Failed,
};

struct SinkFailure {
    Status error = Status::Ok;
    OverflowInfo overflow{};
};

using WriteFn = SinkResult (*)(void* context, std::span<const char> bytes);
using FlushFn = SinkResult (*)(void* context);
using FailureFn = SinkFailure (*)(void* context);

struct ByteSink {
    void* context = nullptr;
    WriteFn write_fn = nullptr;
    FlushFn flush_fn = nullptr;
    FailureFn failure_fn = nullptr;

    [[nodiscard]] SinkResult write(std::span<const char> bytes) const {
        if (write_fn == nullptr) return SinkResult::Failed;
        return write_fn(context, bytes);
    }

    [[nodiscard]] SinkResult write(std::string_view text) const {
        return write(std::span<const char>(text.data(), text.size()));
    }

    [[nodiscard]] SinkResult write(const char* str) const {
        if (str == nullptr) return SinkResult::Failed;
        return write(std::string_view(str));
    }

    [[nodiscard]] SinkResult flush() const {
        if (flush_fn == nullptr) return SinkResult::Failed;
        return flush_fn(context);
    }

    [[nodiscard]] SinkFailure failure() const {
        if (failure_fn == nullptr) {
            return SinkFailure{.error = Status::WriteError};
        }
        return failure_fn(context);
    }
};

struct IoServices {
    ByteSink out;
    ByteSink err;
};

struct MemorySink {
    std::span<char> buffer;
    StorageClass storage_class = StorageClass::StagedOutput;
    std::size_t written = 0;
    SinkFailure last_failure{};

    [[nodiscard]] ByteSink sink() {
        return {
            .context = this,
            .write_fn = &MemorySink::write_callback,
            .flush_fn = &MemorySink::flush_callback,
            .failure_fn = &MemorySink::failure_callback,
        };
    }

    [[nodiscard]] std::string_view view() const {
        return std::string_view(buffer.data(), written);
    }

    void reset() {
        written = 0;
        last_failure = {};
    }

    static SinkResult write_callback(void* ctx, std::span<const char> bytes) {
        if (bytes.empty()) return SinkResult::Accepted;
        auto* self = static_cast<MemorySink*>(ctx);
        if (self == nullptr) return SinkResult::Failed;
        if (self->written > self->buffer.size() ||
            bytes.size() > self->buffer.size() - self->written) {
            std::size_t required = 0;
            if (bytes.size() <=
                std::numeric_limits<std::size_t>::max() - self->written) {
                required = self->written + bytes.size();
            }
            self->last_failure = {
                .error = Status::Overflow,
                .overflow = {
                    .storage_class = self->storage_class,
                    .required = required,
                },
            };
            return SinkResult::Failed;
        }
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            self->buffer[self->written + i] = bytes[i];
        }
        self->written += bytes.size();
        self->last_failure = {};
        return SinkResult::Accepted;
    }

    static SinkResult flush_callback(void* ctx) {
        return ctx != nullptr ? SinkResult::Accepted : SinkResult::Failed;
    }

    static SinkFailure failure_callback(void* ctx) {
        auto* self = static_cast<MemorySink*>(ctx);
        return self != nullptr
            ? self->last_failure
            : SinkFailure{.error = Status::WriteError};
    }
};

struct DiscardSink {
    [[nodiscard]] static ByteSink sink() {
        return {
            .context = nullptr,
            .write_fn = [](void*, std::span<const char>) {
                return SinkResult::Accepted;
            },
            .flush_fn = [](void*) {
                return SinkResult::Accepted;
            },
            .failure_fn = [](void*) {
                return SinkFailure{.error = Status::Ok};
            },
        };
    }
};

}  // namespace mm::shell
