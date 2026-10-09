module;
#include <cstdint>

export module platform.rp2350_touch_lcd_154.rtc;

import mm.mcu;
import mm.rtc;

namespace {

bool valid_date(const mm::rtc::DateTime& time) {
    if (time.year < 1u || time.year > 9999u || time.month < 1u ||
        time.month > 12u || time.day < 1u || time.weekday > 6u ||
        time.hour > 23u || time.minute > 59u || time.second > 59u)
        return false;
    constexpr unsigned int month_days[12]{
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    unsigned int last = month_days[time.month - 1u];
    if (time.month == 2u && time.year % 4u == 0u &&
        (time.year % 100u != 0u || time.year % 400u == 0u)) ++last;
    return time.day <= last;
}

class UptimeClock final : public mm::rtc::Clock {
public:
    [[nodiscard]] mm::rtc::Status initialize() override {
        initialized_ = true;
        return mm::rtc::Status::Ok;
    }

    [[nodiscard]] mm::rtc::Status write(const mm::rtc::DateTime& value) override {
        if (!initialized_) return mm::rtc::Status::NotInitialized;
        if (!valid_date(value)) return mm::rtc::Status::BadArgument;
        unsigned long now = 0;
        if (mm::mcu::ticks_ms(now) != mm::mcu::Status::Ok)
            return mm::rtc::Status::TransportError;
        time_ = value;
        last_tick_ = static_cast<std::uint32_t>(now);
        remainder_ms_ = 0;
        set_ = true;
        return mm::rtc::Status::Ok;
    }

    [[nodiscard]] mm::rtc::Status read(mm::rtc::DateTime& value,
                                        bool& trusted) override {
        if (!initialized_) return mm::rtc::Status::NotInitialized;
        trusted = set_;
        if (!set_) {
            value = {};
            value.year = 2000;
            return mm::rtc::Status::Ok;
        }
        unsigned long now = 0;
        if (mm::mcu::ticks_ms(now) != mm::mcu::Status::Ok)
            return mm::rtc::Status::TransportError;
        const auto tick = static_cast<std::uint32_t>(now);
        const std::uint64_t elapsed =
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(tick - last_tick_)) +
            remainder_ms_;
        last_tick_ = tick;
        remainder_ms_ = static_cast<unsigned int>(elapsed % 1000u);
        advance(static_cast<unsigned int>(elapsed / 1000u));
        value = time_;
        return mm::rtc::Status::Ok;
    }

private:
    void advance(unsigned int seconds) {
        const std::uint64_t total = static_cast<std::uint64_t>(time_.hour) * 3600u +
            time_.minute * 60u + time_.second + seconds;
        unsigned int days = static_cast<unsigned int>(total / 86400u);
        const auto remainder = static_cast<unsigned int>(total % 86400u);
        time_.hour = remainder / 3600u;
        time_.minute = remainder / 60u % 60u;
        time_.second = remainder % 60u;
        while (days != 0u) {
            --days;
            ++time_.day;
            time_.weekday = (time_.weekday + 1u) % 7u;
            if (valid_date(time_)) continue;
            time_.day = 1;
            if (++time_.month > 12u) {
                time_.month = 1;
                ++time_.year;
            }
        }
    }

    mm::rtc::DateTime time_{};
    std::uint32_t last_tick_ = 0;
    unsigned int remainder_ms_ = 0;
    bool initialized_ = false;
    bool set_ = false;
};

UptimeClock clock;

struct Register {
    Register() { mm::rtc::set_clock(clock); }
};

const Register registered;

}  // namespace
