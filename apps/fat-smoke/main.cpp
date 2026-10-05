// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The mm.fs contract, checked on a FAT volume on the board's block storage.
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

import mm.fs;
import mm.fs.conformance;
import mm.fs.fat;
import mm.mcu;
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

// Polls the board's block storage until a device is ready, up to wait_ms.
[[nodiscard]] mm::mcu::Status wait_for_drive(unsigned long wait_ms) {
    unsigned long start = 0;
    if (mm::mcu::ticks_ms(start) != mm::mcu::Status::Ok) start = 0;
    while (true) {
        bool present = false;
        const auto status = mm::mcu::storage_poll(present);
        if (status != mm::mcu::Status::Ok) return status;
        if (present) return mm::mcu::Status::Ok;
        unsigned long now = 0;
        if (mm::mcu::ticks_ms(now) != mm::mcu::Status::Ok || now - start >= wait_ms)
            return mm::mcu::Status::Timeout;
        static_cast<void>(mm::mcu::delay_ms(10));
    }
}

mm::fs::McuStorage drive;

}  // namespace

int main() {
    static_cast<void>(mm::stdio::selected_console().initialize());

    const auto waited = wait_for_drive(10'000);
    if (waited == mm::mcu::Status::Unsupported) {
        say("fat-smoke: this board has no block storage; skip\n");
        return 0;
    }
    if (waited != mm::mcu::Status::Ok) {
        say("fat-smoke: no drive appeared; skip\n");
        return 0;
    }

    const auto mounted = mm::fs::fat::mount("/usb", drive);
    if (mounted == mm::fs::Status::Corrupt) {
        say("fat-smoke: the drive holds no FAT volume; it was not formatted\n");
        return 2;
    }
    if (mounted != mm::fs::Status::Ok) {
        say("fat-smoke: mounting /usb failed\n");
        return 1;
    }

    mm::fs::conformance::Report report;
    if (mm::fs::conformance::run("/usb", report) != mm::fs::Status::Ok) {
        say("fat-smoke: the checks could not run in /usb\n");
        static_cast<void>(mm::fs::fat::unmount("/usb"));
        return 3;
    }
    say("fat-smoke: ");
    say_number(report.passed);
    say(" passed, ");
    say_number(report.failed);
    say(" failed\n");
    if (report.failed != 0) {
        say("fat-smoke: first failure: ");
        say(report.first_failure);
        say("\n");
    }
    static_cast<void>(mm::stdio::selected_console().flush());

    if (mm::fs::fat::unmount("/usb") != mm::fs::Status::Ok) return 4;
    return report.failed == 0 ? 0 : 5;
}
