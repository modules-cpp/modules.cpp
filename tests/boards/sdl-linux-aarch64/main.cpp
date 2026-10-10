// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
// platform.linux.sdl's unattended capture: the frame dump and the touch script,
// run headless with SDL's offscreen video driver.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

import mm.display;
import mm.touch;

namespace {

// A pattern every pixel of which differs from its neighbours' in some channel,
// so a misplaced row or column shows.
std::uint16_t pattern(unsigned int x, unsigned int y, unsigned int width, unsigned int height) {
    const unsigned int red = x * 31u / width;
    const unsigned int green = y * 63u / height;
    const unsigned int blue = (x + y) & 31u;
    return static_cast<std::uint16_t>((red << 11) | (green << 5) | blue);
}

// The PPM the provider writes, compared with the pattern converted the same
// way: five and six bits scaled to eight.
bool frame_matches(const std::string& path, unsigned int width, unsigned int height) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        std::fprintf(stderr, "missing %s\n", path.c_str());
        return false;
    }
    unsigned int w = 0, h = 0, maximum = 0;
    bool ok = std::fscanf(file, "P6 %u %u %u", &w, &h, &maximum) == 3 && w == width &&
              h == height && maximum == 255 && std::fgetc(file) == '\n';
    for (unsigned int y = 0; ok && y < height; ++y) {
        for (unsigned int x = 0; ok && x < width; ++x) {
            std::array<unsigned char, 3> rgb{};
            ok = std::fread(rgb.data(), 1, rgb.size(), file) == rgb.size();
            const auto pixel = pattern(x, y, width, height);
            ok = ok && rgb[0] == ((pixel >> 11) & 0x1f) * 255 / 31 &&
                 rgb[1] == ((pixel >> 5) & 0x3f) * 255 / 63 && rgb[2] == (pixel & 0x1f) * 255 / 31;
            if (!ok) std::fprintf(stderr, "%s differs at %u,%u\n", path.c_str(), x, y);
        }
    }
    ok = ok && std::fgetc(file) == EOF;
    std::fclose(file);
    return ok;
}

bool exists(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return false;
    std::fclose(file);
    return true;
}

struct Touch {
    bool pressed = false;
    unsigned int x = 0;
    unsigned int y = 0;
};

}  // namespace

int main() {
    char directory_template[] = "/tmp/mm-sdl-capture-XXXXXX";
    const char* directory_name = mkdtemp(directory_template);
    if (directory_name == nullptr) return 1;
    const std::string directory{directory_name};
    const auto script_path = directory + "/script.touch";
    if (std::FILE* script = std::fopen(script_path.c_str(), "w")) {
        std::fputs("# a comment, and a blank line\n\n"
                   "wait-refresh 1\n"
                   "tap 10 20\n"
                   "hold 30 40 3   # three reads\n"
                   "shot named\n"
                   "exit\n",
                   script);
        std::fclose(script);
    } else {
        return 2;
    }
    setenv("SDL_VIDEODRIVER", "offscreen", 1);
    setenv("MM_SCREENSHOT_DIR", directory.c_str(), 1);
    setenv("MM_SCREENSHOT_EVERY", "2", 1);
    setenv("MM_TOUCH_SCRIPT", script_path.c_str(), 1);

    auto& display = mm::display::selected_display();
    auto& touch = mm::touch::selected_touch();
    if (display.initialize() != mm::display::Status::Ok) return 3;
    if (touch.initialize() != mm::touch::Status::Ok) return 4;
    const auto geometry = display.geometry();
    const unsigned int width = geometry.width;
    const unsigned int height = geometry.height;
    if (width == 0 || height == 0) return 5;

    std::vector<std::byte> frame(static_cast<std::size_t>(width) * height * 2u);
    for (unsigned int y = 0; y < height; ++y) {
        for (unsigned int x = 0; x < width; ++x) {
            const auto pixel = pattern(x, y, width, height);
            const auto at = (static_cast<std::size_t>(y) * width + x) * 2u;
            frame[at] = static_cast<std::byte>(pixel >> 8);
            frame[at + 1] = static_cast<std::byte>(pixel & 0xff);
        }
    }
    if (display.write({0, 0, width, height}, frame) != mm::display::Status::Ok) return 6;
    // Refreshes 1 and 2: only the second is dumped at MM_SCREENSHOT_EVERY=2.
    for (int i = 0; i < 2; ++i)
        if (display.refresh(mm::display::Refresh::Full) != mm::display::Status::Ok) return 7;

    // One read, then one refresh, until the script ends the run. The first
    // read starts wait-refresh at two refreshes, so it waits for the third.
    std::vector<Touch> seen;
    std::array<mm::touch::Point, 1> points{};
    bool ended = false;
    for (int i = 0; i < 32 && !ended; ++i) {
        std::size_t count = 0;
        const auto status = touch.read(points, count);
        if (status == mm::touch::Status::TransportError) {
            ended = true;
            break;
        }
        if (status != mm::touch::Status::Ok) return 8;
        seen.push_back(count == 0 ? Touch{} : Touch{true, points[0].x, points[0].y});
        if (display.refresh(mm::display::Refresh::Full) != mm::display::Status::Ok) return 9;
    }
    if (!ended) return 10;

    const std::vector<Touch> expected{
        {},                   // waiting for the third refresh
        {true, 10, 20}, {},   // tap
        {true, 30, 40}, {true, 30, 40}, {true, 30, 40}, {},    // hold for three
    };
    if (seen.size() != expected.size()) {
        std::fprintf(stderr, "saw %zu reads, expected %zu\n", seen.size(), expected.size());
        return 11;
    }
    for (std::size_t i = 0; i < seen.size(); ++i) {
        if (seen[i].pressed != expected[i].pressed || seen[i].x != expected[i].x ||
            seen[i].y != expected[i].y) {
            std::fprintf(stderr, "read %zu: %d %u,%u\n", i, seen[i].pressed ? 1 : 0, seen[i].x,
                         seen[i].y);
            return 12;
        }
    }

    // Seven reads each followed by a refresh: refreshes 3 to 9, so even
    // frames 2, 4, 6, 8 were dumped and no odd one.
    for (unsigned int n = 1; n <= 9; ++n) {
        char name[32]{};
        std::snprintf(name, sizeof name, "/frame-%06u.ppm", n);
        const auto path = directory + name;
        if (n % 2 == 0 && !frame_matches(path, width, height)) return 13;
        if (n % 2 != 0 && exists(path)) return 14;
        std::remove(path.c_str());
    }
    if (!frame_matches(directory + "/named.ppm", width, height)) return 15;
    std::remove((directory + "/named.ppm").c_str());

    // MM_SCREENSHOT_EVERY=0 writes no periodic frame; a script's shot still
    // writes. A second initialization rereads the variables.
    (void)touch.sleep();
    setenv("MM_SCREENSHOT_EVERY", "0", 1);
    if (std::FILE* script = std::fopen(script_path.c_str(), "w")) {
        std::fputs("shot only\nexit\n", script);
        std::fclose(script);
    } else {
        return 16;
    }
    if (display.initialize() != mm::display::Status::Ok ||
        touch.initialize() != mm::touch::Status::Ok)
        return 17;
    if (display.write({0, 0, width, height}, frame) != mm::display::Status::Ok) return 18;
    for (int i = 0; i < 4; ++i)
        if (display.refresh(mm::display::Refresh::Full) != mm::display::Status::Ok) return 19;
    std::size_t count = 0;
    if (touch.read(points, count) != mm::touch::Status::TransportError) return 20;
    for (unsigned int n = 1; n <= 4; ++n) {
        char name[32]{};
        std::snprintf(name, sizeof name, "/frame-%06u.ppm", n);
        if (exists(directory + name)) return 21;
    }
    if (!frame_matches(directory + "/only.ppm", width, height)) return 22;
    std::remove((directory + "/only.ppm").c_str());
    std::remove(script_path.c_str());
    std::remove(directory.c_str());

    (void)touch.sleep();
    (void)display.sleep();
    std::puts("sdl capture: frame dump and touch script ok");
    return 0;
}
