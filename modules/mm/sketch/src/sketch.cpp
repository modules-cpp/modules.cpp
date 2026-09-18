// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

module mm.sketch;

import mm.mcu;
import mm.stdio;

namespace mm::sketch {

namespace {

Status from(mm::mcu::Status s) {
    switch (s) {
        case mm::mcu::Status::Ok: return Status::Ok;
        case mm::mcu::Status::BadArgument: return Status::BadArgument;
        case mm::mcu::Status::Unsupported: return Status::Unsupported;
        case mm::mcu::Status::Busy: return Status::Busy;
        case mm::mcu::Status::Timeout: return Status::Timeout;
        case mm::mcu::Status::TransportError: return Status::TransportError;
    }
    return Status::Unsupported;
}

Status from(mm::stdio::Status s) {
    switch (s) {
        case mm::stdio::Status::Ok: return Status::Ok;
        case mm::stdio::Status::BadArgument: return Status::BadArgument;
        case mm::stdio::Status::Unsupported: return Status::Unsupported;
        case mm::stdio::Status::NotInitialized: return Status::NotInitialized;
        case mm::stdio::Status::Busy: return Status::Busy;
        case mm::stdio::Status::Timeout: return Status::Timeout;
        case mm::stdio::Status::TransportError: return Status::TransportError;
    }
    return Status::Unsupported;
}

static Status latched_error_ = Status::Ok;
static const char* latched_call_ = "";

void record_failure(Status s, const char* call) {
    if (s != Status::Ok && latched_error_ == Status::Ok) {
        latched_error_ = s;
        latched_call_ = call;
    }
}

static bool exit_requested_ = false;
static int exit_code_ = 0;

static mm::stdio::Status console_init_status_ = mm::stdio::Status::NotInitialized;

constexpr std::size_t ring_capacity = 64;
static std::byte ring_buffer_[ring_capacity];
static std::size_t ring_head_ = 0;
static std::size_t ring_tail_ = 0;
static std::size_t ring_count_ = 0;

void fill_ring() {
    if (ring_count_ >= ring_capacity) return;
    auto& console = mm::stdio::selected_console();
    std::byte temp[ring_capacity];
    const std::size_t space = ring_capacity - ring_count_;
    std::size_t read_count = 0;
    const auto status = console.read(std::span{temp, space}, read_count);
    if (status == mm::stdio::Status::Ok && read_count > 0) {
        for (std::size_t i = 0; i < read_count; ++i) {
            ring_buffer_[ring_head_] = temp[i];
            ring_head_ = (ring_head_ + 1) % ring_capacity;
            ++ring_count_;
        }
    }
}

static bool dispatching_ = false;

} // namespace

Status lastError() {
    return latched_error_;
}

const char* lastCall() {
    return latched_call_;
}

void clearError() {
    latched_error_ = Status::Ok;
    latched_call_ = "";
}

void requestExit(int code) {
    exit_requested_ = true;
    exit_code_ = code;
}

bool exitRequested() {
    return exit_requested_;
}

int exitCode() {
    return exit_code_;
}

void dispatch() {
    if (dispatching_) return;
    dispatching_ = true;

    fill_ring();

    dispatching_ = false;
}

int run(Setup setup, Loop loop) {
    exit_requested_ = false;
    exit_code_ = 0;

    auto& console = mm::stdio::selected_console();
    console_init_status_ = console.initialize();

    if (setup) {
        setup();
    }

    while (!exit_requested_) {
        if (loop) {
            loop();
        }
        dispatch();
    }

    return exit_code_;
}

bool pinMode(unsigned int pin, Mode mode) {
    mm::mcu::Direction dir = mm::mcu::Direction::In;
    mm::mcu::Pull pull = mm::mcu::Pull::None;
    switch (mode) {
        case Mode::Input:
            dir = mm::mcu::Direction::In; pull = mm::mcu::Pull::None; break;
        case Mode::Output:
            dir = mm::mcu::Direction::Out; pull = mm::mcu::Pull::None; break;
        case Mode::InputPullup:
            dir = mm::mcu::Direction::In; pull = mm::mcu::Pull::Up; break;
        case Mode::InputPulldown:
            dir = mm::mcu::Direction::In; pull = mm::mcu::Pull::Down; break;
    }
    const auto status = mm::mcu::gpio_configure(pin, dir, pull);
    if (status != mm::mcu::Status::Ok) {
        record_failure(from(status), "pinMode");
        return false;
    }
    return true;
}

bool digitalWrite(unsigned int pin, Level level) {
    const auto status = mm::mcu::gpio_write(pin, level == Level::High);
    if (status != mm::mcu::Status::Ok) {
        record_failure(from(status), "digitalWrite");
        return false;
    }
    return true;
}

Level digitalRead(unsigned int pin) {
    bool high = false;
    const auto status = mm::mcu::gpio_read(pin, high);
    if (status != mm::mcu::Status::Ok) {
        record_failure(from(status), "digitalRead");
        return Level::Low;
    }
    return high ? Level::High : Level::Low;
}

bool hasBuiltinLed() {
    return mm::mcu::board().led.has_value();
}

bool pinMode(Led, Mode mode) {
    const auto desc = mm::mcu::board();
    if (!desc.led) {
        record_failure(Status::Unsupported, "pinMode");
        return false;
    }
    return pinMode(desc.led->gpio, mode);
}

bool digitalWrite(Led, Level level) {
    const auto desc = mm::mcu::board();
    if (!desc.led) {
        record_failure(Status::Unsupported, "digitalWrite");
        return false;
    }
    return digitalWrite(desc.led->gpio, level);
}

Level digitalRead(Led) {
    const auto desc = mm::mcu::board();
    if (!desc.led) {
        record_failure(Status::Unsupported, "digitalRead");
        return Level::Low;
    }
    return digitalRead(desc.led->gpio);
}

bool ledOn() {
    const auto desc = mm::mcu::board();
    if (!desc.led) {
        record_failure(Status::Unsupported, "ledOn");
        return false;
    }
    const Level level = desc.led->active_high ? Level::High : Level::Low;
    return digitalWrite(desc.led->gpio, level);
}

bool ledOff() {
    const auto desc = mm::mcu::board();
    if (!desc.led) {
        record_failure(Status::Unsupported, "ledOff");
        return false;
    }
    const Level level = desc.led->active_high ? Level::Low : Level::High;
    return digitalWrite(desc.led->gpio, level);
}

bool delay(unsigned long ms) {
    if (exit_requested_) return false;
    if (ms == 0) {
        dispatch();
        return !exit_requested_;
    }
    while (ms > 0) {
        if (exit_requested_) return false;
        const unsigned long slice = ms > 10 ? 10 : ms;
        const auto status = mm::mcu::delay_ms(slice);
        if (status != mm::mcu::Status::Ok) {
            record_failure(from(status), "delay");
            return false;
        }
        ms -= slice;
        dispatch();
        if (exit_requested_) return false;
    }
    return true;
}

unsigned long millis() {
    unsigned long ticks = 0;
    const auto status = mm::mcu::ticks_ms(ticks);
    if (status != mm::mcu::Status::Ok) {
        record_failure(from(status), "millis");
        return 0;
    }
    return ticks;
}

// Serial implementation
SerialPort Serial;

bool SerialPort::begin(unsigned long) {
    if (console_init_status_ == mm::stdio::Status::NotInitialized) {
        console_init_status_ = mm::stdio::selected_console().initialize();
    }
    if (console_init_status_ != mm::stdio::Status::Ok) {
        record_failure(from(console_init_status_), "Serial.begin");
        return false;
    }
    return true;
}

bool SerialPort::end() {
    return true;
}

std::size_t SerialPort::write(byte b) {
    return write(&b, 1);
}

std::size_t SerialPort::write(const byte* buffer, std::size_t size) {
    if (buffer == nullptr || size == 0) return 0;
    auto& console = mm::stdio::selected_console();
    std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(buffer), size};
    std::size_t offset = 0;
    constexpr unsigned int write_limit = 8;
    for (unsigned int attempt = 0; attempt < write_limit && offset < size; ++attempt) {
        std::size_t written = 0;
        const auto status = console.write(bytes.subspan(offset), written);
        if (status != mm::stdio::Status::Ok) {
            record_failure(from(status), "Serial.write");
            return offset;
        }
        offset += written;
    }
    if (offset < size) {
        record_failure(Status::Timeout, "Serial.write");
    }
    return offset;
}

std::size_t SerialPort::print(const char* s) {
    if (!s) return 0;
    return write(reinterpret_cast<const byte*>(s), std::string_view(s).size());
}

std::size_t SerialPort::print(char c) {
    const byte b = static_cast<byte>(c);
    return write(&b, 1);
}

std::size_t SerialPort::print(std::string_view s) {
    return write(reinterpret_cast<const byte*>(s.data()), s.size());
}

std::size_t SerialPort::println(const char* s) {
    std::size_t written = print(s);
    written += print("\r\n");
    return written;
}

std::size_t SerialPort::println(char c) {
    std::size_t written = print(c);
    written += print("\r\n");
    return written;
}

std::size_t SerialPort::println(std::string_view s) {
    std::size_t written = print(s);
    written += print("\r\n");
    return written;
}

std::size_t SerialPort::println() {
    return print("\r\n");
}

int SerialPort::available() {
    fill_ring();
    return static_cast<int>(ring_count_);
}

int SerialPort::read() {
    fill_ring();
    if (ring_count_ == 0) return -1;
    const int val = static_cast<unsigned char>(ring_buffer_[ring_tail_]);
    ring_tail_ = (ring_tail_ + 1) % ring_capacity;
    --ring_count_;
    return val;
}

int SerialPort::peek() {
    fill_ring();
    if (ring_count_ == 0) return -1;
    return static_cast<unsigned char>(ring_buffer_[ring_tail_]);
}

void SerialPort::flush() {
    (void)mm::stdio::selected_console().flush();
}

bool SerialPort::connected() {
    bool is_conn = false;
    const auto status = mm::stdio::selected_console().connected(is_conn);
    if (status != mm::stdio::Status::Ok) {
        record_failure(from(status), "Serial.connected");
        return false;
    }
    return is_conn;
}

} // namespace mm::sketch
