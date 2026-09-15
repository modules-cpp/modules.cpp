// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <linux/rtc.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

export module platform.linux.rtc;

import mm.rtc;
import platform.linux.map;

namespace {

using Status = mm::rtc::Status;

Status error_status(int value, bool explicit_path = true) {
    if ((value == ENOENT || value == ENODEV) && !explicit_path)
        return Status::Unsupported;
    if ((value == ENOENT || value == ENODEV) && explicit_path)
        return Status::BadArgument;
    if (value == EACCES || value == EPERM) return Status::TransportError;
    if (value == EBUSY) return Status::Busy;
    if (value == ETIMEDOUT) return Status::Timeout;
    return Status::TransportError;
}

bool leap(unsigned int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

bool valid(const mm::rtc::DateTime& value) {
    static constexpr unsigned int days[]{
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (value.year < 1900 || value.month < 1 || value.month > 12 ||
        value.hour > 23 || value.minute > 59 || value.second > 59)
        return false;
    auto limit = days[value.month - 1];
    if (value.month == 2 && leap(value.year)) ++limit;
    return value.day >= 1 && value.day <= limit;
}

bool convert(const mm::rtc::DateTime& value,
             platform::linux::RtcConvention convention,
             std::tm& normalized, time_t& epoch) {
    if (!valid(value)) return false;
    std::tm fields{};
    fields.tm_year = static_cast<int>(value.year) - 1900;
    fields.tm_mon = static_cast<int>(value.month) - 1;
    fields.tm_mday = value.day;
    fields.tm_hour = value.hour;
    fields.tm_min = value.minute;
    fields.tm_sec = value.second;

    if (convention == platform::linux::RtcConvention::Utc) {
        fields.tm_isdst = 0;
        epoch = ::timegm(&fields);
        if (epoch == static_cast<time_t>(-1)) return false;
        std::tm check{};
        ::gmtime_r(&epoch, &check);
        if (check.tm_year != fields.tm_year || check.tm_mon != fields.tm_mon ||
            check.tm_mday != fields.tm_mday || check.tm_hour != fields.tm_hour ||
            check.tm_min != fields.tm_min || check.tm_sec != fields.tm_sec)
            return false;
        normalized = check;
        return true;
    }

    // Local time convention:
    // Ambiguous hour resolves to the earlier offset (DST = 1).
    // A skipped hour is rejected as BadArgument.
    static constexpr int isdsts[]{1, 0, -1};
    for (const int isdst : isdsts) {
        std::tm try_fields = fields;
        try_fields.tm_isdst = isdst;
        const auto candidate_epoch = ::mktime(&try_fields);
        if (candidate_epoch == static_cast<time_t>(-1)) continue;
        std::tm check{};
        ::localtime_r(&candidate_epoch, &check);
        if (check.tm_year == fields.tm_year && check.tm_mon == fields.tm_mon &&
            check.tm_mday == fields.tm_mday && check.tm_hour == fields.tm_hour &&
            check.tm_min == fields.tm_min && check.tm_sec == fields.tm_sec) {
            normalized = check;
            epoch = candidate_epoch;
            return true;
        }
    }
    return false;
}

mm::rtc::DateTime public_time(const std::tm& value) {
    return {static_cast<unsigned int>(value.tm_year + 1900),
            static_cast<unsigned int>(value.tm_mon + 1),
            static_cast<unsigned int>(value.tm_mday),
            static_cast<unsigned int>(value.tm_wday),
            static_cast<unsigned int>(value.tm_hour),
            static_cast<unsigned int>(value.tm_min),
            static_cast<unsigned int>(value.tm_sec)};
}

class LinuxClock final : public mm::rtc::Clock {
public:
    [[nodiscard]] Status initialize() override {
        const auto& resolution = platform::linux::resolve();
        if (resolution.status != platform::linux::MapStatus::Ok ||
            resolution.map == nullptr)
            return Status::BadArgument;
        map_ = resolution.map;
        initialized_ = true;
        if (map_->rtc.path.empty()) return Status::Ok;
        const int fd = ::open(map_->rtc.path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) return error_status(errno);
        ::close(fd);
        return Status::Ok;
    }

    [[nodiscard]] Status read(mm::rtc::DateTime& value,
                              bool& trusted) override {
        if (!initialized_) return Status::NotInitialized;
        if (map_->rtc.path.empty()) return read_system(value, trusted);

        const int fd = ::open(map_->rtc.path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) return error_status(errno);

        rtc_time raw{};
        if (::ioctl(fd, RTC_RD_TIME, &raw) < 0) {
            const auto e = errno;
            ::close(fd);
            return error_status(e);
        }

        unsigned int voltage = 0;
        const bool has_voltage = (::ioctl(fd, RTC_VL_READ, &voltage) == 0);
        const int voltage_error = errno;
        ::close(fd);

        if (!has_voltage && voltage_error != EINVAL &&
            voltage_error != ENOTTY)
            return error_status(voltage_error);

        std::tm fields{};
        fields.tm_year = raw.tm_year;
        fields.tm_mon = raw.tm_mon;
        fields.tm_mday = raw.tm_mday;
        fields.tm_wday = raw.tm_wday;
        fields.tm_hour = raw.tm_hour;
        fields.tm_min = raw.tm_min;
        fields.tm_sec = raw.tm_sec;
        value = public_time(fields);

        const bool invalid =
            has_voltage && ((voltage & RTC_VL_DATA_INVALID) != 0);
        trusted = !invalid || (wrote_ && !clear_supported_);
        return Status::Ok;
    }

    [[nodiscard]] Status write(const mm::rtc::DateTime& value) override {
        if (!initialized_) return Status::NotInitialized;
        std::tm fields{};
        time_t epoch{};
        if (!convert(value, map_->rtc.convention, fields, epoch))
            return Status::BadArgument;

        if (map_->rtc.path.empty()) {
            if (!map_->rtc.allow_write) return Status::Unsupported;
            timespec time{epoch, 0};
            if (::clock_settime(CLOCK_REALTIME, &time) < 0)
                return error_status(errno, false);
            wrote_ = true;
            return Status::Ok;
        }

        const int fd = ::open(map_->rtc.path.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0) return error_status(errno);

        rtc_time raw{};
        raw.tm_year = fields.tm_year;
        raw.tm_mon = fields.tm_mon;
        raw.tm_mday = fields.tm_mday;
        raw.tm_wday = fields.tm_wday;
        raw.tm_hour = fields.tm_hour;
        raw.tm_min = fields.tm_min;
        raw.tm_sec = fields.tm_sec;
        if (::ioctl(fd, RTC_SET_TIME, &raw) < 0) {
            const auto e = errno;
            ::close(fd);
            return error_status(e);
        }

        if (::ioctl(fd, RTC_VL_CLR, nullptr) < 0) {
            const auto e = errno;
            if (e == EINVAL || e == ENOTTY) {
                clear_supported_ = false;
                wrote_ = true;
                ::close(fd);
                return Status::Ok;
            }
            ::close(fd);
            return error_status(e);
        }

        clear_supported_ = true;
        unsigned int voltage = 0;
        if (::ioctl(fd, RTC_VL_READ, &voltage) < 0) {
            const auto e = errno;
            ::close(fd);
            return error_status(e);
        }
        ::close(fd);
        if ((voltage & RTC_VL_DATA_INVALID) != 0) return Status::TransportError;
        wrote_ = true;
        return Status::Ok;
    }

private:
    Status read_system(mm::rtc::DateTime& value, bool& trusted) {
        timespec raw{};
        if (::clock_gettime(CLOCK_REALTIME, &raw) < 0)
            return error_status(errno, false);
        std::tm fields{};
        if (map_->rtc.convention == platform::linux::RtcConvention::Utc)
            ::gmtime_r(&raw.tv_sec, &fields);
        else
            ::localtime_r(&raw.tv_sec, &fields);
        value = public_time(fields);
        trusted = wrote_;
        return Status::Ok;
    }

    const platform::linux::Map* map_ = nullptr;
    bool initialized_ = false;
    bool wrote_ = false;
    bool clear_supported_ = true;
};

LinuxClock clock;
struct Register { Register() { mm::rtc::set_clock(clock); } };
const Register registered;

}
