// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <array>
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <optional>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

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
    using platform::linux::mcu_detail::edge_error_status;
    expect(error_status(ENXIO) == Status::TransportError &&
               error_status(EOPNOTSUPP) == Status::TransportError &&
               edge_error_status(ENXIO) == Status::Unsupported &&
               edge_error_status(EOPNOTSUPP) == Status::Unsupported,
           "unsupported edge ioctls do not change other transport mapping");
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

void gpio_event_read_bound() {
    int ends[2]{};
    expect(::pipe(ends) == 0, "event fixture opens a pipe");
    const int flags = ::fcntl(ends[0], F_GETFL);
    expect(flags >= 0 && ::fcntl(ends[0], F_SETFL, flags | O_NONBLOCK) == 0,
           "event fixture has nonblocking reads");
    std::array<gpio_v2_line_event, 40> events{};
    expect(::write(ends[1], events.data(), sizeof(events)) == sizeof(events),
           "fixture queues forty whole records");
    using platform::linux::mcu_detail::take_events;
    bool pending = false;
    expect(take_events(ends[0], pending) == Status::Ok && pending,
           "first take consumes at most sixteen records");
    int queued = 0;
    expect(::ioctl(ends[0], FIONREAD, &queued) == 0 &&
               queued == 24 * sizeof(events[0]),
           "twenty-four records remain");
    expect(take_events(ends[0], pending) == Status::Ok && pending &&
               ::ioctl(ends[0], FIONREAD, &queued) == 0 &&
               queued == 8 * sizeof(events[0]),
           "second take leaves eight records");
    expect(take_events(ends[0], pending) == Status::Ok && pending &&
               take_events(ends[0], pending) == Status::Ok && !pending,
           "third take drains the queue and fourth sees EAGAIN");
    pending = true;
    ::close(ends[1]);
    expect(take_events(ends[0], pending) == Status::TransportError && pending,
           "zero-byte read preserves output");
    ::close(ends[0]);

    expect(::pipe(ends) == 0, "partial-record fixture opens a pipe");
    expect(::write(ends[1], events.data(), 1) == 1,
           "partial-record fixture writes one byte");
    pending = false;
    expect(take_events(ends[0], pending) == Status::TransportError && !pending &&
               take_events(ends[1], pending) == Status::TransportError && !pending,
           "partial record and EBADF preserve output");
    ::close(ends[0]);
    ::close(ends[1]);
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
    {"GPIO event read bound", &gpio_event_read_bound},
    {"errno mapping", &errno_mapping},
    {"SPI validation and read-back", &spi_validation},
    {"GPIO configuration state", &gpio_configuration_state},
    {"interrupted monotonic delay", &interrupted_delay},
};
const mm::test::registrar reg{"platform.linux.mcu", cases};

}
