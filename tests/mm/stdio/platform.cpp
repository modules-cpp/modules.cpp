// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A stand-in console whose far end has a bounded appetite. The interesting
// behaviour of a real console is what happens when nobody is reading, so this
// models that rather than a transport that always succeeds.
#include <cstddef>
#include <span>
#include <vector>

import mm.stdio;

namespace {

class StandConsole : public mm::stdio::Console {
public:
    [[nodiscard]] mm::stdio::Status initialize() override {
        initialized = true;
        return mm::stdio::Status::Ok;
    }

    [[nodiscard]] mm::stdio::Status write(std::span<const std::byte> data,
                                          std::size_t& written) override {
        if (!initialized) return mm::stdio::Status::NotInitialized;
        if (forced != mm::stdio::Status::Ok) return forced;

        // A disconnected console accepts nothing, which is Ok with a count of
        // zero rather than an error: the caller asked to send and nothing went.
        const auto room = link_connected ? capacity - accepted.size() : 0;
        const auto count = data.size() < room ? data.size() : room;
        accepted.insert(accepted.end(), data.begin(), data.begin() + count);
        written = count;
        return mm::stdio::Status::Ok;
    }

    [[nodiscard]] mm::stdio::Status read(std::span<std::byte> data,
                                         std::size_t& count) override {
        if (!initialized) return mm::stdio::Status::NotInitialized;
        if (forced != mm::stdio::Status::Ok) return forced;

        const auto available = pending.size() - consumed;
        const auto taken = data.size() < available ? data.size() : available;
        for (std::size_t index = 0; index < taken; ++index)
            data[index] = pending[consumed + index];
        consumed += taken;
        count = taken;
        return mm::stdio::Status::Ok;
    }

    [[nodiscard]] mm::stdio::Status flush() override {
        if (!initialized) return mm::stdio::Status::NotInitialized;
        if (forced != mm::stdio::Status::Ok) return forced;
        ++flushes;
        return mm::stdio::Status::Ok;
    }

    [[nodiscard]] mm::stdio::Status connected(bool& value) override {
        if (!initialized) return mm::stdio::Status::NotInitialized;
        if (forced != mm::stdio::Status::Ok) return forced;
        value = link_connected;
        ++connection_queries;
        return mm::stdio::Status::Ok;
    }

    void reset() {
        initialized = false;
        link_connected = true;
        capacity = 1024;
        accepted.clear();
        pending.clear();
        consumed = 0;
        flushes = 0;
        connection_queries = 0;
        forced = mm::stdio::Status::Ok;
    }

    bool initialized = false;
    bool link_connected = true;
    std::size_t capacity = 1024;
    std::vector<std::byte> accepted;
    std::vector<std::byte> pending;
    std::size_t consumed = 0;
    std::size_t flushes = 0;
    std::size_t connection_queries = 0;
    mm::stdio::Status forced = mm::stdio::Status::Ok;
};

StandConsole stand;

struct Register {
    Register() { mm::stdio::set_console(stand); }
};

const Register registered;

}  // namespace

void mm_test_stdio_reset() { stand.reset(); }
void mm_test_stdio_force(mm::stdio::Status status) { stand.forced = status; }
void mm_test_stdio_connect(bool connected) { stand.link_connected = connected; }
void mm_test_stdio_capacity(std::size_t capacity) { stand.capacity = capacity; }
void mm_test_stdio_offer(const unsigned char* bytes, std::size_t size) {
    stand.pending.clear();
    stand.consumed = 0;
    for (std::size_t index = 0; index < size; ++index)
        stand.pending.push_back(static_cast<std::byte>(bytes[index]));
}
std::size_t mm_test_stdio_accepted() { return stand.accepted.size(); }
unsigned int mm_test_stdio_byte(std::size_t index) {
    return index < stand.accepted.size()
               ? static_cast<unsigned int>(stand.accepted[index])
               : 0x100;
}
std::size_t mm_test_stdio_flushes() { return stand.flushes; }
std::size_t mm_test_stdio_connection_queries() { return stand.connection_queries; }
