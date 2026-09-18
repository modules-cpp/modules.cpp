// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <string_view>

export module mm.sketch;

import mm.mcu;
import mm.stdio;

export namespace mm::sketch {

enum class Status {
    Ok,
    BadArgument,
    Unsupported,
    Busy,
    Timeout,
    TransportError,
    NotInitialized
};

[[nodiscard]] Status lastError();
[[nodiscard]] const char* lastCall();
void clearError();

using Setup = void (*)();
using Loop = void (*)();

int run(Setup setup, Loop loop);
void requestExit(int code = 0);
[[nodiscard]] bool exitRequested();
[[nodiscard]] int exitCode();

void dispatch();

enum class Mode { Input, Output, InputPullup, InputPulldown };
inline constexpr Mode INPUT = Mode::Input;
inline constexpr Mode OUTPUT = Mode::Output;
inline constexpr Mode INPUT_PULLUP = Mode::InputPullup;
inline constexpr Mode INPUT_PULLDOWN = Mode::InputPulldown;

enum class Level { Low = 0, High = 1 };
inline constexpr Level LOW = Level::Low;
inline constexpr Level HIGH = Level::High;

bool pinMode(unsigned int pin, Mode mode);
bool digitalWrite(unsigned int pin, Level level);
[[nodiscard]] Level digitalRead(unsigned int pin);

struct Led {};
inline constexpr Led LED_BUILTIN{};

bool pinMode(Led, Mode mode);
bool digitalWrite(Led, Level level);
[[nodiscard]] Level digitalRead(Led);

bool ledOn();
bool ledOff();
[[nodiscard]] bool hasBuiltinLed();

bool delay(unsigned long ms);
[[nodiscard]] unsigned long millis();

using byte = unsigned char;
using word = unsigned short;
using boolean = bool;

class SerialPort {
public:
    bool begin(unsigned long baud = 9600);
    bool end();

    std::size_t print(const char* s);
    std::size_t print(char c);
    std::size_t print(std::string_view s);

    std::size_t println(const char* s);
    std::size_t println(char c);
    std::size_t println(std::string_view s);
    std::size_t println();

    std::size_t write(byte b);
    std::size_t write(const byte* buffer, std::size_t size);

    [[nodiscard]] int available();
    [[nodiscard]] int read();
    [[nodiscard]] int peek();
    void flush();
    [[nodiscard]] bool connected();
};

extern SerialPort Serial;

} // namespace mm::sketch
