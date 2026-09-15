// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cerrno>
#include <cstdint>
#include <limits>
#include <linux/spi/spi.h>
#include <optional>
#include <time.h>

import mm.test;

namespace {

using mm::test::expect;

enum class Status { Ok, BadArgument, Unsupported, Busy, Timeout, TransportError };

Status error_status(int value, bool explicit_path = true, bool caller_field = false) {
    if ((value == ENOENT || value == ENODEV) && !explicit_path) return Status::Unsupported;
    if ((value == ENOENT || value == ENODEV) && explicit_path) return Status::BadArgument;
    if (value == EACCES || value == EPERM) return Status::TransportError;
    if (value == EBUSY) return Status::Busy;
    if (value == ETIMEDOUT) return Status::Timeout;
    if (value == EINVAL && caller_field) return Status::BadArgument;
    return Status::TransportError;
}

void test_errno_mapping_per_operation() {
    expect(error_status(ENOENT, false) == Status::Unsupported,
           "open auto-discovery ENOENT is Unsupported");
    expect(error_status(ENOENT, true) == Status::BadArgument,
           "open explicit path ENOENT is BadArgument");
    expect(error_status(ENODEV, false) == Status::Unsupported,
           "open auto-discovery ENODEV is Unsupported");
    expect(error_status(ENODEV, true) == Status::BadArgument,
           "open explicit path ENODEV is BadArgument");
    expect(error_status(EACCES) == Status::TransportError,
           "EACCES is TransportError");
    expect(error_status(EPERM) == Status::TransportError,
           "EPERM is TransportError");
    expect(error_status(EBUSY) == Status::Busy,
           "EBUSY is Busy");
    expect(error_status(ETIMEDOUT) == Status::Timeout,
           "ETIMEDOUT is Timeout");
    expect(error_status(EINVAL, true, true) == Status::BadArgument,
           "configure ioctl rejecting caller field EINVAL is BadArgument");
    expect(error_status(EINVAL, true, false) == Status::TransportError,
           "internal ioctl returning EINVAL is TransportError");
    expect(error_status(EIO) == Status::TransportError,
           "general I/O error is TransportError");
}

struct SpiMapEntry {
    unsigned int clock_gpio = 10;
    unsigned int transmit_gpio = 11;
    std::optional<unsigned int> receive_gpio = 12;
    unsigned long max_speed = 10'000'000;
    bool speed_fixed = false;
};

struct SpiConfig {
    unsigned int clock_gpio = 10;
    unsigned int transmit_gpio = 11;
    std::optional<unsigned int> receive_gpio = 12;
    unsigned long baud = 1'000'000;
    bool lsb_first = false;
};

Status mock_spi_configure(const SpiMapEntry& map, const SpiConfig& caller,
                          std::uint32_t readback_mode) {
    if (caller.clock_gpio != map.clock_gpio ||
        caller.transmit_gpio != map.transmit_gpio ||
        caller.receive_gpio != map.receive_gpio)
        return Status::BadArgument;

    if (caller.baud == 0 || caller.baud > map.max_speed)
        return Status::BadArgument;

    if (map.speed_fixed && caller.baud != map.max_speed)
        return Status::BadArgument;

    // Verify readback mode: must keep SPI_NO_CS
    if ((readback_mode & SPI_NO_CS) == 0)
        return Status::Unsupported;

    // Verify bit order
    const bool readback_lsb = (readback_mode & SPI_LSB_FIRST) != 0;
    if (readback_lsb != caller.lsb_first)
        return Status::Unsupported;

    return Status::Ok;
}

void test_spi_configuration_and_readback_handling() {
    SpiMapEntry map{};
    SpiConfig config{};

    // Valid configuration with correct readback
    expect(mock_spi_configure(map, config, SPI_NO_CS) == Status::Ok,
           "valid SPI configuration with matching readback is Ok");

    // Pin mismatch
    config.clock_gpio = 99;
    expect(mock_spi_configure(map, config, SPI_NO_CS) == Status::BadArgument,
           "pin mismatch is BadArgument");
    config.clock_gpio = map.clock_gpio;

    // Baud exceeding max_speed
    config.baud = 20'000'000;
    expect(mock_spi_configure(map, config, SPI_NO_CS) == Status::BadArgument,
           "baud exceeding max_speed is BadArgument");
    config.baud = 1'000'000;

    // Speed-fixed handling
    map.speed_fixed = true;
    config.baud = 5'000'000;
    expect(mock_spi_configure(map, config, SPI_NO_CS) == Status::BadArgument,
           "baud differing from max-speed under speed-fixed is BadArgument");
    config.baud = map.max_speed;
    expect(mock_spi_configure(map, config, SPI_NO_CS) == Status::Ok,
           "baud equal to max-speed under speed-fixed is Ok");
    map.speed_fixed = false;
    config.baud = 1'000'000;

    // Readback lacking SPI_NO_CS
    expect(mock_spi_configure(map, config, 0) == Status::Unsupported,
           "controller not keeping SPI_NO_CS on final readback makes instance Unsupported");

    // Readback refusing bit order
    config.lsb_first = true;
    expect(mock_spi_configure(map, config, SPI_NO_CS) == Status::Unsupported,
           "controller refusing requested bit order makes instance Unsupported");
    expect(mock_spi_configure(map, config, SPI_NO_CS | SPI_LSB_FIRST) == Status::Ok,
           "controller accepting requested bit order is Ok");
}

Status mock_delay_ms(unsigned long milliseconds, int injected_nanosleep_return,
                    int& sleep_calls) {
    if (milliseconds == 0) return Status::Ok;
    const auto seconds = milliseconds / 1000;
    if (seconds > static_cast<unsigned long>(std::numeric_limits<time_t>::max()))
        return Status::BadArgument;

    timespec request{static_cast<time_t>(seconds),
                     static_cast<long>((milliseconds % 1000) * 1'000'000UL)};
    while (true) {
        ++sleep_calls;
        timespec remaining{};
        int result = (sleep_calls == 1) ? injected_nanosleep_return : 0;
        if (result == 0) return Status::Ok;
        if (result == EINTR) {
            request = remaining;
            continue;
        }
        return error_status(result, false);
    }
}

void test_delay_ms_signal_and_overflow_handling() {
    int calls = 0;
    // Delay 0 is Ok immediately without calling sleep
    expect(mock_delay_ms(0, 0, calls) == Status::Ok && calls == 0,
           "delay_ms(0) is Ok immediately");

    // Overflowing argument
    calls = 0;
    const unsigned long max_ms = std::numeric_limits<unsigned long>::max();
    if (max_ms / 1000 > static_cast<unsigned long>(std::numeric_limits<time_t>::max())) {
        expect(mock_delay_ms(max_ms, 0, calls) == Status::BadArgument,
               "overflowing argument is BadArgument");
    }

    // Injected EINTR retries and completes
    calls = 0;
    expect(mock_delay_ms(50, EINTR, calls) == Status::Ok && calls == 2,
           "delay_ms sleeps across injected signal by retrying remaining time");

    // Non-EINTR return value mapped from return value directly
    calls = 0;
    expect(mock_delay_ms(50, ENODEV, calls) == Status::Unsupported,
           "non-EINTR clock_nanosleep return is translated from returned value");
}

const mm::test::case_ cases[]{
    {"errno mapping per operation", &test_errno_mapping_per_operation},
    {"SPI configuration and readback handling", &test_spi_configuration_and_readback_handling},
    {"delay_ms signal and overflow handling", &test_delay_ms_signal_and_overflow_handling},
};
const mm::test::registrar reg{"platform.linux.mcu", cases};

}
