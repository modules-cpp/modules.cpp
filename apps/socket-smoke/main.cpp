// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The board's SD socket below any file system: geometry, then a write and
// read-back of the last block, restored afterwards.
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

import mm.fs;
import mm.sdcard.socket;
import mm.stdio;

namespace {

void say(std::string_view text) {
    auto& console = mm::stdio::selected_console();
    auto bytes = std::as_bytes(std::span<const char>{text.data(), text.size()});
    for (unsigned int attempt = 0; attempt < 8 && !bytes.empty(); ++attempt) {
        std::size_t written = 0;
        if (console.write(bytes, written) != mm::stdio::Status::Ok || written == 0) break;
        bytes = bytes.subspan(written);
    }
}

void say_number(std::uint64_t value) {
    std::array<char, 20> digits{};
    std::size_t length = 0;
    do {
        digits[digits.size() - 1 - length++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    say({digits.data() + digits.size() - length, length});
}

int finish(int code) {
    static_cast<void>(mm::stdio::selected_console().flush());
    return code;
}

}  // namespace

int main() {
    static_cast<void>(mm::stdio::selected_console().initialize());
    auto& card = mm::sdcard::socket::card();

    mm::fs::BlockGeometry geometry;
    const auto status = card.geometry(geometry);
    if (status == mm::fs::Status::TransportError || status == mm::fs::Status::Timeout ||
        status == mm::fs::Status::Unsupported) {
        say("socket-smoke: no card in the socket; skip\n");
        return finish(0);
    }
    if (status != mm::fs::Status::Ok || geometry.count == 0 || geometry.size != 512) {
        say("socket-smoke: the card's geometry failed\n");
        return finish(1);
    }
    say("socket-smoke: ");
    say_number(geometry.count);
    say(" blocks of ");
    say_number(geometry.size);
    say(" bytes\n");

    const std::uint64_t last = geometry.count - 1;
    std::array<std::byte, 512> saved{};
    if (card.read(last, saved) != mm::fs::Status::Ok) {
        say("socket-smoke: reading the last block failed\n");
        return finish(2);
    }
    std::array<std::byte, 512> pattern{};
    for (std::size_t i = 0; i < pattern.size(); ++i)
        pattern[i] = static_cast<std::byte>((i * 7 + 0x5a) & 0xffu);
    if (card.write(last, pattern) != mm::fs::Status::Ok) {
        say("socket-smoke: writing the last block failed\n");
        return finish(3);
    }
    std::array<std::byte, 512> back{};
    const bool matched = card.read(last, back) == mm::fs::Status::Ok && back == pattern;
    if (card.write(last, saved) != mm::fs::Status::Ok) {
        say("socket-smoke: restoring the last block failed\n");
        return finish(5);
    }
    if (!matched) {
        say("socket-smoke: the pattern did not read back\n");
        return finish(4);
    }
    say("socket-smoke: the last block wrote, read back, and was restored\n");
    return finish(0);
}
