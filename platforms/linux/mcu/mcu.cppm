// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <climits>
#include <fcntl.h>
#include <limits>
#include <linux/gpio.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#include <optional>
#include <poll.h>
#include <span>
#include <string>
#include <sys/ioctl.h>
#include <string_view>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <vector>

export module platform.linux.mcu;

import mm.mcu;
import platform.linux.map;

export namespace platform::linux::mcu_detail {

class Descriptor {
public:
    explicit Descriptor(int value = -1) : value_(value) {}
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    Descriptor(Descriptor&& other) noexcept : value_(other.value_) {
        other.value_ = -1;
    }
    Descriptor& operator=(Descriptor&& other) noexcept {
        if (this == &other) return *this;
        if (value_ >= 0) ::close(value_);
        value_ = other.value_;
        other.value_ = -1;
        return *this;
    }
    ~Descriptor() {
        if (value_ >= 0) ::close(value_);
    }
    [[nodiscard]] int get() const { return value_; }

private:
    int value_;
};

// Who holds a pad, and one PWM claim's record. Here rather than in the
// provider class because a module unit may not instantiate a standard
// container over a TU-local type.
enum class PadOwner { None, Adc, Pwm };

struct PwmState {
    bool claimed = false;
    std::uint64_t requested = 0;
    std::uint64_t actual = 0;
    std::string directory;
    std::string chip;
    unsigned int channel = 0;
};

[[nodiscard]] mm::mcu::Status error_status(
    int value, bool explicit_path = true, bool caller_field = false);
[[nodiscard]] mm::mcu::Status edge_error_status(int value);
[[nodiscard]] mm::mcu::Status spi_configuration_status(
    const SpiEntry& entry, const mm::mcu::SpiConfiguration& value);
[[nodiscard]] mm::mcu::Status spi_readback_status(
    std::uint32_t actual, mm::mcu::BitOrder requested);
[[nodiscard]] mm::mcu::Status gpio_access_status(
    const std::optional<mm::mcu::Direction>& direction, bool write);
[[nodiscard]] mm::mcu::Status take_events(int descriptor, bool& pending);

// The analog facilities' pure parts. An IIO scale is millivolts per raw
// count as a decimal string; it is read into nanovolts per count so that
// the reference is integer arithmetic. A sysfs number is a decimal with a
// trailing newline.
[[nodiscard]] bool adc_scale_nanovolts(std::string_view text, std::uint64_t& nanovolts);
[[nodiscard]] unsigned int adc_reference_millivolts(std::uint64_t nanovolts_per_count,
                                                    unsigned int bits);
[[nodiscard]] bool sysfs_integer(std::string_view text, long long& value);

}

// A named, non-exported namespace rather than an unnamed one: Clang emits an
// interface unit's unnamed-namespace objects again in every importer, and a
// provider object must exist exactly once.
namespace platform::linux::mcu_provider {

using platform::linux::mcu_detail::Descriptor;
using Status = mm::mcu::Status;

Status error_status(int value, bool explicit_path = true,
                    bool caller_field = false) {
    if ((value == ENOENT || value == ENODEV) && !explicit_path)
        return Status::Unsupported;
    if ((value == ENOENT || value == ENODEV) && explicit_path)
        return Status::BadArgument;
    if (value == EACCES || value == EPERM) return Status::TransportError;
    if (value == EBUSY) return Status::Busy;
    if (value == ETIMEDOUT) return Status::Timeout;
    if (value == EINVAL && caller_field) return Status::BadArgument;
    return Status::TransportError;
}

const platform::linux::Map* map(Status& status) {
    const auto& result = platform::linux::resolve();
    if (result.status != platform::linux::MapStatus::Ok ||
        result.map == nullptr) {
        status = Status::BadArgument;
        return nullptr;
    }
    status = Status::Ok;
    return result.map;
}

speed_t baud_rate(unsigned long baud) {
    switch (baud) {
        case 50: return B50;
        case 75: return B75;
        case 110: return B110;
        case 300: return B300;
        case 600: return B600;
        case 1200: return B1200;
        case 2400: return B2400;
        case 4800: return B4800;
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default: return 0;
    }
}

class LinuxPlatform final : public mm::mcu::Platform {
public:
    ~LinuxPlatform() override {
        for (const int fd : gpio_lines_) {
            if (fd >= 0) ::close(fd);
        }
    }

    [[nodiscard]] mm::mcu::Board board() const override {
        Status status;
        const auto* configured = map(status);
        if (configured == nullptr) return {};
        if (!board_ready_) {
            gpio_inventory_.clear();
            gpio_inventory_.reserve(configured->gpios.size());
            for (std::size_t i = 0; i < configured->gpios.size(); ++i)
                gpio_inventory_.push_back(
                    {static_cast<unsigned int>(i), configured->gpios[i].name});
            board_ready_ = true;
        }
        std::optional<mm::mcu::Led> led;
        if (configured->led_gpio)
            led = mm::mcu::Led{configured->led_name.value_or("LED"),
                               *configured->led_gpio,
                               configured->led_active_high};
        return {configured->board_name, gpio_inventory_, led};
    }

    [[nodiscard]] Status gpio_configure(unsigned int pin,
                                        mm::mcu::Direction direction,
                                        mm::mcu::Pull pull) override {
        Status status;
        const auto* configured = map(status);
        if (!configured) return status;
        if (pin >= configured->gpios.size()) return Status::Unsupported;
        if (pin < gpio_watched_.size() && gpio_watched_[pin]) return Status::Busy;
        if (analog_holds(pin)) return Status::Busy;
        const auto& gpio = configured->gpios[pin];
        Descriptor chip(::open(gpio.chip.c_str(), O_RDONLY | O_CLOEXEC));
        if (chip.get() < 0) return error_status(errno);
        gpio_v2_line_request request{};
        request.offsets[0] = gpio.offset;
        request.num_lines = 1;
        std::strncpy(request.consumer, "modules.cpp",
                     sizeof(request.consumer) - 1);
        request.config.flags =
            direction == mm::mcu::Direction::Out ? GPIO_V2_LINE_FLAG_OUTPUT
                                                 : GPIO_V2_LINE_FLAG_INPUT;
        if (pull == mm::mcu::Pull::Up)
            request.config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
        if (pull == mm::mcu::Pull::Down)
            request.config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
        if (pull == mm::mcu::Pull::None)
            request.config.flags |= GPIO_V2_LINE_FLAG_BIAS_DISABLED;
        if (::ioctl(chip.get(), GPIO_V2_GET_LINE_IOCTL, &request) < 0)
            return error_status(errno, true, true);
        if (gpio_direction_.size() < configured->gpios.size())
            gpio_direction_.resize(configured->gpios.size());
        if (gpio_lines_.size() < configured->gpios.size())
            gpio_lines_.resize(configured->gpios.size(), -1);
        if (gpio_lines_[pin] >= 0) ::close(gpio_lines_[pin]);
        gpio_lines_[pin] = request.fd;
        gpio_direction_[pin] = direction;
        return Status::Ok;
    }

    [[nodiscard]] Status gpio_write(unsigned int pin, bool high) override {
        return gpio_value(pin, true, high);
    }

    [[nodiscard]] Status gpio_read(unsigned int pin, bool& high) override {
        return gpio_value(pin, false, high);
    }

    [[nodiscard]] Status gpio_watch(unsigned int pin, mm::mcu::Pull pull,
                                     mm::mcu::Edge edge) override {
        Status status;
        const auto* configured = map(status);
        if (!configured) return status;
        if (pin >= configured->gpios.size()) return Status::Unsupported;
        if (pin < gpio_watched_.size() && gpio_watched_[pin]) return Status::Busy;
        if (analog_holds(pin)) return Status::Busy;
        if (pull != mm::mcu::Pull::None && pull != mm::mcu::Pull::Up &&
            pull != mm::mcu::Pull::Down) return Status::BadArgument;
        gpio_v2_line_config config{};
        config.flags = GPIO_V2_LINE_FLAG_INPUT;
        if (pull == mm::mcu::Pull::Up)
            config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
        if (pull == mm::mcu::Pull::Down)
            config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
        if (pull == mm::mcu::Pull::None)
            config.flags |= GPIO_V2_LINE_FLAG_BIAS_DISABLED;
        if (edge == mm::mcu::Edge::Rising || edge == mm::mcu::Edge::Both)
            config.flags |= GPIO_V2_LINE_FLAG_EDGE_RISING;
        if (edge == mm::mcu::Edge::Falling || edge == mm::mcu::Edge::Both)
            config.flags |= GPIO_V2_LINE_FLAG_EDGE_FALLING;

        const bool held = pin < gpio_lines_.size() && gpio_lines_[pin] >= 0;
        int fd = -1;
        int old_flags = -1;
        if (held) {
            fd = gpio_lines_[pin];
            old_flags = ::fcntl(fd, F_GETFL);
            if (old_flags < 0) return error_status(errno);
            if (::fcntl(fd, F_SETFL, old_flags | O_NONBLOCK) < 0)
                return error_status(errno);
            if (::ioctl(fd, GPIO_V2_LINE_SET_CONFIG_IOCTL, &config) < 0) {
                const int cause = errno;
                if (::fcntl(fd, F_SETFL, old_flags) < 0) {
                    clear_gpio(pin);
                    return Status::TransportError;
                }
                return platform::linux::mcu_detail::edge_error_status(cause);
            }
        } else {
            const auto& gpio = configured->gpios[pin];
            Descriptor chip(::open(gpio.chip.c_str(), O_RDONLY | O_CLOEXEC));
            if (chip.get() < 0) return error_status(errno);
            gpio_v2_line_request request{};
            request.offsets[0] = gpio.offset;
            request.num_lines = 1;
            request.event_buffer_size = 64;
            std::strncpy(request.consumer, "modules.cpp",
                         sizeof(request.consumer) - 1);
            request.config = config;
            if (::ioctl(chip.get(), GPIO_V2_GET_LINE_IOCTL, &request) < 0)
                return platform::linux::mcu_detail::edge_error_status(errno);
            fd = request.fd;
            const int flags = ::fcntl(fd, F_GETFL);
            if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                const int cause = errno;
                ::close(fd);
                return error_status(cause);
            }
        }
        // The request is nonblocking before the first read. Discard events
        // caused by the takeover itself; bound the drain under a live stream.
        gpio_v2_line_event events[16];
        for (int i = 0; i < 4; ++i) {
            const auto count = ::read(fd, events, sizeof(events));
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            if (count <= 0 || count % sizeof(events[0]) != 0) {
                if (held) clear_gpio(pin); else ::close(fd);
                return Status::TransportError;
            }
        }
        if (gpio_lines_.size() < configured->gpios.size())
            gpio_lines_.resize(configured->gpios.size(), -1);
        if (gpio_direction_.size() < configured->gpios.size())
            gpio_direction_.resize(configured->gpios.size());
        if (gpio_watched_.size() < configured->gpios.size())
            gpio_watched_.resize(configured->gpios.size(), false);
        gpio_lines_[pin] = fd;
        gpio_direction_[pin] = mm::mcu::Direction::In;
        gpio_watched_[pin] = true;
        return Status::Ok;
    }

    [[nodiscard]] Status gpio_take(unsigned int pin, bool& pending) override {
        Status status;
        const auto* configured = map(status);
        if (!configured) return status;
        if (pin >= configured->gpios.size()) return Status::Unsupported;
        if (pin >= gpio_watched_.size() || !gpio_watched_[pin])
            return Status::BadArgument;
        return platform::linux::mcu_detail::take_events(gpio_lines_[pin], pending);
    }

    [[nodiscard]] Status gpio_unwatch(unsigned int pin) override {
        Status status;
        const auto* configured = map(status);
        if (!configured) return status;
        if (pin >= configured->gpios.size()) return Status::Unsupported;
        if (pin >= gpio_watched_.size() || !gpio_watched_[pin])
            return Status::BadArgument;
        clear_gpio(pin);
        return Status::Ok;
    }

    [[nodiscard]] Status gpio_wait(unsigned int pin, unsigned long timeout_ms,
                                    bool& pending) override {
        Status status;
        const auto* configured = map(status);
        if (!configured) return status;
        if (pin >= configured->gpios.size()) return Status::Unsupported;
        if (pin >= gpio_watched_.size() || !gpio_watched_[pin])
            return Status::BadArgument;
        using Clock = std::chrono::steady_clock;
        const auto now = Clock::now();
        const auto available = Clock::time_point::max() - now;
        const auto maximum_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(available).count();
        const auto deadline = timeout_ms >= static_cast<unsigned long>(maximum_ms)
            ? Clock::time_point::max()
            : now + std::chrono::milliseconds(static_cast<long long>(timeout_ms));
        for (;;) {
            const auto taken = gpio_take(pin, pending);
            if (taken != Status::Ok || pending) return taken;
            const auto left = deadline - Clock::now();
            if (left <= Clock::duration::zero()) return Status::Ok;
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(left);
            const int slice = ms.count() >= INT_MAX ? INT_MAX
                            : static_cast<int>(ms.count() + (ms < left ? 1 : 0));
            pollfd item{gpio_lines_[pin], POLLIN, 0};
            const int ready = ::poll(&item, 1, slice);
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0) return error_status(errno);
            if (ready > 0 && (item.revents & (POLLERR | POLLHUP | POLLNVAL)))
                return Status::TransportError;
        }
    }

    [[nodiscard]] Status spi_configure(
        const mm::mcu::SpiConfiguration& value) override {
        Status status;
        const auto* configured = map(status);
        if (!configured) return status;
        if (value.instance >= configured->spis.size())
            return Status::Unsupported;
        const auto& entry = configured->spis[value.instance];
        const auto validation =
            platform::linux::mcu_detail::spi_configuration_status(entry, value);
        if (validation != Status::Ok) return validation;
        Descriptor fd(::open(entry.path.c_str(), O_RDWR | O_CLOEXEC));
        if (fd.get() < 0) return error_status(errno);
        std::uint32_t mode =
            static_cast<std::uint32_t>(value.mode) | SPI_NO_CS;
        std::uint8_t lsb =
            value.bit_order == mm::mcu::BitOrder::LeastSignificantFirst;
        std::uint32_t speed = static_cast<std::uint32_t>(value.baud);
        if (::ioctl(fd.get(), SPI_IOC_WR_MODE32, &mode) < 0 ||
            ::ioctl(fd.get(), SPI_IOC_WR_LSB_FIRST, &lsb) < 0 ||
            (!entry.speed_fixed &&
             ::ioctl(fd.get(), SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0))
            return error_status(errno, true, true);
        std::uint32_t actual = 0;
        if (::ioctl(fd.get(), SPI_IOC_RD_MODE32, &actual) < 0)
            return error_status(errno);
        const auto readback = platform::linux::mcu_detail::spi_readback_status(
            actual, value.bit_order);
        if (readback != Status::Ok) return readback;
        if (spi_configuration_.size() <= value.instance)
            spi_configuration_.resize(value.instance + 1);
        spi_configuration_[value.instance] = value;
        return Status::Ok;
    }

    [[nodiscard]] Status spi_write(unsigned int instance,
                                   std::span<const std::byte> data) override {
        return spi_exchange(instance, data, {});
    }

    [[nodiscard]] Status spi_transfer(unsigned int instance,
                                      std::span<const std::byte> transmit,
                                      std::span<std::byte> receive) override {
        if (transmit.size() != receive.size()) return Status::BadArgument;
        return spi_exchange(instance, transmit, receive);
    }

    [[nodiscard]] Status i2c_configure(
        const mm::mcu::I2cConfiguration& value) override {
        Status status;
        const auto* configured = map(status);
        if (!configured) return status;
        if (value.instance >= configured->i2cs.size())
            return Status::Unsupported;
        const auto& entry = configured->i2cs[value.instance];
        if (value.data_gpio != entry.data_gpio ||
            value.clock_gpio != entry.clock_gpio)
            return Status::BadArgument;
        if (!entry.baud_fixed) return Status::Unsupported;
        if (value.baud != entry.baud) return Status::BadArgument;
        if (i2c_configuration_.size() <= value.instance)
            i2c_configuration_.resize(value.instance + 1);
        i2c_configuration_[value.instance] = value;
        return Status::Ok;
    }

    [[nodiscard]] Status i2c_write(unsigned int instance, unsigned int address,
                                   std::span<const std::byte> data) override {
        return i2c_transaction(instance, address, data, {});
    }

    [[nodiscard]] Status i2c_read(unsigned int instance, unsigned int address,
                                  std::span<std::byte> data) override {
        return i2c_transaction(instance, address, {}, data);
    }

    [[nodiscard]] Status i2c_write_read(unsigned int instance,
                                        unsigned int address,
                                        std::span<const std::byte> command,
                                        std::span<std::byte> data) override {
        return i2c_transaction(instance, address, command, data);
    }

    [[nodiscard]] Status uart_write(unsigned int instance,
                                    const char* text) override {
        if (text == nullptr) return Status::BadArgument;
        Status status;
        const auto* configured = map(status);
        if (!configured) return status;
        if (instance >= configured->uarts.size()) return Status::Unsupported;
        const auto& entry = configured->uarts[instance];
        const auto baud = baud_rate(entry.baud);
        if (baud == 0) return Status::BadArgument;
        Descriptor fd(::open(entry.path.c_str(),
                             O_WRONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC));
        if (fd.get() < 0) return error_status(errno);
        termios tty{};
        if (::tcgetattr(fd.get(), &tty) < 0) return error_status(errno);
        ::cfmakeraw(&tty);
        ::cfsetispeed(&tty, baud);
        ::cfsetospeed(&tty, baud);
        tty.c_cflag = (tty.c_cflag & ~CSIZE) |
                      (entry.data_bits == 5   ? CS5
                       : entry.data_bits == 6 ? CS6
                       : entry.data_bits == 7 ? CS7
                                              : CS8);
        if (entry.parity == 0) {
            tty.c_cflag &= ~PARENB;
        } else {
            tty.c_cflag |= PARENB;
            if (entry.parity == 2)
                tty.c_cflag |= PARODD;
            else
                tty.c_cflag &= ~PARODD;
        }
        if (entry.stop_bits == 2)
            tty.c_cflag |= CSTOPB;
        else
            tty.c_cflag &= ~CSTOPB;
        if (::tcsetattr(fd.get(), TCSANOW, &tty) < 0)
            return error_status(errno, true, true);
        const auto size = std::strlen(text);
        std::size_t done = 0;
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds(entry.write_deadline_ms);
        while (done < size) {
            const auto count = ::write(fd.get(), text + done, size - done);
            if (count > 0) {
                done += count;
                continue;
            }
            if (count < 0 && errno == EINTR) continue;
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                const auto left =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        deadline - std::chrono::steady_clock::now())
                        .count();
                if (left <= 0) return Status::Timeout;
                pollfd p{fd.get(), POLLOUT, 0};
                int ready = 0;
                do {
                    ready = ::poll(&p, 1, static_cast<int>(left));
                } while (ready < 0 && errno == EINTR);
                if (ready == 0) return Status::Timeout;
                if (ready < 0) return error_status(errno);
                continue;
            }
            return error_status(errno);
        }
        return Status::Ok;
    }

    [[nodiscard]] Status delay_ms(unsigned long milliseconds) override {
        if (milliseconds == 0) return Status::Ok;
        const auto seconds = milliseconds / 1000;
        if (seconds >
            static_cast<unsigned long>(std::numeric_limits<time_t>::max()))
            return Status::BadArgument;
        timespec request{
            static_cast<time_t>(seconds),
            static_cast<long>((milliseconds % 1000) * 1'000'000UL)};
        while (true) {
            timespec remaining{};
            const int result =
                ::clock_nanosleep(CLOCK_MONOTONIC, 0, &request, &remaining);
            if (result == 0) return Status::Ok;
            if (result == EINTR) {
                request = remaining;
                continue;
            }
            return error_status(result, false);
        }
    }

    [[nodiscard]] Status ticks_ms(unsigned long& ticks) override {
        timespec value{};
        if (::clock_gettime(CLOCK_MONOTONIC, &value) < 0)
            return error_status(errno, false);
        ticks = static_cast<unsigned long>(value.tv_sec) * 1000UL +
                static_cast<unsigned long>(value.tv_nsec / 1'000'000L);
        return Status::Ok;
    }

    // The inventories are the map's entries; the reference is the map's
    // when it says one, else the IIO scale times the channel's range when
    // the device can be found, else zero.
    [[nodiscard]] mm::mcu::AdcDescription adc_description() const override {
        Status status;
        const auto* configured = map(status);
        if (configured == nullptr) return {};
        if (!adc_described_) {
            adc_inventory_.clear();
            adc_inventory_.reserve(configured->adcs.size());
            std::string device;
            const bool resolved = resolve_adc_device(*configured, device) == Status::Ok;
            for (std::size_t i = 0; i < configured->adcs.size(); ++i) {
                const auto& entry = configured->adcs[i];
                unsigned int reference = entry.reference_millivolts;
                if (reference == 0 && resolved) {
                    std::string text;
                    std::uint64_t nanovolts = 0;
                    if ((read_sysfs(device + "/in_voltage" + std::to_string(entry.channel) +
                                        "_scale", text) == Status::Ok ||
                         read_sysfs(device + "/in_voltage_scale", text) == Status::Ok) &&
                        platform::linux::mcu_detail::adc_scale_nanovolts(text, nanovolts))
                        reference = platform::linux::mcu_detail::adc_reference_millivolts(
                            nanovolts, entry.bits);
                }
                adc_inventory_.push_back({static_cast<unsigned int>(i), entry.name,
                                          entry.gpio, entry.bits, reference});
            }
            adc_described_ = true;
        }
        return {adc_inventory_};
    }

    [[nodiscard]] Status adc_configure(unsigned int channel) override {
        Status status;
        const auto* configured = map(status);
        if (!configured) return status;
        if (channel >= configured->adcs.size()) return Status::Unsupported;
        if (channel < adc_claimed_.size() && adc_claimed_[channel]) return Status::Ok;
        const auto& entry = configured->adcs[channel];
        if (entry.gpio) {
            const auto claim = analog_claim_status(*entry.gpio);
            if (claim != Status::Ok) return claim;
        }
        std::string device;
        const auto resolved = resolve_adc_device(*configured, device);
        if (resolved != Status::Ok) return resolved;
        const auto raw = device + "/in_voltage" + std::to_string(entry.channel) + "_raw";
        std::string text;
        const auto readable = read_sysfs(raw, text, true);
        if (readable != Status::Ok) return readable;
        // Validated; now the takeover, and the record last.
        if (entry.gpio) take_pad(*entry.gpio, Owner::Adc);
        if (adc_claimed_.size() < configured->adcs.size())
            adc_claimed_.resize(configured->adcs.size(), false);
        adc_claimed_[channel] = true;
        return Status::Ok;
    }

    [[nodiscard]] Status adc_read(unsigned int channel, unsigned int& count) override {
        Status status;
        const auto* configured = map(status);
        if (!configured) return status;
        if (channel >= configured->adcs.size()) return Status::Unsupported;
        if (channel >= adc_claimed_.size() || !adc_claimed_[channel])
            return Status::BadArgument;
        const auto& entry = configured->adcs[channel];
        std::string text;
        const auto readable = read_sysfs(
            adc_device_ + "/in_voltage" + std::to_string(entry.channel) + "_raw", text, true);
        if (readable != Status::Ok) return readable;
        long long value = 0;
        if (!platform::linux::mcu_detail::sysfs_integer(text, value) || value < 0 ||
            static_cast<unsigned long long>(value) > ((1ull << entry.bits) - 1))
            return Status::TransportError;
        count = static_cast<unsigned int>(value);
        return Status::Ok;
    }

    [[nodiscard]] Status adc_release(unsigned int channel) override {
        Status status;
        const auto* configured = map(status);
        if (!configured) return status;
        if (channel >= configured->adcs.size()) return Status::Unsupported;
        if (channel >= adc_claimed_.size() || !adc_claimed_[channel]) return Status::Ok;
        const auto& entry = configured->adcs[channel];
        if (entry.gpio) release_pad(*entry.gpio);
        adc_claimed_[channel] = false;
        return Status::Ok;
    }

    // sysfs publishes no period limits, so both are zero; an entry without a
    // group is its own, numbered past any the map could name.
    [[nodiscard]] mm::mcu::PwmDescription pwm_description() const override {
        Status status;
        const auto* configured = map(status);
        if (configured == nullptr) return {};
        if (!pwm_described_) {
            pwm_inventory_.clear();
            pwm_inventory_.reserve(configured->pwms.size());
            for (std::size_t i = 0; i < configured->pwms.size(); ++i) {
                const auto& entry = configured->pwms[i];
                pwm_inventory_.push_back({static_cast<unsigned int>(i), entry.name, entry.gpio,
                                          group_of(*configured, i), 0, 0, 0});
            }
            pwm_described_ = true;
        }
        return {pwm_inventory_};
    }

    // Validated in the contract's order -- inventory, period, this output's
    // claim, the group, the pad -- then sysfs in the kernel's order: export,
    // duty zero, period, enable. A step that fails undoes the claim's own
    // export and nothing anyone else's.
    [[nodiscard]] Status pwm_configure(unsigned int output, std::uint64_t period_ns) override {
        Status status;
        const auto* configured = map(status);
        if (!configured) return status;
        if (output >= configured->pwms.size()) return Status::Unsupported;
        if (period_ns == 0) return Status::BadArgument;
        if (pwm_state_.size() < configured->pwms.size())
            pwm_state_.resize(configured->pwms.size());
        auto& state = pwm_state_[output];
        if (state.claimed)
            return state.requested == period_ns ? Status::Ok : Status::Busy;
        const auto& entry = configured->pwms[output];
        const auto group = group_of(*configured, output);
        for (std::size_t i = 0; i < configured->pwms.size(); ++i) {
            if (i != output && pwm_state_[i].claimed && group_of(*configured, i) == group &&
                pwm_state_[i].requested != period_ns)
                return Status::Busy;
        }
        if (entry.gpio) {
            const auto claim = analog_claim_status(*entry.gpio);
            if (claim != Status::Ok) return claim;
        }

        const std::string chip = "/sys/class/pwm/pwmchip" + std::to_string(entry.chip);
        struct stat info{};
        if (::stat(chip.c_str(), &info) < 0) return error_status(errno, true);
        const std::string directory = chip + "/pwm" + std::to_string(entry.channel);
        // An output someone else exported is theirs; this provider never
        // unexports what it did not export.
        if (::stat(directory.c_str(), &info) == 0) return Status::Busy;
        auto step = write_sysfs(chip + "/export", std::to_string(entry.channel), true);
        if (step != Status::Ok) return step;
        // The kernel creates the directory after export returns; wait a
        // bounded time for its period attribute.
        bool present = false;
        for (unsigned int attempt = 0; attempt < 50 && !present; ++attempt) {
            present = ::stat((directory + "/period").c_str(), &info) == 0;
            if (!present) {
                timespec pause{0, 10'000'000};
                ::nanosleep(&pause, nullptr);
            }
        }
        if (!present) {
            (void)write_sysfs(chip + "/unexport", std::to_string(entry.channel), false);
            return Status::TransportError;
        }
        step = write_sysfs(directory + "/duty_cycle", "0", true);
        if (step == Status::Ok)
            step = write_sysfs(directory + "/period", std::to_string(period_ns), true);
        if (step == Status::Ok) step = write_sysfs(directory + "/enable", "1", true);
        std::string text;
        long long actual = 0;
        if (step == Status::Ok) step = read_sysfs(directory + "/period", text, true);
        if (step == Status::Ok &&
            (!platform::linux::mcu_detail::sysfs_integer(text, actual) || actual <= 0))
            step = Status::TransportError;
        if (step != Status::Ok) {
            (void)write_sysfs(directory + "/enable", "0", false);
            (void)write_sysfs(chip + "/unexport", std::to_string(entry.channel), false);
            return step;
        }
        if (entry.gpio) take_pad(*entry.gpio, Owner::Pwm);
        state.claimed = true;
        state.requested = period_ns;
        state.actual = static_cast<std::uint64_t>(actual);
        state.directory = directory;
        state.chip = chip;
        state.channel = entry.channel;
        return Status::Ok;
    }

    [[nodiscard]] Status pwm_period(unsigned int output, std::uint64_t& actual_ns) override {
        Status status;
        const auto* configured = map(status);
        if (!configured) return status;
        if (output >= configured->pwms.size()) return Status::Unsupported;
        if (output >= pwm_state_.size() || !pwm_state_[output].claimed)
            return Status::BadArgument;
        actual_ns = pwm_state_[output].actual;
        return Status::Ok;
    }

    [[nodiscard]] Status pwm_write(unsigned int output, std::uint64_t duty_ns) override {
        Status status;
        const auto* configured = map(status);
        if (!configured) return status;
        if (output >= configured->pwms.size()) return Status::Unsupported;
        if (output >= pwm_state_.size() || !pwm_state_[output].claimed)
            return Status::BadArgument;
        const auto& state = pwm_state_[output];
        if (duty_ns > state.actual) return Status::BadArgument;
        return write_sysfs(state.directory + "/duty_cycle", std::to_string(duty_ns), true);
    }

    [[nodiscard]] Status pwm_release(unsigned int output) override {
        Status status;
        const auto* configured = map(status);
        if (!configured) return status;
        if (output >= configured->pwms.size()) return Status::Unsupported;
        if (output >= pwm_state_.size() || !pwm_state_[output].claimed) return Status::Ok;
        auto& state = pwm_state_[output];
        auto step = write_sysfs(state.directory + "/enable", "0", false);
        const auto unexported =
            write_sysfs(state.chip + "/unexport", std::to_string(state.channel), false);
        if (step == Status::Ok) step = unexported;
        const auto& entry = configured->pwms[output];
        if (entry.gpio) release_pad(*entry.gpio);
        state = {};
        return step;
    }

private:
    using Owner = platform::linux::mcu_detail::PadOwner;
    using PwmState = platform::linux::mcu_detail::PwmState;
    static constexpr unsigned int own_group = 0x10000;

    [[nodiscard]] static unsigned int group_of(const platform::linux::Map& configured,
                                               std::size_t index) {
        const auto& entry = configured.pwms[index];
        return entry.group ? *entry.group : own_group + static_cast<unsigned int>(index);
    }

    [[nodiscard]] bool analog_holds(unsigned int pin) const {
        return pin < pad_owner_.size() && pad_owner_[pin] != Owner::None;
    }

    // A watched pad and an analog claim answer Busy; a plain GPIO line
    // request is closed by the takeover, which take_pad performs.
    [[nodiscard]] Status analog_claim_status(unsigned int pin) const {
        if (pin < gpio_watched_.size() && gpio_watched_[pin]) return Status::Busy;
        if (analog_holds(pin)) return Status::Busy;
        return Status::Ok;
    }

    void take_pad(unsigned int pin, Owner owner) {
        clear_gpio(pin);
        if (pad_owner_.size() <= pin) pad_owner_.resize(pin + 1, Owner::None);
        pad_owner_[pin] = owner;
    }

    void release_pad(unsigned int pin) {
        if (pin < pad_owner_.size()) pad_owner_[pin] = Owner::None;
    }

    // The selected IIO device: the map's index or name, or the first device
    // that has the first entry's raw channel. Cached once found.
    Status resolve_adc_device(const platform::linux::Map& configured,
                              std::string& device) const {
        if (adc_resolved_) {
            device = adc_device_;
            return Status::Ok;
        }
        if (configured.adcs.empty()) return Status::Unsupported;
        const auto& selector = configured.adc_device;
        const bool explicit_selection = selector.kind != platform::linux::SelectorKind::Auto;
        const auto sysfs = [](unsigned int index) {
            return "/sys/bus/iio/devices/iio:device" + std::to_string(index);
        };
        const auto has_raw = [&](const std::string& candidate) {
            struct stat info{};
            return ::stat((candidate + "/in_voltage" +
                           std::to_string(configured.adcs.front().channel) + "_raw").c_str(),
                          &info) == 0;
        };
        std::optional<unsigned int> selected;
        if (selector.kind == platform::linux::SelectorKind::Index) {
            selected = selector.index;
        } else if (selector.kind == platform::linux::SelectorKind::Name) {
            for (unsigned int i = 0; i < 64 && !selected; ++i) {
                std::string name;
                if (read_sysfs(sysfs(i) + "/name", name) == Status::Ok && name == selector.name)
                    selected = i;
            }
            if (!selected) return Status::BadArgument;
        } else {
            for (unsigned int i = 0; i < 64 && !selected; ++i)
                if (has_raw(sysfs(i))) selected = i;
            if (!selected) return Status::Unsupported;
        }
        const auto candidate = sysfs(*selected);
        struct stat info{};
        if (::stat(candidate.c_str(), &info) < 0)
            return error_status(errno, explicit_selection);
        adc_device_ = candidate;
        adc_resolved_ = true;
        device = candidate;
        return Status::Ok;
    }

    // One line of a sysfs attribute, with the errno mapped as the map's
    // other paths are: a missing file is BadArgument where the caller named
    // it and Unsupported where it was discovered.
    static Status read_sysfs(const std::string& path, std::string& text,
                             bool explicit_path = false) {
        Descriptor fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
        if (fd.get() < 0) return error_status(errno, explicit_path);
        char buffer[64];
        const auto count = ::read(fd.get(), buffer, sizeof(buffer) - 1);
        if (count < 0) return error_status(errno, explicit_path);
        text.assign(buffer, static_cast<std::size_t>(count));
        while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
        return Status::Ok;
    }

    static Status write_sysfs(const std::string& path, const std::string& text,
                              bool caller_field) {
        Descriptor fd(::open(path.c_str(), O_WRONLY | O_CLOEXEC));
        if (fd.get() < 0) return error_status(errno, true);
        const auto count = ::write(fd.get(), text.data(), text.size());
        if (count < 0) return error_status(errno, true, caller_field);
        if (static_cast<std::size_t>(count) != text.size()) return Status::TransportError;
        return Status::Ok;
    }

    void clear_gpio(unsigned int pin) {
        if (pin < gpio_lines_.size() && gpio_lines_[pin] >= 0) {
            ::close(gpio_lines_[pin]);
            gpio_lines_[pin] = -1;
        }
        if (pin < gpio_direction_.size()) gpio_direction_[pin].reset();
        if (pin < gpio_watched_.size()) gpio_watched_[pin] = false;
    }

    Status gpio_value(unsigned int pin, bool write_value, bool& value) {
        Status status;
        const auto* configured = map(status);
        if (!configured) return status;
        if (pin >= configured->gpios.size()) return Status::Unsupported;
        if (analog_holds(pin)) return Status::Busy;
        const auto access = platform::linux::mcu_detail::gpio_access_status(
            pin < gpio_direction_.size() ? gpio_direction_[pin]
                                         : std::optional<mm::mcu::Direction>{},
            write_value);
        if (access != Status::Ok) return access;
        if (pin >= gpio_lines_.size() || gpio_lines_[pin] < 0)
            return Status::BadArgument;
        gpio_v2_line_values values{};
        values.mask = 1;
        if (write_value) {
            values.bits = value ? 1 : 0;
            if (::ioctl(gpio_lines_[pin], GPIO_V2_LINE_SET_VALUES_IOCTL,
                        &values) < 0)
                return error_status(errno);
        } else {
            if (::ioctl(gpio_lines_[pin], GPIO_V2_LINE_GET_VALUES_IOCTL,
                        &values) < 0)
                return error_status(errno);
            value = (values.bits & 1) != 0;
        }
        return Status::Ok;
    }

    Status spi_exchange(unsigned int instance,
                        std::span<const std::byte> transmit,
                        std::span<std::byte> receive) {
        Status status;
        const auto* configured = map(status);
        if (!configured) return status;
        if (instance >= configured->spis.size()) return Status::Unsupported;
        const auto& entry = configured->spis[instance];
        mm::mcu::SpiConfiguration default_configuration{
            instance,
            entry.clock_gpio,
            entry.transmit_gpio,
            entry.receive_gpio,
            entry.max_speed,
            static_cast<mm::mcu::SpiMode>(entry.mode),
            entry.least_significant_first
                ? mm::mcu::BitOrder::LeastSignificantFirst
                : mm::mcu::BitOrder::MostSignificantFirst};
        const auto& configuration =
            instance < spi_configuration_.size() && spi_configuration_[instance]
                ? *spi_configuration_[instance]
                : default_configuration;
        Descriptor fd(::open(entry.path.c_str(), O_RDWR | O_CLOEXEC));
        if (fd.get() < 0) return error_status(errno);
        std::uint32_t mode =
            static_cast<std::uint32_t>(configuration.mode) | SPI_NO_CS;
        std::uint8_t lsb = configuration.bit_order ==
                           mm::mcu::BitOrder::LeastSignificantFirst;
        std::uint32_t speed = configuration.baud;
        if (::ioctl(fd.get(), SPI_IOC_WR_MODE32, &mode) < 0 ||
            ::ioctl(fd.get(), SPI_IOC_WR_LSB_FIRST, &lsb) < 0 ||
            (!entry.speed_fixed &&
             ::ioctl(fd.get(), SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0))
            return error_status(errno);
        std::uint32_t actual = 0;
        if (::ioctl(fd.get(), SPI_IOC_RD_MODE32, &actual) < 0)
            return error_status(errno);
        const auto readback = platform::linux::mcu_detail::spi_readback_status(
            actual, configuration.bit_order);
        if (readback != Status::Ok) return readback;
        spi_ioc_transfer transfer{};
        transfer.tx_buf = reinterpret_cast<std::uintptr_t>(transmit.data());
        transfer.rx_buf = reinterpret_cast<std::uintptr_t>(receive.data());
        transfer.len = transmit.size();
        transfer.speed_hz = speed;
        if (::ioctl(fd.get(), SPI_IOC_MESSAGE(1), &transfer) < 0)
            return error_status(errno);
        return Status::Ok;
    }

    Status i2c_transaction(unsigned int instance, unsigned int address,
                           std::span<const std::byte> out,
                           std::span<std::byte> in) {
        if (address > 0x7f || (!out.size() && !in.size()))
            return Status::BadArgument;
        Status status;
        const auto* configured = map(status);
        if (!configured) return status;
        if (instance >= configured->i2cs.size()) return Status::Unsupported;
        if (instance >= i2c_configuration_.size() ||
            !i2c_configuration_[instance])
            return Status::BadArgument;
        const auto path =
            "/dev/i2c-" + std::to_string(configured->i2cs[instance].adapter);
        Descriptor fd(::open(path.c_str(), O_RDWR | O_CLOEXEC));
        if (fd.get() < 0) return error_status(errno);
        i2c_msg messages[2]{};
        unsigned int count = 0;
        if (!out.empty()) {
            messages[count].addr = address;
            messages[count].len = out.size();
            messages[count].buf = reinterpret_cast<__u8*>(
                const_cast<std::byte*>(out.data()));
            ++count;
        }
        if (!in.empty()) {
            messages[count].addr = address;
            messages[count].flags = I2C_M_RD;
            messages[count].len = in.size();
            messages[count].buf = reinterpret_cast<__u8*>(in.data());
            ++count;
        }
        i2c_rdwr_ioctl_data data{messages, count};
        if (::ioctl(fd.get(), I2C_RDWR, &data) < 0) return error_status(errno);
        return Status::Ok;
    }

    mutable bool board_ready_ = false;
    mutable std::vector<mm::mcu::Gpio> gpio_inventory_;
    std::vector<int> gpio_lines_;
    std::vector<std::optional<mm::mcu::Direction>> gpio_direction_;
    std::vector<bool> gpio_watched_;
    std::vector<std::optional<mm::mcu::SpiConfiguration>> spi_configuration_;
    std::vector<std::optional<mm::mcu::I2cConfiguration>> i2c_configuration_;
    mutable bool adc_described_ = false;
    mutable std::vector<mm::mcu::AdcChannel> adc_inventory_;
    mutable bool adc_resolved_ = false;
    mutable std::string adc_device_;
    std::vector<bool> adc_claimed_;
    mutable bool pwm_described_ = false;
    mutable std::vector<mm::mcu::PwmOutput> pwm_inventory_;
    std::vector<PwmState> pwm_state_;
    std::vector<Owner> pad_owner_;
};

LinuxPlatform linux_platform;
struct Register { Register(){mm::mcu::set_platform(linux_platform);} };
const Register registered;

}

namespace platform::linux::mcu_detail {

// "0.805664062" is millivolts per count; the answer is nanovolts per count,
// the sixth fraction digit kept and the seventh rounded. No sign, no
// exponent: IIO writes neither.
bool adc_scale_nanovolts(std::string_view text, std::uint64_t& nanovolts) {
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.remove_suffix(1);
    if (text.empty()) return false;
    const auto point = text.find('.');
    const auto whole = text.substr(0, point);
    if (whole.empty()) return false;
    std::uint64_t millivolts = 0;
    const auto parsed = std::from_chars(whole.data(), whole.data() + whole.size(), millivolts);
    if (parsed.ec != std::errc{} || parsed.ptr != whole.data() + whole.size()) return false;
    if (millivolts > 1'000'000'000) return false;
    std::uint64_t fraction = 0;
    unsigned int digits = 0;
    bool round_up = false;
    if (point != std::string_view::npos) {
        const auto rest = text.substr(point + 1);
        if (rest.empty()) return false;
        for (const char c : rest) {
            if (c < '0' || c > '9') return false;
            if (digits < 6) {
                fraction = fraction * 10 + static_cast<unsigned int>(c - '0');
                ++digits;
            } else if (digits == 6) {
                round_up = c >= '5';
                ++digits;
            }
        }
    }
    while (digits < 6) {
        fraction *= 10;
        ++digits;
    }
    nanovolts = millivolts * 1'000'000 + fraction + (round_up ? 1 : 0);
    return true;
}

// scale times the channel's full-scale count, rounded to a millivolt; zero
// for a width outside [1, 31] or a product past 64 bits.
unsigned int adc_reference_millivolts(std::uint64_t nanovolts_per_count, unsigned int bits) {
    if (bits < 1 || bits > 31 || nanovolts_per_count == 0) return 0;
    const std::uint64_t full_scale = (std::uint64_t{1} << bits) - 1;
    if (nanovolts_per_count > std::numeric_limits<std::uint64_t>::max() / full_scale) return 0;
    const std::uint64_t nanovolts = nanovolts_per_count * full_scale;
    const std::uint64_t millivolts = (nanovolts + 500'000) / 1'000'000;
    return millivolts > 0xffff'ffffu ? 0 : static_cast<unsigned int>(millivolts);
}

bool sysfs_integer(std::string_view text, long long& value) {
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.remove_suffix(1);
    if (text.empty()) return false;
    long long parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false;
    value = parsed;
    return true;
}

mm::mcu::Status take_events(int descriptor, bool& pending) {
    gpio_v2_line_event events[16];
    const auto count = ::read(descriptor, events, sizeof(events));
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        pending = false;
        return mm::mcu::Status::Ok;
    }
    if (count <= 0 || count % sizeof(events[0]) != 0)
        return mm::mcu::Status::TransportError;
    pending = true;
    return mm::mcu::Status::Ok;
}

mm::mcu::Status error_status(int value, bool explicit_path,
                             bool caller_field) {
    return mcu_provider::error_status(value, explicit_path, caller_field);
}

mm::mcu::Status edge_error_status(int value) {
    if (value == ENXIO || value == EOPNOTSUPP)
        return mm::mcu::Status::Unsupported;
    return mcu_provider::error_status(value, true, true);
}

mm::mcu::Status spi_configuration_status(
    const SpiEntry& entry, const mm::mcu::SpiConfiguration& value) {
    if (value.clock_gpio != entry.clock_gpio ||
        value.transmit_gpio != entry.transmit_gpio ||
        value.receive_gpio != entry.receive_gpio || value.baud == 0 ||
        value.baud > entry.max_speed ||
        (entry.speed_fixed && value.baud != entry.max_speed))
        return mm::mcu::Status::BadArgument;
    return mm::mcu::Status::Ok;
}

mm::mcu::Status spi_readback_status(std::uint32_t actual,
                                    mm::mcu::BitOrder requested) {
    if ((actual & SPI_NO_CS) == 0) return mm::mcu::Status::Unsupported;
    const bool actual_lsb = (actual & SPI_LSB_FIRST) != 0;
    const bool requested_lsb =
        requested == mm::mcu::BitOrder::LeastSignificantFirst;
    return actual_lsb == requested_lsb ? mm::mcu::Status::Ok :
                                        mm::mcu::Status::Unsupported;
}

mm::mcu::Status gpio_access_status(
    const std::optional<mm::mcu::Direction>& direction, bool write) {
    if (!direction) return mm::mcu::Status::BadArgument;
    if (write && *direction != mm::mcu::Direction::Out)
        return mm::mcu::Status::BadArgument;
    return mm::mcu::Status::Ok;
}

}
