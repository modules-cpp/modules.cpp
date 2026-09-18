// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <string_view>

import mm.sketch;
import mm.test;

extern int test_console_read_calls();
extern void reset_test_console_read_calls();

extern void test_start_gpio_log();
extern std::size_t test_gpio_log_size();
extern unsigned int test_gpio_log_pin(std::size_t idx);
extern bool test_gpio_log_high(std::size_t idx);
extern void test_stop_gpio_log();
extern void test_setup_shift_in(unsigned int data_pin, unsigned int clock_pin, unsigned char val, bool lsb_first);
extern void test_clear_shift_in();

namespace {

using mm::test::expect;
using namespace mm::sketch;

static int setup1_calls = 0;
static int loop1_calls = 0;

void setup1() {
    ++setup1_calls;
}

void loop1() {
    ++loop1_calls;
    requestExit(42);
}

static int setup2_calls = 0;
static int loop2_calls = 0;

void setup2() {
    ++setup2_calls;
}

void loop2() {
    ++loop2_calls;
    requestExit(0);
}

void runner_clean_start_twice() {
    reset_test_console_read_calls();
    setup1_calls = 0;
    loop1_calls = 0;
    const int code1 = run(&setup1, &loop1);
    expect(code1 == 42, "first run should return exit code 42");
    expect(setup1_calls == 1, "setup1 should run once");
    expect(loop1_calls == 1, "loop1 should run once");
    expect(test_console_read_calls() == 0, "dispatch should not run after loop requests exit");

    setup2_calls = 0;
    loop2_calls = 0;
    const int code2 = run(&setup2, &loop2);
    expect(code2 == 0, "second run should return exit code 0");
    expect(setup2_calls == 1, "setup2 should run once");
    expect(loop2_calls == 1, "loop2 should run once");
}

void status_latch_first_failure() {
    clearError();
    expect(lastError() == Status::Ok, "initial error should be Ok");
    expect(std::string_view(lastCall()).empty(), "initial call should be empty");

    // First failure: pin 999 is invalid
    const bool w1 = digitalWrite(999, HIGH);
    expect(!w1, "digitalWrite on invalid pin should fail");
    expect(lastError() == Status::BadArgument, "error should latch BadArgument");
    expect(std::string_view(lastCall()) == "digitalWrite", "call should latch digitalWrite");

    // Second failure: should NOT overwrite the first failure
    const bool p1 = pinMode(999, INPUT);
    expect(!p1, "pinMode on invalid pin should fail");
    expect(lastError() == Status::BadArgument, "first error must be preserved");
    expect(std::string_view(lastCall()) == "digitalWrite", "first call must be preserved");

    clearError();
    expect(lastError() == Status::Ok, "clearError should reset error");
    expect(std::string_view(lastCall()).empty(), "clearError should reset call");
}

void delay_cancellation_preserves_latch() {
    clearError();
    requestExit(1);
    expect(exitRequested(), "exitRequested must be true");

    // delay returns false on cancellation, but error latch remains untouched
    const bool delay_ok = delay(20);
    expect(!delay_ok, "delay must answer false on cancellation");
    expect(exitRequested(), "exitRequested must still be true");
    expect(lastError() == Status::Ok, "cancellation must not record into error latch");

    // Subsequent real failure must latch properly
    digitalWrite(999, HIGH);
    expect(lastError() == Status::BadArgument, "latch must still record subsequent real failure");
    clearError();
}

static int setup_skips_counter = 0;

void setup_exit() {
    requestExit(7);
}

void loop_skips() {
    ++setup_skips_counter;
}

void exit_from_setup_skips_loop() {
    setup_skips_counter = 0;
    const int code = run(&setup_exit, &loop_skips);
    expect(code == 7, "run should return exit code from setup");
    expect(setup_skips_counter == 0, "loop must not run when setup requests exit");
}

static bool completed_loop_after_exit = false;

void setup_nop() {}

void loop_completes() {
    requestExit(0);
    completed_loop_after_exit = true;
}

void exit_from_loop_completes_that_loop() {
    completed_loop_after_exit = false;
    const int code = run(&setup_nop, &loop_completes);
    expect(code == 0, "run should return 0");
    expect(completed_loop_after_exit, "code after requestExit in loop must execute");
}

void digital_io_and_led() {
    expect(pinMode(10, OUTPUT), "pinMode 10 OUTPUT should succeed");
    expect(digitalWrite(10, HIGH), "digitalWrite 10 HIGH should succeed");
    expect(digitalRead(10) == HIGH, "digitalRead 10 should be HIGH");
    expect(digitalWrite(10, LOW), "digitalWrite 10 LOW should succeed");
    expect(digitalRead(10) == LOW, "digitalRead 10 should be LOW");

    expect(hasBuiltinLed(), "board has builtin LED");
    expect(pinMode(LED_BUILTIN, OUTPUT), "pinMode LED_BUILTIN OUTPUT should succeed");
    expect(ledOn(), "ledOn should succeed");
    expect(digitalRead(LED_BUILTIN) == HIGH, "digitalRead LED_BUILTIN should be HIGH when illuminated");
    expect(ledOff(), "ledOff should succeed");
    expect(digitalRead(LED_BUILTIN) == LOW, "digitalRead LED_BUILTIN should be LOW when off");
}

void time_and_delay() {
    const unsigned long t1 = millis();
    expect(t1 > 0, "millis should return ticks");
    expect(delay(15), "delay 15ms should succeed");
    const unsigned long t2 = millis();
    expect(t2 >= t1 + 15, "millis should advance after delay");
}

void serial_communication() {
    expect(Serial.begin(115200), "Serial.begin should succeed");
    expect(Serial.print("hello") == 5, "print hello should write 5 bytes");
    expect(Serial.println("world") == 7, "println world should write 7 bytes");
    expect(Serial.println() == 2, "println() should write 2 bytes");
    expect(Serial.end(), "Serial.end should succeed");
}

void math_operations() {
    expect(min(10, 20) == 10, "min(10, 20) should be 10");
    expect(min(3.5, 1.2) == 1.2, "min(3.5, 1.2) should be 1.2");
    expect(max(10, 20) == 20, "max(10, 20) should be 20");
    expect(max(3.5, 1.2) == 3.5, "max(3.5, 1.2) should be 3.5");

    expect(abs(-42) == 42, "abs(-42) should be 42");
    expect(abs(42) == 42, "abs(42) should be 42");
    expect(abs(-3.14) > 3.13 && abs(-3.14) < 3.15, "abs(-3.14) should be ~3.14");

    expect(sq(5L) == 25L, "sq(5L) should be 25L");
    expect(sq(2.5) == 6.25, "sq(2.5) should be 6.25");

    expect(sqrt(16.0) == 4.0, "sqrt(16.0) should be 4.0");
    expect(pow(2.0, 3.0) == 8.0, "pow(2.0, 3.0) should be 8.0");
    expect(sin(0.0) == 0.0, "sin(0.0) should be 0.0");
    expect(cos(0.0) == 1.0, "cos(0.0) should be 1.0");
    expect(tan(0.0) == 0.0, "tan(0.0) should be 0.0");

    expect(constrain(15L, 0L, 10L) == 10L, "constrain high");
    expect(constrain(-5L, 0L, 10L) == 0L, "constrain low");
    expect(constrain(5L, 0L, 10L) == 5L, "constrain in-bounds");
    expect(constrain(15.5, 0.0, 10.0) == 10.0, "constrain double");

    long counter = 5;
    const long c = constrain(counter++, 0L, 10L);
    expect(c == 5, "constrain returned value");
    expect(counter == 6, "constrain must evaluate argument exactly once");

    expect(map(50L, 0L, 100L, 0L, 1000L) == 500L, "map integer scale");
    expect(map(25L, 0L, 100L, 200L, 300L) == 225L, "map integer offset");
    expect(map(50.0, 0.0, 100.0, 0.0, 1000.0) == 500.0, "map double scale");
    expect(map(10L, 5L, 5L, 100L, 200L) == 100L, "map equal range returns out_min");
}

void character_classifiers() {
    expect(isAlpha('a'), "isAlpha('a') should be true");
    expect(isAlpha('Z'), "isAlpha('Z') should be true");
    expect(!isAlpha('1'), "isAlpha('1') should be false");
    expect(!isAlpha(' '), "isAlpha(' ') should be false");

    expect(isAlphaNumeric('a'), "isAlphaNumeric('a') should be true");
    expect(isAlphaNumeric('9'), "isAlphaNumeric('9') should be true");
    expect(!isAlphaNumeric('!'), "isAlphaNumeric('!') should be false");

    expect(isAscii(0), "isAscii(0) should be true");
    expect(isAscii(127), "isAscii(127) should be true");
    expect(!isAscii(128), "isAscii(128) should be false");
    expect(!isAscii(-1), "isAscii(-1) should be false");

    expect(isControl('\n'), "isControl('\\n') should be true");
    expect(isControl('\r'), "isControl('\\r') should be true");
    expect(isControl('\0'), "isControl('\\0') should be true");
    expect(!isControl('a'), "isControl('a') should be false");

    expect(isDigit('0'), "isDigit('0') should be true");
    expect(isDigit('9'), "isDigit('9') should be true");
    expect(!isDigit('a'), "isDigit('a') should be false");

    expect(isGraph('a'), "isGraph('a') should be true");
    expect(isGraph('!'), "isGraph('!') should be true");
    expect(!isGraph(' '), "isGraph(' ') should be false");
    expect(!isGraph('\n'), "isGraph('\\n') should be false");

    expect(isHexadecimalDigit('0'), "isHexadecimalDigit('0') should be true");
    expect(isHexadecimalDigit('a'), "isHexadecimalDigit('a') should be true");
    expect(isHexadecimalDigit('F'), "isHexadecimalDigit('F') should be true");
    expect(!isHexadecimalDigit('g'), "isHexadecimalDigit('g') should be false");

    expect(isLowerCase('a'), "isLowerCase('a') should be true");
    expect(isLowerCase('z'), "isLowerCase('z') should be true");
    expect(!isLowerCase('A'), "isLowerCase('A') should be false");

    expect(isPrintable('a'), "isPrintable('a') should be true");
    expect(isPrintable(' '), "isPrintable(' ') should be true");
    expect(!isPrintable('\n'), "isPrintable('\\n') should be false");

    expect(isPunct('!'), "isPunct('!') should be true");
    expect(isPunct('.'), "isPunct('.') should be true");
    expect(!isPunct('a'), "isPunct('a') should be false");

    expect(isSpace(' '), "isSpace(' ') should be true");
    expect(isSpace('\t'), "isSpace('\\t') should be true");
    expect(isSpace('\n'), "isSpace('\\n') should be true");
    expect(isSpace('\r'), "isSpace('\\r') should be true");
    expect(!isSpace('a'), "isSpace('a') should be false");

    expect(isUpperCase('A'), "isUpperCase('A') should be true");
    expect(isUpperCase('Z'), "isUpperCase('Z') should be true");
    expect(!isUpperCase('a'), "isUpperCase('a') should be false");

    expect(isWhitespace(' '), "isWhitespace(' ') should be true");
    expect(isWhitespace('\t'), "isWhitespace('\\t') should be true");
    expect(!isWhitespace('\n'), "isWhitespace('\\n') should be false (distinct from isSpace)");
    expect(!isWhitespace('\r'), "isWhitespace('\\r') should be false (distinct from isSpace)");
}

void random_numbers() {
    randomSeed(42);
    const long r1 = random(100);
    const long r2 = random(10, 20);

    randomSeed(42);
    const long r3 = random(100);
    const long r4 = random(10, 20);

    expect(r1 == r3, "re-seeded random produces same sequence (r1 == r3)");
    expect(r2 == r4, "re-seeded random produces same sequence (r2 == r4)");

    expect(r1 >= 0 && r1 < 100, "random(max) within [0, max - 1]");
    expect(r2 >= 10 && r2 < 20, "random(min, max) within [min, max - 1]");

    expect(random(0) == 0, "random(0) is 0");
    expect(random(-5) == 0, "random(negative) is 0");
    expect(random(10, 10) == 10, "random(min, min) is min");
    expect(random(20, 10) == 20, "random(min > max) is min");
}

void bits_and_bytes() {
    expect(bit(0) == 1UL, "bit(0) should be 1");
    expect(bit(3) == 8UL, "bit(3) should be 8");
    expect(bit(16) == 65536UL, "bit(16) should be 65536");

    const unsigned char c_val = 0xAB;
    const unsigned int u_val = 0x1234;
    const unsigned long l_val = 0x12345678UL;

    expect(lowByte(c_val) == 0xAB, "lowByte(unsigned char)");
    expect(lowByte(u_val) == 0x34, "lowByte(unsigned int)");
    expect(lowByte(l_val) == 0x78, "lowByte(unsigned long)");

    expect(highByte(c_val) == 0x00, "highByte(unsigned char)");
    expect(highByte(u_val) == 0x12, "highByte(unsigned int)");
    expect(highByte(l_val) == 0x56, "highByte(unsigned long)");

    expect(bitRead(0b1010u, 0) == 0, "bitRead bit 0");
    expect(bitRead(0b1010u, 1) == 1, "bitRead bit 1");
    expect(bitRead(0b1010u, 2) == 0, "bitRead bit 2");
    expect(bitRead(0b1010u, 3) == 1, "bitRead bit 3");
    expect(bitRead(0b1010u, 50) == 0, "bitRead out-of-bounds");

    unsigned char c = 0;
    bitSet(c, 2);
    expect(c == 4, "bitSet unsigned char");
    bitClear(c, 2);
    expect(c == 0, "bitClear unsigned char");

    unsigned int u = 0;
    bitSet(u, 4);
    expect(u == 16, "bitSet unsigned int");
    bitClear(u, 4);
    expect(u == 0, "bitClear unsigned int");

    unsigned long l = 0;
    bitSet(l, 8);
    expect(l == 256, "bitSet unsigned long");
    bitClear(l, 8);
    expect(l == 0, "bitClear unsigned long");

    unsigned int val = 0;
    bitWrite(val, 1, static_cast<byte>(1));
    expect(val == 2, "bitWrite byte 1");
    bitWrite(val, 1, static_cast<byte>(0));
    expect(val == 0, "bitWrite byte 0");
    bitWrite(val, 3, HIGH);
    expect(val == 8, "bitWrite HIGH");
    bitWrite(val, 3, LOW);
    expect(val == 0, "bitWrite LOW");
}

void shift_in_and_shift_out() {
    expect(pinMode(2, OUTPUT), "pinMode 2 OUTPUT");
    expect(pinMode(3, OUTPUT), "pinMode 3 OUTPUT");

    // Test shiftOut LSBFIRST
    test_start_gpio_log();
    shiftOut(2, 3, LSBFIRST, 0x53); // 0x53 = 0b01010011
    test_stop_gpio_log();

    expect(test_gpio_log_size() == 24, "shiftOut should log 24 transitions (8 bits x 3 writes)");
    for (std::size_t i = 0; i < 8; ++i) {
        const bool expected_bit = ((0x53 >> i) & 1) != 0;
        expect(test_gpio_log_pin(3 * i + 0) == 2, "data pin write");
        expect(test_gpio_log_high(3 * i + 0) == expected_bit, "data pin bit value in LSBFIRST");
        expect(test_gpio_log_pin(3 * i + 1) == 3, "clock pin write HIGH");
        expect(test_gpio_log_high(3 * i + 1) == true, "clock pin HIGH");
        expect(test_gpio_log_pin(3 * i + 2) == 3, "clock pin write LOW");
        expect(test_gpio_log_high(3 * i + 2) == false, "clock pin LOW");
    }

    // Test shiftOut MSBFIRST
    test_start_gpio_log();
    shiftOut(2, 3, MSBFIRST, 0x53);
    test_stop_gpio_log();

    expect(test_gpio_log_size() == 24, "shiftOut MSBFIRST should log 24 transitions");
    for (std::size_t i = 0; i < 8; ++i) {
        const bool expected_bit = ((0x53 >> (7 - i)) & 1) != 0;
        expect(test_gpio_log_pin(3 * i + 0) == 2, "data pin write");
        expect(test_gpio_log_high(3 * i + 0) == expected_bit, "data pin bit value in MSBFIRST");
        expect(test_gpio_log_pin(3 * i + 1) == 3, "clock pin write HIGH");
        expect(test_gpio_log_high(3 * i + 1) == true, "clock pin HIGH");
        expect(test_gpio_log_pin(3 * i + 2) == 3, "clock pin write LOW");
        expect(test_gpio_log_high(3 * i + 2) == false, "clock pin LOW");
    }

    // Test shiftIn LSBFIRST
    test_setup_shift_in(2, 3, 0xB4, true);
    const byte in_lsb = shiftIn(2, 3, LSBFIRST);
    test_clear_shift_in();
    expect(in_lsb == 0xB4, "shiftIn LSBFIRST should read 0xB4");

    // Test shiftIn MSBFIRST
    test_setup_shift_in(2, 3, 0xB4, false);
    const byte in_msb = shiftIn(2, 3, MSBFIRST);
    test_clear_shift_in();
    expect(in_msb == 0xB4, "shiftIn MSBFIRST should read 0xB4");
}

const mm::test::case_ cases[] = {
    {"runner clean start twice", &runner_clean_start_twice},
    {"status latch first failure", &status_latch_first_failure},
    {"delay cancellation preserves latch", &delay_cancellation_preserves_latch},
    {"exit from setup skips loop", &exit_from_setup_skips_loop},
    {"exit from loop completes that loop", &exit_from_loop_completes_that_loop},
    {"digital io and led", &digital_io_and_led},
    {"time and delay", &time_and_delay},
    {"serial communication", &serial_communication},
    {"math operations", &math_operations},
    {"character classifiers", &character_classifiers},
    {"random numbers", &random_numbers},
    {"bits and bytes", &bits_and_bytes},
    {"shift in and shift out", &shift_in_and_shift_out},
};

const mm::test::registrar reg{"mm.sketch", cases};

} // namespace

