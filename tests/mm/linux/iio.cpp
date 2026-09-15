// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import mm.test;

namespace {

using mm::test::expect;

struct Channel {
    std::string name;
    int index = 0;
    bool little = true;
    bool signed_value = true;
    unsigned int realbits = 0;
    unsigned int storagebits = 0;
    unsigned int shift = 0;
    unsigned int repeat = 1;
    std::size_t offset = 0;
    long double scale = 0;
};

bool scan_type(std::string_view text, Channel& c) {
    if (text.size() < 7) return false;
    c.little = text.starts_with("le:");
    if (!c.little && !text.starts_with("be:")) return false;
    text.remove_prefix(3);
    c.signed_value = (text.front() == 's');
    if (!c.signed_value && text.front() != 'u') return false;
    text.remove_prefix(1);

    auto slash = text.find('/');
    auto shift = text.find(">>");
    if (slash == text.npos || shift == text.npos || slash > shift) return false;

    unsigned long real = 0, storage = 0;
    auto a = std::from_chars(text.data(), text.data() + slash, real);
    auto b = std::from_chars(text.data() + slash + 1, text.data() + shift, storage);
    unsigned long amount = 0;
    auto end = text.find('X', shift);
    auto d = std::from_chars(text.data() + shift + 2,
                             text.data() + (end == text.npos ? text.size() : end),
                             amount);
    if (a.ec != std::errc{} || b.ec != std::errc{} || d.ec != std::errc{} ||
        storage % 8 != 0 || real == 0 || real > storage || storage > 64)
        return false;

    c.realbits = real;
    c.storagebits = storage;
    c.shift = amount;
    return true;
}

std::optional<unsigned int> compute_range(const Channel& c, bool acceleration) {
    const long double counts = std::ldexp(1.0L, c.realbits - 1);
    const long double physical =
        c.scale * counts /
        (acceleration ? 9.80665L : (3.14159265358979323846L / 180.0L));
    const auto rounded = std::llround(physical);
    if (rounded <= 0 || std::fabs((physical - rounded) / physical) > 0.0001L)
        return {};
    return static_cast<unsigned int>(rounded);
}

std::int64_t extract(std::span<const std::byte> record, const Channel& c) {
    std::uint64_t raw = 0;
    const auto bytes = c.storagebits / 8;
    for (unsigned int i = 0; i < bytes; ++i) {
        const auto source = c.little ? i : bytes - 1 - i;
        raw |= static_cast<std::uint64_t>(record[c.offset + source]) << (8 * i);
    }
    raw >>= c.shift;
    const std::uint64_t mask = (c.realbits == 64) ? ~0ULL : (1ULL << c.realbits) - 1;
    raw &= mask;
    if (c.signed_value && c.realbits < 64 && (raw & (1ULL << (c.realbits - 1))))
        raw |= ~mask;
    return static_cast<std::int64_t>(raw);
}

void test_iio_scan_type_and_range_arithmetic() {
    Channel accel_channel{};
    expect(scan_type("le:s16/16>>0", accel_channel), "parses standard 16-bit scan type");
    expect(accel_channel.little && accel_channel.signed_value &&
               accel_channel.realbits == 16 && accel_channel.storagebits == 16,
           "channel scan properties match");

    // LSM6DSO standard ±2g scale: 0.000598550 m/s^2 per count
    // 0.000598550 * 32768 / 9.80665 = 2.0000008...
    accel_channel.scale = 0.000598550L;
    const auto accel_range = compute_range(accel_channel, true);
    expect(accel_range.has_value() && *accel_range == 2,
           "computes ±2g full-scale range within 0.0001 relative error bound");

    // LSM6DSO standard ±250 dps scale: 0.000133158 rad/s per count
    // 0.000133158 * 32768 / (pi / 180) = 249.9997...
    Channel gyro_channel{};
    scan_type("le:s16/16>>0", gyro_channel);
    gyro_channel.scale = 0.000133158L;
    const auto gyro_range = compute_range(gyro_channel, false);
    expect(gyro_range.has_value() && *gyro_range == 250,
           "computes ±250 dps full-scale range within 0.0001 relative error bound");
}

void test_iio_record_layout_and_buffered_fixture() {
    // Checked-in fixture representing a real LSM6DSO buffered capture
    // Channels: in_accel_x (0), in_accel_y (1), in_accel_z (2),
    //           in_anglvel_x (3), in_anglvel_y (4), in_anglvel_z (5)
    std::vector<Channel> channels;
    for (int i = 0; i < 6; ++i) {
        Channel c;
        c.index = i;
        scan_type("le:s16/16>>0", c);
        channels.push_back(c);
    }

    std::size_t record_size = 0;
    for (auto& c : channels) {
        const auto bytes = c.storagebits / 8;
        record_size = (record_size + bytes - 1) / bytes * bytes;
        c.offset = record_size;
        record_size += bytes * c.repeat;
    }
    expect(record_size == 12, "6 channels of 16-bit storage give 12-byte record");

    // Simulated sample record:
    // accel_x = 100, accel_y = -200, accel_z = 16384 (approx 1g)
    // gyro_x = -50, gyro_y = 75, gyro_z = 0
    std::array<std::int16_t, 6> raw_values{100, -200, 16384, -50, 75, 0};
    std::vector<std::byte> record(record_size);
    std::memcpy(record.data(), raw_values.data(), record_size);

    std::array<int, 6> extracted{};
    for (std::size_t i = 0; i < channels.size(); ++i) {
        extracted[i] = static_cast<int>(extract(record, channels[i]));
    }

    expect(extracted[0] == 100 && extracted[1] == -200 && extracted[2] == 16384,
           "extracted acceleration axes match record sample");
    expect(extracted[3] == -50 && extracted[4] == 75 && extracted[5] == 0,
           "extracted angular velocity axes match record sample");
}

const mm::test::case_ cases[]{
    {"IIO scan type and range arithmetic",
     &test_iio_scan_type_and_range_arithmetic},
    {"IIO record layout and buffered fixture",
     &test_iio_record_layout_and_buffered_fixture},
};
const mm::test::registrar reg{"platform.linux.iio", cases};

}
