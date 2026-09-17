// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <span>
#include <vector>

import mm.display;

namespace {

struct Write {
    mm::display::Rectangle rectangle;
    std::vector<std::byte> bytes;
};

class RecordingDisplay final : public mm::display::Display {
public:
    [[nodiscard]] mm::display::Geometry geometry() const override { return geometry_; }
    [[nodiscard]] mm::display::Status write(mm::display::Rectangle rectangle,
                                            std::span<const std::byte> bytes) override {
        writes.push_back({rectangle, {bytes.begin(), bytes.end()}});
        return mm::display::Status::Ok;
    }
    [[nodiscard]] mm::display::Status refresh(mm::display::Refresh) override {
        ++refreshes;
        return mm::display::Status::Ok;
    }
    void reset(unsigned int width, unsigned int height, unsigned int depth) {
        geometry_ = {width, height, depth};
        writes.clear();
        refreshes = 0;
    }
    mm::display::Geometry geometry_{};
    std::vector<Write> writes;
    unsigned int refreshes = 0;
};

RecordingDisplay recorder;

}

mm::display::Display& mm_gfx_recording_display(unsigned int width,
                                                unsigned int height,
                                                unsigned int depth) {
    recorder.reset(width, height, depth);
    return recorder;
}
std::size_t mm_gfx_write_count() { return recorder.writes.size(); }
mm::display::Rectangle mm_gfx_rectangle(std::size_t index) {
    return recorder.writes[index].rectangle;
}
std::size_t mm_gfx_write_size(std::size_t index) {
    return recorder.writes[index].bytes.size();
}
std::byte mm_gfx_write_byte(std::size_t index, std::size_t offset) {
    return recorder.writes[index].bytes[offset];
}
unsigned int mm_gfx_refresh_count() { return recorder.refreshes; }
