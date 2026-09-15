#include <cstddef>
#include <span>

import mm.stdio;

int main() {
    auto& console = mm::stdio::selected_console();
    const auto initialized = console.initialize();
    if (initialized == mm::stdio::Status::Unsupported) return 0;
    if (initialized != mm::stdio::Status::Ok) return 1;

    bool connected = false;
    constexpr unsigned int poll_limit = 5'000'000;
    for (unsigned int attempt = 0; attempt < poll_limit && !connected; ++attempt) {
        const auto status = console.connected(connected);
        if (status != mm::stdio::Status::Ok) return 2;
    }
    if (!connected) return 0;

    constexpr char message[] = "modules.cpp mm.stdio over USB CDC\n";
    const auto bytes = std::as_bytes(std::span{message}).first(sizeof(message) - 1);
    std::size_t offset = 0;
    constexpr unsigned int write_limit = 8;
    for (unsigned int attempt = 0;
         attempt < write_limit && offset < bytes.size(); ++attempt) {
        std::size_t written = 0;
        const auto status = console.write(bytes.subspan(offset), written);
        if (status != mm::stdio::Status::Ok) return 3;
        if (written == 0) break;
        offset += written;
    }

    if (console.flush() != mm::stdio::Status::Ok) return 4;
    return offset == bytes.size() ? 0 : 5;
}
