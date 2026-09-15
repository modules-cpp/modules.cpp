// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <array>
#include <cstddef>
#include <span>

module mm.rtc.pcf85063;

namespace mm::rtc::pcf85063 {

namespace {

constexpr unsigned int control_1 = 0;
constexpr unsigned int seconds = 4;

constexpr std::size_t calendar_bytes = 7;

// The part carries two year digits. Fixing the century here is a decision, not
// an oversight: a driver that wrapped 2100 into 2000 would report a plausible
// wrong answer, which is worse than refusing to store one.
constexpr unsigned int century = 2000;

// Bit seven of the seconds register is set when the oscillator has stopped
// since the clock was last written, which is exactly mm.rtc's trusted flag
// inverted. It rides along with the time rather than costing a transaction.
constexpr unsigned int oscillator_stopped = 0x80;

constexpr unsigned int seconds_mask = 0x7f;
constexpr unsigned int minutes_mask = 0x7f;
constexpr unsigned int hours_mask = 0x3f;
constexpr unsigned int days_mask = 0x3f;
constexpr unsigned int weekdays_mask = 0x07;
constexpr unsigned int months_mask = 0x1f;

[[nodiscard]] unsigned int from_bcd(unsigned int value) {
    return ((value >> 4) & 0x0f) * 10 + (value & 0x0f);
}

[[nodiscard]] unsigned int to_bcd(unsigned int value) {
    return ((value / 10) << 4) | (value % 10);
}

// Checked before anything reaches the part, so a rejected write leaves the
// clock as it was rather than half updated.
[[nodiscard]] bool plausible(const mm::rtc::DateTime& time) {
    return time.year >= century && time.year < century + 100 && time.month >= 1 &&
           time.month <= 12 && time.day >= 1 && time.day <= 31 && time.weekday <= 6 &&
           time.hour <= 23 && time.minute <= 59 && time.second <= 59;
}

}  // namespace

mm::rtc::Status Clock::from_mcu(mm::mcu::Status status) const {
    switch (status) {
        case mm::mcu::Status::Ok: return mm::rtc::Status::Ok;
        case mm::mcu::Status::BadArgument: return mm::rtc::Status::BadArgument;
        case mm::mcu::Status::Unsupported: return mm::rtc::Status::Unsupported;
        case mm::mcu::Status::Busy: return mm::rtc::Status::Busy;
        case mm::mcu::Status::Timeout: return mm::rtc::Status::Timeout;
        case mm::mcu::Status::TransportError: return mm::rtc::Status::TransportError;
    }
    return mm::rtc::Status::TransportError;
}

// The part needs no configuration to keep time: it has been counting since it
// last had power. Initialization proves it answers, and nothing more, because
// writing a control register here would disturb a clock that is already right.
mm::rtc::Status Clock::initialize() {
    auto status = from_mcu(mm::mcu::i2c_configure(wiring_.i2c));
    if (status != mm::rtc::Status::Ok) return status;

    const std::array command{static_cast<std::byte>(control_1)};
    std::array<std::byte, 1> control{};
    status = from_mcu(mm::mcu::i2c_write_read(wiring_.i2c.instance, wiring_.address,
                                              command, control));
    if (status != mm::rtc::Status::Ok) return status;

    ready_ = true;
    return mm::rtc::Status::Ok;
}

mm::rtc::Status Clock::read(mm::rtc::DateTime& time, bool& trusted) {
    if (!ready_) return mm::rtc::Status::NotInitialized;

    // One read of seven consecutive registers, so the fields belong to one
    // instant rather than straddling a tick between two transactions.
    const std::array command{static_cast<std::byte>(seconds)};
    std::array<std::byte, calendar_bytes> calendar{};
    const auto status = from_mcu(mm::mcu::i2c_write_read(
        wiring_.i2c.instance, wiring_.address, command, calendar));
    if (status != mm::rtc::Status::Ok) return status;

    const auto raw_seconds = static_cast<unsigned int>(calendar[0]);
    trusted = (raw_seconds & oscillator_stopped) == 0;

    time.second = from_bcd(raw_seconds & seconds_mask);
    time.minute = from_bcd(static_cast<unsigned int>(calendar[1]) & minutes_mask);
    time.hour = from_bcd(static_cast<unsigned int>(calendar[2]) & hours_mask);
    time.day = from_bcd(static_cast<unsigned int>(calendar[3]) & days_mask);
    time.weekday = from_bcd(static_cast<unsigned int>(calendar[4]) & weekdays_mask);
    time.month = from_bcd(static_cast<unsigned int>(calendar[5]) & months_mask);
    time.year = century + from_bcd(static_cast<unsigned int>(calendar[6]));
    return mm::rtc::Status::Ok;
}

mm::rtc::Status Clock::write(const mm::rtc::DateTime& time) {
    if (!ready_) return mm::rtc::Status::NotInitialized;
    if (!plausible(time)) return mm::rtc::Status::BadArgument;

    // The register address and all seven fields in one transfer, so the part's
    // address pointer walks the calendar without it being written in pieces.
    const std::array data{
        static_cast<std::byte>(seconds),
        // Writing the seconds register clears the oscillator-stop bit, which is
        // what makes the next reading trusted.
        static_cast<std::byte>(to_bcd(time.second) & seconds_mask),
        static_cast<std::byte>(to_bcd(time.minute) & minutes_mask),
        static_cast<std::byte>(to_bcd(time.hour) & hours_mask),
        static_cast<std::byte>(to_bcd(time.day) & days_mask),
        static_cast<std::byte>(to_bcd(time.weekday) & weekdays_mask),
        static_cast<std::byte>(to_bcd(time.month) & months_mask),
        static_cast<std::byte>(to_bcd(time.year - century)),
    };
    return from_mcu(mm::mcu::i2c_write(wiring_.i2c.instance, wiring_.address, data));
}

}  // namespace mm::rtc::pcf85063
