// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The mm.mcu seam over an emulated LCD controller, plus the window that shows
// what its glass would. Each emulated board's provider module constructs one
// EmulatedPanel with its pins, its SPI wiring, and its glass, and registers
// it; this module registers nothing. The adapter is deliberately thin: it
// forwards the driver's bytes and pin edges to the chip and shows the chip's
// image when it changed.
//
// Unlike e-paper, time here is the wall clock. An LCD has no busy line for
// virtual time to drive, and a sketch measures time with millis while it
// draws, so ticks read CLOCK_MONOTONIC and delays sleep.
//
// The glass refreshes on its own, as a real panel scans its frame memory
// without being asked, so a program that draws and then spins without
// another call still shows what it drew. That is the one thread here: it
// starts with the first byte the chip takes, owns SDL, and every
// frame_interval_ms shows the chip's image if it changed. It runs no program
// code and touches the chip only under the lock the seam's calls take, as
// docs/modules-execution.mdy allows a provider. SDL's signal handlers are
// turned off, so Ctrl-C and SIGTERM end the program as they would without a
// window, and closing the window ends it with status zero.
//
// MM_LCD_SNAPSHOT names a file that receives each shown image as a binary
// PPM, replaced atomically, so a run can be checked without watching it. With
// it set, a machine without a display runs windowless instead of failing.
module;

// Granular headers, never SDL.h, for the reasons platform.linux.sdl states.
#include <SDL2/SDL_error.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_hints.h>
#include <SDL2/SDL_pixels.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_video.h>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <optional>
#include <span>
#include <time.h>
#include <vector>

export module platform.linux.lcd.panel;

import mm.mcu;
import platform.linux.lcd.chip;

export namespace platform::linux::lcd {

// One emulated board: what board() reports, the window's title, and the
// chip's pins and glass.
struct PanelBoard {
    std::string_view name;
    std::string_view title;
    std::span<const mm::mcu::Gpio> gpios;
    mm::mcu::SpiWiring spi;
    EmulationOptions chip;
};

}

namespace platform::linux::lcd_panel {

// The window is twice the glass, so a 240 by 320 panel is watchable. The
// texture stays at the glass's size and the renderer scales it.
constexpr unsigned int scale = 2;
constexpr unsigned long frame_interval_ms = 33;

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

    [[nodiscard]] bool create(unsigned int texture_width, unsigned int texture_height,
                              std::string_view title) {
        release();
        if (SDL_CreateWindowAndRenderer(static_cast<int>(texture_width * scale),
                                        static_cast<int>(texture_height * scale), 0,
                                        &window_, &renderer_) != 0)
            return false;
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB565,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     static_cast<int>(texture_width),
                                     static_cast<int>(texture_height));
        if (texture_ == nullptr) {
            release();
            return false;
        }
        const std::string text{title};
        SDL_SetWindowTitle(window_, text.c_str());
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

    [[nodiscard]] bool present(std::span<const std::uint16_t> pixels, unsigned int width) {
        if (texture_ == nullptr || renderer_ == nullptr) return false;
        const int pitch = static_cast<int>(width) * 2;
        if (SDL_UpdateTexture(texture_, nullptr, pixels.data(), pitch) != 0) return false;
        if (SDL_RenderClear(renderer_) != 0) return false;
        if (SDL_RenderCopy(renderer_, texture_, nullptr, nullptr) != 0) return false;
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

}  // namespace platform::linux::lcd_panel

export namespace platform::linux::lcd {

class EmulatedPanel final : public mm::mcu::Platform {
public:
    explicit EmulatedPanel(const PanelBoard& board) : board_(board), chip_(board.chip) {
        start_ = now_us();
        if (const char* path = std::getenv("MM_LCD_SNAPSHOT"); path != nullptr && *path != '\0')
            snapshot_path_ = path;
    }
    ~EmulatedPanel() override {
        stop_ = true;
        if (scanner_.joinable()) scanner_.join();
    }
    EmulatedPanel(const EmulatedPanel&) = delete;
    EmulatedPanel& operator=(const EmulatedPanel&) = delete;

    [[nodiscard]] mm::mcu::Capabilities capabilities() const override {
        return {.board = true, .gpio = true, .spi = true, .timer = true};
    }

    [[nodiscard]] mm::mcu::Board board() const override {
        mm::mcu::Board value{};
        value.name = board_.name;
        value.gpios = board_.gpios;
        value.spi = board_.spi;
        return value;
    }

    // Every line latches what is written and reads it back, so another
    // device's lines on the same board work as plain outputs; the chip's own
    // lines also reach the chip.
    [[nodiscard]] mm::mcu::Status gpio_configure(unsigned int pin, mm::mcu::Direction,
                                                 mm::mcu::Pull) override {
        if (pin >= pin_count) return mm::mcu::Status::Unsupported;
        configured_[pin] = true;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_write(unsigned int pin, bool high) override {
        if (failed_) return mm::mcu::Status::TransportError;
        if (pin >= pin_count) return mm::mcu::Status::Unsupported;
        levels_[pin] = high;
        {
            const std::lock_guard lock{mutex_};
            chip_.gpio_write(pin, high);
        }
        start_scanning();
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_read(unsigned int pin, bool& high) override {
        if (failed_) return mm::mcu::Status::TransportError;
        if (pin >= pin_count || !configured_[pin]) return mm::mcu::Status::Unsupported;
        high = levels_[pin];
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status spi_configure(
        const mm::mcu::SpiConfiguration& value) override {
        if (value.instance != board_.spi.instance || value.baud == 0)
            return mm::mcu::Status::BadArgument;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status spi_write(unsigned int instance,
                                            std::span<const std::byte> data) override {
        if (failed_ || instance != board_.spi.instance) return mm::mcu::Status::TransportError;
        {
            const std::lock_guard lock{mutex_};
            chip_.spi_write(data);
        }
        start_scanning();
        return mm::mcu::Status::Ok;
    }

    // The chip drives nothing back, so a read clocks in zeros.
    [[nodiscard]] mm::mcu::Status spi_transfer(unsigned int instance,
                                               std::span<const std::byte> transmit,
                                               std::span<std::byte> receive) override {
        if (failed_ || instance != board_.spi.instance) return mm::mcu::Status::TransportError;
        if (receive.size() != transmit.size()) return mm::mcu::Status::BadArgument;
        {
            const std::lock_guard lock{mutex_};
            chip_.spi_write(transmit);
        }
        for (auto& value : receive) value = std::byte{};
        start_scanning();
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status delay_ms(unsigned long milliseconds) override {
        return delay(milliseconds, 1000);
    }

    [[nodiscard]] mm::mcu::Status delay_us(unsigned long microseconds) override {
        return delay(microseconds, 1);
    }

    [[nodiscard]] mm::mcu::Status ticks_ms(unsigned long& ticks) override {
        ticks = static_cast<unsigned long>((now_us() - start_) / 1000);
        return failed_ ? mm::mcu::Status::TransportError : mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status ticks_us(unsigned long& ticks) override {
        ticks = static_cast<unsigned long>(now_us() - start_);
        return failed_ ? mm::mcu::Status::TransportError : mm::mcu::Status::Ok;
    }

private:
    static constexpr unsigned int pin_count = 32;

    static std::uint64_t now_us() {
        timespec value{};
        ::clock_gettime(CLOCK_MONOTONIC, &value);
        return static_cast<std::uint64_t>(value.tv_sec) * 1'000'000u +
               static_cast<std::uint64_t>(value.tv_nsec) / 1000u;
    }

    [[nodiscard]] mm::mcu::Status delay(unsigned long count, unsigned long unit_us) {
        if (failed_) return mm::mcu::Status::TransportError;
        if (count > std::numeric_limits<unsigned long>::max() / unit_us)
            return mm::mcu::Status::BadArgument;
        if (!sleep(count * unit_us)) return mm::mcu::Status::TransportError;
        return failed_ ? mm::mcu::Status::TransportError : mm::mcu::Status::Ok;
    }

    static bool sleep(unsigned long microseconds) {
        if (microseconds == 0) return true;
        const auto seconds = microseconds / 1'000'000UL;
        if (seconds > static_cast<unsigned long>(std::numeric_limits<time_t>::max()))
            return false;
        timespec request{static_cast<time_t>(seconds),
                         static_cast<long>((microseconds % 1'000'000UL) * 1000UL)};
        while (true) {
            timespec remaining{};
            const int result = ::clock_nanosleep(CLOCK_MONOTONIC, 0, &request, &remaining);
            if (result == 0) return true;
            if (result == EINTR) {
                request = remaining;
                continue;
            }
            return false;
        }
    }

    // The glass starts scanning with the first byte or edge the chip sees, so
    // a program that never draws opens no window.
    void start_scanning() {
        if (scanner_.joinable() || stop_) return;
        scanner_ = std::thread{[this] { scan(); }};
    }

    // The panel's own refresh. A window that cannot be opened, or a snapshot
    // that cannot be written, fails every later call of the seam: the program
    // would keep drawing on a panel that no longer exists, and answering Ok
    // would hide it.
    void scan() {
        lcd_panel::Video video;
        lcd_panel::Window window;
        const auto& options = chip_.options();
        SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
        bool headless = false;
        if (!video.acquire()) {
            if (snapshot_path_.empty()) {
                failed_ = true;
                return;
            }
            headless = true;
        }
        if (!headless &&
            !window.create(options.visible_width, options.visible_height, board_.title)) {
            failed_ = true;
            return;
        }
        std::vector<std::uint16_t> frame(static_cast<std::size_t>(options.visible_width) *
                                         options.visible_height);
        while (!stop_) {
            if (!headless) {
                window.pump();
                if (window.closed()) {
                    // The user closed the panel: the program ends, as it
                    // would if its terminal were closed.
                    window.release();
                    video.release();
                    std::_Exit(0);
                }
            }
            bool fresh = false;
            {
                const std::lock_guard lock{mutex_};
                if (chip_.changed()) {
                    chip_.render(frame);
                    chip_.presented();
                    fresh = true;
                }
            }
            if (fresh) {
                if (!headless && !window.present(frame, options.visible_width)) {
                    failed_ = true;
                    return;
                }
                if (!snapshot_path_.empty() &&
                    !write_snapshot(frame, options.visible_width, options.visible_height)) {
                    failed_ = true;
                    return;
                }
            }
            static_cast<void>(sleep(lcd_panel::frame_interval_ms * 1000));
        }
    }

    // The shown image as a binary PPM, written beside the named file and
    // renamed over it, so a reader never sees half a frame.
    [[nodiscard]] bool write_snapshot(std::span<const std::uint16_t> frame, unsigned int width,
                                      unsigned int height) const {
        const auto temporary = snapshot_path_ + ".tmp";
        std::FILE* file = std::fopen(temporary.c_str(), "wb");
        if (file == nullptr) return false;
        bool ok = std::fprintf(file, "P6\n%u %u\n255\n", width, height) > 0;
        for (const auto pixel : frame) {
            if (!ok) break;
            const unsigned char rgb[3]{
                static_cast<unsigned char>(((pixel >> 11) & 0x1f) * 255 / 31),
                static_cast<unsigned char>(((pixel >> 5) & 0x3f) * 255 / 63),
                static_cast<unsigned char>((pixel & 0x1f) * 255 / 31)};
            ok = std::fwrite(rgb, 1, sizeof rgb, file) == sizeof rgb;
        }
        ok = std::fclose(file) == 0 && ok;
        return ok && std::rename(temporary.c_str(), snapshot_path_.c_str()) == 0;
    }

    PanelBoard board_;
    EmulatedSt7789 chip_;
    std::mutex mutex_;
    std::thread scanner_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> failed_{false};
    bool configured_[pin_count]{};
    bool levels_[pin_count]{};
    std::uint64_t start_ = 0;
    std::string snapshot_path_;
};

}  // namespace platform::linux::lcd
