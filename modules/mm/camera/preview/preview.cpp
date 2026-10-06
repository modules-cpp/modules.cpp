// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
// Project preview processing and controls; crop follows Waveshare main.c.
module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

module mm.camera.preview;

namespace mm::camera::preview {
bool Button::update(bool high, bool falling, std::uint32_t now) {
    if (falling || !high) {
        releasing = false;
        // Level fallback covers a press arriving between taking the IRQ
        // latch and reading the pin. The next latched edge is suppressed.
        const bool pressed = armed;
        armed = false;
        return pressed;
    }
    if (!armed) {
        if (!releasing) { releasing = true; released_at = now; }
        else if (static_cast<std::uint32_t>(now - released_at) >= 30) armed = true;
    }
    return false;
}

namespace {
struct Coverage {
    unsigned first = 0, count = 0;
    std::array<unsigned, 4> weight{};
};
// Coordinates use destination-pixel units. Precompute fractional source-pixel
// overlaps so area sampling does no coordinate division in the pixel loop.
constexpr void coverage(unsigned Source, unsigned Destination,
                        std::span<Coverage> result) {
    for (unsigned d = 0; d < Destination; ++d) {
        const unsigned begin = d * Source, end = (d + 1) * Source;
        auto& cell = result[d];
        cell.first = begin / Destination;
        cell.count = (end + Destination - 1) / Destination - cell.first;
        for (unsigned i = 0; i < cell.count; ++i) {
            const unsigned left = (cell.first + i) * Destination;
            const unsigned right = left + Destination;
            cell.weight[i] = (right < end ? right : end) -
                             (left > begin ? left : begin);
        }
    }
}
constexpr auto column_coverage() {
    std::array<Coverage, width> result{};
    coverage(sensor_width, width, result);
    return result;
}
constexpr auto row_coverage() {
    std::array<Coverage, height> result{};
    coverage(sensor_height, height, result);
    return result;
}
constexpr auto columns = column_coverage();
constexpr auto rows = row_coverage();
} // namespace

unsigned sample(const Frame& frame, unsigned x, unsigned y, Mode mode) {
    if (mode == Mode::Crop)
        return std::to_integer<unsigned>(frame[(height - 1 - y) * sensor_width + x]);
    const auto& column = columns[x];
    const auto& row = rows[height - 1 - y];
    if (mode == Mode::Nearest)
        return std::to_integer<unsigned>(frame[row.first * sensor_width + column.first]);
    unsigned sum = 0;
    for (unsigned dy = 0; dy < row.count; ++dy)
        for (unsigned dx = 0; dx < column.count; ++dx)
            sum += std::to_integer<unsigned>(frame[(row.first + dy) * sensor_width +
                                                  column.first + dx]) *
                   row.weight[dy] * column.weight[dx];
    constexpr unsigned area = sensor_width * sensor_height;
    return (sum + area / 2) / area;
}

void process(const Frame& frame, Image& image, Mode mode, bool history) {
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            auto& pixel = image[y * width + x];
            const unsigned current = sample(frame, x, y, mode);
            pixel = static_cast<std::uint8_t>(mode == Mode::Temporal && history
                ? (current + pixel + 1) / 2 : current);
        }
}

void encode(const Image& image, RgbImage& pixels) {
    for (std::size_t i = 0; i < image.size(); ++i) {
        const unsigned grey = image[i];
        const unsigned color = ((grey & 0xf8) << 8) | ((grey & 0xfc) << 3) | (grey >> 3);
        pixels[i * 2] = static_cast<std::byte>(color >> 8);
        pixels[i * 2 + 1] = static_cast<std::byte>(color & 0xff);
    }
}

}
