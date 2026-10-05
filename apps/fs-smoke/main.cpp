// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The mm.fs contract, checked on whatever the board calls its own storage.
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

import mm.fs;
import mm.fs.conformance;
import mm.fs.local;
import mm.stdio;

namespace {

// Writes all of text, or as much as the console takes in a few attempts.
void say(std::string_view text) {
    auto& console = mm::stdio::selected_console();
    auto bytes = std::as_bytes(std::span<const char>{text.data(), text.size()});
    for (unsigned int attempt = 0; attempt < 8 && !bytes.empty(); ++attempt) {
        std::size_t written = 0;
        if (console.write(bytes, written) != mm::stdio::Status::Ok || written == 0) break;
        bytes = bytes.subspan(written);
    }
}

// A count as decimal digits, without a formatting library.
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

    const auto mounted = mm::fs::local::mount("/data");
    if (mounted == mm::fs::Status::Unsupported || mounted == mm::fs::Status::NotFound) {
        say("fs-smoke: no local storage; skip\n");
        return 0;
    }
    if (mounted != mm::fs::Status::Ok) {
        say("fs-smoke: mounting /data failed\n");
        return 1;
    }

    mm::fs::conformance::Report report;
    if (mm::fs::conformance::run("/data", report) != mm::fs::Status::Ok) {
        say("fs-smoke: the checks could not run in /data\n");
        static_cast<void>(mm::fs::local::unmount("/data"));
        return 2;
    }
    say("fs-smoke: ");
    say_number(report.passed);
    say(" passed, ");
    say_number(report.failed);
    say(" failed\n");
    if (report.failed != 0) {
        say("fs-smoke: first failure: ");
        say(report.first_failure);
        say("\n");
    }
    static_cast<void>(mm::stdio::selected_console().flush());

    if (mm::fs::local::unmount("/data") != mm::fs::Status::Ok) return 3;
    return report.failed == 0 ? 0 : 4;
}
