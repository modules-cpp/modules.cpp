// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
import mm.camera.preview;
import mm.camera;
import mm.display;
import mm.fonts;
import mm.gfx;
import mm.mcu;

namespace {
namespace preview = mm::camera::preview;
preview::Frame frame;
preview::Image image;
std::array<std::byte, preview::width * 2> row;
std::array<std::byte, (preview::width / 8) * 17> banner;
constexpr unsigned capture_pin = 18, mode_pin = 19;
constexpr unsigned warmup_frames = 8;

// Restore standby and release button watches on every error exit.
struct Resources {
    explicit Resources(mm::camera::Camera& selected) : camera(selected) {}
    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;
    Resources(Resources&&) = delete;
    Resources& operator=(Resources&&) = delete;
    mm::camera::Camera& camera;
    bool capture_watch = false, mode_watch = false, active = false;
    ~Resources() {
        if (active) (void)camera.sleep();
        if (mode_watch) (void)mm::mcu::gpio_unwatch(mode_pin);
        if (capture_watch) (void)mm::mcu::gpio_unwatch(capture_pin);
    }
};

bool poll(unsigned pin, preview::Button& button, std::uint32_t now, bool& pressed) {
    bool high = true, falling = false;
    if (mm::mcu::gpio_take(pin, falling) != mm::mcu::Status::Ok ||
        mm::mcu::gpio_read(pin, high) != mm::mcu::Status::Ok) return false;
    pressed = button.update(high, falling, now);
    return true;
}

int status(mm::display::Display& display, const char8_t* state, preview::Mode mode,
           bool running) {
    // One text line overlays the top 17 image rows; keep the full frame scale.
    banner.fill(std::byte{0xff});
    mm::gfx::Surface surface{preview::width, 17, 1, banner};
    const char8_t* name = mode == preview::Mode::Nearest ? u8"NEAREST" :
                         mode == preview::Mode::Area ? u8"AREA" : u8"AREA+TIME";
    unsigned x = 0;
    const char8_t* parts[] = {state, u8" ", name,
                             running ? u8" K3:STOP K4:MODE" : u8" K3:RUN K4:MODE"};
    for (const auto* text : parts) {
        std::size_t length = 0;
        while (text[length] != 0) ++length;
        if (mm::fonts::render(text, length, mm::fonts::kMono12,
                             mm::display::Color::Black, x, 0, surface) !=
            mm::display::Status::Ok) return 6;
        x += static_cast<unsigned>(length) * mm::fonts::kMono12.advance;
    }
    const mm::gfx::Palette palette{mm::gfx::rgb565_white,
                                   running ? mm::gfx::rgb(0, 64, 0) :
                                             mm::gfx::rgb(0, 0, 64)};
    if (mm::gfx::write(display, surface, 0, 0, palette, {}, row) !=
        mm::display::Status::Ok) return 6;
    return display.refresh(mm::display::Refresh::Full) == mm::display::Status::Ok ? 0 : 7;
}

int show(mm::display::Display& display) {
    for (unsigned y = 17; y < preview::height; ++y) {
        for (unsigned x = 0; x < preview::width; ++x) {
            const unsigned grey = image[y * preview::width + x];
            const auto pixel = ((grey & 0xf8) << 8) | ((grey & 0xfc) << 3) | (grey >> 3);
            row[x * 2] = static_cast<std::byte>(pixel >> 8);
            row[x * 2 + 1] = static_cast<std::byte>(pixel & 0xff);
        }
        if (display.write({0, y, preview::width, 1}, row) != mm::display::Status::Ok)
            return 6;
    }
    return 0;
}
}

int main() {
    auto& display = mm::display::selected_display();
    auto& camera = mm::camera::selected_camera();
    Resources resources{camera};
    if (display.initialize() != mm::display::Status::Ok) return 2;
    const auto sensor = camera.geometry();
    const auto panel = display.geometry();
    if (sensor.width != preview::sensor_width || sensor.height != preview::sensor_height ||
        panel.width != preview::width || panel.height != preview::height ||
        panel.bits_per_pixel != 16) return 5;
    if (mm::mcu::gpio_watch(capture_pin, mm::mcu::Pull::Up, mm::mcu::Edge::Falling) !=
        mm::mcu::Status::Ok) return 9;
    resources.capture_watch = true;
    if (mm::mcu::gpio_watch(mode_pin, mm::mcu::Pull::Up, mm::mcu::Edge::Falling) !=
        mm::mcu::Status::Ok) return 9;
    resources.mode_watch = true;
    preview::Button capture_button, mode_button;
    preview::Mode mode = preview::Mode::Area;
    bool have_frame = false, history = false;
    unsigned warmup = 0;
    const char8_t* state = u8"STOP";
    if (const int error = show(display)) return error;
    if (const int error = status(display, state, mode, false)) return error;

    for (;;) {
        unsigned long ticks = 0;
        bool toggle = false, cycle = false;
        if (mm::mcu::ticks_ms(ticks) != mm::mcu::Status::Ok ||
            !poll(capture_pin, capture_button, static_cast<std::uint32_t>(ticks), toggle) ||
            !poll(mode_pin, mode_button, static_cast<std::uint32_t>(ticks), cycle)) return 9;
        bool changed = false;
        if (toggle) {
            history = false;
            if (resources.active) {
                const auto result = camera.sleep();
                resources.active = false;
                state = result == mm::camera::Status::Ok ? u8"STOP" : u8"ERR8";
            } else {
                have_frame = false; // Warmup captures will overwrite the retained raw frame.
                const auto result = camera.initialize();
                resources.active = result == mm::camera::Status::Ok;
                state = resources.active ? u8"WAIT" : u8"ERR3";
                warmup = warmup_frames;
            }
            changed = true;
        }
        if (cycle) {
            mode = preview::next(mode);
            history = false;
            // Reprocess the retained raw frame while paused, without adding
            // it again to the temporal average.
            if (!resources.active && have_frame) {
                preview::process(frame, image, mode, false);
                if (const int error = show(display)) return error;
            }
            changed = true;
        }
        if (changed)
            if (const int error = status(display, state, mode, resources.active)) return error;
        if (!resources.active) {
            if (mm::mcu::delay_ms(5) != mm::mcu::Status::Ok) return 9;
            continue;
        }
        if (camera.capture(frame, 500) != mm::camera::Status::Ok) {
            (void)camera.sleep();
            resources.active = false;
            history = false;
            have_frame = false; // Failed DMA may have overwritten the raw buffer.
            state = u8"ERR4";
            if (const int error = status(display, state, mode, false)) return error;
            continue; // K3 retries initialization; retain the last displayed image.
        }
        // Let auto exposure settle over several complete frames on each start.
        if (warmup != 0) { --warmup; continue; }
        have_frame = true;
        preview::process(frame, image, mode, history);
        history = true;
        if (const int error = show(display)) return error;
        state = u8"LIVE";
        if (const int error = status(display, state, mode, true)) return error;
    }
}
