// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include "stdio-cxx.h"
#include <cstddef>
#include <span>

export module platform.pico.stdio;

import mm.stdio;

namespace {

mm::stdio::Status from(int code) {
    switch (code) {
        case MM_PICO_STDIO_OK: return mm::stdio::Status::Ok;
        case MM_PICO_STDIO_BAD_ARGUMENT: return mm::stdio::Status::BadArgument;
        case MM_PICO_STDIO_UNSUPPORTED: return mm::stdio::Status::Unsupported;
        case MM_PICO_STDIO_NOT_INITIALIZED: return mm::stdio::Status::NotInitialized;
        case MM_PICO_STDIO_BUSY: return mm::stdio::Status::Busy;
        case MM_PICO_STDIO_TIMEOUT: return mm::stdio::Status::Timeout;
        case MM_PICO_STDIO_TRANSPORT_ERROR: return mm::stdio::Status::TransportError;
        default: return mm::stdio::Status::TransportError;
    }
}

class PicoConsole : public mm::stdio::Console {
public:
    [[nodiscard]] mm::stdio::Status initialize() override {
        return from(mm_pico_stdio_initialize());
    }

    [[nodiscard]] mm::stdio::Status write(std::span<const std::byte> data,
                                           std::size_t& written) override {
        std::size_t raw = 0;
        const auto status = from(mm_pico_stdio_write(
            reinterpret_cast<const unsigned char*>(data.data()), data.size(), &raw));
        if (status == mm::stdio::Status::Ok) written = raw;
        return status;
    }

    [[nodiscard]] mm::stdio::Status read(std::span<std::byte> data,
                                          std::size_t& count) override {
        std::size_t raw = 0;
        const auto status = from(mm_pico_stdio_read(
            reinterpret_cast<unsigned char*>(data.data()), data.size(), &raw));
        if (status == mm::stdio::Status::Ok) count = raw;
        return status;
    }

    [[nodiscard]] mm::stdio::Status flush() override {
        return from(mm_pico_stdio_flush());
    }

    [[nodiscard]] mm::stdio::Status connected(bool& value) override {
        int raw = 0;
        const auto status = from(mm_pico_stdio_connected(&raw));
        if (status == mm::stdio::Status::Ok) value = raw != 0;
        return status;
    }
};

PicoConsole pico_console;

struct Register {
    Register() { mm::stdio::set_console(pico_console); }
};

const Register registered;

}  // namespace
