// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A recording MCU platform that answers as an ES8311 on I2C and a full-duplex
// I2S link do. The driver's correctness is the registers it programs and the
// frames it hands and takes from the link, all observable here without a
// codec. Each direction buffers four frames; the transmitter sends one only
// when a case ticks it, and the receiver receives one only when a case feeds
// it.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

import mm.mcu;

namespace {

constexpr unsigned int device_address = 0x18;
constexpr std::size_t link_depth = 4;
constexpr std::uint64_t link_clock_hz = 12'288'000;

struct Frame {
    std::int32_t left = 0;
    std::int32_t right = 0;
};

class RecordingPlatform : public mm::mcu::Platform {
public:
    RecordingPlatform() { reset(); }

    void reset() {
        i2c_configured = false;
        i2c_writes.clear();
        identity_1 = 0x83;
        identity_2 = 0x11;
        link.reset();
        link_configures = 0;
        link_releases = 0;
        started = false;
        queue.clear();
        sent.clear();
        progress = {};
        receiving = false;
        received.clear();
        received_progress = {};
        accept_limit.reset();
        write_error = mm::mcu::Status::Ok;
        read_error = mm::mcu::Status::Ok;
        ticks = 0;
        forced = mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2c_configure(
        const mm::mcu::I2cConfiguration& configuration) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (configuration.baud == 0) return mm::mcu::Status::BadArgument;
        i2c_configured = true;
        return mm::mcu::Status::Ok;
    }

    // A write is a register byte and its value, the only kind this codec takes.
    [[nodiscard]] mm::mcu::Status i2c_write(unsigned int, unsigned int address,
                                            std::span<const std::byte> data) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!i2c_configured || address != device_address || data.size() != 2)
            return mm::mcu::Status::BadArgument;
        i2c_writes.push_back({static_cast<unsigned int>(data[0]),
                              static_cast<unsigned int>(data[1])});
        return mm::mcu::Status::Ok;
    }

    // Only the two identification registers are read.
    [[nodiscard]] mm::mcu::Status i2c_write_read(unsigned int, unsigned int address,
                                                 std::span<const std::byte> command,
                                                 std::span<std::byte> data) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!i2c_configured || address != device_address || command.size() != 1 ||
            data.size() != 1)
            return mm::mcu::Status::BadArgument;
        const auto reg = static_cast<unsigned int>(command[0]);
        data[0] = static_cast<std::byte>(reg == 0xfd ? identity_1 : reg == 0xfe ? identity_2 : 0);
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2s_configure(
        const mm::mcu::I2sConfiguration& configuration) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (configuration.rate_hz == 0 ||
            (!configuration.transmit_gpio && !configuration.receive_gpio))
            return mm::mcu::Status::BadArgument;
        if (link) {
            return link->rate_hz == configuration.rate_hz &&
                           link->slot_bits == configuration.slot_bits
                       ? mm::mcu::Status::Ok
                       : mm::mcu::Status::Busy;
        }
        link = configuration;
        ++link_configures;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2s_rate(unsigned int, mm::mcu::Frequency& actual) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!link) return mm::mcu::Status::BadArgument;
        const auto divisor = (link_clock_hz + link->rate_hz / 2) / link->rate_hz;
        actual = {link_clock_hz, divisor};
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2s_start(unsigned int,
                                            mm::mcu::I2sDirection direction) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!carries(direction)) return mm::mcu::Status::BadArgument;
        if (direction == mm::mcu::I2sDirection::Transmit) {
            started = true;
            progress = {};
        } else {
            receiving = true;
            received.clear();
            received_progress = {};
        }
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2s_read(unsigned int, std::span<std::int16_t> words,
                                           std::size_t& count) override {
        if (!link || link->slot_bits != 16) return mm::mcu::Status::BadArgument;
        if (read_error != mm::mcu::Status::Ok) return read_error;
        if (!receiving) return mm::mcu::Status::BadArgument;
        const auto moved = std::min(words.size() / 2, received.size());
        for (std::size_t i = 0; i < moved; ++i) {
            words[2 * i] = static_cast<std::int16_t>(received[i].left);
            words[2 * i + 1] = static_cast<std::int16_t>(received[i].right);
        }
        received.erase(received.begin(), received.begin() + static_cast<long>(moved));
        count = moved;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2s_read(unsigned int, std::span<std::int32_t> words,
                                           std::size_t& count) override {
        if (!link || link->slot_bits == 16) return mm::mcu::Status::BadArgument;
        if (read_error != mm::mcu::Status::Ok) return read_error;
        if (!receiving) return mm::mcu::Status::BadArgument;
        const auto moved = std::min(words.size() / 2, received.size());
        for (std::size_t i = 0; i < moved; ++i) {
            words[2 * i] = received[i].left;
            words[2 * i + 1] = received[i].right;
        }
        received.erase(received.begin(), received.begin() + static_cast<long>(moved));
        count = moved;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2s_write(unsigned int, std::span<const std::int16_t> words,
                                            std::size_t& accepted) override {
        if (!link || link->slot_bits != 16) return mm::mcu::Status::BadArgument;
        std::vector<Frame> frames;
        for (std::size_t i = 0; i + 1 < words.size(); i += 2)
            frames.push_back({words[i], words[i + 1]});
        return queue_frames(frames, accepted);
    }

    [[nodiscard]] mm::mcu::Status i2s_write(unsigned int, std::span<const std::int32_t> words,
                                            std::size_t& accepted) override {
        if (!link || link->slot_bits == 16) return mm::mcu::Status::BadArgument;
        std::vector<Frame> frames;
        for (std::size_t i = 0; i + 1 < words.size(); i += 2)
            frames.push_back({words[i], words[i + 1]});
        return queue_frames(frames, accepted);
    }

    [[nodiscard]] mm::mcu::Status i2s_progress(unsigned int, mm::mcu::I2sDirection direction,
                                               mm::mcu::Progress& out) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!carries(direction)) return mm::mcu::Status::BadArgument;
        if (direction == mm::mcu::I2sDirection::Transmit) {
            out = progress;
            out.queued = queue.size();
        } else {
            out = received_progress;
            out.queued = received.size();
        }
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2s_stop(unsigned int,
                                           mm::mcu::I2sDirection direction) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (!carries(direction)) return mm::mcu::Status::BadArgument;
        if (direction == mm::mcu::I2sDirection::Transmit) {
            started = false;
            queue.clear();
        } else {
            receiving = false;
            received.clear();
        }
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status i2s_release(unsigned int) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        link.reset();
        started = false;
        queue.clear();
        receiving = false;
        received.clear();
        ++link_releases;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status delay_ms(unsigned long milliseconds) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        ticks += milliseconds;
        return mm::mcu::Status::Ok;
    }

    // Frames passing on the link: a queued frame is sent, an empty queue
    // sends zeros and counts a miss.
    void tick(std::size_t frames) {
        for (std::size_t i = 0; i < frames && started; ++i) {
            if (queue.empty()) {
                ++progress.missed;
            } else {
                sent.push_back(queue.front());
                queue.erase(queue.begin());
                ++progress.completed;
            }
        }
    }

    // One frame arriving at the receiver: buffered, or dropped and counted.
    void receive(std::int32_t left, std::int32_t right) {
        if (!receiving) return;
        ++received_progress.completed;
        if (received.size() < link_depth)
            received.push_back({left, right});
        else
            ++received_progress.missed;
    }

    [[nodiscard]] bool carries(mm::mcu::I2sDirection direction) const {
        if (!link) return false;
        return direction == mm::mcu::I2sDirection::Transmit ? link->transmit_gpio.has_value()
                                                            : link->receive_gpio.has_value();
    }

    bool i2c_configured = false;
    std::vector<std::pair<unsigned int, unsigned int>> i2c_writes;
    unsigned int identity_1 = 0x83;
    unsigned int identity_2 = 0x11;
    std::optional<mm::mcu::I2sConfiguration> link;
    unsigned int link_configures = 0;
    unsigned int link_releases = 0;
    bool started = false;
    std::vector<Frame> queue;
    std::vector<Frame> sent;
    mm::mcu::Progress progress;
    bool receiving = false;
    std::vector<Frame> received;
    mm::mcu::Progress received_progress;
    mm::mcu::Status read_error = mm::mcu::Status::Ok;
    std::optional<std::size_t> accept_limit;
    mm::mcu::Status write_error = mm::mcu::Status::Ok;
    unsigned long ticks = 0;
    mm::mcu::Status forced = mm::mcu::Status::Ok;

private:
    // Room is the buffer's, further limited when a case asks for partial or
    // zero acceptance; an error set by the case takes nothing.
    [[nodiscard]] mm::mcu::Status queue_frames(const std::vector<Frame>& frames,
                                               std::size_t& accepted) {
        if (write_error != mm::mcu::Status::Ok) return write_error;
        if (!started || !carries(mm::mcu::I2sDirection::Transmit))
            return mm::mcu::Status::BadArgument;
        auto room = link_depth - queue.size();
        if (accept_limit) room = std::min(room, *accept_limit);
        const auto moved = std::min(frames.size(), room);
        queue.insert(queue.end(), frames.begin(), frames.begin() + static_cast<long>(moved));
        accepted = moved;
        return mm::mcu::Status::Ok;
    }
};

RecordingPlatform platform;

struct Register {
    Register() { mm::mcu::set_platform(platform); }
};

const Register registered;

}  // namespace

// A configured board may inject its own mm.mcu platform into this binary, and
// its static registration may run after ours. Reclaim the seam so every case
// exercises the driver through this stand-in.
void mm_test_es8311_reset() {
    mm::mcu::set_platform(platform);
    platform.reset();
}
void mm_test_es8311_force(mm::mcu::Status status) { platform.forced = status; }
void mm_test_es8311_identity(unsigned int first, unsigned int second) {
    platform.identity_1 = first;
    platform.identity_2 = second;
}
std::size_t mm_test_es8311_write_count() { return platform.i2c_writes.size(); }
unsigned int mm_test_es8311_write_register(std::size_t index) {
    return index < platform.i2c_writes.size() ? platform.i2c_writes[index].first : 0x100;
}
unsigned int mm_test_es8311_write_value(std::size_t index) {
    return index < platform.i2c_writes.size() ? platform.i2c_writes[index].second : 0x100;
}
unsigned long mm_test_es8311_ticks() { return platform.ticks; }
bool mm_test_es8311_link_configured() { return platform.link.has_value(); }
unsigned long mm_test_es8311_link_rate() { return platform.link ? platform.link->rate_hz : 0; }
unsigned int mm_test_es8311_link_configures() { return platform.link_configures; }
unsigned int mm_test_es8311_link_releases() { return platform.link_releases; }
bool mm_test_es8311_link_started() { return platform.started; }
void mm_test_es8311_tick(std::size_t frames) { platform.tick(frames); }
void mm_test_es8311_accept(std::size_t limit) { platform.accept_limit = limit; }
void mm_test_es8311_accept_all() { platform.accept_limit.reset(); }
void mm_test_es8311_write_error(mm::mcu::Status status) { platform.write_error = status; }
std::size_t mm_test_es8311_sent_count() { return platform.sent.size(); }
std::int32_t mm_test_es8311_sent_left(std::size_t index) {
    return index < platform.sent.size() ? platform.sent[index].left : 0x7fffffff;
}
std::int32_t mm_test_es8311_sent_right(std::size_t index) {
    return index < platform.sent.size() ? platform.sent[index].right : 0x7fffffff;
}
std::size_t mm_test_es8311_queued() { return platform.queue.size(); }
void mm_test_es8311_receive(std::int32_t left, std::int32_t right) {
    platform.receive(left, right);
}
bool mm_test_es8311_receiving() { return platform.receiving; }
std::size_t mm_test_es8311_received_waiting() { return platform.received.size(); }
void mm_test_es8311_read_error(mm::mcu::Status status) { platform.read_error = status; }
