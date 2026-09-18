// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
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

enum class Trigger {
    Rising,
    Falling,
    Change
};

inline constexpr Trigger RISING = Trigger::Rising;
inline constexpr Trigger FALLING = Trigger::Falling;
inline constexpr Trigger CHANGE = Trigger::Change;

bool attachInterrupt(int pin, void (*handler)(), Trigger trigger);
bool attachInterrupt(unsigned int pin, void (*handler)(), Trigger trigger);
bool detachInterrupt(int pin);
bool detachInterrupt(unsigned int pin);
[[nodiscard]] int digitalPinToInterrupt(int pin);
[[nodiscard]] unsigned int digitalPinToInterrupt(unsigned int pin);

enum class Base {
    Dec = 10,
    Hex = 16,
    Oct = 8,
    Bin = 2
};

inline constexpr Base DEC = Base::Dec;
inline constexpr Base HEX = Base::Hex;
inline constexpr Base OCT = Base::Oct;
inline constexpr Base BIN = Base::Bin;

class SerialPort {
public:
    bool begin(unsigned long baud = 9600);
    bool end();

    std::size_t write(byte b);
    std::size_t write(const byte* buffer, std::size_t size);
    std::size_t write(const char* buffer, std::size_t size);
    std::size_t write(const char* s);

    std::size_t print(const char* s);
    std::size_t print(char c);
    std::size_t print(std::string_view s);
    std::size_t print(bool b);
    std::size_t print(int n, Base base = DEC);
    std::size_t print(unsigned int n, Base base = DEC);
    std::size_t print(long n, Base base = DEC);
    std::size_t print(unsigned long n, Base base = DEC);
    std::size_t print(double n, int digits = 2);
    std::size_t print(unsigned char n, Base base = DEC) { return print(static_cast<unsigned int>(n), base); }
    std::size_t print(short n, Base base = DEC) { return print(static_cast<int>(n), base); }
    std::size_t print(unsigned short n, Base base = DEC) { return print(static_cast<unsigned int>(n), base); }

    std::size_t println(const char* s);
    std::size_t println(char c);
    std::size_t println(std::string_view s);
    std::size_t println(bool b);
    std::size_t println(int n, Base base = DEC);
    std::size_t println(unsigned int n, Base base = DEC);
    std::size_t println(long n, Base base = DEC);
    std::size_t println(unsigned long n, Base base = DEC);
    std::size_t println(double n, int digits = 2);
    std::size_t println(unsigned char n, Base base = DEC) { return println(static_cast<unsigned int>(n), base); }
    std::size_t println(short n, Base base = DEC) { return println(static_cast<int>(n), base); }
    std::size_t println(unsigned short n, Base base = DEC) { return println(static_cast<unsigned int>(n), base); }
    std::size_t println();

    [[nodiscard]] int available();
    int read();
    int peek();
    bool flush();
    [[nodiscard]] bool connected();
    explicit operator bool() { return connected(); }

    void setTimeout(unsigned long ms);
    [[nodiscard]] unsigned long getTimeout() const;

    std::size_t readBytes(char* buffer, std::size_t length);
    std::size_t readBytes(byte* buffer, std::size_t length);
    std::size_t readBytesUntil(char terminator, char* buffer, std::size_t length);
    std::size_t readBytesUntil(byte terminator, byte* buffer, std::size_t length);

    std::string readString();
    std::string readStringUntil(char terminator);

    bool find(const char* target);
    bool find(char target);
    bool findUntil(const char* target, const char* terminator);
    bool findUntil(const char* target, char terminator);

    long parseInt();
    double parseFloat();
};

extern SerialPort Serial;

void onSerial(void (*fn)());

} // namespace mm::sketch


