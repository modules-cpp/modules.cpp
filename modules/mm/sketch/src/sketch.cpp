// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <random>
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
static const char* active_call_ = nullptr;

struct CallScope {
    const char* prev_;
    bool set_;
    explicit CallScope(const char* name) : prev_(active_call_), set_(false) {
        if (!active_call_) {
            active_call_ = name;
            set_ = true;
        }
    }
    ~CallScope() {
        if (set_) {
            active_call_ = prev_;
        }
    }
};

void record_failure(Status s, const char* default_call) {
    if (s != Status::Ok && latched_error_ == Status::Ok) {
        latched_error_ = s;
        latched_call_ = active_call_ ? active_call_ : default_call;
    }
}

uint64_t div128_64(uint64_t hi, uint64_t lo, uint64_t w) {
    if (w == 0) return 0;
    if (hi >= w) return UINT64_MAX;
    uint64_t rem = hi;
    uint64_t quot = 0;
    for (int i = 63; i >= 0; --i) {
        rem = (rem << 1) | ((lo >> i) & 1u);
        if (rem >= w) {
            rem -= w;
            quot |= (1ULL << i);
        }
    }
    return quot;
}

void mul64_64(uint64_t u, uint64_t v, uint64_t& hi, uint64_t& lo) {
    const uint64_t u1 = u >> 32;
    const uint64_t u0 = u & 0xFFFFFFFFULL;
    const uint64_t v1 = v >> 32;
    const uint64_t v0 = v & 0xFFFFFFFFULL;

    const uint64_t t0 = u0 * v0;
    const uint64_t t1 = u0 * v1;
    const uint64_t t2 = u1 * v0;
    const uint64_t t3 = u1 * v1;

    uint64_t mid = t1 + (t0 >> 32);
    uint64_t carry = (mid < t1) ? 1ULL : 0ULL;
    mid += t2;
    if (mid < t2) {
        carry += 1ULL;
    }

    hi = t3 + (mid >> 32) + (carry << 32);
    lo = (t0 & 0xFFFFFFFFULL) | (mid << 32);
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

static std::minstd_rand random_engine_{1};

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
        if (exit_requested_) {
            break;
        }
        dispatch();
    }

    const int code = exit_code_;
    exit_requested_ = false;
    exit_code_ = 0;
    return code;
}

bool pinMode(unsigned int pin, Mode mode) {
    CallScope scope{"pinMode"};
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
    CallScope scope{"digitalWrite"};
    const auto status = mm::mcu::gpio_write(pin, level == Level::High);
    if (status != mm::mcu::Status::Ok) {
        record_failure(from(status), "digitalWrite");
        return false;
    }
    return true;
}

Level digitalRead(unsigned int pin) {
    CallScope scope{"digitalRead"};
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
    CallScope scope{"pinMode"};
    const auto desc = mm::mcu::board();
    if (!desc.led) {
        record_failure(Status::Unsupported, "pinMode");
        return false;
    }
    return pinMode(desc.led->gpio, mode);
}

bool digitalWrite(Led, Level level) {
    CallScope scope{"digitalWrite"};
    const auto desc = mm::mcu::board();
    if (!desc.led) {
        record_failure(Status::Unsupported, "digitalWrite");
        return false;
    }
    return digitalWrite(desc.led->gpio, level);
}

Level digitalRead(Led) {
    CallScope scope{"digitalRead"};
    const auto desc = mm::mcu::board();
    if (!desc.led) {
        record_failure(Status::Unsupported, "digitalRead");
        return Level::Low;
    }
    return digitalRead(desc.led->gpio);
}

bool ledOn() {
    CallScope scope{"ledOn"};
    const auto desc = mm::mcu::board();
    if (!desc.led) {
        record_failure(Status::Unsupported, "ledOn");
        return false;
    }
    const Level level = desc.led->active_high ? Level::High : Level::Low;
    return digitalWrite(desc.led->gpio, level);
}

bool ledOff() {
    CallScope scope{"ledOff"};
    const auto desc = mm::mcu::board();
    if (!desc.led) {
        record_failure(Status::Unsupported, "ledOff");
        return false;
    }
    const Level level = desc.led->active_high ? Level::Low : Level::High;
    return digitalWrite(desc.led->gpio, level);
}

bool delay(unsigned long ms) {
    CallScope scope{"delay"};
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
    CallScope scope{"millis"};
    unsigned long ticks = 0;
    const auto status = mm::mcu::ticks_ms(ticks);
    if (status != mm::mcu::Status::Ok) {
        record_failure(from(status), "millis");
        return 0;
    }
    return ticks;
}

byte shiftIn(unsigned int data_pin, unsigned int clock_pin, BitOrder bit_order) {
    CallScope scope{"shiftIn"};
    byte value = 0;
    for (unsigned int i = 0; i < 8; ++i) {
        digitalWrite(clock_pin, HIGH);
        const Level bit_val = digitalRead(data_pin);
        if (bit_val == HIGH) {
            if (bit_order == BitOrder::LsbFirst) {
                value = static_cast<byte>(value | (1u << i));
            } else {
                value = static_cast<byte>(value | (1u << (7 - i)));
            }
        }
        digitalWrite(clock_pin, LOW);
    }
    return value;
}

void shiftOut(unsigned int data_pin, unsigned int clock_pin, BitOrder bit_order, byte val) {
    CallScope scope{"shiftOut"};
    for (unsigned int i = 0; i < 8; ++i) {
        Level bit_val = Level::Low;
        if (bit_order == BitOrder::LsbFirst) {
            bit_val = ((val & (1u << i)) != 0) ? HIGH : LOW;
        } else {
            bit_val = ((val & (1u << (7 - i))) != 0) ? HIGH : LOW;
        }
        digitalWrite(data_pin, bit_val);
        digitalWrite(clock_pin, HIGH);
        digitalWrite(clock_pin, LOW);
    }
}

long sq(long x) {
    return x * x;
}

double sq(double x) {
    return x * x;
}

long constrain(long x, long a, long b) {
    if (x < a) return a;
    if (x > b) return b;
    return x;
}

double constrain(double x, double a, double b) {
    if (x < a) return a;
    if (x > b) return b;
    return x;
}

long map(long x, long in_min, long in_max, long out_min, long out_max) {
    if (in_max == in_min) {
        return out_min;
    }

    bool neg_in = false;
    uint64_t diff_x = 0;
    if (x >= in_min) {
        diff_x = static_cast<uint64_t>(x) - static_cast<uint64_t>(in_min);
    } else {
        neg_in = true;
        diff_x = static_cast<uint64_t>(in_min) - static_cast<uint64_t>(x);
    }

    bool neg_out_range = false;
    uint64_t range_out = 0;
    if (out_max >= out_min) {
        range_out = static_cast<uint64_t>(out_max) - static_cast<uint64_t>(out_min);
    } else {
        neg_out_range = true;
        range_out = static_cast<uint64_t>(out_min) - static_cast<uint64_t>(out_max);
    }

    bool neg_in_range = false;
    uint64_t range_in = 0;
    if (in_max >= in_min) {
        range_in = static_cast<uint64_t>(in_max) - static_cast<uint64_t>(in_min);
    } else {
        neg_in_range = true;
        range_in = static_cast<uint64_t>(in_min) - static_cast<uint64_t>(in_max);
    }

    const bool negative = (neg_in ^ neg_out_range ^ neg_in_range);

    uint64_t hi = 0;
    uint64_t lo = 0;
    mul64_64(diff_x, range_out, hi, lo);
    const uint64_t quot = div128_64(hi, lo, range_in);

    if (negative) {
        return static_cast<long>(static_cast<uint64_t>(out_min) - quot);
    }
    return static_cast<long>(static_cast<uint64_t>(out_min) + quot);
}

double map(double x, double in_min, double in_max, double out_min, double out_max) {
    if (in_max == in_min) {
        return out_min;
    }
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

bool isAlpha(int c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

bool isAlphaNumeric(int c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

bool isAscii(int c) {
    return c >= 0 && c <= 127;
}

bool isControl(int c) {
    return std::iscntrl(static_cast<unsigned char>(c)) != 0;
}

bool isDigit(int c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

bool isGraph(int c) {
    return std::isgraph(static_cast<unsigned char>(c)) != 0;
}

bool isHexadecimalDigit(int c) {
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

bool isLowerCase(int c) {
    return std::islower(static_cast<unsigned char>(c)) != 0;
}

bool isPrintable(int c) {
    return std::isprint(static_cast<unsigned char>(c)) != 0;
}

bool isPunct(int c) {
    return std::ispunct(static_cast<unsigned char>(c)) != 0;
}

bool isSpace(int c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

bool isUpperCase(int c) {
    return std::isupper(static_cast<unsigned char>(c)) != 0;
}

bool isWhitespace(int c) {
    return std::isblank(static_cast<unsigned char>(c)) != 0;
}

long random(long max) {
    if (max <= 0) return 0;
    const unsigned long umax = static_cast<unsigned long>(max);
    const unsigned long val = static_cast<unsigned long>(random_engine_()) % umax;
    return static_cast<long>(val);
}

long random(long min, long max) {
    if (min >= max) return min;
    const unsigned long span = static_cast<unsigned long>(max) - static_cast<unsigned long>(min);
    uint64_t rand_val = (static_cast<uint64_t>(random_engine_()) << 32) | static_cast<uint64_t>(random_engine_());
    const unsigned long offset = static_cast<unsigned long>(rand_val % span);
    return static_cast<long>(static_cast<unsigned long>(min) + offset);
}

void randomSeed(unsigned long seed) {
    if (seed == 0) {
        random_engine_.seed(1);
    } else {
        random_engine_.seed(seed);
    }
}

unsigned long bit(unsigned int n) {
    if (n >= sizeof(unsigned long) * 8) return 0;
    return 1ul << n;
}

unsigned int bitRead(unsigned char x, unsigned int n) {
    if (n >= 8) return 0;
    return (static_cast<unsigned int>(x) >> n) & 1u;
}

unsigned int bitRead(unsigned int x, unsigned int n) {
    if (n >= sizeof(unsigned int) * 8) return 0;
    return (x >> n) & 1u;
}

unsigned int bitRead(unsigned long x, unsigned int n) {
    if (n >= sizeof(unsigned long) * 8) return 0;
    return static_cast<unsigned int>((x >> n) & 1u);
}

void bitSet(unsigned char& x, unsigned int n) {
    if (n < 8) {
        x = static_cast<unsigned char>(x | (1u << n));
    }
}

void bitSet(unsigned int& x, unsigned int n) {
    if (n < sizeof(unsigned int) * 8) {
        x |= (1u << n);
    }
}

void bitSet(unsigned long& x, unsigned int n) {
    if (n < sizeof(unsigned long) * 8) {
        x |= (1ul << n);
    }
}

void bitClear(unsigned char& x, unsigned int n) {
    if (n < 8) {
        x = static_cast<unsigned char>(x & ~(1u << n));
    }
}

void bitClear(unsigned int& x, unsigned int n) {
    if (n < sizeof(unsigned int) * 8) {
        x &= ~(1u << n);
    }
}

void bitClear(unsigned long& x, unsigned int n) {
    if (n < sizeof(unsigned long) * 8) {
        x &= ~(1ul << n);
    }
}

void bitWrite(unsigned char& x, unsigned int n, byte b) {
    if (b != 0) {
        bitSet(x, n);
    } else {
        bitClear(x, n);
    }
}

void bitWrite(unsigned int& x, unsigned int n, byte b) {
    if (b != 0) {
        bitSet(x, n);
    } else {
        bitClear(x, n);
    }
}

void bitWrite(unsigned long& x, unsigned int n, byte b) {
    if (b != 0) {
        bitSet(x, n);
    } else {
        bitClear(x, n);
    }
}

void bitWrite(unsigned char& x, unsigned int n, Level b) {
    bitWrite(x, n, b == Level::High ? static_cast<byte>(1) : static_cast<byte>(0));
}

void bitWrite(unsigned int& x, unsigned int n, Level b) {
    bitWrite(x, n, b == Level::High ? static_cast<byte>(1) : static_cast<byte>(0));
}

void bitWrite(unsigned long& x, unsigned int n, Level b) {
    bitWrite(x, n, b == Level::High ? static_cast<byte>(1) : static_cast<byte>(0));
}

byte lowByte(unsigned char x) {
    return x;
}

byte lowByte(unsigned int x) {
    return static_cast<byte>(x & 0xFFu);
}

byte lowByte(unsigned long x) {
    return static_cast<byte>(x & 0xFFu);
}

byte highByte(unsigned char) {
    return 0;
}

byte highByte(unsigned int x) {
    return static_cast<byte>((x >> 8) & 0xFFu);
}

byte highByte(unsigned long x) {
    return static_cast<byte>((x >> 8) & 0xFFu);
}

// Serial implementation
SerialPort Serial;

bool SerialPort::begin(unsigned long) {
    CallScope scope{"Serial.begin"};
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

namespace {
std::size_t serial_write(const byte* buffer, std::size_t size) {
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
} // namespace

std::size_t SerialPort::print(const char* s) {
    CallScope scope{"Serial.print"};
    if (!s) return 0;
    return serial_write(reinterpret_cast<const byte*>(s), std::string_view(s).size());
}

std::size_t SerialPort::print(char c) {
    CallScope scope{"Serial.print"};
    const byte b = static_cast<byte>(c);
    return serial_write(&b, 1);
}

std::size_t SerialPort::print(std::string_view s) {
    CallScope scope{"Serial.print"};
    return serial_write(reinterpret_cast<const byte*>(s.data()), s.size());
}

std::size_t SerialPort::println(const char* s) {
    CallScope scope{"Serial.println"};
    std::size_t written = print(s);
    written += print("\r\n");
    return written;
}

std::size_t SerialPort::println(char c) {
    CallScope scope{"Serial.println"};
    std::size_t written = print(c);
    written += print("\r\n");
    return written;
}

std::size_t SerialPort::println(std::string_view s) {
    CallScope scope{"Serial.println"};
    std::size_t written = print(s);
    written += print("\r\n");
    return written;
}

std::size_t SerialPort::println() {
    CallScope scope{"Serial.println"};
    return print("\r\n");
}

} // namespace mm::sketch
