// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// mm.display and mm.touch over SDL2, in one module because SDL has one video
// subsystem, one window, one event queue, and shared state between drawing and
// input. Two modules could not own that between them.
//
// This is the backend for developing on a desktop. The DRM provider needs DRM
// master and a compositor holds it, so anything visual there means leaving the
// session for a virtual terminal. A window does not.
module;

// Granular headers, never SDL.h. SDL_main.h defines main as SDL_main on the
// platforms that need it, and the remedy is a macro definition, which project
// code may not write: #include is the only preprocessor directive allowed here.
// SDL_VideoInit and SDL_VideoQuit are declared in SDL_video.h and are what a
// video-only client wants anyway.
#include <SDL2/SDL_error.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_hints.h>
#include <SDL2/SDL_mouse.h>
#include <SDL2/SDL_pixels.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_video.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module platform.linux.sdl;

import mm.display;
import mm.touch;
import platform.linux.map;

// A named, non-exported namespace rather than an unnamed one: Clang emits an
// interface unit's unnamed-namespace objects again in every importer, and a
// provider object must exist exactly once.
namespace platform::linux::sdl_provider {

// The window when the map names no logical size. A panel-shaped default,
// because the applications this exists to show are written for panels.
constexpr unsigned int default_width = 480;
constexpr unsigned int default_height = 320;

// SDL_PIXELFORMAT_RGB565 is a native-endian sixteen-bit format, and
// mm.display's packing is most significant byte first. On a little-endian host
// the two disagree and nothing reports it: SDL accepts the buffer and draws the
// wrong colours. Measured, feeding red as 0xf8 0x00 straight through produced a
// blue pixel. The swap is not an optimisation to skip.
std::uint16_t native_from_packed(std::byte high, std::byte low) {
    return static_cast<std::uint16_t>((static_cast<unsigned int>(high) << 8) |
                                      static_cast<unsigned int>(low));
}

// Owns the SDL video subsystem for as long as it exists. SDL_VideoQuit is safe
// to call after a failed SDL_VideoInit only if it was never entered, so the
// flag is the ownership.
class Video {
public:
    Video() = default;
    Video(const Video&) = delete;
    Video& operator=(const Video&) = delete;
    ~Video() { release(); }

    [[nodiscard]] bool acquire() {
        if (held_) return true;
        if (SDL_VideoInit(nullptr) != 0) return false;
        held_ = true;
        return true;
    }

    void release() {
        if (!held_) return;
        SDL_VideoQuit();
        held_ = false;
    }

    [[nodiscard]] bool held() const { return held_; }

private:
    bool held_ = false;
};

class Surface {
public:
    Surface() = default;
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
    ~Surface() { release(); }

    [[nodiscard]] bool create(unsigned int width, unsigned int height) {
        release();
        if (SDL_CreateWindowAndRenderer(static_cast<int>(width),
                                        static_cast<int>(height), 0, &window_,
                                        &renderer_) != 0)
            return false;
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB565,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     static_cast<int>(width),
                                     static_cast<int>(height));
        if (texture_ == nullptr) {
            release();
            return false;
        }
        SDL_SetWindowTitle(window_, "modules.cpp");
        return true;
    }

    void release() {
        if (texture_ != nullptr) SDL_DestroyTexture(texture_);
        if (renderer_ != nullptr) SDL_DestroyRenderer(renderer_);
        if (window_ != nullptr) SDL_DestroyWindow(window_);
        texture_ = nullptr;
        renderer_ = nullptr;
        window_ = nullptr;
    }

    [[nodiscard]] bool present(const std::vector<std::uint16_t>& pixels,
                               unsigned int width) {
        if (texture_ == nullptr || renderer_ == nullptr) return false;
        const int pitch = static_cast<int>(width) * 2;
        if (SDL_UpdateTexture(texture_, nullptr, pixels.data(), pitch) != 0)
            return false;
        if (SDL_RenderClear(renderer_) != 0) return false;
        if (SDL_RenderCopy(renderer_, texture_, nullptr, nullptr) != 0)
            return false;
        SDL_RenderPresent(renderer_);
        return true;
    }

    [[nodiscard]] bool created() const { return window_ != nullptr; }

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
};

// One subsystem, one window, one queue: drawing and input share this.
Video video;
Surface surface;
unsigned int logical_width = 0;
unsigned int logical_height = 0;
bool closed = false;

// SDL delivers window and input events on one queue, and nothing is serviced
// until somebody drains it. Both providers pump, because either may be the only
// one an application uses.
void pump() {
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_QUIT) closed = true;
        if (event.type == SDL_WINDOWEVENT &&
            event.window.event == SDL_WINDOWEVENT_CLOSE)
            closed = true;
    }
}

void resolve_geometry() {
    const auto& resolved = platform::linux::resolve();
    logical_width = default_width;
    logical_height = default_height;
    if (resolved.status != platform::linux::MapStatus::Ok || !resolved.map)
        return;
    if (resolved.map->display.width != 0) logical_width = resolved.map->display.width;
    if (resolved.map->display.height != 0) logical_height = resolved.map->display.height;
}

class SdlDisplay : public mm::display::Display {
public:
    [[nodiscard]] mm::display::Geometry geometry() const override {
        return {logical_width, logical_height, 16};
    }

    [[nodiscard]] mm::display::Status initialize() override {
        release();
        resolve_geometry();
        if (!video.acquire()) return mm::display::Status::TransportError;
        if (!surface.create(logical_width, logical_height)) {
            video.release();
            return mm::display::Status::TransportError;
        }
        shadow_.assign(static_cast<std::size_t>(logical_width) * logical_height, 0);
        closed = false;
        ready_ = true;
        return mm::display::Status::Ok;
    }

    [[nodiscard]] mm::display::Status clear(mm::display::Color color) override {
        if (!ready_) return mm::display::Status::NotInitialized;
        std::uint16_t value = 0x0000;
        if (color == mm::display::Color::White) value = 0xffff;
        if (color == mm::display::Color::Red) value = 0xf800;
        for (auto& pixel : shadow_) pixel = value;
        return mm::display::Status::Ok;
    }

    // Into the shadow, never the window. write changes display memory and
    // refresh is what makes it visible, which is mm.display's contract and not
    // this provider's choice.
    [[nodiscard]] mm::display::Status write(
        mm::display::Rectangle rectangle,
        std::span<const std::byte> pixels) override {
        if (!ready_) return mm::display::Status::NotInitialized;
        if (rectangle.width == 0 || rectangle.height == 0)
            return mm::display::Status::BadArgument;
        if (rectangle.x + rectangle.width > logical_width ||
            rectangle.y + rectangle.height > logical_height)
            return mm::display::Status::BadArgument;

        const std::size_t needed =
            static_cast<std::size_t>(rectangle.width) * rectangle.height * 2;
        if (pixels.size() < needed) return mm::display::Status::BadArgument;

        std::size_t source = 0;
        for (unsigned int y = 0; y < rectangle.height; ++y) {
            const std::size_t row =
                static_cast<std::size_t>(rectangle.y + y) * logical_width +
                rectangle.x;
            for (unsigned int x = 0; x < rectangle.width; ++x) {
                shadow_[row + x] =
                    native_from_packed(pixels[source], pixels[source + 1]);
                source += 2;
            }
        }
        return mm::display::Status::Ok;
    }

    [[nodiscard]] mm::display::Status refresh(mm::display::Refresh refresh) override {
        if (!ready_) return mm::display::Status::NotInitialized;
        // No dirty region is tracked, so a partial refresh cannot promise less
        // work than a full one, and claiming otherwise would be a lie.
        if (refresh == mm::display::Refresh::Partial)
            return mm::display::Status::Unsupported;
        pump();
        if (!surface.present(shadow_, logical_width))
            return mm::display::Status::TransportError;
        return mm::display::Status::Ok;
    }

    [[nodiscard]] mm::display::Status sleep() override {
        if (!ready_) return mm::display::Status::NotInitialized;
        release();
        return mm::display::Status::Ok;
    }

private:
    void release() {
        surface.release();
        video.release();
        shadow_.clear();
        ready_ = false;
    }

    std::vector<std::uint16_t> shadow_;
    bool ready_ = false;
};

class SdlTouch : public mm::touch::Touch {
public:
    [[nodiscard]] mm::touch::Geometry geometry() const override {
        return {logical_width, logical_height, 1};
    }

    // The window belongs to the display provider. A program that wants touch
    // without a display gets Unsupported rather than a second window.
    [[nodiscard]] mm::touch::Status initialize() override {
        if (!video.held() || !surface.created())
            return mm::touch::Status::Unsupported;
        ready_ = true;
        return mm::touch::Status::Ok;
    }

    [[nodiscard]] mm::touch::Status read(std::span<mm::touch::Point> points,
                                         std::size_t& count) override {
        if (!ready_) return mm::touch::Status::NotInitialized;
        pump();
        if (closed) return mm::touch::Status::TransportError;

        count = 0;
        if (points.empty()) return mm::touch::Status::Ok;

        int x = 0;
        int y = 0;
        const auto buttons = SDL_GetMouseState(&x, &y);
        if ((buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) == 0)
            return mm::touch::Status::Ok;
        if (x < 0 || y < 0) return mm::touch::Status::Ok;

        auto within = [](int value, unsigned int limit) {
            const auto unsigned_value = static_cast<unsigned int>(value);
            return unsigned_value < limit ? unsigned_value : limit - 1;
        };
        points[0] = {within(x, logical_width), within(y, logical_height)};
        count = 1;
        return mm::touch::Status::Ok;
    }

    [[nodiscard]] mm::touch::Status sleep() override {
        if (!ready_) return mm::touch::Status::NotInitialized;
        ready_ = false;
        return mm::touch::Status::Ok;
    }

private:
    bool ready_ = false;
};

SdlDisplay sdl_display;
SdlTouch sdl_touch;

struct Register {
    Register() {
        mm::display::set_display(sdl_display);
        mm::touch::set_touch(sdl_touch);
    }
};

const Register registered;

}  // namespace
