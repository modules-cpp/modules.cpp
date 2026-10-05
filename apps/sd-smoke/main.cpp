// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The mm.fs contract, checked on a FAT volume on the card in the board's TF
// socket.
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

import mm.fs;
import mm.fs.conformance;
import mm.fs.fat;
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

void say_number(unsigned int value) {
    std::array<char, 10> digits{};
    std::size_t length = 0;
    do {
        digits[digits.size() - 1 - length++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    say({digits.data() + digits.size() - length, length});
}

}  // namespace

int main() {
    static_cast<void>(mm::stdio::selected_console().initialize());

    const auto mounted = mm::fs::fat::mount("/sd", mm::sdcard::socket::card());
    if (mounted == mm::fs::Status::TransportError || mounted == mm::fs::Status::Timeout) {
        say("sd-smoke: no card in the socket; skip\n");
        return 0;
    }
    if (mounted == mm::fs::Status::Corrupt) {
        say("sd-smoke: the card holds no FAT volume; it was not formatted\n");
        return 2;
    }
    if (mounted != mm::fs::Status::Ok) {
        say("sd-smoke: mounting /sd failed\n");
        return 1;
    }

    mm::fs::conformance::Report report;
    if (mm::fs::conformance::run("/sd", report) != mm::fs::Status::Ok) {
        say("sd-smoke: the checks could not run in /sd\n");
        static_cast<void>(mm::fs::fat::unmount("/sd"));
        return 3;
    }
    say("sd-smoke: ");
    say_number(report.passed);
    say(" passed, ");
    say_number(report.failed);
    say(" failed\n");
    if (report.failed != 0) {
        say("sd-smoke: first failure: ");
        say(report.first_failure);
        say("\n");
    }
    static_cast<void>(mm::stdio::selected_console().flush());

    if (mm::fs::fat::unmount("/sd") != mm::fs::Status::Ok) return 4;
    return report.failed == 0 ? 0 : 5;
}
