// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The mm.mcu seam over the emulated SSD1680, plus the window that shows its
// RAM. The adapter is deliberately thin: it forwards the driver's bytes and
// pin edges to the chip and feeds the chip real milliseconds, so the
// driver's own busy polling is what observes the emulated refresh, exactly
// as it observes the real one.
//
// The window shows the RAM only when a master activation releases busy —
// the moment the physical panel updates. write never touches it.
module;

// Granular headers, never SDL.h, for the reasons platform.linux.sdl states.
#include <SDL2/SDL_error.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_pixels.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_video.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <time.h>
#include <vector>

export module platform.linux.epaper.device;

import mm.mcu;
import platform.linux.epaper.chip;

using platform::linux::epaper::EmulationOptions;
using platform::linux::epaper::EmulatedSsd1680;

// A named, non-exported namespace rather than an unnamed one: Clang emits an
// interface unit's unnamed-namespace objects again in every importer, and a
// provider object must exist exactly once.
namespace platform::linux::epaper_device_provider {

// One bit per pixel is the panel's native resolution; the upscale is a
// window convenience so the emulation is watchable at arm's length. The
// texture is kept at native size and the renderer scales it, so the fill
// and the chip agree on one layout.
constexpr unsigned int scale = 3;

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

class Window {
public:
    Window() = default;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    ~Window() { release(); }

    [[nodiscard]] bool create(unsigned int window_width, unsigned int window_height,
                              unsigned int texture_width,
                              unsigned int texture_height) {
        release();
        if (SDL_CreateWindowAndRenderer(static_cast<int>(window_width),
                                        static_cast<int>(window_height), 0, &window_,
                                        &renderer_) != 0)
            return false;
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB565,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     static_cast<int>(texture_width),
                                     static_cast<int>(texture_height));
        if (texture_ == nullptr) {
            release();
            return false;
        }
        SDL_SetWindowTitle(window_, "epaper emulation");
        SDL_RenderSetScale(renderer_, scale, scale);
        return true;
    }

    void release() {
        if (texture_ != nullptr) SDL_DestroyTexture(texture_);
        if (renderer_ != nullptr) SDL_DestroyRenderer(renderer_);
        if (window_ != nullptr) SDL_DestroyWindow(window_);
        texture_ = nullptr;
        renderer_ = nullptr;
        window_ = nullptr;
        closed_ = false;
    }

    void pump() {
        if (window_ == nullptr) return;
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) closed_ = true;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE)
                closed_ = true;
        }
    }

    [[nodiscard]] bool present(std::span<const std::uint16_t> pixels,
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
    [[nodiscard]] bool closed() const { return closed_; }

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    bool closed_ = false;
};

class EmulatedPlatform final : public mm::mcu::Platform {
public:
    EmulatedPlatform() : chip_(options_) {}
    ~EmulatedPlatform() override { window_.release(); video_.release(); }

    [[nodiscard]] mm::mcu::Capabilities capabilities() const override {
        return {.board = true, .gpio = true, .spi = true, .timer = true};
    }

    [[nodiscard]] mm::mcu::Board board() const override {
        // The board's fixed wiring, the same pins EmulationOptions names by
        // default and the display provider wires.
        static const mm::mcu::Gpio gpios[]{
            {8, "DC"}, {9, "CS"}, {10, "SCLK"}, {11, "SDIN"},
            {12, "RST"}, {13, "BUSY"}};
        return {"epaper-linux", gpios, std::nullopt};
    }

    [[nodiscard]] mm::mcu::Status gpio_configure(unsigned int pin,
                                                 mm::mcu::Direction direction,
                                                 mm::mcu::Pull) override {
        if (pin >= pin_count) return mm::mcu::Status::Unsupported;
        directions_[pin] = direction;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_write(unsigned int pin,
                                             bool high) override {
        if (present_failed_) return mm::mcu::Status::TransportError;
        if (pin >= pin_count) return mm::mcu::Status::Unsupported;
        chip_.gpio_write(pin, high);
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_read(unsigned int pin,
                                            bool& high) override {
        if (present_failed_) return mm::mcu::Status::TransportError;
        if (pin >= pin_count || !chip_.gpio_read(pin, high))
            return mm::mcu::Status::Unsupported;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status spi_configure(
        const mm::mcu::SpiConfiguration& value) override {
        if (value.instance != 0 || value.baud == 0)
            return mm::mcu::Status::BadArgument;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status spi_write(
        unsigned int instance, std::span<const std::byte> data) override {
        if (present_failed_ || instance != 0)
            return mm::mcu::Status::TransportError;
        chip_.spi_write(data);
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status delay_ms(unsigned long milliseconds) override {
        if (present_failed_) return mm::mcu::Status::TransportError;
        if (milliseconds != 0 && !sleep(milliseconds))
            return mm::mcu::Status::TransportError;
        chip_.advance(milliseconds);
        service_refresh();
        return present_failed_ ? mm::mcu::Status::TransportError
                               : mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status ticks_ms(unsigned long& ticks) override {
        ticks = chip_.ticks_ms();
        return mm::mcu::Status::Ok;
    }

private:
    static constexpr unsigned int pin_count = 16;

    static bool sleep(unsigned long milliseconds) {
        if (milliseconds == 0) return true;
        const auto seconds = milliseconds / 1000;
        if (seconds >
            static_cast<unsigned long>(std::numeric_limits<time_t>::max()))
            return false;
        timespec request{static_cast<time_t>(seconds),
                         static_cast<long>((milliseconds % 1000) * 1'000'000UL)};
        while (true) {
            timespec remaining{};
            const int result =
                ::clock_nanosleep(CLOCK_MONOTONIC, 0, &request, &remaining);
            if (result == 0) return true;
            if (result == EINTR) {
                request = remaining;
                continue;
            }
            return false;
        }
    }

    // The refresh the chip finished gets shown once, then acknowledged. A
    // window that cannot be shown is a transport failure for every later
    // operation: the driver would keep running against a panel that no
    // longer exists, and answering Ok would hide it.
    void service_refresh() {
        window_.pump();
        if (video_.held() && window_.closed()) present_failed_ = true;
        if (!chip_.refresh_due()) return;

        if (!video_.held() && !video_.acquire()) {
            present_failed_ = true;
            return;
        }
        if (!window_.created() &&
            !window_.create(options_.width * scale, options_.height * scale,
                            options_.width, options_.height)) {
            present_failed_ = true;
            return;
        }
        frame_.resize(frame_bytes());
        fill_frame();
        present_failed_ = !window_.present(frame_, options_.width);
        chip_.refresh_presented();
    }

    [[nodiscard]] std::size_t frame_bytes() const {
        return static_cast<std::size_t>(options_.width) * options_.height;
    }

    void fill_frame() {
        const auto black_white = chip_.black_white_ram();
        const auto chromatic = chip_.chromatic_ram();
        std::size_t source = 0;
        for (unsigned int y = 0; y < options_.height; ++y) {
            for (unsigned int x = 0; x < options_.width; ++x, ++source) {
                const std::byte mask{static_cast<unsigned char>(
                    1u << (7 - (x & 7)))};
                const bool red =
                    (chromatic[static_cast<std::size_t>(y) * row_bytes() +
                               (x >> 3)] &
                     mask) != std::byte{};
                const bool white =
                    (black_white[static_cast<std::size_t>(y) * row_bytes() +
                                 (x >> 3)] &
                     mask) != std::byte{};
                frame_[source] = red ? 0xf800 : white ? 0xffff : 0x0000;
            }
        }
    }

    [[nodiscard]] std::size_t row_bytes() const {
        return (static_cast<std::size_t>(options_.width) + 7) / 8;
    }

    EmulationOptions options_;
    EmulatedSsd1680 chip_;
    Video video_;
    Window window_;
    std::vector<std::uint16_t> frame_;
    std::optional<mm::mcu::Direction> directions_[pin_count]{};
    bool present_failed_ = false;
};

EmulatedPlatform emulated_platform;
struct Register {
    Register() { mm::mcu::set_platform(emulated_platform); }
};
const Register registered;

}  // namespace
