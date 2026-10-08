// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
// Read-only hardware diagnostics for the RP2350 Touch LCD 2.8 TF socket.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

import mm.display;
import mm.fonts;
import mm.gfx;
import mm.mcu;
import mm.fs;
import mm.fs.fat;
import mm.sdcard;
import mm.sdcard.socket;
import mm.stdio;

// The Pico adapter's bring-up diagnostic, mcu-c.h's mm_pico_sdio_last_failure.
extern "C" {
struct mm_pico_sdio_failure_t {
    unsigned int reason;
    unsigned int index;
    std::uint32_t response;
    unsigned int words;
    unsigned int pc;
    unsigned int pins;
    unsigned int data_us;
};
void mm_pico_sdio_last_failure(mm_pico_sdio_failure_t* failure);
unsigned int mm_pico_sdio_last_data_us();
// The Pico SDK's pad controls, for the signal-integrity experiment: slew 0
// slow, 1 fast; drive 0 to 3 for 2, 4, 8, 12 mA.
void gpio_set_slew_rate(unsigned int gpio, unsigned int slew);
void gpio_set_drive_strength(unsigned int gpio, unsigned int drive);
}

namespace {

using mm::fs::Status;
constexpr unsigned width = 240;
constexpr unsigned line_height = mm::fonts::kMono12.line_height;
constexpr unsigned max_lines = 320 / line_height;
constexpr unsigned max_columns = width / mm::fonts::kMono12.advance;
constexpr auto green = mm::gfx::rgb(96, 255, 128);
constexpr auto amber = mm::gfx::rgb(255, 190, 72);
constexpr auto red = mm::gfx::rgb(255, 90, 80);

alignas(4) std::array<std::byte, 512> sector{};
alignas(4) std::array<std::byte, 512> repeat{};
alignas(4) std::array<std::byte, 4 * 512> separate{};
alignas(4) std::array<std::byte, 4 * 512> together{};
std::array<std::byte, 513> unaligned{};
std::array<std::byte, (width / 8) * line_height> bits{};
std::array<std::byte, width * line_height * 2> pixels{};

[[nodiscard]] const char* name(Status status) {
    switch (status) {
        case Status::Ok: return "OK";
        case Status::BadArgument: return "BadArgument";
        case Status::Unsupported: return "Unsupported";
        case Status::NotFound: return "NotFound";
        case Status::Exists: return "Exists";
        case Status::NotDirectory: return "NotDirectory";
        case Status::IsDirectory: return "IsDirectory";
        case Status::NotEmpty: return "NotEmpty";
        case Status::NoSpace: return "NoSpace";
        case Status::ReadOnly: return "ReadOnly";
        case Status::NameTooLong: return "NameTooLong";
        case Status::TooMany: return "TooMany";
        case Status::Busy: return "Busy";
        case Status::CrossVolume: return "CrossVolume";
        case Status::Corrupt: return "Corrupt";
        case Status::Timeout: return "Timeout";
        case Status::TransportError: return "TransportError";
    }
    return "Unknown";
}

class Report {
public:
    explicit Report(mm::display::Display& display) : display_(display) {
        screen_ = display.initialize() == mm::display::Status::Ok;
        if (screen_) {
            (void)display.clear(mm::display::Color::Red);
            (void)mm::mcu::delay_ms(1'000);
            screen_ = display.clear(mm::display::Color::Black) ==
                      mm::display::Status::Ok;
        }
        if (screen_) {
            for (std::size_t i = 0; i < pixels.size(); i += 2) {
                pixels[i] = std::byte{0x07};
                pixels[i + 1] = std::byte{0xe0};
            }
            screen_ = display.write({0, 0, width, line_height}, pixels) ==
                      mm::display::Status::Ok;
        }
        auto& console = mm::stdio::selected_console();
        serial_ = console.initialize() == mm::stdio::Status::Ok;
        if (serial_) {
            for (int i = 0; i < 20; ++i) {
                bool conn = false;
                if (console.connected(conn) == mm::stdio::Status::Ok && conn) break;
                (void)mm::mcu::delay_ms(100);
            }
        }
        line("SD/FAT READ-ONLY TEST", green);
    }

    void dump_serial() {
        if (!serial_) return;
        auto& console = mm::stdio::selected_console();
        bool conn = false;
        if (console.connected(conn) == mm::stdio::Status::Ok && conn) {
            for (unsigned i = 0; i < logged_; ++i) {
                const auto len = std::strlen(log_[i].data());
                std::size_t written = 0;
                (void)console.write(std::as_bytes(std::span{log_[i].data(), len}), written);
                const char newline = '\n';
                (void)console.write(std::as_bytes(std::span{&newline, 1}), written);
            }
            (void)console.flush();
        }
    }

    void check(const char* label, Status status) {
        char message[64]{};
        std::snprintf(message, sizeof message, "%s: %s", label, name(status));
        line(message, status == Status::Ok ? green : red);
        if (status != Status::Ok) ++failed_;
    }

    void check(const char* label, bool passed) {
        line_pair(label, passed ? "OK" : "FAIL", passed ? green : red);
        if (!passed) ++failed_;
    }

    void note(const char* message) { line(message, amber); }

    void info(const char* label, std::uint64_t value) {
        char message[64]{};
        std::snprintf(message, sizeof message, "%s: %llu", label,
                      static_cast<unsigned long long>(value));
        line(message, amber);
    }

    void finish() {
        char message[64]{};
        std::snprintf(message, sizeof message, "DONE: %u FAILURE(S)", failed_);
        line(message, failed_ == 0 ? green : red);
    }

private:
    struct ScreenLine {
        std::array<char, max_columns + 1> text{};
        mm::gfx::Rgb565 color = green;
    };

    void line_pair(const char* label, const char* result, mm::gfx::Rgb565 color) {
        char message[64]{};
        std::snprintf(message, sizeof message, "%s: %s", label, result);
        line(message, color);
    }

    void line(const char* message, mm::gfx::Rgb565 color) {
        if (logged_ < log_.size()) {
            std::snprintf(log_[logged_].data(), log_[logged_].size(), "%s", message);
            ++logged_;
        }
        if (serial_) {
            auto& console = mm::stdio::selected_console();
            const auto length = std::strlen(message);
            std::size_t written = 0;
            (void)console.write(std::as_bytes(std::span{message, length}), written);
            const char newline = '\n';
            (void)console.write(std::as_bytes(std::span{&newline, 1}), written);
        }
        if (!screen_) return;
        unsigned row = rows_;
        if (rows_ == max_lines) {
            for (unsigned i = 1; i < max_lines; ++i) lines_[i - 1] = lines_[i];
            row = max_lines - 1;
        } else {
            ++rows_;
        }
        auto& stored = lines_[row];
        stored.text.fill(0);
        const auto length = std::strlen(message);
        const auto shown = length < max_columns ? length : max_columns;
        std::memcpy(stored.text.data(), message, shown);
        stored.color = color;
        if (row == max_lines - 1 && rows_ == max_lines) {
            for (unsigned i = 0; i < max_lines; ++i) draw_row(i);
        } else {
            draw_row(row);
        }
        (void)display_.refresh(mm::display::Refresh::Full);
    }

    void draw_row(unsigned row) {
        const auto& stored = lines_[row];
        bits.fill(std::byte{0xff});
        const auto shown = std::strlen(stored.text.data());
        const mm::gfx::Surface surface{width, line_height, 1, bits};
        if (mm::fonts::render(reinterpret_cast<const char8_t*>(stored.text.data()), shown,
                              mm::fonts::kMono12, mm::display::Color::Black,
                              0, 0, surface) != mm::display::Status::Ok) return;
        for (unsigned y = 0; y < line_height; ++y) {
            const auto packed = std::span<const std::byte>{bits}.subspan(y * width / 8,
                                                                          width / 8);
            const auto rgb = std::span<std::byte>{pixels}.subspan(y * width * 2,
                                                                   width * 2);
            if (mm::gfx::expand_row(packed, width, {stored.color, mm::gfx::rgb565_black}, rgb) !=
                mm::display::Status::Ok) return;
        }
        (void)display_.write({0, row * line_height, width, line_height}, pixels);
    }

    mm::display::Display& display_;
    bool screen_ = false;
    bool serial_ = false;
    std::array<ScreenLine, max_lines> lines_{};
    unsigned rows_ = 0;
    unsigned failed_ = 0;
    // Every line, untruncated, for the serial dump: the screen keeps only
    // its last screenful.
    std::array<std::array<char, 48>, 256> log_{};
    unsigned logged_ = 0;
};

[[nodiscard]] Status read_sector(mm::fs::BlockDevice& card, std::uint64_t block,
                                 std::span<std::byte> out) {
    return card.read(block, out);
}

// What the platform saw on the last failed transfer: the response or data
// stage, the command, the R1 or raw response word, and data words received
// (or, for a data CRC, the failing block).
void why(Report& report) {
    mm_pico_sdio_failure_t failure{};
    mm_pico_sdio_last_failure(&failure);
    static constexpr const char* reasons[] = {"NONE",     "RSP TIMEOUT",  "RSP FRAME",
                                              "R1 ERROR", "DATA TIMEOUT", "DATA CRC",
                                              "BUSY TIMEOUT"};
    const char* reason = failure.reason < std::size(reasons) ? reasons[failure.reason] : "?";
    char message[64]{};
    std::snprintf(message, sizeof message, " WHY %s C%u R%08lx W%u", reason, failure.index,
                  static_cast<unsigned long>(failure.response), failure.words);
    report.note(message);
}

// A pad setting for the six SD lines: the clock's and the others'.
struct Pads {
    const char* name;
    unsigned int clock_slew, clock_drive, line_slew, line_drive;
};

void apply(const Pads& pads) {
    gpio_set_slew_rate(19, pads.clock_slew);
    gpio_set_drive_strength(19, pads.clock_drive);
    for (unsigned int gpio = 20; gpio <= 24; ++gpio) {
        gpio_set_slew_rate(gpio, pads.line_slew);
        gpio_set_drive_strength(gpio, pads.line_drive);
    }
}

// Reads LBA0 count times at hz, identifying the card again after each
// failure, and reports the successes and each failure reason's count.
void try_clock(mm::fs::BlockDevice& card, const char* pads, unsigned long hz, Report& report) {
    constexpr unsigned count = 32;
    unsigned ok = 0, mismatched = 0, lost = 0, good_us = 0;
    unsigned reasons[7]{};
    unsigned long actual = hz;
    for (unsigned i = 0; i < count; ++i) {
        mm::fs::BlockGeometry geometry{};
        if (card.geometry(geometry) != Status::Ok ||
            mm::mcu::sdio_clock(0, hz, actual) != mm::mcu::Status::Ok) {
            ++lost;
            continue;
        }
        const auto status = read_sector(card, 0, repeat);
        const auto us = mm_pico_sdio_last_data_us();
        if (status == Status::Ok && ok == 0) good_us = us;
        if (status == Status::Ok) {
            if (repeat == sector) ++ok;
            else ++mismatched;
            continue;
        }
        // A data CRC failure leaves the block in repeat: which lines differ
        // from the reference, where first, and whether it is the reference
        // moved by whole nibbles.
        if (mm_pico_sdio_failure_t f{}; (mm_pico_sdio_last_failure(&f), f.reason == 5) &&
                                        reasons[5] < 2) {
            const auto nibble = [](const auto& block, int at) -> int {
                if (at < 0 || at >= 1024) return -1;
                const auto byte = static_cast<unsigned>(block[at / 2]);
                return at % 2 == 0 ? static_cast<int>(byte >> 4) : static_cast<int>(byte & 15u);
            };
            unsigned lines = 0;
            int first = -1;
            for (int at = 0; at < 1024; ++at) {
                const auto d = nibble(repeat, at) ^ nibble(sector, at);
                if (d != 0 && first < 0) first = at;
                lines |= static_cast<unsigned>(d);
            }
            int shift = 0;
            for (int k = -2; k <= 2 && shift == 0; ++k) {
                if (k == 0) continue;
                bool same = true;
                for (int at = 16; at < 1000 && same; ++at)
                    same = nibble(repeat, at) == nibble(sector, at - k);
                if (same) shift = k;
            }
            unsigned differing = 0;
            for (int at = 0; at < 1024; ++at) differing += nibble(repeat, at) != nibble(sector, at);
            char message[64]{};
            std::snprintf(message, sizeof message, "  C lines%x first%d shift%d n%u %uus", lines,
                          first, shift, differing, us);
            report.note(message);
            if (first >= 0) {
                char want[17]{}, got[17]{};
                for (int k = 0; k < 16; ++k) {
                    const int w = nibble(sector, first - 4 + k), g = nibble(repeat, first - 4 + k);
                    want[k] = w < 0 ? '.' : "0123456789abcdef"[w];
                    got[k] = g < 0 ? '.' : "0123456789abcdef"[g];
                }
                std::snprintf(message, sizeof message, "  want %s", want);
                report.note(message);
                std::snprintf(message, sizeof message, "  got  %s", got);
                report.note(message);
            }
            // The first corrupted block in full, and the reference, for
            // offline comparison.
            static bool dumped = true;    // set false to dump a block
            if (!dumped) {
                dumped = true;
                for (const auto* block : {&sector, &repeat}) {
                    for (unsigned row = 0; row < 32; ++row) {
                        char hex[40]{};
                        std::snprintf(hex, 4, "%c%02u", block == &sector ? 'R' : 'B', row);
                        for (unsigned k = 0; k < 16; ++k)
                            std::snprintf(hex + 3 + 2 * k, 3, "%02x",
                                          static_cast<unsigned>((*block)[row * 16 + k]));
                        report.note(hex);
                    }
                }
            }
        }
        mm_pico_sdio_failure_t failure{};
        mm_pico_sdio_last_failure(&failure);
        if (failure.reason < 7) ++reasons[failure.reason];
        // The first two failures of each kind: where the PIO program was,
        // the lines' levels, and the card's own account by CMD13 at the
        // same clock -- its state and whether it saw a bad command.
        if ((failure.reason == 1 || failure.reason == 5) && reasons[failure.reason] <= 2) {
            const auto& sdio = static_cast<const mm::sdcard::SdioCard&>(card);
            std::array<std::uint32_t, 1> word{};
            const auto probe = mm::mcu::sdio_command(0, 13, sdio.relative_card_address() << 16,
                                                     mm::mcu::SdioResponse::Short, word);
            char message[64]{};
            std::snprintf(message, sizeof message, "  %c pc%u p%02x CMD13 %s %08lx st%lu",
                          failure.reason == 1 ? 'T' : 'C', failure.pc, failure.pins,
                          probe == mm::mcu::Status::Ok ? "OK" : "FAIL",
                          static_cast<unsigned long>(word[0]),
                          static_cast<unsigned long>((word[0] >> 9) & 15u));
            report.note(message);
        }
    }
    // The plain command path at the same clock: CMD13 on the selected card.
    unsigned status_ok = 0, status_timeouts = 0;
    {
        mm::fs::BlockGeometry geometry{};
        unsigned long ignored = 0;
        const auto& sdio = static_cast<const mm::sdcard::SdioCard&>(card);
        if (card.geometry(geometry) == Status::Ok &&
            mm::mcu::sdio_clock(0, hz, ignored) == mm::mcu::Status::Ok) {
            for (unsigned i = 0; i < count; ++i) {
                std::array<std::uint32_t, 1> word{};
                const auto probe = mm::mcu::sdio_command(
                    0, 13, sdio.relative_card_address() << 16, mm::mcu::SdioResponse::Short,
                    word);
                if (probe == mm::mcu::Status::Ok) ++status_ok;
                else if (probe == mm::mcu::Status::Timeout) ++status_timeouts;
            }
        }
    }
    char message[64]{};
    std::snprintf(message, sizeof message, "   CMD13 x%u: %u OK %u T, good read %uus", count,
                  status_ok, status_timeouts, good_us);
    report.note(message);
    char rate[8]{};
    if (actual >= 1'000'000)
        std::snprintf(rate, sizeof rate, "%2luM", (actual + 500'000) / 1'000'000);
    else
        std::snprintf(rate, sizeof rate, "%3luk", (actual + 500) / 1'000);
    std::snprintf(message, sizeof message, "%s %s %2u/%u T%u F%u E%u D%u C%u X%u L%u", pads,
                  rate, ok, count, reasons[1], reasons[2], reasons[3],
                  reasons[4], reasons[5], mismatched, lost);
    report.note(message);
}

void test_raw(mm::fs::BlockDevice& card, const mm::fs::BlockGeometry& geometry,
              Report& report) {
    auto& sdio = static_cast<mm::sdcard::SdioCard&>(card);
    report.info("DRIVER HZ", sdio.data_clock_hz());
    auto status = read_sector(card, 0, sector);
    report.check("LBA0 at DRIVER HZ", status);
    if (status != Status::Ok) why(report);

    // A reference LBA0 at 1 MHz, then every clock the PIO makes from an
    // integer divider at 150 MHz, and 4 MHz, under three pad settings. Each
    // line: successes of 32, then T response timeout, F response frame, E R1
    // error bits, D data timeout, C data CRC, X data mismatch, L identify lost.
    {
        mm::fs::BlockGeometry ignored{};
        unsigned long actual = 0;
        bool reference = false;
        for (unsigned i = 0; i < 8 && !reference; ++i)
            reference = card.geometry(ignored) == Status::Ok &&
                        mm::mcu::sdio_clock(0, 1'000'000, actual) == mm::mcu::Status::Ok &&
                        read_sector(card, 0, sector) == Status::Ok;
        // The reference must be the same block three times over.
        for (unsigned i = 0; i < 3 && reference; ++i)
            reference = read_sector(card, 0, repeat) == Status::Ok && repeat == sector;
        report.check("REFERENCE LBA0", reference);
        if (reference) {
            // Slow-slew and stronger settings made no difference; the
            // board's own setting only.
            static constexpr Pads pads[] = {{"F8/F4 ", 1, 2, 1, 1}};
            static constexpr unsigned long sweep[] = {25'000'000, 12'500'000, 8'333'334,
                                                      6'250'000,  4'000'000,  1'000'000,
                                                      400'000,    100'000};
            for (const auto& setting : pads) {
                apply(setting);
                for (const auto hz : sweep) try_clock(card, setting.name, hz, report);
            }
            apply(pads[0]);
        }
    }

    // Let the driver settle on its own clock: each failure halves it. The
    // sweep left the bus at its own last clock; put back the driver's.
    if (sdio.data_clock_hz() != 0) {
        unsigned long actual = 0;
        static_cast<void>(mm::mcu::sdio_clock(0, sdio.data_clock_hz(), actual));
    }
    status = Status::Timeout;
    for (unsigned attempt = 0; attempt < 6 && status != Status::Ok; ++attempt) {
        mm::fs::BlockGeometry ignored{};
        status = card.geometry(ignored);
        if (status == Status::Ok) status = read_sector(card, 0, sector);
        if (status != Status::Ok) why(report);
    }
    report.check("DRIVER SETTLED", status);
    report.info("SETTLED HZ", sdio.data_clock_hz());
    if (status != Status::Ok) return;

    bool stable = true;
    for (unsigned i = 0; i < 8; ++i) {
        status = read_sector(card, 0, repeat);
        if (status != Status::Ok) {
            report.check("LBA0 REPEAT", status);
            report.info("REPEAT FAILED AT", i);
            why(report);
            return;
        }
        if (sector != repeat) stable = false;
    }
    report.check("LBA0 STABLE x8", stable);

    status = read_sector(card, 0, std::span<std::byte>{unaligned}.subspan(1, 512));
    report.check("UNALIGNED READ", status);
    if (status == Status::Ok)
        report.check("UNALIGNED MATCH", std::memcmp(sector.data(), unaligned.data() + 1,
                                                    sector.size()) == 0);

    if (geometry.count >= 6) {
        bool singles_ok = true;
        for (unsigned i = 0; i < 4; ++i) {
            status = read_sector(card, 1 + i,
                                 std::span<std::byte>{separate}.subspan(i * 512, 512));
            if (status != Status::Ok) {
                singles_ok = false;
                break;
            }
        }
        report.check("4 SINGLE READS", singles_ok ? Status::Ok : status);
        status = read_sector(card, 1, together);
        report.check("4-BLOCK READ", status);
        if (singles_ok && status == Status::Ok)
            report.check("MULTIBLOCK MATCH", separate == together);
    }

    status = read_sector(card, geometry.count - 1, repeat);
    report.check("LAST LBA", status);
    status = read_sector(card, geometry.count, repeat);
    report.check("BOUNDARY REJECT", status == Status::BadArgument);
}

void test_fat(mm::fs::BlockDevice& card, Report& report) {
    auto status = mm::fs::fat::mount("/sd", card, {.read_only = true});
    report.check("FAT MOUNT", status);
    if (status != Status::Ok) {
        why(report);
        return;
    }

    mm::fs::Space space{};
    status = mm::fs::space("/sd", space);
    report.check("FAT SPACE", status);
    if (status == Status::Ok) {
        report.info("TOTAL BYTES", space.total);
        report.info("FREE BYTES", space.free);
    }

    mm::fs::Directory directory;
    status = mm::fs::open_directory("/sd", directory);
    report.check("ROOT OPEN", status);
    std::array<char, mm::fs::max_path + 1> first_file{};
    unsigned files = 0;
    unsigned directories = 0;
    if (status == Status::Ok) {
        std::array<char, mm::fs::max_name + 1> entry{};
        for (unsigned i = 0; i < 64; ++i) {
            std::size_t length = 0;
            mm::fs::Stat stat{};
            bool done = false;
            status = directory.next(entry, length, stat, done);
            if (status != Status::Ok || done) break;
            if (stat.kind == mm::fs::Kind::Directory) {
                ++directories;
            } else {
                ++files;
                if (first_file[0] == 0 && length + 5 <= mm::fs::max_path) {
                    std::memcpy(first_file.data(), "/sd/", 4);
                    std::memcpy(first_file.data() + 4, entry.data(), length);
                    first_file[4 + length] = 0;
                }
            }
        }
        report.check("ROOT ENUMERATE", status);
        report.info("ROOT FILES <=64", files);
        report.info("ROOT DIRS <=64", directories);
        report.check("ROOT CLOSE", directory.close());
    }

    if (first_file[0] != 0) {
        mm::fs::Stat stat{};
        status = mm::fs::stat(first_file.data(), stat);
        report.check("FILE STAT", status);
        mm::fs::File file;
        status = mm::fs::open(first_file.data(), mm::fs::Access::Read,
                              mm::fs::Disposition::OpenExisting, file);
        report.check("FILE OPEN", status);
        if (status == Status::Ok) {
            std::array<std::byte, 64> one{};
            std::array<std::byte, 64> two{};
            std::size_t a = 0, b = 0;
            status = file.read(one, a);
            report.check("FILE READ", status);
            if (status == Status::Ok) {
                status = file.seek(0);
                report.check("FILE SEEK 0", status);
                if (status == Status::Ok) {
                    status = file.read(two, b);
                    report.check("FILE REREAD", status);
                    if (status == Status::Ok)
                        report.check("FILE MATCH", a == b && one == two);
                }
            }
            report.check("FILE CLOSE", file.close());
        }
    }
    report.check("FAT UNMOUNT", mm::fs::fat::unmount("/sd"));
}

}  // namespace

int main() {
    Report report{mm::display::selected_display()};
    auto& card = mm::sdcard::socket::card();
    mm::fs::BlockGeometry geometry{};
    const auto status = card.geometry(geometry);
    report.check("CARD IDENTIFY", status);
    if (status == Status::Ok) {
        report.check("512-BYTE SECTORS", geometry.size == 512 && geometry.count > 8);
        if (geometry.size == 512 && geometry.count > 8) {
            report.info("SECTOR COUNT", geometry.count);
            test_raw(card, geometry, report);
            test_fat(card, report);
        }
    }
    report.finish();
    for (;;) {
        report.dump_serial();
        (void)mm::mcu::delay_ms(2'000);
    }
}
