// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <optional>
#include <string>
#include <vector>

export module platform.linux.map;

export namespace platform::linux {

enum class MapStatus { Ok, NoDefaults, FileError, SyntaxError };
enum class SelectorKind { Auto, Index, Name };
enum class RtcConvention { Utc, Local };

struct Selector {
    SelectorKind kind = SelectorKind::Auto;
    unsigned int index = 0;
    std::string name;
};

struct GpioEntry {
    std::string chip;
    unsigned int offset = 0;
    std::string name;
};

struct SpiEntry {
    std::string path;
    unsigned int clock_gpio = 0;
    unsigned int transmit_gpio = 0;
    std::optional<unsigned int> receive_gpio;
    unsigned long max_speed = 0;
    bool speed_fixed = false;
    unsigned int mode = 0;
    bool least_significant_first = false;
};

struct I2cEntry {
    unsigned int adapter = 0;
    unsigned int data_gpio = 0;
    unsigned int clock_gpio = 0;
    unsigned long baud = 400'000;
    bool baud_fixed = true;
};

struct UartEntry {
    std::string path;
    unsigned long baud = 115'200;
    unsigned int parity = 0;  // 0 none, 1 even, 2 odd
    unsigned int data_bits = 8;
    unsigned int stop_bits = 1;
    unsigned long write_deadline_ms = 500;
};

struct RtcEntry {
    std::string path;
    RtcConvention convention = RtcConvention::Utc;
    bool allow_write = false;
};

struct DisplayEntry {
    Selector card;
    Selector connector;
    Selector mode;
    unsigned int width = 0;
    unsigned int height = 0;
};

struct TouchEntry {
    Selector device;
    bool invert_x = false;
    bool invert_y = false;
    bool swap_axes = false;
};

struct ImuEntry {
    Selector device;
    Selector trigger;
    unsigned long timeout_ms = 500;
};

struct Map {
    std::string board_name = "linux";
    std::optional<std::string> led_name;
    std::optional<unsigned int> led_gpio;
    bool led_active_high = true;
    std::vector<GpioEntry> gpios;
    std::vector<SpiEntry> spis;
    std::vector<I2cEntry> i2cs;
    std::vector<UartEntry> uarts;
    RtcEntry rtc;
    DisplayEntry display;
    TouchEntry touch;
    ImuEntry imu;
};

struct ParseError {
    std::string file;
    unsigned int line = 0;
    unsigned int previous_line = 0;
    std::string key;
    std::string reason;
};

struct Resolution {
    MapStatus status = MapStatus::NoDefaults;
    const Map* map = nullptr;
    ParseError error;
};

// The selected provider registers one static-lifetime map. The first resolve
// samples MM_LINUX_DEVICE_MAP and returns a process-lifetime immutable result.
void set_map(const Map& defaults);
[[nodiscard]] const Resolution& resolve();

// Public for machine-independent parser tests and qualification tools.
[[nodiscard]] MapStatus apply_override(Map& map, const std::string& path,
                                       ParseError& error);

}
