// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

import mm.mcu;
import mm.test;

void mm_test_reset();
void mm_test_force(mm::mcu::Status status);
void mm_test_set_level(unsigned int pin, bool high);
unsigned long mm_test_ticks();
unsigned int mm_test_uart_instance();
bool mm_test_uart_written();
bool mm_test_spi_ready();
unsigned long mm_test_spi_baud();
std::size_t mm_test_spi_size();
unsigned int mm_test_spi_byte(std::size_t index);
bool mm_test_i2c_ready();
unsigned long mm_test_i2c_baud();
unsigned int mm_test_i2c_address();
std::size_t mm_test_i2c_size();
unsigned int mm_test_i2c_byte(std::size_t index);
std::size_t mm_test_i2c_write_reads();

namespace {

using mm::mcu::Direction;
using mm::mcu::Edge;
using mm::mcu::Pull;
using mm::mcu::Status;
using mm::test::expect;

void board_describes_gpio_inventory_and_led_attachment() {
    mm_test_reset();
    const auto description = mm::mcu::board();
    expect(description.name == "stand", "the selected platform names its board");
    expect(description.gpios.size() == 32, "the board enumerates every GPIO");
    expect(description.gpios.front().number == 0 &&
               description.gpios.front().name == "GPIO0",
           "the first GPIO carries its number and name");
    expect(description.gpios.back().number == 31 &&
               description.gpios.back().name == "GPIO31",
           "the last GPIO carries its number and name");
    expect(description.led && description.led->name == "status" &&
               description.led->gpio == 25 && !description.led->active_high,
           "the LED names its attached GPIO and electrical polarity");

    bool attachment_exists = false;
    for (const auto& gpio : description.gpios)
        if (description.led && gpio.number == description.led->gpio)
            attachment_exists = true;
    expect(attachment_exists, "the LED attachment names a GPIO in the inventory");
}

void gpio_round_trip() {
    mm_test_reset();
    expect(mm::mcu::gpio_configure(25, Direction::Out, Pull::None) == Status::Ok,
           "configuring an output pin succeeds");
    expect(mm::mcu::gpio_write(25, true) == Status::Ok, "writing a configured output succeeds");

    bool high = false;
    expect(mm::mcu::gpio_read(25, high) == Status::Ok && high,
           "reading back the level a write set");
    expect(mm::mcu::gpio_write(25, false) == Status::Ok, "clearing the level succeeds");
    expect(mm::mcu::gpio_read(25, high) == Status::Ok && !high, "the cleared level reads back");
}

void gpio_rejects_what_the_platform_rejects() {
    mm_test_reset();
    expect(mm::mcu::gpio_configure(99, Direction::Out, Pull::None) == Status::BadArgument,
           "a pin the platform does not have is BadArgument");
    expect(mm::mcu::gpio_write(7, true) == Status::BadArgument,
           "writing an unconfigured pin is BadArgument");
    expect(mm::mcu::gpio_configure(7, Direction::In, Pull::Up) == Status::Ok,
           "configuring an input succeeds");
    expect(mm::mcu::gpio_write(7, true) == Status::Unsupported,
           "writing an input is Unsupported, not a failure to report");
}

// The out parameter is the part an interface gets wrong by default: a caller that
// checks the status must not have to wonder whether the variable was touched.
void a_failed_read_leaves_its_output_alone() {
    mm_test_reset();
    bool high = true;
    expect(mm::mcu::gpio_read(99, high) == Status::BadArgument, "reading a bad pin fails");
    expect(high, "a failed read does not write its output");

    mm_test_reset();
    mm_test_set_level(3, true);
    expect(mm::mcu::gpio_configure(3, Direction::In, Pull::None) == Status::Ok, "input configured");
    bool sensed = false;
    expect(mm::mcu::gpio_read(3, sensed) == Status::Ok && sensed,
           "a level the platform reports reads through");

    mm_test_reset();
    unsigned long ticks = 41;
    mm_test_force(Status::Busy);
    expect(mm::mcu::ticks_ms(ticks) == Status::Busy, "a busy platform reports Busy");
    expect(ticks == 41, "a failed tick read does not write its output");
}

void uart_reports_an_absent_instance() {
    mm_test_reset();
    expect(mm::mcu::uart_write(0, "hello") == Status::Ok, "writing instance zero succeeds");
    expect(mm_test_uart_instance() == 0 && mm_test_uart_written(),
           "the platform observed the write");
    expect(mm::mcu::uart_write(4, "hello") == Status::Unsupported,
           "an instance the platform lacks is Unsupported");
    expect(mm::mcu::uart_write(0, nullptr) == Status::BadArgument,
           "a null string is the platform's BadArgument");
}

void timer_advances_and_reads_back() {
    mm_test_reset();
    unsigned long before = 1;
    expect(mm::mcu::ticks_ms(before) == Status::Ok && before == 0, "ticks start at the platform's zero");
    expect(mm::mcu::delay_ms(500) == Status::Ok, "delaying succeeds");
    expect(mm::mcu::delay_ms(250) == Status::Ok, "delaying again succeeds");

    unsigned long after = 0;
    expect(mm::mcu::ticks_ms(after) == Status::Ok && after == 750,
           "ticks advance by what was delayed");

    unsigned long before_us = 1;
    expect(mm::mcu::ticks_us(before_us) == Status::Ok && before_us == 750000,
           "ticks_us tracks previous delay");
    expect(mm::mcu::delay_us(500) == Status::Ok, "microsecond delay succeeds");
    unsigned long after_us = 0;
    expect(mm::mcu::ticks_us(after_us) == Status::Ok && after_us == 750500,
           "ticks_us advances by microsecond delay");
}

void spi_configures_and_transfers_spans() {
    mm_test_reset();
    const mm::mcu::SpiConfiguration configuration{
        .instance = 1,
        .clock_gpio = 10,
        .transmit_gpio = 11,
        .receive_gpio = 12,
        .baud = 4'000'000,
        .mode = mm::mcu::SpiMode::Mode0,
        .bit_order = mm::mcu::BitOrder::MostSignificantFirst,
    };
    expect(mm::mcu::spi_configure(configuration) == Status::Ok,
           "a valid SPI configuration succeeds");
    expect(mm_test_spi_ready() && mm_test_spi_baud() == 4'000'000,
           "the platform receives the complete SPI configuration");

    std::array<std::byte, 128> payload;
    for (std::size_t index = 0; index < payload.size(); ++index)
        payload[index] = std::byte(index);
    expect(mm::mcu::spi_write(1, payload) == Status::Ok,
           "SPI writes one contiguous payload");
    expect(mm_test_spi_size() == payload.size() && mm_test_spi_byte(0) == 0 &&
               mm_test_spi_byte(127) == 127,
           "the platform observes the payload in order");

    std::array received{std::byte{0}, std::byte{0}};
    const std::array transmitted{std::byte{0xa5}, std::byte{0x5a}};
    expect(mm::mcu::spi_transfer(1, transmitted, received) == Status::Ok &&
               received == transmitted,
           "a full-duplex transfer preserves span boundaries");
}

void spi_rejects_invalid_configuration_and_transfer() {
    mm_test_reset();
    mm::mcu::SpiConfiguration invalid;
    invalid.baud = 0;
    expect(mm::mcu::spi_configure(invalid) == Status::BadArgument,
           "zero baud is rejected by the platform");
    invalid.baud = 1'000'000;
    invalid.clock_gpio = 3;
    invalid.transmit_gpio = 3;
    expect(mm::mcu::spi_configure(invalid) == Status::BadArgument,
           "one pin cannot carry two SPI signals");
    const std::array payload{std::byte{1}};
    expect(mm::mcu::spi_write(0, payload) == Status::BadArgument,
           "a transfer before configuration is rejected");
}

// Every status a platform can answer reaches the caller unchanged. The interface
// is typed end to end now, so there is no undefined code for it to translate.
void every_status_reaches_the_caller() {
    mm_test_reset();
    mm_test_force(Status::Ok);
    expect(mm::mcu::delay_ms(1) == Status::Ok, "0 is Ok");
    mm_test_force(Status::BadArgument);
    expect(mm::mcu::delay_ms(1) == Status::BadArgument, "1 is BadArgument");
    mm_test_force(Status::Unsupported);
    expect(mm::mcu::delay_ms(1) == Status::Unsupported, "2 is Unsupported");
    mm_test_force(Status::Busy);
    expect(mm::mcu::delay_ms(1) == Status::Busy, "3 is Busy");
    mm_test_force(Status::Timeout);
    expect(mm::mcu::delay_ms(1) == Status::Timeout, "4 is Timeout");
    // an out-of-range code cannot be forged through a typed interface
    mm_test_reset();
}

// A platform overrides what it has and inherits Unsupported for the rest, so an
// interface may grow a facility without every platform growing with it.
void an_unserved_facility_answers_unsupported() {
    mm_test_reset();
    mm::mcu::Platform bare;
    const auto description = bare.board();
    expect(description.name.empty() && description.gpios.empty() && !description.led,
           "an unserved platform has an empty board description");
    expect(bare.gpio_write(0, true) == Status::Unsupported,
           "an unimplemented facility answers Unsupported rather than failing to link");
    expect(bare.uart_write(0, "x") == Status::Unsupported, "so does an unimplemented uart");
    expect(bare.spi_configure({}) == Status::Unsupported,
           "so does an unimplemented SPI controller");
    expect(bare.i2c_configure({}) == Status::Unsupported,
           "so does an unimplemented I2C controller");
    expect(bare.i2c_write_read(0, 0x1a, {}, {}) == Status::Unsupported,
           "an unserved register read answers rather than dereferencing nothing");
    expect(bare.delay_ms(1) == Status::Unsupported, "so does an unimplemented timer");
    bool pending = true;
    expect(bare.gpio_watch(1, Pull::Up, Edge::Rising) == Status::Unsupported &&
               bare.gpio_take(1, pending) == Status::Unsupported && pending &&
               bare.gpio_wait(1, 10, pending) == Status::Unsupported && pending &&
               bare.gpio_unwatch(1) == Status::Unsupported,
           "the edge facility defaults to Unsupported without changing output");
}

void gpio_edge_forwards_and_preserves_output() {
    class Recording final : public mm::mcu::Platform {
    public:
        Status answer = Status::Ok;
        unsigned int pin = 0;
        Pull pull = Pull::None;
        Edge edge = Edge::Rising;
        unsigned long timeout = 0;
        unsigned int calls = 0;
        Status gpio_watch(unsigned int p, Pull u, Edge e) override {
            pin = p; pull = u; edge = e; ++calls; return answer;
        }
        Status gpio_take(unsigned int p, bool& pending) override {
            pin = p; ++calls;
            if (answer == Status::Ok) pending = true;
            return answer;
        }
        Status gpio_unwatch(unsigned int p) override {
            pin = p; ++calls; return answer;
        }
        Status gpio_wait(unsigned int p, unsigned long ms, bool& pending) override {
            pin = p; timeout = ms; ++calls;
            if (answer == Status::Ok) pending = false;
            return answer;
        }
    } record;
    auto& previous = mm::mcu::platform();
    mm::mcu::set_platform(record);
    expect(mm::mcu::gpio_watch(7, Pull::Down, static_cast<Edge>(99)) ==
               Status::BadArgument && record.calls == 0,
           "invalid edge is rejected before the platform seam");
    expect(mm::mcu::gpio_watch(7, Pull::Down, Edge::Both) == Status::Ok &&
               record.pin == 7 && record.pull == Pull::Down &&
               record.edge == Edge::Both,
           "watch forwards every argument");
    bool pending = false;
    expect(mm::mcu::gpio_take(8, pending) == Status::Ok && pending && record.pin == 8,
           "take forwards the pin and success output");
    expect(mm::mcu::gpio_wait(9, 123, pending) == Status::Ok && !pending &&
               record.pin == 9 && record.timeout == 123,
           "wait forwards pin and duration");
    expect(mm::mcu::gpio_unwatch(10) == Status::Ok && record.pin == 10,
           "unwatch forwards its pin");
    record.answer = Status::Busy;
    pending = true;
    expect(mm::mcu::gpio_take(8, pending) == Status::Busy && pending &&
               mm::mcu::gpio_wait(8, 1, pending) == Status::Busy && pending,
           "failed take and wait preserve the output");
    mm::mcu::set_platform(previous);
}

void i2c_configures_and_addresses_a_device() {
    mm_test_reset();
    const mm::mcu::I2cConfiguration configuration{
        .instance = 1,
        .data_gpio = 6,
        .clock_gpio = 7,
        .baud = 400'000,
    };
    expect(mm::mcu::i2c_configure(configuration) == Status::Ok,
           "a valid I2C configuration succeeds");
    expect(mm_test_i2c_ready() && mm_test_i2c_baud() == 400'000,
           "the platform receives the complete I2C configuration");

    const auto address = mm_test_i2c_address();
    const std::array command{std::byte{0xd0}, std::byte{0x01}};
    expect(mm::mcu::i2c_write(1, address, command) == Status::Ok,
           "a write to the device on the bus succeeds");
    expect(mm_test_i2c_size() == 2 && mm_test_i2c_byte(0) == 0xd0 &&
               mm_test_i2c_byte(1) == 0x01,
           "the platform observes the payload in order");

    std::array<std::byte, 4> received{};
    expect(mm::mcu::i2c_read(1, address, received) == Status::Ok,
           "a read from the device on the bus succeeds");
    expect(received[0] == std::byte{0} && received[3] == std::byte{3},
           "the caller's span is filled to its own size");
}

// The default is the bus's common case, so a caller that says nothing about
// speed still gets a working configuration rather than a zero one.
void i2c_configuration_defaults_to_fast_mode() {
    mm_test_reset();
    const mm::mcu::I2cConfiguration configuration{.instance = 0,
                                                  .data_gpio = 4,
                                                  .clock_gpio = 5};
    expect(configuration.baud == 400'000, "the default baud is fast mode");
    expect(mm::mcu::i2c_configure(configuration) == Status::Ok,
           "a configuration that names only pins succeeds");
}

void i2c_write_read_is_one_transaction() {
    mm_test_reset();
    const mm::mcu::I2cConfiguration configuration{
        .instance = 1, .data_gpio = 6, .clock_gpio = 7, .baud = 400'000};
    expect(mm::mcu::i2c_configure(configuration) == Status::Ok, "the bus configures");

    const auto address = mm_test_i2c_address();
    const std::array command{std::byte{0x15}};
    std::array<std::byte, 2> received{};
    expect(mm::mcu::i2c_write_read(1, address, command, received) == Status::Ok,
           "a register read succeeds");
    expect(mm_test_i2c_write_reads() == 1,
           "the platform saw one transaction rather than a write and a read");
    expect(mm_test_i2c_size() == 1 && mm_test_i2c_byte(0) == 0x15,
           "the command reached the device before the read");
    expect(received[0] == std::byte{0} && received[1] == std::byte{1},
           "the read filled the caller's span");
}

void i2c_rejects_invalid_use() {
    mm_test_reset();
    mm::mcu::I2cConfiguration invalid;
    invalid.baud = 0;
    expect(mm::mcu::i2c_configure(invalid) == Status::BadArgument,
           "zero baud is rejected by the platform");
    invalid.baud = 400'000;
    invalid.data_gpio = 6;
    invalid.clock_gpio = 6;
    expect(mm::mcu::i2c_configure(invalid) == Status::BadArgument,
           "one pin cannot carry both signals");

    const mm::mcu::I2cConfiguration configuration{
        .instance = 1, .data_gpio = 6, .clock_gpio = 7, .baud = 400'000};
    expect(mm::mcu::i2c_configure(configuration) == Status::Ok, "the bus configures");

    const auto address = mm_test_i2c_address();
    const std::array command{std::byte{0x15}};
    std::array<std::byte, 1> received{};
    expect(mm::mcu::i2c_write(1, address + 1, command) == Status::BadArgument,
           "an address no device answers is rejected");
    expect(mm::mcu::i2c_write(0, address, command) == Status::BadArgument,
           "an instance the bus was not configured on is rejected");
    expect(mm::mcu::i2c_write(1, address, std::span<const std::byte>{}) ==
               Status::BadArgument,
           "an empty write is not a device probe");
    expect(mm::mcu::i2c_read(1, address, std::span<std::byte>{}) == Status::BadArgument,
           "an empty read asks for nothing");
    expect(mm::mcu::i2c_write_read(1, address, std::span<const std::byte>{}, received) ==
               Status::BadArgument,
           "a register read needs a register");
    expect(mm_test_i2c_size() == 0, "a rejected call writes nothing to the bus");
}

const mm::test::case_ cases[] = {
    {"GPIO edge forwarding", &gpio_edge_forwards_and_preserves_output},
    {"board describes GPIOs and LED", &board_describes_gpio_inventory_and_led_attachment},
    {"gpio round trip", &gpio_round_trip},
    {"gpio rejects what the platform rejects", &gpio_rejects_what_the_platform_rejects},
    {"a failed read leaves its output alone", &a_failed_read_leaves_its_output_alone},
    {"uart reports an absent instance", &uart_reports_an_absent_instance},
    {"timer advances and reads back", &timer_advances_and_reads_back},
    {"spi configures and transfers spans", &spi_configures_and_transfers_spans},
    {"spi rejects invalid use", &spi_rejects_invalid_configuration_and_transfer},
    {"i2c configures and addresses a device", &i2c_configures_and_addresses_a_device},
    {"i2c defaults to fast mode", &i2c_configuration_defaults_to_fast_mode},
    {"i2c write_read is one transaction", &i2c_write_read_is_one_transaction},
    {"i2c rejects invalid use", &i2c_rejects_invalid_use},
    {"every status reaches the caller", &every_status_reaches_the_caller},
    {"an unserved facility answers Unsupported", &an_unserved_facility_answers_unsupported},
};

const mm::test::registrar reg{"mm.mcu", cases};

}  // namespace
