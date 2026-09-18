// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <cmath>
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

enum class BitOrder : byte { LsbFirst = 0, MsbFirst = 1 };
inline constexpr BitOrder LSBFIRST = BitOrder::LsbFirst;
inline constexpr BitOrder MSBFIRST = BitOrder::MsbFirst;

byte shiftIn(unsigned int data_pin, unsigned int clock_pin, BitOrder bit_order);
void shiftOut(unsigned int data_pin, unsigned int clock_pin, BitOrder bit_order, byte val);

using std::abs;
using std::min;
using std::max;
using std::pow;
using std::sqrt;
using std::sin;
using std::cos;
using std::tan;

long sq(long x);
double sq(double x);

long constrain(long x, long a, long b);
double constrain(double x, double a, double b);

long map(long x, long in_min, long in_max, long out_min, long out_max);
double map(double x, double in_min, double in_max, double out_min, double out_max);

bool isAlpha(int c);
bool isAlphaNumeric(int c);
bool isAscii(int c);
bool isControl(int c);
bool isDigit(int c);
bool isGraph(int c);
bool isHexadecimalDigit(int c);
bool isLowerCase(int c);
bool isPrintable(int c);
bool isPunct(int c);
bool isSpace(int c);
bool isUpperCase(int c);
bool isWhitespace(int c);

long random(long max);
long random(long min, long max);
void randomSeed(unsigned long seed);

unsigned long bit(unsigned int n);

unsigned int bitRead(unsigned char x, unsigned int n);
unsigned int bitRead(unsigned int x, unsigned int n);
unsigned int bitRead(unsigned long x, unsigned int n);

unsigned int bitRead(int, unsigned int) = delete;
unsigned int bitRead(long, unsigned int) = delete;

void bitSet(unsigned char& x, unsigned int n);
void bitSet(unsigned int& x, unsigned int n);
void bitSet(unsigned long& x, unsigned int n);

void bitSet(int&, unsigned int) = delete;
void bitSet(long&, unsigned int) = delete;

void bitClear(unsigned char& x, unsigned int n);
void bitClear(unsigned int& x, unsigned int n);
void bitClear(unsigned long& x, unsigned int n);

void bitClear(int&, unsigned int) = delete;
void bitClear(long&, unsigned int) = delete;

void bitWrite(unsigned char& x, unsigned int n, byte b);
void bitWrite(unsigned int& x, unsigned int n, byte b);
void bitWrite(unsigned long& x, unsigned int n, byte b);

void bitWrite(unsigned char& x, unsigned int n, Level b);
void bitWrite(unsigned int& x, unsigned int n, Level b);
void bitWrite(unsigned long& x, unsigned int n, Level b);

void bitWrite(int&, unsigned int, byte) = delete;
void bitWrite(long&, unsigned int, byte) = delete;
void bitWrite(int&, unsigned int, Level) = delete;
void bitWrite(long&, unsigned int, Level) = delete;

byte lowByte(unsigned char x);
byte lowByte(unsigned int x);
byte lowByte(unsigned long x);

byte lowByte(int) = delete;
byte lowByte(long) = delete;

byte highByte(unsigned char x);
byte highByte(unsigned int x);
byte highByte(unsigned long x);

byte highByte(int) = delete;
byte highByte(long) = delete;

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
};

extern SerialPort Serial;

} // namespace mm::sketch

