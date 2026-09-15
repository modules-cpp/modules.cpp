// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

[[nodiscard]] mm::mcu::Status error_status(
    int value, bool explicit_path = true, bool caller_field = false);
[[nodiscard]] mm::mcu::Status spi_configuration_status(
    const SpiEntry& entry, const mm::mcu::SpiConfiguration& value);
[[nodiscard]] mm::mcu::Status spi_readback_status(
    std::uint32_t actual, mm::mcu::BitOrder requested);
[[nodiscard]] mm::mcu::Status gpio_access_status(
    const std::optional<mm::mcu::Direction>& direction, bool write);

}

namespace {

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

private:
    Status gpio_value(unsigned int pin, bool write_value, bool& value) {
        Status status;
        const auto* configured = map(status);
        if (!configured) return status;
        if (pin >= configured->gpios.size()) return Status::Unsupported;
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
    std::vector<std::optional<mm::mcu::SpiConfiguration>> spi_configuration_;
    std::vector<std::optional<mm::mcu::I2cConfiguration>> i2c_configuration_;
};

LinuxPlatform linux_platform;
struct Register { Register(){mm::mcu::set_platform(linux_platform);} };
const Register registered;

}

namespace platform::linux::mcu_detail {

mm::mcu::Status error_status(int value, bool explicit_path,
                             bool caller_field) {
    return ::error_status(value, explicit_path, caller_field);
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
