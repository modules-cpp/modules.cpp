// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <linux/spi/spi.h>
#include <optional>
#include <sys/time.h>

import mm.mcu;
import mm.test;
import platform.linux.map;
import platform.linux.mcu;

namespace {

using mm::mcu::Status;
using mm::test::expect;

void errno_mapping() {
    using platform::linux::mcu_detail::error_status;
    expect(error_status(ENOENT, false) == Status::Unsupported,
           "auto-discovery ENOENT is Unsupported");
    expect(error_status(ENOENT, true) == Status::BadArgument,
           "explicit ENOENT is BadArgument");
    expect(error_status(EACCES) == Status::TransportError &&
               error_status(EPERM) == Status::TransportError,
           "permission errors are TransportError");
    expect(error_status(EBUSY) == Status::Busy &&
               error_status(ETIMEDOUT) == Status::Timeout,
           "busy and timeout retain their meanings");
    expect(error_status(EINVAL, true, true) == Status::BadArgument &&
               error_status(EINVAL) == Status::TransportError,
           "EINVAL is contextual to caller-controlled fields");
}

void spi_validation() {
    platform::linux::SpiEntry entry{};
    entry.clock_gpio = 10;
    entry.transmit_gpio = 11;
    entry.receive_gpio = 12;
    entry.max_speed = 10'000'000;
    mm::mcu::SpiConfiguration configuration{
        0, 10, 11, 12, 1'000'000, mm::mcu::SpiMode::Mode0,
        mm::mcu::BitOrder::MostSignificantFirst};

    using platform::linux::mcu_detail::spi_configuration_status;
    using platform::linux::mcu_detail::spi_readback_status;
    expect(spi_configuration_status(entry, configuration) == Status::Ok,
           "production SPI validation accepts matching pins and speed");
    configuration.clock_gpio = 9;
    expect(spi_configuration_status(entry, configuration) ==
               Status::BadArgument,
           "production SPI validation rejects a pin mismatch");
    configuration.clock_gpio = 10;
    entry.speed_fixed = true;
    expect(spi_configuration_status(entry, configuration) ==
               Status::BadArgument,
           "a fixed-speed controller requires its exact speed");

    expect(spi_readback_status(
               0, mm::mcu::BitOrder::MostSignificantFirst) ==
               Status::Unsupported,
           "production read-back requires SPI_NO_CS");
    expect(spi_readback_status(
               SPI_NO_CS, mm::mcu::BitOrder::LeastSignificantFirst) ==
               Status::Unsupported,
           "production read-back detects a refused bit order");
    expect(spi_readback_status(
               SPI_NO_CS | SPI_LSB_FIRST,
               mm::mcu::BitOrder::LeastSignificantFirst) == Status::Ok,
           "production read-back accepts the requested bit order");
}

void gpio_configuration_state() {
    using platform::linux::mcu_detail::gpio_access_status;
    expect(gpio_access_status({}, false) == Status::BadArgument,
           "an unconfigured GPIO cannot be read");
    expect(gpio_access_status(mm::mcu::Direction::In, true) ==
               Status::BadArgument,
           "an input GPIO cannot be written");
    expect(gpio_access_status(mm::mcu::Direction::Out, true) == Status::Ok,
           "a configured output GPIO can be written");
}

volatile std::sig_atomic_t alarms = 0;
void alarm_handler(int) { alarms = 1; }

void interrupted_delay() {
    const auto previous = std::signal(SIGALRM, &alarm_handler);
    itimerval timer{};
    timer.it_value.tv_usec = 10'000;
    ::setitimer(ITIMER_REAL, &timer, nullptr);
    const auto start = std::chrono::steady_clock::now();
    const auto status = mm::mcu::platform().delay_ms(50);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    timer = {};
    ::setitimer(ITIMER_REAL, &timer, nullptr);
    std::signal(SIGALRM, previous);

    expect(status == Status::Ok && alarms > 0,
           "production delay retries after an injected signal");
    expect(elapsed >= std::chrono::milliseconds(50),
           "production delay does not shorten the requested interval");
}

const mm::test::case_ cases[]{
    {"errno mapping", &errno_mapping},
    {"SPI validation and read-back", &spi_validation},
    {"GPIO configuration state", &gpio_configuration_state},
    {"interrupted monotonic delay", &interrupted_delay},
};
const mm::test::registrar reg{"platform.linux.mcu", cases};

}
