// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>
#include <fstream>
#include <map>
#include <string>
#include <vector>

import mm.test;
import platform.linux.imu;

namespace {

using mm::test::expect;
using platform::linux::iio_detail::Channel;

std::vector<std::byte> read_capture() {
    std::ifstream input("tests/mm/linux/fixtures/lsm6dso/buffer.hex");
    std::vector<std::byte> result;
    std::string token;
    while (input >> token)
        result.push_back(static_cast<std::byte>(std::stoul(token, nullptr, 16)));
    return result;
}

std::map<std::string, std::string> read_sysfs_fixture() {
    std::ifstream input("tests/mm/linux/fixtures/lsm6dso/sysfs.mdy");
    std::map<std::string, std::string> result;
    std::string line;
    while (std::getline(input, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos || line.starts_with('#')) continue;
        auto value = line.substr(colon + 1);
        if (!value.empty() && value.front() == ' ') value.erase(0, 1);
        result.emplace(line.substr(0, colon), std::move(value));
    }
    return result;
}

void scan_type_and_range_arithmetic() {
    Channel acceleration{};
    expect(platform::linux::iio_detail::parse_scan_type(
               "le:s16/16>>0", acceleration),
           "production parser accepts a 16-bit IIO scan type");
    acceleration.scale = 0.000598550L;
    const auto acceleration_range =
        platform::linux::iio_detail::physical_range(acceleration, true);
    expect(acceleration_range && *acceleration_range == 2,
           "production range recovery finds plus/minus 2 g");

    Channel rotation{};
    expect(!platform::linux::iio_detail::parse_scan_type(
               "le:s16junk/16>>0", rotation),
           "production parser rejects trailing scan-type garbage");
    expect(platform::linux::iio_detail::parse_scan_type(
               "le:s16/16>>0", rotation),
           "production parser accepts the gyroscope scan type");
    rotation.scale = 0.000133158L;
    const auto rotation_range =
        platform::linux::iio_detail::physical_range(rotation, false);
    expect(rotation_range && *rotation_range == 250,
           "production range recovery finds plus/minus 250 dps");
}

void recorded_layout_and_extraction() {
    const auto fixture = read_sysfs_fixture();
    expect(fixture.contains("device") && fixture.at("device") == "lsm6dso",
           "the checked-in sysfs fixture identifies an LSM6DSO");
    std::array<Channel, 6> channels{};
    static constexpr std::array names{
        "in_accel_x", "in_accel_y", "in_accel_z",
        "in_anglvel_x", "in_anglvel_y", "in_anglvel_z"};
    for (std::size_t i = 0; i < channels.size(); ++i) {
        channels[i].name = names[i];
        std::string key = names[i];
        for (auto& character : key)
            if (character == '_') character = '-';
        const auto& declaration = fixture.at(key);
        const auto space = declaration.find(' ');
        channels[i].index = std::stoi(declaration.substr(0, space));
        expect(platform::linux::iio_detail::parse_scan_type(
                   declaration.substr(space + 1), channels[i]),
               "fixture scan type parses");
    }

    const auto size = platform::linux::iio_detail::layout(channels);
    const auto record = read_capture();
    expect(size && *size == record.size(),
           "fixture record size follows its scan indices and storage types");

    std::array<int, 6> values{};
    for (std::size_t i = 0; i < channels.size(); ++i)
        values[i] = static_cast<int>(
            platform::linux::iio_detail::extract_value(record, channels[i]));
    expect(values == std::array{100, -200, 16384, -50, 75, 0},
           "semantic axes survive an interleaved scan-index layout");
}

const mm::test::case_ cases[]{
    {"IIO production scan parser and range", &scan_type_and_range_arithmetic},
    {"IIO recorded layout and extraction", &recorded_layout_and_extraction},
};
const mm::test::registrar reg{"platform.linux.iio", cases};

}
