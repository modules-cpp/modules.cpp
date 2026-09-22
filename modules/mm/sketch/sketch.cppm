// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <cmath>
#include <compare>
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

[[nodiscard]] int analogRead(unsigned int pin);
void analogReadResolution(int bits);

void analogWrite(unsigned int pin, int value);
void analogWrite(Led, int value);
void analogWriteResolution(int bits);

void tone(unsigned int pin, unsigned int frequency, unsigned long duration = 0);
void noTone(unsigned int pin);

bool delay(unsigned long ms);
bool delayMicroseconds(unsigned int us);
[[nodiscard]] unsigned long millis();
[[nodiscard]] unsigned long micros();

[[nodiscard]] unsigned long pulseIn(unsigned int pin, Level value, unsigned long timeout = 1000000UL);
[[nodiscard]] unsigned long pulseInLong(unsigned int pin, Level value, unsigned long timeout = 1000000UL);

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

// The rest of <cmath> a sketch reaches for. Each is the standard routine
// under the name the sketch writes, so an expression carried over from a
// sketch means here what it meant there.
using std::asin;
using std::acos;
using std::atan;
using std::atan2;
using std::sinh;
using std::cosh;
using std::tanh;
using std::exp;
using std::log;
using std::log10;
using std::log2;
using std::cbrt;
using std::hypot;
using std::fabs;
using std::fmod;
using std::floor;
using std::ceil;
using std::round;
using std::trunc;
using std::frexp;
using std::ldexp;
using std::modf;
using std::isnan;
using std::isinf;
using std::isfinite;
using std::signbit;

// The float-suffixed C spellings, which a sketch written for a machine
// without double-precision hardware uses to stay in single precision.
using std::fabsf;
using std::fmodf;
using std::powf;
using std::sqrtf;
using std::sinf;
using std::cosf;
using std::tanf;
using std::atan2f;
using std::expf;
using std::logf;
using std::floorf;
using std::ceilf;
using std::roundf;
using std::hypotf;
using std::frexpf;
using std::ldexpf;
using std::modff;

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

class Print;

// The sketch string, which a vendored library returns, takes, and stores.
// It is a std::string with that surface on it: the storage, the
// allocation and the character handling are the standard library's, and
// what this class adds is the vocabulary a sketch writes. A sketch of this
// project's own has no reason to reach for it; docs/modules-sketch.mdy says
// which to use when.
class String {
public:
    String() = default;
    String(const char* s);
    String(const char* buffer, unsigned int length);
    String(char c);
    // Explicit: an implicit one would let a std::string_view comparison
    // resolve through String and become ambiguous with the standard's own.
    explicit String(std::string_view s);
    explicit String(int value, unsigned char base = 10);
    explicit String(unsigned int value, unsigned char base = 10);
    explicit String(long value, unsigned char base = 10);
    explicit String(unsigned long value, unsigned char base = 10);
    explicit String(double value, unsigned char decimal_places = 2);
    explicit String(float value, unsigned char decimal_places = 2);

    String& operator=(const char* s);
    String& operator=(std::string_view s);

    // The standard string underneath, for a caller on this project's side of
    // the boundary. Nothing converts implicitly: a String is asked.
    [[nodiscard]] const std::string& str() const noexcept { return text_; }
    [[nodiscard]] std::string_view view() const noexcept { return text_; }

    [[nodiscard]] const char* c_str() const noexcept { return text_.c_str(); }
    [[nodiscard]] unsigned int length() const noexcept;
    [[nodiscard]] bool isEmpty() const noexcept { return text_.empty(); }
    bool reserve(unsigned int size);

    [[nodiscard]] char charAt(unsigned int index) const;
    void setCharAt(unsigned int index, char c);
    [[nodiscard]] char operator[](unsigned int index) const;

    bool concat(const String& other);
    bool concat(const char* s);
    bool concat(char c);
    bool concat(int value);
    bool concat(unsigned int value);
    bool concat(long value);
    bool concat(unsigned long value);
    bool concat(double value);

    String& operator+=(const String& other);
    String& operator+=(const char* s);
    String& operator+=(char c);
    String& operator+=(int value);
    String& operator+=(unsigned int value);
    String& operator+=(long value);
    String& operator+=(unsigned long value);
    String& operator+=(double value);

    [[nodiscard]] int compareTo(const String& other) const;
    [[nodiscard]] bool equals(const String& other) const;
    [[nodiscard]] bool equals(const char* s) const;
    [[nodiscard]] bool equalsIgnoreCase(const String& other) const;
    [[nodiscard]] bool startsWith(const String& prefix) const;
    [[nodiscard]] bool startsWith(const String& prefix, unsigned int offset) const;
    [[nodiscard]] bool endsWith(const String& suffix) const;

    [[nodiscard]] int indexOf(char c) const;
    [[nodiscard]] int indexOf(char c, unsigned int from) const;
    [[nodiscard]] int indexOf(const String& needle) const;
    [[nodiscard]] int indexOf(const String& needle, unsigned int from) const;
    [[nodiscard]] int lastIndexOf(char c) const;
    [[nodiscard]] int lastIndexOf(char c, unsigned int from) const;
    [[nodiscard]] int lastIndexOf(const String& needle) const;
    [[nodiscard]] int lastIndexOf(const String& needle, unsigned int from) const;

    [[nodiscard]] String substring(unsigned int from) const;
    [[nodiscard]] String substring(unsigned int from, unsigned int to) const;

    void replace(char from, char to);
    void replace(const String& from, const String& to);
    void remove(unsigned int index);
    void remove(unsigned int index, unsigned int count);
    void toLowerCase();
    void toUpperCase();
    void trim();

    void getBytes(byte* buffer, unsigned int size, unsigned int index = 0) const;
    void toCharArray(char* buffer, unsigned int size, unsigned int index = 0) const;

    [[nodiscard]] long toInt() const;
    [[nodiscard]] float toFloat() const;
    [[nodiscard]] double toDouble() const;

private:
    std::string text_;
};

[[nodiscard]] bool operator==(const String& a, const String& b);
[[nodiscard]] bool operator==(const String& a, const char* b);
[[nodiscard]] std::strong_ordering operator<=>(const String& a, const String& b);
[[nodiscard]] String operator+(const String& a, const String& b);
[[nodiscard]] String operator+(const String& a, const char* b);
[[nodiscard]] String operator+(const char* a, const String& b);
[[nodiscard]] String operator+(const String& a, char b);
[[nodiscard]] String operator+(const String& a, int b);
[[nodiscard]] String operator+(const String& a, unsigned int b);
[[nodiscard]] String operator+(const String& a, long b);
[[nodiscard]] String operator+(const String& a, unsigned long b);
[[nodiscard]] String operator+(const String& a, double b);

// A vendored library prints an object of its own by deriving from Printable
// and a sink of its own by deriving from Print. Both are the sketch shapes,
// and both are the single level of virtual dispatch docs/modules-c++20.mdy
// permits: Print is the base, a sink is a concrete class, and printTo is the
// one thing a printable object supplies.
class Printable {
public:
    virtual ~Printable() = default;
    virtual std::size_t printTo(Print& out) const = 0;
};

// Everything a sink can print, written once in terms of the one thing a sink
// must supply: write. The formatting matches SerialPort's, which keeps its
// own copies because they reach the console without a virtual call.
class Print {
public:
    virtual ~Print() = default;

    virtual std::size_t write(byte b) = 0;
    virtual std::size_t write(const byte* buffer, std::size_t size);
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
    std::size_t print(const Printable& object);
    std::size_t print(const String& s);
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
    std::size_t println(const Printable& object);
    std::size_t println(const String& s);
    std::size_t println(unsigned char n, Base base = DEC) { return println(static_cast<unsigned int>(n), base); }
    std::size_t println(short n, Base base = DEC) { return println(static_cast<int>(n), base); }
    std::size_t println(unsigned short n, Base base = DEC) { return println(static_cast<unsigned int>(n), base); }
    std::size_t println();
};

class SerialPort : public Print {
public:
    // The console's own overloads answer every call a sketch makes on
    // Serial; these bring in the ones only Print declares, such as printing
    // a Printable, without displacing any of them.
    using Print::print;
    using Print::println;

    bool begin(unsigned long baud = 9600);
    bool end();

    std::size_t write(byte b) override;
    std::size_t write(const byte* buffer, std::size_t size) override;
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

enum class SpiMode { Mode0, Mode1, Mode2, Mode3 };
inline constexpr SpiMode SPI_MODE0 = SpiMode::Mode0;
inline constexpr SpiMode SPI_MODE1 = SpiMode::Mode1;
inline constexpr SpiMode SPI_MODE2 = SpiMode::Mode2;
inline constexpr SpiMode SPI_MODE3 = SpiMode::Mode3;

class SPISettings {
public:
    unsigned long clock = 4'000'000;
    BitOrder bit_order = BitOrder::MsbFirst;
    SpiMode data_mode = SpiMode::Mode0;

    constexpr SPISettings() = default;
    constexpr SPISettings(unsigned long clock_speed, BitOrder order, SpiMode mode)
        : clock(clock_speed), bit_order(order), data_mode(mode) {}
};

class SPIClass {
public:
    bool begin();
    bool end();

    void beginTransaction(SPISettings settings);
    void endTransaction();

    byte transfer(byte val);
    word transfer16(word val);
    void transfer(void* buffer, std::size_t size);
    void transfer(byte* buffer, std::size_t size);
};

extern SPIClass SPI;

class TwoWire {
public:
    bool begin();
    bool end();
    void setClock(unsigned long clock_speed);

    void beginTransmission(byte address);
    void beginTransmission(int address);
    std::size_t write(byte val);
    std::size_t write(const byte* buffer, std::size_t size);
    std::size_t write(const char* s);
    byte endTransmission(bool send_stop = true);

    std::size_t requestFrom(byte address, std::size_t quantity, bool send_stop = true);
    std::size_t requestFrom(int address, int quantity, int send_stop = 1);

    [[nodiscard]] int available();
    int read();
    int peek();
    void flush();
};

extern TwoWire Wire;

} // namespace mm::sketch
