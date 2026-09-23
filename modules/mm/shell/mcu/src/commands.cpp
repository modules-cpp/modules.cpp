// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

module mm.shell.mcu;

import mm.shell;
import mm.mcu;
import mm.stdio;

namespace mm::shell::mcu {
namespace {

using Args = std::span<const std::string_view>;

struct Text {
    char* data = nullptr;
    std::size_t capacity = 0;
    std::size_t used = 0;

    [[nodiscard]] bool append(std::string_view value) {
        if (used > capacity || value.size() > capacity - used) {
            return false;
        }
        for (char c : value) data[used++] = c;
        return true;
    }

    [[nodiscard]] bool decimal(std::uint64_t value) {
        char digits[20]{};
        std::size_t count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        if (used > capacity || count > capacity - used) return false;
        while (count != 0) data[used++] = digits[--count];
        return true;
    }

    [[nodiscard]] bool byte(std::byte value) {
        if (used > capacity || 2 > capacity - used) return false;
        constexpr char hex[] = "0123456789abcdef";
        const auto number = std::to_integer<unsigned int>(value);
        data[used++] = hex[number >> 4];
        data[used++] = hex[number & 15U];
        return true;
    }

    [[nodiscard]] std::string_view view() const {
        return {data, used};
    }
};

[[nodiscard]] Text text_buffer(CommandContext& context) {
    return {reinterpret_cast<char*>(context.scratch.data()),
            context.scratch.size(), 0};
}

[[nodiscard]] bool unsigned_value(std::string_view spelling,
                                  std::uint64_t maximum,
                                  std::uint64_t& value,
                                  bool allow_hex = false) {
    if (spelling.empty()) return false;
    unsigned int base = 10;
    if (allow_hex && spelling.size() > 2 && spelling[0] == '0' &&
        (spelling[1] == 'x' || spelling[1] == 'X')) {
        spelling.remove_prefix(2);
        base = 16;
    }
    if (spelling.empty()) return false;
    std::uint64_t parsed = 0;
    for (char c : spelling) {
        unsigned int digit = 16;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (base == 16 && c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        }
        if (digit >= base || parsed > (maximum - digit) / base) {
            return false;
        }
        parsed = parsed * base + digit;
    }
    value = parsed;
    return true;
}

[[nodiscard]] bool number(std::string_view spelling,
                          unsigned int& value,
                          bool hex = false) {
    std::uint64_t parsed = 0;
    if (!unsigned_value(spelling,
                        std::numeric_limits<unsigned int>::max(),
                        parsed, hex)) return false;
    value = static_cast<unsigned int>(parsed);
    return true;
}

[[nodiscard]] bool duration(std::string_view spelling,
                            unsigned long& value) {
    std::uint64_t parsed = 0;
    if (!unsigned_value(spelling,
                        std::numeric_limits<unsigned long>::max(),
                        parsed)) return false;
    value = static_cast<unsigned long>(parsed);
    return true;
}

void diagnostic(CommandContext& context, std::string_view message) {
    (void)context.io.err.write(message);
}

void usage(CommandContext& context, CommandResult& result) {
    result.status = 2;
    result.error = Status::BadArgument;
    diagnostic(context, "mcu: bad argument\n");
}

void overflow(CommandContext& context, CommandResult& result,
              std::size_t required) {
    result.status = 1;
    result.error = Status::Overflow;
    result.overflow = {StorageClass::TransactionScratch, required};
    diagnostic(context, "mcu: transaction scratch overflow\n");
}

void provider(CommandContext& context, CommandResult& result,
              mm::mcu::Status status) {
    switch (status) {
        case mm::mcu::Status::Ok: return;
        case mm::mcu::Status::BadArgument:
            usage(context, result);
            return;
        case mm::mcu::Status::Unsupported:
            result.status = 70;
            result.error = Status::Unsupported;
            diagnostic(context, "mcu: provider contract failure\n");
            return;
        case mm::mcu::Status::Busy:
            result.status = 75;
            result.error = Status::Unavailable;
            diagnostic(context, "mcu: busy\n");
            return;
        case mm::mcu::Status::Timeout:
            result.status = 124;
            result.error = Status::Unavailable;
            diagnostic(context, "mcu: timeout\n");
            return;
        case mm::mcu::Status::TransportError:
            result.status = 74;
            result.error = Status::ReadError;
            diagnostic(context, "mcu: transport error\n");
            return;
    }
}

void provider(CommandContext& context, CommandResult& result,
              mm::stdio::Status status) {
    switch (status) {
        case mm::stdio::Status::Ok: return;
        case mm::stdio::Status::BadArgument:
            usage(context, result);
            return;
        case mm::stdio::Status::Unsupported:
        case mm::stdio::Status::NotInitialized:
            result.status = 70;
            result.error = Status::Unsupported;
            diagnostic(context, "mcu: console contract failure\n");
            return;
        case mm::stdio::Status::Busy:
            result.status = 75;
            result.error = Status::Unavailable;
            diagnostic(context, "mcu: busy\n");
            return;
        case mm::stdio::Status::Timeout:
            result.status = 124;
            result.error = Status::Unavailable;
            diagnostic(context, "mcu: timeout\n");
            return;
        case mm::stdio::Status::TransportError:
            result.status = 74;
            result.error = Status::ReadError;
            diagnostic(context, "mcu: transport error\n");
            return;
    }
}

void output(CommandContext& context, CommandResult& result,
            std::string_view value) {
    const auto written = context.io.out.write(value);
    if (written == SinkResult::Accepted) return;
    result.status = 1;
    result.error = written == SinkResult::WouldBlock
        ? Status::Unavailable : Status::WriteError;
    result.overflow = context.io.out.failure().overflow;
}

void output_number(CommandContext& context, CommandResult& result,
                   std::uint64_t value) {
    char buffer[21]{};
    Text text{buffer, sizeof(buffer)};
    if (!text.decimal(value) || !text.append("\n")) {
        overflow(context, result, 21);
        return;
    }
    output(context, result, text.view());
}

void output_bool(CommandContext& context, CommandResult& result,
                 bool value) {
    output(context, result, value ? "1\n" : "0\n");
}

[[nodiscard]] bool append_list_item(Text& text, unsigned int number,
                                    std::string_view name) {
    return text.decimal(number) && text.append(" ") &&
           text.append(name) && text.append("\n");
}

[[nodiscard]] std::size_t decimal_width(unsigned int value) {
    std::size_t width = 1;
    while (value >= 10) {
        value /= 10;
        ++width;
    }
    return width;
}

void add_list_size(std::size_t& required, unsigned int number,
                   std::string_view name) {
    const auto line = decimal_width(number) + 2 + name.size();
    const auto maximum = std::numeric_limits<std::size_t>::max();
    required = line > maximum - required ? maximum : required + line;
}

void board_handler(void*, Args args, CommandContext& context,
                   CommandResult& result) {
    if (args.size() != 1) { usage(context, result); return; }
    const auto board = mm::mcu::board();
    if (board.name.empty()) {
        provider(context, result, mm::mcu::Status::Unsupported);
        return;
    }
    auto text = text_buffer(context);
    if (!text.append(board.name) || !text.append("\n")) {
        overflow(context, result, board.name.size() + 1);
        return;
    }
    output(context, result, text.view());
}

void gpio_handler(void*, Args args, CommandContext& context,
                  CommandResult& result) {
    if (args.size() == 2 && args[1] == "list") {
        const auto board = mm::mcu::board();
        std::size_t required = 0;
        for (const auto& gpio : board.gpios) {
            add_list_size(required, gpio.number, gpio.name);
        }
        if (required > context.scratch.size()) {
            overflow(context, result, required);
            return;
        }
        auto text = text_buffer(context);
        for (const auto& gpio : board.gpios) {
            if (!append_list_item(text, gpio.number, gpio.name)) {
                overflow(context, result, required);
                return;
            }
        }
        output(context, result, text.view());
        return;
    }
    if (args.size() < 3) { usage(context, result); return; }
    unsigned int pin = 0;
    if (!number(args[2], pin)) { usage(context, result); return; }
    if (args[1] == "configure" && args.size() == 5) {
        mm::mcu::Direction direction;
        mm::mcu::Pull pull;
        if (args[3] == "in") direction = mm::mcu::Direction::In;
        else if (args[3] == "out") direction = mm::mcu::Direction::Out;
        else { usage(context, result); return; }
        if (args[4] == "none") pull = mm::mcu::Pull::None;
        else if (args[4] == "up") pull = mm::mcu::Pull::Up;
        else if (args[4] == "down") pull = mm::mcu::Pull::Down;
        else { usage(context, result); return; }
        provider(context, result,
                 mm::mcu::gpio_configure(pin, direction, pull));
        return;
    }
    if (args[1] == "read" && args.size() == 3) {
        bool high = false;
        const auto status = mm::mcu::gpio_read(pin, high);
        provider(context, result, status);
        if (status == mm::mcu::Status::Ok) {
            output_bool(context, result, high);
        }
        return;
    }
    if (args[1] == "write" && args.size() == 4) {
        if (args[3] != "0" && args[3] != "1") {
            usage(context, result);
            return;
        }
        provider(context, result,
                 mm::mcu::gpio_write(pin, args[3] == "1"));
        return;
    }
    if (args[1] == "watch" && args.size() == 5) {
        mm::mcu::Pull pull;
        mm::mcu::Edge edge;
        if (args[3] == "none") pull = mm::mcu::Pull::None;
        else if (args[3] == "up") pull = mm::mcu::Pull::Up;
        else if (args[3] == "down") pull = mm::mcu::Pull::Down;
        else { usage(context, result); return; }
        if (args[4] == "rise") edge = mm::mcu::Edge::Rising;
        else if (args[4] == "fall") edge = mm::mcu::Edge::Falling;
        else if (args[4] == "both") edge = mm::mcu::Edge::Both;
        else { usage(context, result); return; }
        provider(context, result, mm::mcu::gpio_watch(pin, pull, edge));
        return;
    }
    if (args[1] == "take" && args.size() == 3) {
        bool pending = false;
        const auto status = mm::mcu::gpio_take(pin, pending);
        provider(context, result, status);
        if (status == mm::mcu::Status::Ok) {
            output_bool(context, result, pending);
        }
        return;
    }
    if (args[1] == "wait" && args.size() == 4) {
        unsigned long milliseconds = 0;
        if (!duration(args[3], milliseconds)) {
            usage(context, result);
            return;
        }
        bool pending = false;
        const auto status = mm::mcu::gpio_wait(
            pin, milliseconds, pending);
        provider(context, result, status);
        if (status == mm::mcu::Status::Ok) {
            output_bool(context, result, pending);
        }
        return;
    }
    if (args[1] == "unwatch" && args.size() == 3) {
        provider(context, result, mm::mcu::gpio_unwatch(pin));
        return;
    }
    usage(context, result);
}

void adc_handler(void*, Args args, CommandContext& context,
                 CommandResult& result) {
    if (args.size() == 2 && args[1] == "list") {
        const auto channels = mm::mcu::adc_description().channels;
        std::size_t required = 0;
        for (const auto& channel : channels) {
            add_list_size(required, channel.number, channel.name);
        }
        if (required > context.scratch.size()) {
            overflow(context, result, required);
            return;
        }
        auto text = text_buffer(context);
        for (const auto& channel : channels) {
            if (!append_list_item(text, channel.number, channel.name)) {
                overflow(context, result, required);
                return;
            }
        }
        output(context, result, text.view());
        return;
    }
    if (args.size() != 3) { usage(context, result); return; }
    unsigned int channel = 0;
    if (!number(args[2], channel)) { usage(context, result); return; }
    if (args[1] == "configure") {
        provider(context, result, mm::mcu::adc_configure(channel));
    } else if (args[1] == "release") {
        provider(context, result, mm::mcu::adc_release(channel));
    } else if (args[1] == "read") {
        unsigned int count = 0;
        const auto status = mm::mcu::adc_read(channel, count);
        provider(context, result, status);
        if (status == mm::mcu::Status::Ok) {
            output_number(context, result, count);
        }
    } else {
        usage(context, result);
    }
}

void pwm_handler(void*, Args args, CommandContext& context,
                 CommandResult& result) {
    if (args.size() == 2 && args[1] == "list") {
        const auto outputs = mm::mcu::pwm_description().outputs;
        std::size_t required = 0;
        for (const auto& item : outputs) {
            add_list_size(required, item.number, item.name);
        }
        if (required > context.scratch.size()) {
            overflow(context, result, required);
            return;
        }
        auto text = text_buffer(context);
        for (const auto& item : outputs) {
            if (!append_list_item(text, item.number, item.name)) {
                overflow(context, result, required);
                return;
            }
        }
        output(context, result, text.view());
        return;
    }
    if (args.size() < 3 || args.size() > 4) {
        usage(context, result);
        return;
    }
    unsigned int index = 0;
    if (!number(args[2], index)) { usage(context, result); return; }
    if (args[1] == "release" && args.size() == 3) {
        provider(context, result, mm::mcu::pwm_release(index));
    } else if (args[1] == "period" && args.size() == 3) {
        std::uint64_t period = 0;
        const auto status = mm::mcu::pwm_period(index, period);
        provider(context, result, status);
        if (status == mm::mcu::Status::Ok) {
            output_number(context, result, period);
        }
    } else if (args.size() == 4 &&
               (args[1] == "configure" || args[1] == "write")) {
        std::uint64_t value = 0;
        if (!unsigned_value(args[3],
                            std::numeric_limits<std::uint64_t>::max(),
                            value)) {
            usage(context, result);
            return;
        }
        const auto status = args[1] == "configure"
            ? mm::mcu::pwm_configure(index, value)
            : mm::mcu::pwm_write(index, value);
        provider(context, result, status);
    } else {
        usage(context, result);
    }
}

void ticks_handler(void*, Args args, CommandContext& context,
                   CommandResult& result) {
    if (args.size() != 2 ||
        (args[1] != "ms" && args[1] != "us")) {
        usage(context, result);
        return;
    }
    unsigned long count = 0;
    const auto status = args[1] == "ms"
        ? mm::mcu::ticks_ms(count) : mm::mcu::ticks_us(count);
    provider(context, result, status);
    if (status == mm::mcu::Status::Ok) {
        output_number(context, result, count);
    }
}

void delay_handler(void*, Args args, CommandContext& context,
                   CommandResult& result) {
    if (args.size() != 3 ||
        (args[1] != "ms" && args[1] != "us")) {
        usage(context, result);
        return;
    }
    unsigned long count = 0;
    if (!duration(args[2], count)) { usage(context, result); return; }
    provider(context, result, args[1] == "ms"
        ? mm::mcu::delay_ms(count) : mm::mcu::delay_us(count));
}

void console_handler(void* data, Args args, CommandContext& context,
                     CommandResult& result) {
    if (args.size() != 2) { usage(context, result); return; }
    auto* binding = static_cast<Level2Binding*>(data);
    if (binding == nullptr || binding->console == nullptr ||
        !binding->console->attached()) {
        result.status = 125;
        result.error = Status::Unavailable;
        return;
    }
    if (args[1] == "connected") {
        bool connected = false;
        const auto status = binding->console->connected(connected);
        provider(context, result, status);
        if (status == mm::stdio::Status::Ok) {
            output_bool(context, result, connected);
        }
    } else if (args[1] == "flush") {
        provider(context, result, binding->console->flush());
    } else {
        usage(context, result);
    }
}

[[nodiscard]] bool bytes_output(Text& text,
                                std::span<const std::byte> bytes) {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i != 0 && !text.append(" ")) return false;
        if (!text.byte(bytes[i])) return false;
    }
    return text.append("\n");
}

void spi_handler(void*, Args args, CommandContext& context,
                 CommandResult& result) {
    if (args.size() < 4) { usage(context, result); return; }
    unsigned int instance = 0;
    if (!number(args[2], instance)) { usage(context, result); return; }
    if (args[1] == "configure") {
        if (args.size() != 9) { usage(context, result); return; }
        mm::mcu::SpiConfiguration config{};
        config.instance = instance;
        if (!number(args[3], config.clock_gpio) ||
            !number(args[4], config.transmit_gpio) ||
            !duration(args[6], config.baud)) {
            usage(context, result);
            return;
        }
        if (args[5] != "-") {
            unsigned int receive = 0;
            if (!number(args[5], receive)) {
                usage(context, result);
                return;
            }
            config.receive_gpio = receive;
        }
        unsigned int mode = 0;
        if (!number(args[7], mode) || mode > 3 ||
            (args[8] != "msb" && args[8] != "lsb")) {
            usage(context, result);
            return;
        }
        config.mode = static_cast<mm::mcu::SpiMode>(mode);
        config.bit_order = args[8] == "msb"
            ? mm::mcu::BitOrder::MostSignificantFirst
            : mm::mcu::BitOrder::LeastSignificantFirst;
        provider(context, result, mm::mcu::spi_configure(config));
        return;
    }
    const bool transfer = args[1] == "transfer";
    if (!transfer && args[1] != "write") {
        usage(context, result);
        return;
    }
    const auto count = args.size() - 3;
    const auto unit = transfer ? 5U : 1U;
    if (count > context.scratch.size() / unit) {
        const auto required = count >
            std::numeric_limits<std::size_t>::max() / unit
            ? std::numeric_limits<std::size_t>::max() : count * unit;
        overflow(context, result, required);
        return;
    }
    auto transmit = context.scratch.first(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::uint64_t value = 0;
        if (!unsigned_value(args[3 + i], 255, value, true)) {
            usage(context, result);
            return;
        }
        transmit[i] = static_cast<std::byte>(value);
    }
    if (!transfer) {
        provider(context, result,
                 mm::mcu::spi_write(instance, transmit));
        return;
    }
    auto receive = context.scratch.subspan(count, count);
    const auto status = mm::mcu::spi_transfer(
        instance, transmit, receive);
    provider(context, result, status);
    if (status != mm::mcu::Status::Ok) return;
    auto text = Text{reinterpret_cast<char*>(
                         context.scratch.data() + count * 2),
                     context.scratch.size() - count * 2, 0};
    if (!bytes_output(text, receive)) {
        overflow(context, result, count * 5);
        return;
    }
    output(context, result, text.view());
}

void i2c_handler(void*, Args args, CommandContext& context,
                 CommandResult& result) {
    if (args.size() < 4) { usage(context, result); return; }
    unsigned int instance = 0;
    if (!number(args[2], instance)) { usage(context, result); return; }
    if (args[1] == "configure") {
        if (args.size() != 6) { usage(context, result); return; }
        mm::mcu::I2cConfiguration config{};
        config.instance = instance;
        if (!number(args[3], config.data_gpio) ||
            !number(args[4], config.clock_gpio) ||
            !duration(args[5], config.baud)) {
            usage(context, result);
            return;
        }
        provider(context, result, mm::mcu::i2c_configure(config));
        return;
    }
    unsigned int address = 0;
    if (!number(args[3], address, true) || address > 127) {
        usage(context, result);
        return;
    }
    if (args[1] == "read" && args.size() == 5) {
        unsigned int count = 0;
        if (!number(args[4], count) || count == 0) {
            usage(context, result);
            return;
        }
        if (count > context.scratch.size() / 4) {
            const auto required = static_cast<std::size_t>(count) >
                std::numeric_limits<std::size_t>::max() / 4
                ? std::numeric_limits<std::size_t>::max()
                : static_cast<std::size_t>(count) * 4;
            overflow(context, result, required);
            return;
        }
        auto bytes = context.scratch.first(count);
        const auto status = mm::mcu::i2c_read(instance, address, bytes);
        provider(context, result, status);
        if (status != mm::mcu::Status::Ok) return;
        Text text{reinterpret_cast<char*>(
                      context.scratch.data() + count),
                  context.scratch.size() - count, 0};
        if (!bytes_output(text, bytes)) {
            overflow(context, result,
                     static_cast<std::size_t>(count) * 4);
            return;
        }
        output(context, result, text.view());
        return;
    }
    const bool write_read = args[1] == "write-read";
    if (!write_read && args[1] != "write") {
        usage(context, result);
        return;
    }
    unsigned int read_count = 0;
    const std::size_t first_byte = write_read ? 5 : 4;
    if (write_read &&
        (args.size() < 6 || !number(args[4], read_count) ||
         read_count == 0)) {
        usage(context, result);
        return;
    }
    if (!write_read && args.size() < 5) {
        usage(context, result);
        return;
    }
    const auto count = args.size() - first_byte;
    if (count > context.scratch.size() ||
        (write_read && read_count >
            (context.scratch.size() - count) / 4)) {
        const auto maximum = std::numeric_limits<std::size_t>::max();
        const auto required = static_cast<std::size_t>(read_count) >
            (maximum - count) / 4
            ? maximum : count + static_cast<std::size_t>(read_count) * 4;
        overflow(context, result, required);
        return;
    }
    auto request = context.scratch.first(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::uint64_t byte = 0;
        if (!unsigned_value(args[first_byte + i], 255, byte, true)) {
            usage(context, result);
            return;
        }
        request[i] = static_cast<std::byte>(byte);
    }
    if (!write_read) {
        provider(context, result,
                 mm::mcu::i2c_write(instance, address, request));
        return;
    }
    auto response = context.scratch.subspan(count, read_count);
    const auto status = mm::mcu::i2c_write_read(
        instance, address, request, response);
    provider(context, result, status);
    if (status != mm::mcu::Status::Ok) return;
    Text text{reinterpret_cast<char*>(
                  context.scratch.data() + count + read_count),
              context.scratch.size() - count - read_count, 0};
    if (!bytes_output(text, response)) {
        overflow(context, result,
                 count + static_cast<std::size_t>(read_count) * 4);
        return;
    }
    output(context, result, text.view());
}

void uart_handler(void*, Args args, CommandContext& context,
                  CommandResult& result) {
    if (args.size() < 4 || args[1] != "write") {
        usage(context, result);
        return;
    }
    unsigned int instance = 0;
    if (!number(args[2], instance)) { usage(context, result); return; }
    std::size_t required = 1;
    for (std::size_t i = 3; i < args.size(); ++i) {
        const auto separator = i == 3 ? 0U : 1U;
        if (required > context.scratch.size() ||
            separator > context.scratch.size() - required ||
            args[i].size() >
                context.scratch.size() - required - separator) {
            overflow(context, result,
                     required + separator + args[i].size());
            return;
        }
        required += args[i].size() + separator;
    }
    auto text = text_buffer(context);
    for (std::size_t i = 3; i < args.size(); ++i) {
        if (i != 3) (void)text.append(" ");
        (void)text.append(args[i]);
    }
    text.data[text.used] = '\0';
    provider(context, result,
             mm::mcu::uart_write(instance, text.data));
}

}  // namespace

CapabilitySet capabilities(const McuConsole* console) {
    auto active = CapabilitySet::level2();
    const auto provider = mm::mcu::capabilities();
    if (provider.board) active.set(Capability::Board);
    if (provider.gpio) active.set(Capability::Gpio);
    if (provider.spi) active.set(Capability::Spi);
    if (provider.i2c) active.set(Capability::I2c);
    if (provider.uart) active.set(Capability::Uart);
    if (provider.timer) active.set(Capability::Timer);
    if (provider.adc) active.set(Capability::Adc);
    if (provider.pwm) active.set(Capability::Pwm);
    if (console != nullptr && console->attached()) {
        active.set(Capability::Console);
    }
    return active;
}

InstallResult install_level2(Registry& registry,
                             Introspection& intro,
                             Level2Binding& binding) {
    const struct Entry {
        std::string_view name;
        std::string_view summary;
        Capability required;
        CommandHandler handler;
    } entries[mcu_builtin_count]{
        {"board", "show the selected board", Capability::Board,
         &board_handler},
        {"gpio", "GPIO inventory and digital I/O", Capability::Gpio,
         &gpio_handler},
        {"spi", "SPI transactions", Capability::Spi, &spi_handler},
        {"i2c", "I2C transactions", Capability::I2c, &i2c_handler},
        {"uart", "UART text output", Capability::Uart, &uart_handler},
        {"ticks", "monotonic MCU ticks", Capability::Timer,
         &ticks_handler},
        {"delay", "bounded MCU delay", Capability::Timer,
         &delay_handler},
        {"adc", "analog input", Capability::Adc, &adc_handler},
        {"pwm", "pulse width output", Capability::Pwm,
         &pwm_handler},
        {"console", "interactive console state", Capability::Console,
         &console_handler},
    };
    CommandDescriptor extras[mcu_builtin_count]{};
    for (std::size_t i = 0; i < mcu_builtin_count; ++i) {
        CapabilitySet required;
        required.set(entries[i].required);
        extras[i] = {
            .name = entries[i].name,
            .summary = entries[i].summary,
            .command_class = CommandClass::Builtin,
            .required_capabilities = required,
            .handler = entries[i].handler,
            .context = &binding,
        };
    }
    CommandDescriptor combined[core_builtin_count + mcu_builtin_count]{};
    return install_level1_with(registry, intro, extras, combined);
}

}  // namespace mm::shell::mcu
