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
//
// It also captures an application's screens unattended. MM_SCREENSHOT_DIR
// names a directory that receives refreshed frames as binary PPMs, every
// MM_SCREENSHOT_EVERY-th refresh (0: none) up to MM_SCREENSHOT_LIMIT files,
// and F12 writes the current frame on demand. MM_TOUCH_SCRIPT names a file of
// timed touch steps that replaces the pointer, so a run can visit every
// screen, shoot each, and end. MM_SDL_DISPLAY_SIZE=WxH sets the logical size.
// With SDL_VIDEODRIVER=offscreen no window is created at all.
module;

// Granular headers, never SDL.h. SDL_main.h defines main as SDL_main on the
// platforms that need it, and the remedy is a macro definition, which project
// code may not write: #include is the only preprocessor directive allowed here.
// SDL_VideoInit and SDL_VideoQuit are declared in SDL_video.h and are what a
// video-only client wants anyway.
#include <SDL2/SDL_error.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_hints.h>
#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_mouse.h>
#include <SDL2/SDL_pixels.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_video.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <utility>
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

// The display memory, at the logical size, in SDL's native RGB565. Shared
// rather than the display's own because the touch script shoots it too.
std::vector<std::uint16_t> shadow;
// Frames presented since the display initialized; the touch script waits on it.
unsigned long refreshes = 0;
bool shot_requested = false;

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
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F12 &&
            event.key.repeat == 0)
            shot_requested = true;
    }
}

[[nodiscard]] std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::string{value} : std::string{};
}

// A decimal variable, or fallback when it is unset or malformed. Zero is a
// value only where zero_allowed says so.
[[nodiscard]] unsigned long environment_number(const char* name, unsigned long fallback,
                                               bool zero_allowed = false) {
    const auto text = environment(name);
    if (text.empty()) return fallback;
    char* end = nullptr;
    const auto value = std::strtoul(text.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || (value == 0 && !zero_allowed)) return fallback;
    return value;
}

// Where frames go: MM_SCREENSHOT_DIR, empty when capture is off. Every
// screenshot_every-th refresh is written; zero writes none, leaving only F12
// and a touch script's shot steps.
std::string screenshot_directory;
unsigned long screenshot_every = 1;
unsigned long screenshot_limit = 1000;
unsigned long screenshots_written = 0;

void read_capture_settings() {
    screenshot_directory = environment("MM_SCREENSHOT_DIR");
    screenshot_every = environment_number("MM_SCREENSHOT_EVERY", 1, true);
    screenshot_limit = environment_number("MM_SCREENSHOT_LIMIT", 1000);
    screenshots_written = 0;
}

// The shadow as a binary PPM, written beside the named file and renamed over
// it, so a reader never sees half a frame -- the format and the method
// platform.linux.lcd's MM_LCD_SNAPSHOT already use.
[[nodiscard]] bool write_frame(const std::string& path) {
    const auto temporary = path + ".tmp";
    std::FILE* file = std::fopen(temporary.c_str(), "wb");
    if (file == nullptr) return false;
    bool ok = std::fprintf(file, "P6\n%u %u\n255\n", logical_width, logical_height) > 0;
    for (const auto pixel : shadow) {
        if (!ok) break;
        const unsigned char rgb[3]{
            static_cast<unsigned char>(((pixel >> 11) & 0x1f) * 255 / 31),
            static_cast<unsigned char>(((pixel >> 5) & 0x3f) * 255 / 63),
            static_cast<unsigned char>((pixel & 0x1f) * 255 / 31)};
        ok = std::fwrite(rgb, 1, sizeof rgb, file) == sizeof rgb;
    }
    ok = std::fclose(file) == 0 && ok;
    if (ok && std::rename(temporary.c_str(), path.c_str()) == 0) return true;
    std::fprintf(stderr, "sdl: cannot write %s\n", path.c_str());
    return false;
}

// A file in the screenshot directory, or the current one when none is set.
[[nodiscard]] std::string frame_path(const std::string& name) {
    const auto& directory = screenshot_directory;
    if (directory.empty()) return name + ".ppm";
    return directory + (directory.back() == '/' ? "" : "/") + name + ".ppm";
}

// After a refresh: the periodic dump and an F12 request.
void capture_after_refresh() {
    if (!screenshot_directory.empty() && screenshot_every != 0 &&
        refreshes % screenshot_every == 0 &&
        screenshots_written < screenshot_limit) {
        char name[32]{};
        std::snprintf(name, sizeof name, "frame-%06lu", refreshes);
        if (write_frame(frame_path(name))) ++screenshots_written;
    }
    if (shot_requested) {
        shot_requested = false;
        char name[32]{};
        std::snprintf(name, sizeof name, "shot-%06lu", refreshes);
        static_cast<void>(write_frame(frame_path(name)));
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

// MM_SDL_DISPLAY_SIZE=WxH overrides the size the map gives, so a capture can
// match the panel an application was written for. It exists because the
// generic boards register no map override reader, which makes
// MM_LINUX_DEVICE_MAP unusable on them. A malformed value is ignored.
void apply_size_override() {
    const char* value = std::getenv("MM_SDL_DISPLAY_SIZE");
    if (value == nullptr) return;
    char* end = nullptr;
    const auto width = std::strtoul(value, &end, 10);
    if (end == nullptr || (*end != 'x' && *end != 'X')) return;
    const auto height = std::strtoul(end + 1, &end, 10);
    if (end == nullptr || *end != '\0' || width == 0 || height == 0 || width > 4096 ||
        height > 4096)
        return;
    logical_width = static_cast<unsigned int>(width);
    logical_height = static_cast<unsigned int>(height);
}

class SdlDisplay : public mm::display::Display {
public:
    [[nodiscard]] mm::display::Geometry geometry() const override {
        return {logical_width, logical_height, 16};
    }

    [[nodiscard]] mm::display::Status initialize() override {
        release();
        resolve_geometry();
        apply_size_override();
        if (!video.acquire()) return mm::display::Status::TransportError;
        if (!surface.create(logical_width, logical_height)) {
            video.release();
            return mm::display::Status::TransportError;
        }
        shadow.assign(static_cast<std::size_t>(logical_width) * logical_height, 0);
        read_capture_settings();
        refreshes = 0;
        closed = false;
        ready_ = true;
        return mm::display::Status::Ok;
    }

    [[nodiscard]] mm::display::Status clear(mm::display::Color color) override {
        if (!ready_) return mm::display::Status::NotInitialized;
        std::uint16_t value = 0x0000;
        if (color == mm::display::Color::White) value = 0xffff;
        if (color == mm::display::Color::Red) value = 0xf800;
        for (auto& pixel : shadow) pixel = value;
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
                shadow[row + x] =
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
        if (!surface.present(shadow, logical_width))
            return mm::display::Status::TransportError;
        ++refreshes;
        capture_after_refresh();
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
        shadow.clear();
        ready_ = false;
    }

    bool ready_ = false;
};

// A touch script: one step per line, '#' starting a comment.
//
//   wait-refresh N     until N more frames have been presented
//   wait-ms N          until N milliseconds have passed
//   tap X Y            press at (X, Y) for one read, then release for one
//   hold X Y N         press at (X, Y) for N reads, then release for one
//   shot NAME          write the current frame as NAME.ppm
//   exit               end the run as closing the window does
//
// Coordinates are logical pixels. Presses span whole reads, so an
// application acting on the press edge sees one press and one release. When
// the steps run out the touch stays released.
struct Step {
    enum class Kind { WaitRefresh, WaitMs, Press, Shot, Exit } kind = Kind::Exit;
    unsigned long count = 0;
    unsigned int x = 0;
    unsigned int y = 0;
    std::string name;
};

[[nodiscard]] bool parse_script(const std::string& path, std::vector<Step>& steps) {
    std::ifstream input(path);
    if (!input) {
        std::fprintf(stderr, "touch script: cannot read %s\n", path.c_str());
        return false;
    }
    std::string line;
    unsigned int number = 0;
    while (std::getline(input, line)) {
        ++number;
        if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        std::istringstream words(line);
        std::string command;
        if (!(words >> command)) continue;
        Step step;
        bool ok = true;
        if (command == "wait-refresh") {
            step.kind = Step::Kind::WaitRefresh;
            ok = static_cast<bool>(words >> step.count);
        } else if (command == "wait-ms") {
            step.kind = Step::Kind::WaitMs;
            ok = static_cast<bool>(words >> step.count);
        } else if (command == "tap") {
            step.kind = Step::Kind::Press;
            step.count = 1;
            ok = static_cast<bool>(words >> step.x >> step.y);
        } else if (command == "hold") {
            step.kind = Step::Kind::Press;
            ok = static_cast<bool>(words >> step.x >> step.y >> step.count) && step.count != 0;
        } else if (command == "shot") {
            step.kind = Step::Kind::Shot;
            ok = static_cast<bool>(words >> step.name) &&
                 step.name.find('/') == std::string::npos;
        } else if (command == "exit") {
            step.kind = Step::Kind::Exit;
        } else {
            ok = false;
        }
        std::string extra;
        if (ok && words >> extra) ok = false;
        if (ok && step.kind == Step::Kind::Press &&
            (step.x >= logical_width || step.y >= logical_height))
            ok = false;
        if (!ok) {
            std::fprintf(stderr, "touch script: %s:%u: cannot use \"%s\"\n", path.c_str(),
                         number, line.c_str());
            return false;
        }
        steps.push_back(std::move(step));
    }
    return true;
}

// Plays a script one read at a time.
class Script {
public:
    [[nodiscard]] bool load(const std::string& path) {
        steps_.clear();
        next_ = 0;
        started_ = false;
        active_ = parse_script(path, steps_);
        return active_;
    }

    [[nodiscard]] bool active() const { return active_; }

    // The touch for this read: true and a point while pressing.
    [[nodiscard]] bool read(mm::touch::Point& point) {
        while (next_ < steps_.size()) {
            auto& step = steps_[next_];
            if (!started_) {
                started_ = true;
                refresh_target_ = refreshes + step.count;
                start_ms_ = SDL_GetTicks64();
                remaining_ = step.count;
                releasing_ = false;
            }
            switch (step.kind) {
                case Step::Kind::WaitRefresh:
                    if (refreshes < refresh_target_) return false;
                    break;
                case Step::Kind::WaitMs:
                    if (SDL_GetTicks64() - start_ms_ < step.count) return false;
                    break;
                case Step::Kind::Press:
                    if (remaining_ != 0) {
                        --remaining_;
                        point = {step.x, step.y};
                        return true;
                    }
                    if (!releasing_) {
                        releasing_ = true;
                        return false;
                    }
                    break;
                case Step::Kind::Shot:
                    static_cast<void>(write_frame(frame_path(step.name)));
                    break;
                case Step::Kind::Exit:
                    closed = true;
                    return false;
            }
            ++next_;
            started_ = false;
        }
        return false;
    }

private:
    std::vector<Step> steps_;
    std::size_t next_ = 0;
    bool started_ = false;
    bool active_ = false;
    bool releasing_ = false;
    unsigned long refresh_target_ = 0;
    unsigned long remaining_ = 0;
    std::uint64_t start_ms_ = 0;
};

Script script;

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
        // A script that cannot be read is a failed run, not a silent fall
        // back to the pointer.
        if (const auto path = environment("MM_TOUCH_SCRIPT"); !path.empty() && !script.load(path))
            return mm::touch::Status::TransportError;
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

        if (script.active()) {
            mm::touch::Point point{};
            const bool pressed = script.read(point);
            if (closed) return mm::touch::Status::TransportError;
            if (pressed) {
                points[0] = point;
                count = 1;
            }
            return mm::touch::Status::Ok;
        }

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
