// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cctype>
#include <charconv>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
        const uint64_t carry = rem >> 63;
        rem = (rem << 1) | ((lo >> i) & 1ULL);
        if (carry != 0 || rem >= w) {
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

bool fill_ring() {
    if (ring_count_ >= ring_capacity) return true;
    auto& console = mm::stdio::selected_console();
    std::byte temp[ring_capacity];
    const std::size_t space = ring_capacity - ring_count_;
    std::size_t read_count = 0;
    const auto status = console.read(std::span{temp, space}, read_count);
    if (status != mm::stdio::Status::Ok) {
        record_failure(from(status), "Serial.read");
        return false;
    }
    if (read_count > 0) {
        for (std::size_t i = 0; i < read_count; ++i) {
            ring_buffer_[ring_head_] = temp[i];
            ring_head_ = (ring_head_ + 1) % ring_capacity;
            ++ring_count_;
        }
    }
    return true;
}

static bool dispatching_ = false;

static std::minstd_rand random_engine_{1};

using SerialCallback = void (*)();
static SerialCallback serial_callback_ = nullptr;
static unsigned long timeout_ms_ = 1000;

struct InterruptEntry {
    bool active = false;
    unsigned int pin = 0;
    void (*handler)() = nullptr;
    unsigned int generation = 0;
};

constexpr std::size_t max_interrupts = 8;
static InterruptEntry interrupt_table_[max_interrupts]{};

constexpr std::size_t pin_pull_count = 64;
static mm::mcu::Pull pin_pulls_[pin_pull_count]{};

mm::mcu::SpiMode to_mcu_spi_mode(SpiMode mode) {
    switch (mode) {
        case SpiMode::Mode0: return mm::mcu::SpiMode::Mode0;
        case SpiMode::Mode1: return mm::mcu::SpiMode::Mode1;
        case SpiMode::Mode2: return mm::mcu::SpiMode::Mode2;
        case SpiMode::Mode3: return mm::mcu::SpiMode::Mode3;
    }
    return mm::mcu::SpiMode::Mode0;
}

mm::mcu::BitOrder to_mcu_bit_order(BitOrder order) {
    switch (order) {
        case BitOrder::MsbFirst: return mm::mcu::BitOrder::MostSignificantFirst;
        case BitOrder::LsbFirst: return mm::mcu::BitOrder::LeastSignificantFirst;
    }
    return mm::mcu::BitOrder::MostSignificantFirst;
}

static bool spi_begun_ = false;
static SPISettings spi_current_settings_{};

constexpr std::size_t wire_buffer_capacity = 32;
static bool wire_begun_ = false;
static unsigned long wire_clock_ = 100'000;
static unsigned int wire_tx_address_ = 0;
static bool wire_transmitting_ = false;
static bool wire_tx_overflow_ = false;
static bool wire_pending_write_read_ = false;
static std::byte wire_tx_buf_[wire_buffer_capacity];
static std::size_t wire_tx_len_ = 0;

static std::byte wire_rx_buf_[wire_buffer_capacity];
static std::size_t wire_rx_len_ = 0;
static std::size_t wire_rx_head_ = 0;

bool flush_pending_wire_write() {
    if (!wire_pending_write_read_) return true;
    wire_pending_write_read_ = false;
    bool ok = true;
    const auto board = mm::mcu::board();
    if (board.i2c && wire_tx_len_ > 0) {
        const auto st = mm::mcu::i2c_write(board.i2c->instance, wire_tx_address_,
                                           std::span<const std::byte>(wire_tx_buf_, wire_tx_len_));
        if (st != mm::mcu::Status::Ok) {
            const char* const saved_call = active_call_;
            active_call_ = nullptr;
            record_failure(from(st), "Wire.endTransmission");
            active_call_ = saved_call;
            ok = false;
        }
    }
    wire_tx_len_ = 0;
    return ok;
}

static unsigned int analog_read_resolution_ = 10;
static unsigned int analog_write_resolution_ = 8;

constexpr std::uint64_t arduino_pwm_period_ns = 2'040'816; // 490 Hz

constexpr std::size_t max_pwm_outputs = 32;
struct PwmState {
    bool configured = false;
    std::uint64_t period_ns = 0;
};
static PwmState pwm_states_[max_pwm_outputs]{};

constexpr std::size_t max_adc_channels = 32;
static bool adc_configured_[max_adc_channels]{};

struct ActiveTone {
    bool active = false;
    unsigned int pin = 0;
    unsigned int output = 0;
    bool has_deadline = false;
    unsigned long end_ms = 0;
};
constexpr std::size_t max_tones = 8;
static ActiveTone active_tones_[max_tones]{};

void check_tones() {
    unsigned long now = 0;
    if (mm::mcu::ticks_ms(now) == mm::mcu::Status::Ok) {
        for (std::size_t i = 0; i < max_tones; ++i) {
            if (active_tones_[i].active && active_tones_[i].has_deadline) {
                if (static_cast<long>(now - active_tones_[i].end_ms) >= 0) {
                    const char* const saved_call = active_call_;
                    active_call_ = nullptr;
                    noTone(active_tones_[i].pin);
                    active_call_ = saved_call;
                }
            }
        }
    }
}

const mm::mcu::AdcChannel* find_adc_channel(unsigned int pin, unsigned int& channel) {
    const auto desc = mm::mcu::adc_description();
    if (mm::mcu::adc_channel_for_gpio(pin, channel) == mm::mcu::Status::Ok) {
        for (const auto& ch : desc.channels) {
            if (ch.number == channel) {
                return &ch;
            }
        }
    }
    for (const auto& ch : desc.channels) {
        if (ch.number == pin) {
            channel = pin;
            return &ch;
        }
    }
    return nullptr;
}

const mm::mcu::PwmOutput* find_pwm_output(unsigned int pin, unsigned int& output) {
    const auto desc = mm::mcu::pwm_description();
    if (mm::mcu::pwm_output_for_gpio(pin, output) == mm::mcu::Status::Ok) {
        for (const auto& out : desc.outputs) {
            if (out.number == output) {
                return &out;
            }
        }
    }
    for (const auto& out : desc.outputs) {
        if (out.number == pin) {
            output = pin;
            return &out;
        }
    }
    return nullptr;
}

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

void onSerial(void (*fn)()) {
    serial_callback_ = fn;
}

void dispatch() {
    if (dispatching_) return;
    dispatching_ = true;

    fill_ring();
    check_tones();

    unsigned int snapshot_generations[max_interrupts];
    for (std::size_t i = 0; i < max_interrupts; ++i) {
        snapshot_generations[i] = interrupt_table_[i].generation;
    }

    for (std::size_t i = 0; i < max_interrupts; ++i) {
        if (exit_requested_) break;
        if (!interrupt_table_[i].active) {
            continue;
        }
        if (interrupt_table_[i].generation != snapshot_generations[i]) {
            continue;
        }
        const unsigned int pin = interrupt_table_[i].pin;
        bool pending = false;
        const auto status = mm::mcu::gpio_take(pin, pending);
        if (status != mm::mcu::Status::Ok) {
            record_failure(from(status), "dispatch");
            continue;
        }
        if (pending && interrupt_table_[i].active &&
            interrupt_table_[i].generation == snapshot_generations[i]) {
            auto handler = interrupt_table_[i].handler;
            if (handler) {
                const char* const saved_call = active_call_;
                active_call_ = nullptr;
                handler();
                active_call_ = saved_call;
                if (exit_requested_) break;
            }
        }
    }

    if (!exit_requested_ && serial_callback_ && ring_count_ > 0) {
        serial_callback_();
    }

    dispatching_ = false;
}

int run(Setup setup, Loop loop) {
    exit_requested_ = false;
    exit_code_ = 0;
    ring_head_ = 0;
    ring_tail_ = 0;
    ring_count_ = 0;
    serial_callback_ = nullptr;
    timeout_ms_ = 1000;
    dispatching_ = false;
    spi_begun_ = false;
    wire_begun_ = false;
    wire_transmitting_ = false;
    wire_tx_overflow_ = false;
    wire_pending_write_read_ = false;
    wire_tx_len_ = 0;
    wire_rx_len_ = 0;
    wire_rx_head_ = 0;
    analog_read_resolution_ = 10;
    analog_write_resolution_ = 8;

    for (std::size_t i = 0; i < max_tones; ++i) {
        active_tones_[i] = {};
    }
    for (std::size_t i = 0; i < max_pwm_outputs; ++i) {
        if (pwm_states_[i].configured) {
            (void)mm::mcu::pwm_release(i);
            pwm_states_[i] = {};
        }
    }
    for (std::size_t i = 0; i < max_adc_channels; ++i) {
        if (adc_configured_[i]) {
            (void)mm::mcu::adc_release(i);
            adc_configured_[i] = false;
        }
    }

    for (std::size_t i = 0; i < pin_pull_count; ++i) {
        pin_pulls_[i] = mm::mcu::Pull::None;
    }

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

    flush_pending_wire_write();
    for (std::size_t i = 0; i < max_tones; ++i) {
        active_tones_[i] = {};
    }
    for (std::size_t i = 0; i < max_pwm_outputs; ++i) {
        if (pwm_states_[i].configured) {
            (void)mm::mcu::pwm_release(i);
            pwm_states_[i] = {};
        }
    }
    for (std::size_t i = 0; i < max_adc_channels; ++i) {
        if (adc_configured_[i]) {
            (void)mm::mcu::adc_release(i);
            adc_configured_[i] = false;
        }
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
    if (pin < pin_pull_count) {
        pin_pulls_[pin] = pull;
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

int analogRead(unsigned int pin) {
    CallScope scope{"analogRead"};
    const auto desc = mm::mcu::adc_description();
    if (desc.channels.empty()) {
        record_failure(Status::Unsupported, "analogRead");
        return 0;
    }
    unsigned int channel = 0;
    const auto* entry = find_adc_channel(pin, channel);
    if (!entry) {
        record_failure(Status::BadArgument, "analogRead");
        return 0;
    }

    const auto status = mm::mcu::adc_configure(channel);
    if (status != mm::mcu::Status::Ok) {
        record_failure(from(status), "analogRead");
        return 0;
    }
    if (channel < max_adc_channels) {
        adc_configured_[channel] = true;
    }

    unsigned int raw_count = 0;
    const auto read_status = mm::mcu::adc_read(channel, raw_count);
    if (read_status != mm::mcu::Status::Ok) {
        record_failure(from(read_status), "analogRead");
        return 0;
    }

    const unsigned int hw_bits = entry->bits;
    const unsigned int target_bits = analog_read_resolution_;
    unsigned int scaled = raw_count;
    if (hw_bits != 0 && target_bits != 0) {
        if (target_bits < hw_bits) {
            scaled = raw_count >> (hw_bits - target_bits);
        } else if (target_bits > hw_bits) {
            scaled = raw_count << (target_bits - hw_bits);
        }
    }
    return static_cast<int>(scaled);
}

void analogReadResolution(int bits) {
    CallScope scope{"analogReadResolution"};
    if (bits < 1 || bits > 31) {
        record_failure(Status::BadArgument, "analogReadResolution");
        return;
    }
    analog_read_resolution_ = static_cast<unsigned int>(bits);
}

void analogWrite(unsigned int pin, int value) {
    CallScope scope{"analogWrite"};
    const auto desc = mm::mcu::pwm_description();
    if (desc.outputs.empty()) {
        record_failure(Status::Unsupported, "analogWrite");
        return;
    }
    unsigned int output = 0;
    const auto* entry = find_pwm_output(pin, output);
    if (!entry) {
        record_failure(Status::BadArgument, "analogWrite");
        return;
    }

    for (std::size_t i = 0; i < max_tones; ++i) {
        if (active_tones_[i].active && active_tones_[i].output == output) {
            active_tones_[i].active = false;
        }
    }

    bool need_configure = true;
    if (output < max_pwm_outputs && pwm_states_[output].configured) {
        if (pwm_states_[output].period_ns == arduino_pwm_period_ns) {
            need_configure = false;
        } else {
            (void)mm::mcu::pwm_release(output);
            pwm_states_[output].configured = false;
        }
    }

    if (need_configure) {
        const auto st = mm::mcu::pwm_configure(output, arduino_pwm_period_ns);
        if (st != mm::mcu::Status::Ok) {
            record_failure(from(st), "analogWrite");
            return;
        }
        if (output < max_pwm_outputs) {
            pwm_states_[output].configured = true;
            pwm_states_[output].period_ns = arduino_pwm_period_ns;
        }
    }

    std::uint64_t actual_period_ns = 0;
    auto st = mm::mcu::pwm_period(output, actual_period_ns);
    if (st != mm::mcu::Status::Ok) {
        record_failure(from(st), "analogWrite");
        return;
    }

    std::uint64_t duty_ns = 0;
    const std::uint64_t max_val = (analog_write_resolution_ >= 64)
                                      ? UINT64_MAX
                                      : ((1ULL << analog_write_resolution_) - 1);
    if (value <= 0) {
        duty_ns = 0;
    } else if (static_cast<std::uint64_t>(value) >= max_val) {
        duty_ns = actual_period_ns;
    } else {
        uint64_t hi = 0, lo = 0;
        mul64_64(actual_period_ns, static_cast<uint64_t>(value), hi, lo);
        duty_ns = div128_64(hi, lo, max_val);
    }

    st = mm::mcu::pwm_write(output, duty_ns);
    if (st != mm::mcu::Status::Ok) {
        record_failure(from(st), "analogWrite");
    }
}

void analogWrite(Led, int value) {
    CallScope scope{"analogWrite"};
    const auto board = mm::mcu::board();
    if (!board.led) {
        record_failure(Status::Unsupported, "analogWrite");
        return;
    }
    analogWrite(board.led->gpio, value);
}

void analogWriteResolution(int bits) {
    CallScope scope{"analogWriteResolution"};
    if (bits < 1 || bits > 31) {
        record_failure(Status::BadArgument, "analogWriteResolution");
        return;
    }
    analog_write_resolution_ = static_cast<unsigned int>(bits);
}

void tone(unsigned int pin, unsigned int frequency, unsigned long duration) {
    CallScope scope{"tone"};
    if (frequency == 0) {
        noTone(pin);
        return;
    }
    const auto desc = mm::mcu::pwm_description();
    if (desc.outputs.empty()) {
        record_failure(Status::Unsupported, "tone");
        return;
    }
    unsigned int output = 0;
    const auto* entry = find_pwm_output(pin, output);
    if (!entry) {
        record_failure(Status::BadArgument, "tone");
        return;
    }

    const std::uint64_t period_ns = 1'000'000'000ULL / frequency;
    if (period_ns == 0) {
        record_failure(Status::BadArgument, "tone");
        return;
    }

    bool need_configure = true;
    if (output < max_pwm_outputs && pwm_states_[output].configured) {
        if (pwm_states_[output].period_ns == period_ns) {
            need_configure = false;
        } else {
            (void)mm::mcu::pwm_release(output);
            pwm_states_[output].configured = false;
        }
    }

    if (need_configure) {
        const auto st = mm::mcu::pwm_configure(output, period_ns);
        if (st != mm::mcu::Status::Ok) {
            record_failure(from(st), "tone");
            return;
        }
        if (output < max_pwm_outputs) {
            pwm_states_[output].configured = true;
            pwm_states_[output].period_ns = period_ns;
        }
    }

    std::uint64_t actual_period_ns = 0;
    auto st = mm::mcu::pwm_period(output, actual_period_ns);
    if (st != mm::mcu::Status::Ok) {
        record_failure(from(st), "tone");
        return;
    }

    const std::uint64_t duty_ns = actual_period_ns / 2;
    st = mm::mcu::pwm_write(output, duty_ns);
    if (st != mm::mcu::Status::Ok) {
        record_failure(from(st), "tone");
        return;
    }

    std::size_t slot = max_tones;
    for (std::size_t i = 0; i < max_tones; ++i) {
        if (active_tones_[i].active && active_tones_[i].output == output) {
            slot = i;
            break;
        }
    }
    if (slot == max_tones) {
        for (std::size_t i = 0; i < max_tones; ++i) {
            if (!active_tones_[i].active) {
                slot = i;
                break;
            }
        }
    }
    if (slot < max_tones) {
        active_tones_[slot].active = true;
        active_tones_[slot].pin = pin;
        active_tones_[slot].output = output;
        if (duration > 0) {
            unsigned long now = 0;
            if (mm::mcu::ticks_ms(now) == mm::mcu::Status::Ok) {
                active_tones_[slot].has_deadline = true;
                active_tones_[slot].end_ms = now + duration;
            } else {
                active_tones_[slot].has_deadline = false;
            }
        } else {
            active_tones_[slot].has_deadline = false;
        }
    }
}

void noTone(unsigned int pin) {
    CallScope scope{"noTone"};
    const auto desc = mm::mcu::pwm_description();
    if (desc.outputs.empty()) {
        record_failure(Status::Unsupported, "noTone");
        return;
    }
    unsigned int output = 0;
    const auto* entry = find_pwm_output(pin, output);
    if (!entry) {
        record_failure(Status::BadArgument, "noTone");
        return;
    }

    if (output < max_pwm_outputs) {
        if (pwm_states_[output].configured) {
            const auto st = mm::mcu::pwm_release(output);
            if (st != mm::mcu::Status::Ok) {
                record_failure(from(st), "noTone");
            }
            pwm_states_[output].configured = false;
            pwm_states_[output].period_ns = 0;
        }
    } else {
        (void)mm::mcu::pwm_release(output);
    }

    for (std::size_t i = 0; i < max_tones; ++i) {
        if (active_tones_[i].active && active_tones_[i].output == output) {
            active_tones_[i].active = false;
        }
    }
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

bool delayMicroseconds(unsigned int us) {
    CallScope scope{"delayMicroseconds"};
    if (exit_requested_) return false;
    const auto status = mm::mcu::delay_us(static_cast<unsigned long>(us));
    if (status != mm::mcu::Status::Ok) {
        record_failure(from(status), "delayMicroseconds");
        return false;
    }
    return true;
}

unsigned long micros() {
    CallScope scope{"micros"};
    unsigned long ticks = 0;
    const auto status = mm::mcu::ticks_us(ticks);
    if (status != mm::mcu::Status::Ok) {
        record_failure(from(status), "micros");
        return 0;
    }
    return ticks;
}

namespace {
unsigned long pulse_in_impl(const char* func_name, unsigned int pin, Level value, unsigned long timeout) {
    CallScope scope{func_name};
    if (exit_requested_) return 0;
    unsigned long start_micros = 0;
    const auto t_status = mm::mcu::ticks_us(start_micros);
    if (t_status != mm::mcu::Status::Ok) {
        record_failure(from(t_status), func_name);
        return 0;
    }

    // 1. Wait for any previous pulse to finish (pin != value)
    while (!exit_requested_) {
        bool pin_state = false;
        const auto r_status = mm::mcu::gpio_read(pin, pin_state);
        if (r_status != mm::mcu::Status::Ok) {
            record_failure(from(r_status), func_name);
            return 0;
        }
        const Level cur = pin_state ? Level::High : Level::Low;
        if (cur != value) break;

        unsigned long now = 0;
        if (mm::mcu::ticks_us(now) != mm::mcu::Status::Ok) {
            record_failure(from(t_status), func_name);
            return 0;
        }
        if (now - start_micros >= timeout) return 0;
    }
    if (exit_requested_) return 0;

    // 2. Wait for pulse to start (pin == value)
    unsigned long pulse_start = 0;
    while (!exit_requested_) {
        bool pin_state = false;
        const auto r_status = mm::mcu::gpio_read(pin, pin_state);
        if (r_status != mm::mcu::Status::Ok) {
            record_failure(from(r_status), func_name);
            return 0;
        }
        const Level cur = pin_state ? Level::High : Level::Low;
        if (cur == value) {
            if (mm::mcu::ticks_us(pulse_start) != mm::mcu::Status::Ok) {
                record_failure(from(t_status), func_name);
                return 0;
            }
            break;
        }

        unsigned long now = 0;
        if (mm::mcu::ticks_us(now) != mm::mcu::Status::Ok) {
            record_failure(from(t_status), func_name);
            return 0;
        }
        if (now - start_micros >= timeout) return 0;
    }
    if (exit_requested_) return 0;

    // 3. Wait for pulse to end (pin != value)
    while (!exit_requested_) {
        bool pin_state = false;
        const auto r_status = mm::mcu::gpio_read(pin, pin_state);
        if (r_status != mm::mcu::Status::Ok) {
            record_failure(from(r_status), func_name);
            return 0;
        }
        const Level cur = pin_state ? Level::High : Level::Low;
        if (cur != value) {
            unsigned long pulse_end = 0;
            if (mm::mcu::ticks_us(pulse_end) != mm::mcu::Status::Ok) {
                record_failure(from(t_status), func_name);
                return 0;
            }
            return pulse_end - pulse_start;
        }

        unsigned long now = 0;
        if (mm::mcu::ticks_us(now) != mm::mcu::Status::Ok) {
            record_failure(from(t_status), func_name);
            return 0;
        }
        if (now - start_micros >= timeout) return 0;
    }
    return 0;
}
} // namespace

unsigned long pulseIn(unsigned int pin, Level value, unsigned long timeout) {
    return pulse_in_impl("pulseIn", pin, value, timeout);
}

unsigned long pulseInLong(unsigned int pin, Level value, unsigned long timeout) {
    return pulse_in_impl("pulseInLong", pin, value, timeout);
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

bool attachInterrupt(unsigned int pin, void (*handler)(), Trigger trigger) {
    CallScope scope{"attachInterrupt"};
    if (!handler) {
        record_failure(Status::BadArgument, "attachInterrupt");
        return false;
    }
    mm::mcu::Edge edge = mm::mcu::Edge::Both;
    if (trigger == Trigger::Rising) {
        edge = mm::mcu::Edge::Rising;
    } else if (trigger == Trigger::Falling) {
        edge = mm::mcu::Edge::Falling;
    } else if (trigger == Trigger::Change) {
        edge = mm::mcu::Edge::Both;
    } else {
        record_failure(Status::BadArgument, "attachInterrupt");
        return false;
    }

    const mm::mcu::Pull pull = (pin < pin_pull_count) ? pin_pulls_[pin] : mm::mcu::Pull::None;

    int existing_slot = -1;
    int free_slot = -1;
    for (std::size_t i = 0; i < max_interrupts; ++i) {
        if (interrupt_table_[i].active && interrupt_table_[i].pin == pin) {
            existing_slot = static_cast<int>(i);
            break;
        }
        if (!interrupt_table_[i].active && free_slot == -1) {
            free_slot = static_cast<int>(i);
        }
    }

    if (existing_slot >= 0) {
        static_cast<void>(mm::mcu::gpio_unwatch(pin));
        const auto status = mm::mcu::gpio_watch(pin, pull, edge);
        if (status != mm::mcu::Status::Ok) {
            interrupt_table_[existing_slot].active = false;
            interrupt_table_[existing_slot].handler = nullptr;
            interrupt_table_[existing_slot].pin = 0;
            record_failure(from(status), "attachInterrupt");
            return false;
        }
        interrupt_table_[existing_slot].generation++;
        interrupt_table_[existing_slot].handler = handler;
        interrupt_table_[existing_slot].active = true;
        return true;
    }

    if (free_slot < 0) {
        record_failure(Status::Busy, "attachInterrupt");
        return false;
    }

    const auto status = mm::mcu::gpio_watch(pin, pull, edge);
    if (status != mm::mcu::Status::Ok) {
        record_failure(from(status), "attachInterrupt");
        return false;
    }
    interrupt_table_[free_slot].pin = pin;
    interrupt_table_[free_slot].handler = handler;
    interrupt_table_[free_slot].generation++;
    interrupt_table_[free_slot].active = true;
    return true;
}

bool attachInterrupt(int pin, void (*handler)(), Trigger trigger) {
    CallScope scope{"attachInterrupt"};
    if (pin < 0) {
        record_failure(Status::BadArgument, "attachInterrupt");
        return false;
    }
    return attachInterrupt(static_cast<unsigned int>(pin), handler, trigger);
}

bool detachInterrupt(unsigned int pin) {
    CallScope scope{"detachInterrupt"};
    for (std::size_t i = 0; i < max_interrupts; ++i) {
        if (interrupt_table_[i].active && interrupt_table_[i].pin == pin) {
            interrupt_table_[i].active = false;
            interrupt_table_[i].handler = nullptr;
            interrupt_table_[i].pin = 0;
            const auto status = mm::mcu::gpio_unwatch(pin);
            if (status != mm::mcu::Status::Ok) {
                record_failure(from(status), "detachInterrupt");
                return false;
            }
            return true;
        }
    }
    const auto status = mm::mcu::gpio_unwatch(pin);
    if (status != mm::mcu::Status::Ok) {
        record_failure(from(status), "detachInterrupt");
        return false;
    }
    return true;
}

bool detachInterrupt(int pin) {
    CallScope scope{"detachInterrupt"};
    if (pin < 0) {
        record_failure(Status::BadArgument, "detachInterrupt");
        return false;
    }
    return detachInterrupt(static_cast<unsigned int>(pin));
}

int digitalPinToInterrupt(int pin) {
    return pin;
}

unsigned int digitalPinToInterrupt(unsigned int pin) {
    return pin;
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

struct Deadline {
    unsigned long start_ms = 0;
    unsigned long timeout_ms = 0;
    bool clock_ok = false;

    explicit Deadline(unsigned long timeout) : timeout_ms(timeout) {
        const auto status = mm::mcu::ticks_ms(start_ms);
        if (status == mm::mcu::Status::Ok) {
            clock_ok = true;
        } else {
            record_failure(from(status), "Serial.read");
        }
    }

    [[nodiscard]] bool expired() const {
        if (!clock_ok) return true;
        unsigned long now = 0;
        const auto status = mm::mcu::ticks_ms(now);
        if (status != mm::mcu::Status::Ok) {
            record_failure(from(status), "Serial.read");
            return true;
        }
        return (now - start_ms) >= timeout_ms;
    }
};

int timed_peek(const Deadline& deadline) {
    while (!exitRequested()) {
        if (!fill_ring()) {
            return -1;
        }
        if (ring_count_ > 0) {
            if (deadline.clock_ok && deadline.timeout_ms > 0 && deadline.expired()) {
                return -1;
            }
            return static_cast<int>(static_cast<byte>(ring_buffer_[ring_tail_]));
        }
        if (deadline.expired()) {
            break;
        }
        dispatch();
        if (exitRequested()) return -1;
        const auto status = mm::mcu::delay_ms(1);
        if (status != mm::mcu::Status::Ok) {
            record_failure(from(status), "Serial.read");
            return -1;
        }
    }
    return -1;
}

int timed_read(const Deadline& deadline) {
    while (!exitRequested()) {
        if (!fill_ring()) {
            return -1;
        }
        if (ring_count_ > 0) {
            if (deadline.clock_ok && deadline.timeout_ms > 0 && deadline.expired()) {
                return -1;
            }
            const byte b = static_cast<byte>(ring_buffer_[ring_tail_]);
            ring_tail_ = (ring_tail_ + 1) % ring_capacity;
            --ring_count_;
            return static_cast<int>(b);
        }
        if (deadline.expired()) {
            break;
        }
        dispatch();
        if (exitRequested()) return -1;
        const auto status = mm::mcu::delay_ms(1);
        if (status != mm::mcu::Status::Ok) {
            record_failure(from(status), "Serial.read");
            return -1;
        }
    }
    return -1;
}

} // namespace

std::size_t SerialPort::write(byte b) {
    CallScope scope{"Serial.write"};
    return serial_write(&b, 1);
}

std::size_t SerialPort::write(const byte* buffer, std::size_t size) {
    CallScope scope{"Serial.write"};
    return serial_write(buffer, size);
}

std::size_t SerialPort::write(const char* buffer, std::size_t size) {
    CallScope scope{"Serial.write"};
    return serial_write(reinterpret_cast<const byte*>(buffer), size);
}

std::size_t SerialPort::write(const char* s) {
    CallScope scope{"Serial.write"};
    if (!s) return 0;
    return serial_write(reinterpret_cast<const byte*>(s), std::string_view(s).size());
}

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

std::size_t SerialPort::print(bool b) {
    CallScope scope{"Serial.print"};
    const char c = b ? '1' : '0';
    return serial_write(reinterpret_cast<const byte*>(&c), 1);
}

namespace {
bool is_valid_base(Base b) {
    return b == Base::Bin || b == Base::Oct || b == Base::Dec || b == Base::Hex;
}
} // namespace

std::size_t SerialPort::print(int n, Base base) {
    CallScope scope{"Serial.print"};
    if (!is_valid_base(base)) {
        record_failure(Status::BadArgument, "Serial.print");
        return 0;
    }
    char buf[64];
    if (base == Base::Dec) {
        auto res = std::to_chars(buf, buf + sizeof(buf), n);
        if (res.ec != std::errc{}) {
            record_failure(Status::BadArgument, "Serial.print");
            return 0;
        }
        return serial_write(reinterpret_cast<const byte*>(buf), res.ptr - buf);
    }
    auto res = std::to_chars(buf, buf + sizeof(buf), static_cast<unsigned int>(n), static_cast<int>(base));
    if (res.ec != std::errc{}) {
        record_failure(Status::BadArgument, "Serial.print");
        return 0;
    }
    if (base == Base::Hex) {
        for (char* p = buf; p < res.ptr; ++p) {
            if (*p >= 'a' && *p <= 'f') {
                *p = static_cast<char>(*p - 'a' + 'A');
            }
        }
    }
    return serial_write(reinterpret_cast<const byte*>(buf), res.ptr - buf);
}

std::size_t SerialPort::print(unsigned int n, Base base) {
    CallScope scope{"Serial.print"};
    if (!is_valid_base(base)) {
        record_failure(Status::BadArgument, "Serial.print");
        return 0;
    }
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), n, static_cast<int>(base));
    if (res.ec != std::errc{}) {
        record_failure(Status::BadArgument, "Serial.print");
        return 0;
    }
    if (base == Base::Hex) {
        for (char* p = buf; p < res.ptr; ++p) {
            if (*p >= 'a' && *p <= 'f') {
                *p = static_cast<char>(*p - 'a' + 'A');
            }
        }
    }
    return serial_write(reinterpret_cast<const byte*>(buf), res.ptr - buf);
}

std::size_t SerialPort::print(long n, Base base) {
    CallScope scope{"Serial.print"};
    if (!is_valid_base(base)) {
        record_failure(Status::BadArgument, "Serial.print");
        return 0;
    }
    char buf[64];
    if (base == Base::Dec) {
        auto res = std::to_chars(buf, buf + sizeof(buf), n);
        if (res.ec != std::errc{}) {
            record_failure(Status::BadArgument, "Serial.print");
            return 0;
        }
        return serial_write(reinterpret_cast<const byte*>(buf), res.ptr - buf);
    }
    auto res = std::to_chars(buf, buf + sizeof(buf), static_cast<unsigned long>(n), static_cast<int>(base));
    if (res.ec != std::errc{}) {
        record_failure(Status::BadArgument, "Serial.print");
        return 0;
    }
    if (base == Base::Hex) {
        for (char* p = buf; p < res.ptr; ++p) {
            if (*p >= 'a' && *p <= 'f') {
                *p = static_cast<char>(*p - 'a' + 'A');
            }
        }
    }
    return serial_write(reinterpret_cast<const byte*>(buf), res.ptr - buf);
}

std::size_t SerialPort::print(unsigned long n, Base base) {
    CallScope scope{"Serial.print"};
    if (!is_valid_base(base)) {
        record_failure(Status::BadArgument, "Serial.print");
        return 0;
    }
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), n, static_cast<int>(base));
    if (res.ec != std::errc{}) {
        record_failure(Status::BadArgument, "Serial.print");
        return 0;
    }
    if (base == Base::Hex) {
        for (char* p = buf; p < res.ptr; ++p) {
            if (*p >= 'a' && *p <= 'f') {
                *p = static_cast<char>(*p - 'a' + 'A');
            }
        }
    }
    return serial_write(reinterpret_cast<const byte*>(buf), res.ptr - buf);
}

std::size_t SerialPort::print(double n, int digits) {
    CallScope scope{"Serial.print"};
    char buf[64];
    const int precision = digits >= 0 ? digits : 0;
    auto res = std::to_chars(buf, buf + sizeof(buf), n, std::chars_format::fixed, precision);
    if (res.ec != std::errc{}) {
        record_failure(Status::BadArgument, "Serial.print");
        return 0;
    }
    return serial_write(reinterpret_cast<const byte*>(buf), res.ptr - buf);
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

std::size_t SerialPort::println(bool b) {
    CallScope scope{"Serial.println"};
    std::size_t written = print(b);
    written += print("\r\n");
    return written;
}

std::size_t SerialPort::println(int n, Base base) {
    CallScope scope{"Serial.println"};
    std::size_t written = print(n, base);
    written += print("\r\n");
    return written;
}

std::size_t SerialPort::println(unsigned int n, Base base) {
    CallScope scope{"Serial.println"};
    std::size_t written = print(n, base);
    written += print("\r\n");
    return written;
}

std::size_t SerialPort::println(long n, Base base) {
    CallScope scope{"Serial.println"};
    std::size_t written = print(n, base);
    written += print("\r\n");
    return written;
}

std::size_t SerialPort::println(unsigned long n, Base base) {
    CallScope scope{"Serial.println"};
    std::size_t written = print(n, base);
    written += print("\r\n");
    return written;
}

std::size_t SerialPort::println(double n, int digits) {
    CallScope scope{"Serial.println"};
    std::size_t written = print(n, digits);
    written += print("\r\n");
    return written;
}

std::size_t SerialPort::println() {
    CallScope scope{"Serial.println"};
    return print("\r\n");
}

int SerialPort::available() {
    CallScope scope{"Serial.available"};
    if (!fill_ring()) return 0;
    return static_cast<int>(ring_count_);
}

int SerialPort::read() {
    CallScope scope{"Serial.read"};
    if (!fill_ring()) return -1;
    if (ring_count_ == 0) return -1;
    const byte b = static_cast<byte>(ring_buffer_[ring_tail_]);
    ring_tail_ = (ring_tail_ + 1) % ring_capacity;
    --ring_count_;
    return static_cast<int>(b);
}

int SerialPort::peek() {
    CallScope scope{"Serial.peek"};
    if (!fill_ring()) return -1;
    if (ring_count_ == 0) return -1;
    return static_cast<int>(static_cast<byte>(ring_buffer_[ring_tail_]));
}

bool SerialPort::flush() {
    CallScope scope{"Serial.flush"};
    auto& console = mm::stdio::selected_console();
    const auto status = console.flush();
    if (status != mm::stdio::Status::Ok) {
        record_failure(from(status), "Serial.flush");
        return false;
    }
    return true;
}

bool SerialPort::connected() {
    CallScope scope{"Serial.connected"};
    auto& console = mm::stdio::selected_console();
    bool value = false;
    const auto status = console.connected(value);
    if (status != mm::stdio::Status::Ok) {
        record_failure(from(status), "Serial.connected");
        return false;
    }
    return value;
}

void SerialPort::setTimeout(unsigned long ms) {
    timeout_ms_ = ms;
}

unsigned long SerialPort::getTimeout() const {
    return timeout_ms_;
}

std::size_t SerialPort::readBytes(char* buffer, std::size_t length) {
    CallScope scope{"Serial.readBytes"};
    if (!buffer || length == 0) return 0;
    Deadline deadline{timeout_ms_};
    std::size_t count = 0;
    while (count < length && !exitRequested()) {
        const int c = timed_read(deadline);
        if (c < 0) break;
        buffer[count++] = static_cast<char>(c);
    }
    return count;
}

std::size_t SerialPort::readBytes(byte* buffer, std::size_t length) {
    CallScope scope{"Serial.readBytes"};
    return readBytes(reinterpret_cast<char*>(buffer), length);
}

std::size_t SerialPort::readBytesUntil(char terminator, char* buffer, std::size_t length) {
    CallScope scope{"Serial.readBytesUntil"};
    if (!buffer || length == 0) return 0;
    Deadline deadline{timeout_ms_};
    std::size_t count = 0;
    while (count < length && !exitRequested()) {
        const int c = timed_read(deadline);
        if (c < 0) break;
        if (static_cast<char>(c) == terminator) break;
        buffer[count++] = static_cast<char>(c);
    }
    return count;
}

std::size_t SerialPort::readBytesUntil(byte terminator, byte* buffer, std::size_t length) {
    CallScope scope{"Serial.readBytesUntil"};
    return readBytesUntil(static_cast<char>(terminator), reinterpret_cast<char*>(buffer), length);
}

std::string SerialPort::readString() {
    CallScope scope{"Serial.readString"};
    std::string result;
    Deadline deadline{timeout_ms_};
    while (!exitRequested()) {
        const int c = timed_read(deadline);
        if (c < 0) break;
        result.push_back(static_cast<char>(c));
    }
    return result;
}

std::string SerialPort::readStringUntil(char terminator) {
    CallScope scope{"Serial.readStringUntil"};
    std::string result;
    Deadline deadline{timeout_ms_};
    while (!exitRequested()) {
        const int c = timed_read(deadline);
        if (c < 0) break;
        if (static_cast<char>(c) == terminator) break;
        result.push_back(static_cast<char>(c));
    }
    return result;
}

namespace {
std::vector<std::size_t> compute_kmp_table(std::string_view pattern) {
    const std::size_t m = pattern.size();
    std::vector<std::size_t> pi(m, 0);
    for (std::size_t i = 1; i < m; ++i) {
        std::size_t j = pi[i - 1];
        while (j > 0 && pattern[i] != pattern[j]) {
            j = pi[j - 1];
        }
        if (pattern[i] == pattern[j]) {
            ++j;
        }
        pi[i] = j;
    }
    return pi;
}

void stream_kmp_step(char ch, std::string_view pattern, const std::vector<std::size_t>& pi, std::size_t& idx) {
    while (idx > 0 && ch != pattern[idx]) {
        idx = pi[idx - 1];
    }
    if (ch == pattern[idx]) {
        ++idx;
    }
}
} // namespace

bool SerialPort::findUntil(const char* target, const char* terminator) {
    CallScope scope{"Serial.findUntil"};
    if (!target || target[0] == '\0') return true;

    const std::string_view tgt{target};
    const std::string_view trm{terminator ? terminator : ""};
    const auto pi_tgt = compute_kmp_table(tgt);
    const auto pi_trm = trm.empty() ? std::vector<std::size_t>{} : compute_kmp_table(trm);

    Deadline deadline{timeout_ms_};
    std::size_t target_idx = 0;
    std::size_t term_idx = 0;

    while (!exitRequested()) {
        const int c = timed_read(deadline);
        if (c < 0) return false;
        const char ch = static_cast<char>(c);

        stream_kmp_step(ch, tgt, pi_tgt, target_idx);
        if (target_idx == tgt.size()) return true;

        if (!trm.empty()) {
            stream_kmp_step(ch, trm, pi_trm, term_idx);
            if (term_idx == trm.size()) return false;
        }
    }
    return false;
}

bool SerialPort::find(const char* target) {
    CallScope scope{"Serial.find"};
    return findUntil(target, "");
}

bool SerialPort::find(char target) {
    CallScope scope{"Serial.find"};
    const char buf[2] = {target, '\0'};
    return find(buf);
}

bool SerialPort::findUntil(const char* target, char terminator) {
    CallScope scope{"Serial.findUntil"};
    const char buf[2] = {terminator, '\0'};
    return findUntil(target, buf);
}

long SerialPort::parseInt() {
    CallScope scope{"Serial.parseInt"};
    Deadline deadline{timeout_ms_};
    while (!exitRequested()) {
        const int c = timed_peek(deadline);
        if (c < 0) return 0;
        if ((c >= '0' && c <= '9') || c == '-' || c == '+') {
            break;
        }
        timed_read(deadline);
    }
    if (exitRequested()) return 0;

    bool negative = false;
    int c = timed_peek(deadline);
    if (c == '-' || c == '+') {
        timed_read(deadline);
        if (c == '-') negative = true;
        c = timed_peek(deadline);
        if (c < '0' || c > '9') return 0;
    }

    uint64_t accum = 0;
    bool has_digits = false;
    bool overflow = false;
    const uint64_t limit = negative ?
        (static_cast<uint64_t>(-(LONG_MIN + 1)) + 1ULL) :
        static_cast<uint64_t>(LONG_MAX);

    while (!exitRequested()) {
        c = timed_peek(deadline);
        if (c >= '0' && c <= '9') {
            timed_read(deadline);
            has_digits = true;
            const unsigned int digit = static_cast<unsigned int>(c - '0');
            if (overflow) {
                continue;
            }
            if (accum > (limit - digit) / 10ULL) {
                overflow = true;
                record_failure(Status::BadArgument, "Serial.parseInt");
                continue;
            }
            accum = accum * 10ULL + digit;
        } else {
            break;
        }
    }

    if (!has_digits) return 0;
    if (overflow) {
        return negative ? LONG_MIN : LONG_MAX;
    }
    if (negative) {
        if (accum == (static_cast<uint64_t>(-(LONG_MIN + 1)) + 1ULL)) {
            return LONG_MIN;
        }
        return -static_cast<long>(accum);
    }
    return static_cast<long>(accum);
}

double SerialPort::parseFloat() {
    CallScope scope{"Serial.parseFloat"};
    Deadline deadline{timeout_ms_};
    while (!exitRequested()) {
        const int c = timed_peek(deadline);
        if (c < 0) return 0.0;
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
            break;
        }
        timed_read(deadline);
    }
    if (exitRequested()) return 0.0;

    bool negative = false;
    int c = timed_peek(deadline);
    if (c == '-' || c == '+') {
        timed_read(deadline);
        if (c == '-') negative = true;
        c = timed_peek(deadline);
        if ((c < '0' || c > '9') && c != '.') return 0.0;
    }

    double value = 0.0;
    while (!exitRequested()) {
        c = timed_peek(deadline);
        if (c >= '0' && c <= '9') {
            timed_read(deadline);
            value = value * 10.0 + (c - '0');
        } else {
            break;
        }
    }

    c = timed_peek(deadline);
    if (c == '.') {
        timed_read(deadline);
        double frac = 1.0;
        while (!exitRequested()) {
            const int d = timed_peek(deadline);
            if (d >= '0' && d <= '9') {
                timed_read(deadline);
                frac *= 0.1;
                value += (d - '0') * frac;
            } else {
                break;
            }
        }
    }
    return negative ? -value : value;
}

// SPIClass implementation
bool SPIClass::begin() {
    CallScope scope{"SPI.begin"};
    const auto board = mm::mcu::board();
    if (!board.spi) {
        record_failure(Status::Unsupported, "SPI.begin");
        return false;
    }
    spi_current_settings_ = SPISettings{};
    const mm::mcu::SpiConfiguration config{
        board.spi->instance,
        board.spi->clock_gpio,
        board.spi->transmit_gpio,
        board.spi->receive_gpio,
        spi_current_settings_.clock,
        to_mcu_spi_mode(spi_current_settings_.data_mode),
        to_mcu_bit_order(spi_current_settings_.bit_order)
    };
    const auto st = mm::mcu::spi_configure(config);
    if (st != mm::mcu::Status::Ok) {
        record_failure(from(st), "SPI.begin");
        return false;
    }
    spi_begun_ = true;
    return true;
}

bool SPIClass::end() {
    CallScope scope{"SPI.end"};
    spi_begun_ = false;
    return true;
}

void SPIClass::beginTransaction(SPISettings settings) {
    CallScope scope{"SPI.beginTransaction"};
    if (!spi_begun_) {
        record_failure(Status::NotInitialized, "SPI.beginTransaction");
        return;
    }
    const auto board = mm::mcu::board();
    if (!board.spi) {
        record_failure(Status::Unsupported, "SPI.beginTransaction");
        return;
    }
    const mm::mcu::SpiConfiguration config{
        board.spi->instance,
        board.spi->clock_gpio,
        board.spi->transmit_gpio,
        board.spi->receive_gpio,
        settings.clock,
        to_mcu_spi_mode(settings.data_mode),
        to_mcu_bit_order(settings.bit_order)
    };
    const auto st = mm::mcu::spi_configure(config);
    if (st != mm::mcu::Status::Ok) {
        record_failure(from(st), "SPI.beginTransaction");
        return;
    }
    spi_current_settings_ = settings;
}

void SPIClass::endTransaction() {
    CallScope scope{"SPI.endTransaction"};
    if (!spi_begun_) {
        record_failure(Status::NotInitialized, "SPI.endTransaction");
    }
}

byte SPIClass::transfer(byte val) {
    CallScope scope{"SPI.transfer"};
    if (!spi_begun_) {
        record_failure(Status::NotInitialized, "SPI.transfer");
        return 0;
    }
    const auto board = mm::mcu::board();
    if (!board.spi) {
        record_failure(Status::Unsupported, "SPI.transfer");
        return 0;
    }
    const std::byte tx = static_cast<std::byte>(val);
    std::byte rx{0};
    const auto st = mm::mcu::spi_transfer(board.spi->instance,
                                          std::span<const std::byte>(&tx, 1),
                                          std::span<std::byte>(&rx, 1));
    if (st != mm::mcu::Status::Ok) {
        record_failure(from(st), "SPI.transfer");
        return 0;
    }
    return static_cast<byte>(rx);
}

word SPIClass::transfer16(word val) {
    CallScope scope{"SPI.transfer16"};
    if (!spi_begun_) {
        record_failure(Status::NotInitialized, "SPI.transfer16");
        return 0;
    }
    const auto board = mm::mcu::board();
    if (!board.spi) {
        record_failure(Status::Unsupported, "SPI.transfer16");
        return 0;
    }
    std::byte tx[2];
    std::byte rx[2]{};
    if (spi_current_settings_.bit_order == BitOrder::MsbFirst) {
        tx[0] = static_cast<std::byte>((val >> 8) & 0xFF);
        tx[1] = static_cast<std::byte>(val & 0xFF);
    } else {
        tx[0] = static_cast<std::byte>(val & 0xFF);
        tx[1] = static_cast<std::byte>((val >> 8) & 0xFF);
    }
    const auto st = mm::mcu::spi_transfer(board.spi->instance, tx, rx);
    if (st != mm::mcu::Status::Ok) {
        record_failure(from(st), "SPI.transfer16");
        return 0;
    }
    if (spi_current_settings_.bit_order == BitOrder::MsbFirst) {
        return static_cast<word>((static_cast<word>(static_cast<byte>(rx[0])) << 8) |
                                 static_cast<word>(static_cast<byte>(rx[1])));
    } else {
        return static_cast<word>(static_cast<word>(static_cast<byte>(rx[0])) |
                                 (static_cast<word>(static_cast<byte>(rx[1])) << 8));
    }
}

void SPIClass::transfer(void* buffer, std::size_t size) {
    CallScope scope{"SPI.transfer"};
    if (!buffer || size == 0) return;
    if (!spi_begun_) {
        record_failure(Status::NotInitialized, "SPI.transfer");
        return;
    }
    const auto board = mm::mcu::board();
    if (!board.spi) {
        record_failure(Status::Unsupported, "SPI.transfer");
        return;
    }
    std::vector<std::byte> tx(size);
    std::memcpy(tx.data(), buffer, size);
    std::vector<std::byte> rx(size);
    const auto st = mm::mcu::spi_transfer(board.spi->instance, tx, rx);
    if (st != mm::mcu::Status::Ok) {
        record_failure(from(st), "SPI.transfer");
        return;
    }
    std::memcpy(buffer, rx.data(), size);
}

void SPIClass::transfer(byte* buffer, std::size_t size) {
    transfer(static_cast<void*>(buffer), size);
}

SPIClass SPI;

// TwoWire implementation
bool TwoWire::begin() {
    CallScope scope{"Wire.begin"};
    flush_pending_wire_write();
    const auto board = mm::mcu::board();
    if (!board.i2c) {
        record_failure(Status::Unsupported, "Wire.begin");
        return false;
    }
    wire_clock_ = 100'000;
    const mm::mcu::I2cConfiguration config{
        board.i2c->instance,
        board.i2c->data_gpio,
        board.i2c->clock_gpio,
        wire_clock_
    };
    const auto st = mm::mcu::i2c_configure(config);
    if (st != mm::mcu::Status::Ok) {
        record_failure(from(st), "Wire.begin");
        return false;
    }
    wire_begun_ = true;
    wire_transmitting_ = false;
    wire_tx_overflow_ = false;
    wire_pending_write_read_ = false;
    wire_tx_len_ = 0;
    wire_rx_len_ = 0;
    wire_rx_head_ = 0;
    return true;
}

bool TwoWire::end() {
    CallScope scope{"Wire.end"};
    const bool flushed = flush_pending_wire_write();
    wire_begun_ = false;
    wire_transmitting_ = false;
    wire_tx_overflow_ = false;
    wire_pending_write_read_ = false;
    wire_tx_len_ = 0;
    wire_rx_len_ = 0;
    wire_rx_head_ = 0;
    return flushed;
}

void TwoWire::setClock(unsigned long clock_speed) {
    CallScope scope{"Wire.setClock"};
    if (!wire_begun_) {
        record_failure(Status::NotInitialized, "Wire.setClock");
        return;
    }
    const auto board = mm::mcu::board();
    if (!board.i2c) {
        record_failure(Status::Unsupported, "Wire.setClock");
        return;
    }
    wire_clock_ = clock_speed;
    const mm::mcu::I2cConfiguration config{
        board.i2c->instance,
        board.i2c->data_gpio,
        board.i2c->clock_gpio,
        wire_clock_
    };
    const auto st = mm::mcu::i2c_configure(config);
    if (st != mm::mcu::Status::Ok) {
        record_failure(from(st), "Wire.setClock");
    }
}

void TwoWire::beginTransmission(byte address) {
    CallScope scope{"Wire.beginTransmission"};
    if (address > 0x7F) {
        record_failure(Status::BadArgument, "Wire.beginTransmission");
        return;
    }
    flush_pending_wire_write();
    wire_tx_address_ = static_cast<unsigned int>(address);
    wire_tx_len_ = 0;
    wire_tx_overflow_ = false;
    wire_transmitting_ = true;
}

void TwoWire::beginTransmission(int address) {
    if (address < 0 || address > 0x7F) {
        CallScope scope{"Wire.beginTransmission"};
        record_failure(Status::BadArgument, "Wire.beginTransmission");
        return;
    }
    beginTransmission(static_cast<byte>(address));
}

std::size_t TwoWire::write(byte val) {
    CallScope scope{"Wire.write"};
    if (!wire_transmitting_) return 0;
    if (wire_tx_len_ >= wire_buffer_capacity) {
        wire_tx_overflow_ = true;
        return 0;
    }
    wire_tx_buf_[wire_tx_len_++] = static_cast<std::byte>(val);
    return 1;
}

std::size_t TwoWire::write(const byte* buffer, std::size_t size) {
    CallScope scope{"Wire.write"};
    if (!wire_transmitting_ || !buffer || size == 0) return 0;
    const std::size_t space = wire_buffer_capacity - wire_tx_len_;
    const std::size_t count = std::min(size, space);
    for (std::size_t i = 0; i < count; ++i) {
        wire_tx_buf_[wire_tx_len_++] = static_cast<std::byte>(buffer[i]);
    }
    if (size > space) {
        wire_tx_overflow_ = true;
    }
    return count;
}

std::size_t TwoWire::write(const char* s) {
    CallScope scope{"Wire.write"};
    if (!s) return 0;
    return write(reinterpret_cast<const byte*>(s), std::strlen(s));
}

byte TwoWire::endTransmission(bool send_stop) {
    CallScope scope{"Wire.endTransmission"};
    if (!wire_begun_) {
        record_failure(Status::NotInitialized, "Wire.endTransmission");
        wire_transmitting_ = false;
        wire_tx_len_ = 0;
        return 4;
    }
    const auto board = mm::mcu::board();
    if (!board.i2c) {
        record_failure(Status::Unsupported, "Wire.endTransmission");
        wire_transmitting_ = false;
        wire_tx_len_ = 0;
        return 4;
    }
    if (!wire_transmitting_) {
        return 4;
    }
    wire_transmitting_ = false;
    if (wire_tx_overflow_) {
        record_failure(Status::BadArgument, "Wire.endTransmission");
        wire_tx_len_ = 0;
        wire_tx_overflow_ = false;
        return 1;
    }
    if (!send_stop) {
        if (wire_tx_address_ > 0x7F) {
            record_failure(Status::BadArgument, "Wire.endTransmission");
            wire_tx_len_ = 0;
            return 2;
        }
        wire_pending_write_read_ = true;
        return 0;
    }
    wire_pending_write_read_ = false;
    const auto st = mm::mcu::i2c_write(board.i2c->instance, wire_tx_address_,
                                       std::span<const std::byte>(wire_tx_buf_, wire_tx_len_));
    wire_tx_len_ = 0;
    if (st != mm::mcu::Status::Ok) {
        record_failure(from(st), "Wire.endTransmission");
        switch (st) {
            case mm::mcu::Status::BadArgument: return 2;
            case mm::mcu::Status::Timeout: return 5;
            default: return 4;
        }
    }
    return 0;
}

std::size_t TwoWire::requestFrom(byte address, std::size_t quantity, bool send_stop) {
    CallScope scope{"Wire.requestFrom"};
    (void)send_stop;
    if (address > 0x7F) {
        record_failure(Status::BadArgument, "Wire.requestFrom");
        return 0;
    }
    if (!wire_begun_) {
        record_failure(Status::NotInitialized, "Wire.requestFrom");
        return 0;
    }
    const auto board = mm::mcu::board();
    if (!board.i2c) {
        record_failure(Status::Unsupported, "Wire.requestFrom");
        return 0;
    }
    if (quantity == 0) return 0;
    const std::size_t count = std::min(quantity, wire_buffer_capacity);
    wire_rx_len_ = 0;
    wire_rx_head_ = 0;

    mm::mcu::Status st;
    if (wire_pending_write_read_ && wire_tx_address_ == static_cast<unsigned int>(address)) {
        wire_pending_write_read_ = false;
        st = mm::mcu::i2c_write_read(
            board.i2c->instance, address,
            std::span<const std::byte>(wire_tx_buf_, wire_tx_len_),
            std::span<std::byte>(wire_rx_buf_, count));
        wire_tx_len_ = 0;
    } else {
        if (!flush_pending_wire_write()) {
            return 0;
        }
        st = mm::mcu::i2c_read(board.i2c->instance, address,
                               std::span<std::byte>(wire_rx_buf_, count));
    }

    if (st != mm::mcu::Status::Ok) {
        record_failure(from(st), "Wire.requestFrom");
        return 0;
    }
    wire_rx_len_ = count;
    return count;
}

std::size_t TwoWire::requestFrom(int address, int quantity, int send_stop) {
    if (address < 0 || address > 0x7F) {
        CallScope scope{"Wire.requestFrom"};
        record_failure(Status::BadArgument, "Wire.requestFrom");
        return 0;
    }
    if (quantity <= 0) return 0;
    return requestFrom(static_cast<byte>(address), static_cast<std::size_t>(quantity), send_stop != 0);
}

int TwoWire::available() {
    CallScope scope{"Wire.available"};
    return static_cast<int>(wire_rx_len_ - wire_rx_head_);
}

int TwoWire::read() {
    CallScope scope{"Wire.read"};
    if (wire_rx_head_ >= wire_rx_len_) return -1;
    return static_cast<int>(static_cast<byte>(wire_rx_buf_[wire_rx_head_++]));
}

int TwoWire::peek() {
    CallScope scope{"Wire.peek"};
    if (wire_rx_head_ >= wire_rx_len_) return -1;
    return static_cast<int>(static_cast<byte>(wire_rx_buf_[wire_rx_head_]));
}

void TwoWire::flush() {
    CallScope scope{"Wire.flush"};
}

TwoWire Wire;

} // namespace mm::sketch
