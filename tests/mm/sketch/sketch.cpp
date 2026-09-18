// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <climits>
#include <cstddef>
#include <string>
#include <string_view>

import mm.sketch;
import mm.test;

extern int test_console_read_calls();
extern void reset_test_console_read_calls();
extern void test_set_console_write_fail(bool fail);
extern void test_console_feed_input(std::string_view input);
extern void test_console_clear_input();
extern std::string test_console_get_written();
extern void test_console_clear_written();

extern void test_start_gpio_log();
extern std::size_t test_gpio_log_size();
extern unsigned int test_gpio_log_pin(std::size_t idx);
extern bool test_gpio_log_high(std::size_t idx);
extern void test_stop_gpio_log();
extern void test_setup_shift_in(unsigned int data_pin, unsigned int clock_pin, unsigned char val, bool lsb_first);
extern void test_clear_shift_in();

extern void test_gpio_set_edge(unsigned int pin, bool pending);
extern bool test_gpio_is_watched(unsigned int pin);
extern void test_gpio_clear_all_edges();
extern void test_set_ticks_fail(bool fail);
extern void test_set_delay_fail(bool fail);
extern void test_set_console_read_fail(bool fail);
extern void test_setup_pulse(unsigned int pin, std::initializer_list<bool> levels, unsigned long step_us);
extern void test_clear_pulse();

extern void test_set_spi_present(bool present);
extern void test_set_spi_fail(bool fail);
extern void test_set_spi_xor_mask(unsigned char mask);
extern void test_reset_spi();

extern void test_set_i2c_present(bool present);
extern void test_set_i2c_fail(bool fail);
extern void test_set_i2c_read_data(const unsigned char* data, std::size_t size);
extern std::size_t test_get_i2c_written_size();
extern unsigned char test_get_i2c_written_byte(std::size_t idx);
extern unsigned int test_get_i2c_written_address();
extern void test_reset_i2c();

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

    const unsigned long u1 = micros();
    expect(u1 > 0, "micros should return ticks");
    expect(delayMicroseconds(500), "delayMicroseconds 500 should succeed");
    const unsigned long u2 = micros();
    expect(u2 >= u1 + 500, "micros should advance after delayMicroseconds");

    // pulseIn test
    test_setup_pulse(4, {false, true, false}, 250);
    const unsigned long width = pulseIn(4, HIGH, 1000000UL);
    expect(width == 250, "pulseIn measures 250us pulse");
    test_clear_pulse();

    // pulseInLong test
    test_setup_pulse(4, {true, false, true}, 300);
    const unsigned long width_long = pulseInLong(4, LOW, 1000000UL);
    expect(width_long == 300, "pulseInLong measures 300us pulse");
    test_clear_pulse();

    // pulse timeout test
    test_setup_pulse(4, {false}, 2000000UL);
    const unsigned long timed_out = pulseIn(4, HIGH, 1000UL);
    expect(timed_out == 0, "pulseIn times out when pin does not transition");
    test_clear_pulse();
}

void serial_communication() {
    expect(Serial.begin(115200), "Serial.begin should succeed");
    expect(Serial.print("hello") == 5, "print hello should write 5 bytes");
    expect(Serial.println("world") == 7, "println world should write 7 bytes");
    expect(Serial.println() == 2, "println() should write 2 bytes");
    expect(Serial.end(), "Serial.end should succeed");

    clearError();
    test_set_console_write_fail(true);
    expect(Serial.print("fail") == 0, "Serial.print fails when console write fails");
    expect(lastError() == Status::TransportError, "Serial.print failure latches TransportError");
    expect(std::string_view(lastCall()) == "Serial.print", "Serial.print records Serial.print, not Serial.write");

    clearError();
    expect(Serial.println("fail") == 0, "Serial.println fails when console write fails");
    expect(lastError() == Status::TransportError, "Serial.println failure latches TransportError");
    expect(std::string_view(lastCall()) == "Serial.println", "Serial.println records Serial.println, not Serial.write");
    test_set_console_write_fail(false);
    clearError();
}

static int serial_callback_count = 0;
void test_on_serial_handler() {
    ++serial_callback_count;
    if (Serial.available() > 0) {
        Serial.read();
    }
}

void serial_formatting() {
    test_console_clear_written();
    expect(Serial.begin(115200), "Serial.begin");
    expect(Serial.connected(), "Serial.connected should be true");
    expect(bool(Serial), "operator bool(Serial) should be true");
    expect(Serial.flush(), "Serial.flush should succeed");

    test_console_clear_written();
    Serial.print(true);
    expect(test_console_get_written() == "1", "print bool true");

    test_console_clear_written();
    Serial.print(false);
    expect(test_console_get_written() == "0", "print bool false");

    test_console_clear_written();
    Serial.print(123);
    expect(test_console_get_written() == "123", "print int dec");

    test_console_clear_written();
    Serial.print(255, HEX);
    expect(test_console_get_written() == "FF", "print hex uppercase");

    test_console_clear_written();
    Serial.print(12, OCT);
    expect(test_console_get_written() == "14", "print oct");

    test_console_clear_written();
    Serial.print(5, BIN);
    expect(test_console_get_written() == "101", "print bin");

    test_console_clear_written();
    Serial.print(-42L, DEC);
    expect(test_console_get_written() == "-42", "print negative long");

    test_console_clear_written();
    Serial.print(3.14159, 2);
    expect(test_console_get_written() == "3.14", "print double 2 digits");

    test_console_clear_written();
    Serial.write(static_cast<byte>('Z'));
    expect(test_console_get_written() == "Z", "write byte");

    test_console_clear_written();
    Serial.write("data", 4);
    expect(test_console_get_written() == "data", "write buffer");

    test_console_clear_written();
    Serial.println(42);
    expect(test_console_get_written() == "42\r\n", "println int");

    // Invalid base validation
    clearError();
    expect(Serial.print(42, static_cast<Base>(5)) == 0, "print invalid base fails");
    expect(lastError() == Status::BadArgument, "invalid base latches BadArgument");
    expect(std::string_view(lastCall()) == "Serial.print", "lastCall is Serial.print");
    clearError();

    // Excessive double precision safety
    expect(Serial.print(3.14159, 100) == 0, "print double huge precision fails safely");
    expect(lastError() == Status::BadArgument, "huge precision latches BadArgument");
    expect(std::string_view(lastCall()) == "Serial.print", "lastCall is Serial.print");
    clearError();
}

void serial_input_and_waiting() {
    test_console_clear_input();
    Serial.setTimeout(50);
    expect(Serial.getTimeout() == 50, "getTimeout should return 50");

    test_console_feed_input("hello world\n");
    expect(Serial.available() == 12, "available should report 12 bytes");
    expect(Serial.peek() == 'h', "peek should return 'h'");
    expect(Serial.available() == 12, "peek does not consume byte");
    expect(Serial.read() == 'h', "read should return 'h'");
    expect(Serial.available() == 11, "read consumes byte");

    char buf[16] = {0};
    const std::size_t n1 = Serial.readBytes(buf, 4);
    expect(n1 == 4, "readBytes 4 bytes");
    expect(std::string_view(buf, 4) == "ello", "readBytes content");

    char line[16] = {0};
    const std::size_t n2 = Serial.readBytesUntil('\n', line, 16);
    expect(n2 == 6, "readBytesUntil newline read 6 bytes");
    expect(std::string_view(line, 6) == " world", "readBytesUntil content");
    expect(Serial.available() == 0, "stream should be empty after newline consumed");

    // Test readString and readStringUntil
    test_console_feed_input("first\nsecond");
    const std::string s1 = Serial.readStringUntil('\n');
    expect(s1 == "first", "readStringUntil should return first");
    const std::string s2 = Serial.readString();
    expect(s2 == "second", "readString should return remaining until timeout");

    // Test find and findUntil
    test_console_feed_input("123TARGET456");
    expect(Serial.find("TARGET"), "find TARGET should succeed");
    expect(Serial.read() == '4', "next byte after target is '4'");
    Serial.readString(); // drain remaining

    test_console_feed_input("prefix TERM target");
    expect(!Serial.findUntil("target", "TERM"), "findUntil stops on terminator");
    Serial.readString(); // drain

    // Test parseInt and parseFloat
    test_console_feed_input("   -789, 12.50\n");
    expect(Serial.parseInt() == -789, "parseInt should parse -789");
    expect(Serial.peek() == ',', "comma remains after parseInt");
    Serial.read(); // consume comma
    const double val = Serial.parseFloat();
    expect(val > 12.49 && val < 12.51, "parseFloat should parse 12.50");
    Serial.readString(); // drain

    // Boundary testing for parseInt: LONG_MAX, LONG_MIN, and overflows
    test_console_feed_input("9223372036854775807\n");
    expect(Serial.parseInt() == LONG_MAX, "parseInt parses LONG_MAX exactly");
    expect(lastError() == Status::Ok, "LONG_MAX does not latch error");
    Serial.readString(); // drain

    test_console_feed_input("-9223372036854775808\n");
    expect(Serial.parseInt() == LONG_MIN, "parseInt parses LONG_MIN exactly");
    expect(lastError() == Status::Ok, "LONG_MIN does not latch error");
    Serial.readString(); // drain

    clearError();
    test_console_feed_input("9223372036854775808\n"); // LONG_MAX + 1
    expect(Serial.parseInt() == LONG_MAX, "parseInt overflow clamps to LONG_MAX");
    expect(lastError() == Status::BadArgument, "parseInt overflow latches BadArgument");
    clearError();
    Serial.readString(); // drain

    test_console_feed_input("-9223372036854775809\n"); // LONG_MIN - 1
    expect(Serial.parseInt() == LONG_MIN, "parseInt underflow clamps to LONG_MIN");
    expect(lastError() == Status::BadArgument, "parseInt underflow latches BadArgument");
    clearError();
    Serial.readString(); // drain

    // Overlapping pattern matching with KMP: "aaab" searching for "aab"
    test_console_feed_input("aaab");
    expect(Serial.find("aab"), "find matches overlapping pattern 'aab' in 'aaab'");
    Serial.readString(); // drain

    test_console_feed_input("ababa");
    expect(Serial.findUntil("aba", "abb"), "findUntil matches 'aba' with overlapping prefix");
    Serial.readString(); // drain

    test_console_feed_input("aabaterm");
    expect(!Serial.findUntil("xyz", "term"), "findUntil terminates on 'term'");
    Serial.readString(); // drain

    // fill_ring error latching on console read failure
    clearError();
    test_set_console_read_fail(true);
    expect(Serial.available() == 0, "available reports 0 on read failure");
    expect(lastError() == Status::TransportError, "available latches TransportError");
    expect(std::string_view(lastCall()) == "Serial.available", "lastCall is Serial.available");
    clearError();

    expect(Serial.read() == -1, "read reports -1 on read failure");
    expect(lastError() == Status::TransportError, "read latches TransportError");
    expect(std::string_view(lastCall()) == "Serial.read", "lastCall is Serial.read");
    clearError();

    expect(Serial.peek() == -1, "peek reports -1 on read failure");
    expect(lastError() == Status::TransportError, "peek latches TransportError");
    expect(std::string_view(lastCall()) == "Serial.peek", "lastCall is Serial.peek");
    clearError();
    test_set_console_read_fail(false);

    // Empty input when clock is unsupported must return immediately without hanging
    clearError();
    test_set_ticks_fail(true);
    char empty_buf[4];
    expect(Serial.readBytes(empty_buf, 4) == 0, "readBytes on unsupported clock returns 0 immediately");
    expect(lastError() == Status::Unsupported, "unsupported clock latches Unsupported");
    test_set_ticks_fail(false);
    clearError();

    // Delay failure during waiting operation must abort immediately
    test_set_delay_fail(true);
    expect(Serial.readString().empty(), "readString aborts on delay failure");
    expect(lastError() == Status::TransportError, "delay failure latches TransportError");
    test_set_delay_fail(false);
    clearError();
}

void serial_event_callback() {
    test_console_clear_input();
    serial_callback_count = 0;
    onSerial(&test_on_serial_handler);

    test_console_feed_input("AB");
    dispatch();
    expect(serial_callback_count == 1, "dispatch should invoke onSerial once when bytes present");
    expect(Serial.available() == 1, "handler consumed 'A', 1 byte remaining");

    dispatch();
    expect(serial_callback_count == 2, "dispatch should invoke onSerial again for remaining byte");
    expect(Serial.available() == 0, "handler consumed 'B', 0 bytes remaining");

    dispatch();
    expect(serial_callback_count == 2, "dispatch should NOT invoke onSerial when ring is empty");

    onSerial(nullptr);
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
    expect(map(LONG_MAX, 0L, LONG_MAX, 0L, LONG_MAX) == LONG_MAX, "map LONG_MAX without overflow");
    expect(map(LONG_MIN, LONG_MIN, LONG_MAX, LONG_MIN, LONG_MAX) == LONG_MIN, "map LONG_MIN without overflow");
    expect(map(0L, LONG_MIN, LONG_MAX, LONG_MIN, LONG_MAX) == 0L, "map midpoint across full signed range");
    expect(map(LONG_MAX, LONG_MIN, LONG_MAX, LONG_MIN, LONG_MAX) == LONG_MAX, "map max across full signed range");
    expect(map(LONG_MIN, LONG_MIN, LONG_MAX, 0L, 100L) == 0L, "map LONG_MIN to 0");
    expect(map(LONG_MAX, LONG_MIN, LONG_MAX, 0L, 100L) == 100L, "map LONG_MAX to 100");
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

    const long r_span = random(LONG_MIN, LONG_MAX);
    expect(r_span >= LONG_MIN && r_span < LONG_MAX, "random(LONG_MIN, LONG_MAX) within range");
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

    clearError();
    shiftOut(999, 3, LSBFIRST, 0x55);
    expect(lastError() == Status::BadArgument, "shiftOut on invalid pin fails");
    expect(std::string_view(lastCall()) == "shiftOut", "shiftOut failure records shiftOut, not digitalWrite");

    clearError();
    shiftIn(999, 3, LSBFIRST);
    expect(lastError() == Status::BadArgument, "shiftIn on invalid pin fails");
    expect(std::string_view(lastCall()) == "shiftIn", "shiftIn failure records shiftIn, not digitalRead");
    clearError();
}

static int int_test_h1_count = 0;
void int_test_h1() {
    ++int_test_h1_count;
}

static int int_test_h2_count = 0;
void int_test_h2() {
    ++int_test_h2_count;
}

void interrupt_registration_and_errors() {
    test_gpio_clear_all_edges();
    clearError();

    expect(digitalPinToInterrupt(4) == 4, "digitalPinToInterrupt int identity");
    expect(digitalPinToInterrupt(7u) == 7u, "digitalPinToInterrupt unsigned int identity");

    // Invalid handler
    expect(!attachInterrupt(2, nullptr, RISING), "attachInterrupt null handler fails");
    expect(lastError() == Status::BadArgument, "null handler sets BadArgument");
    expect(std::string_view(lastCall()) == "attachInterrupt", "lastCall is attachInterrupt");
    clearError();

    // Invalid negative pin
    expect(!attachInterrupt(-1, &int_test_h1, RISING), "attachInterrupt negative pin fails");
    expect(lastError() == Status::BadArgument, "negative pin sets BadArgument");
    expect(std::string_view(lastCall()) == "attachInterrupt", "lastCall is attachInterrupt");
    clearError();

    // Invalid trigger
    expect(!attachInterrupt(2, &int_test_h1, static_cast<Trigger>(99)), "attachInterrupt invalid trigger fails");
    expect(lastError() == Status::BadArgument, "invalid trigger sets BadArgument");
    expect(std::string_view(lastCall()) == "attachInterrupt", "lastCall is attachInterrupt");
    clearError();

    // Invalid negative detach
    expect(!detachInterrupt(-1), "detachInterrupt negative pin fails");
    expect(lastError() == Status::BadArgument, "negative detach sets BadArgument");
    expect(std::string_view(lastCall()) == "detachInterrupt", "lastCall is detachInterrupt");
    clearError();

    // Successful attach
    expect(pinMode(2, INPUT_PULLUP), "pinMode 2 INPUT_PULLUP succeeds");
    expect(attachInterrupt(2, &int_test_h1, FALLING), "attachInterrupt pin 2 succeeds");
    expect(test_gpio_is_watched(2), "pin 2 is watched by platform");

    // Re-attaching same pin succeeds and updates handler/trigger
    expect(attachInterrupt(2, &int_test_h2, RISING), "re-attaching pin 2 succeeds");
    expect(test_gpio_is_watched(2), "pin 2 remains watched");

    // Successful detach
    expect(detachInterrupt(2), "detachInterrupt pin 2 succeeds");
    expect(!test_gpio_is_watched(2), "pin 2 is no longer watched");

    // Detaching already detached pin returns true
    expect(detachInterrupt(2), "detachInterrupt unattached pin returns true");

    test_gpio_clear_all_edges();
    clearError();
}

void interrupt_table_capacity() {
    test_gpio_clear_all_edges();
    clearError();

    // Attach 8 distinct pins (pins 0 through 7)
    for (unsigned int p = 0; p < 8; ++p) {
        expect(attachInterrupt(p, &int_test_h1, RISING), "attach interrupt slot succeeds");
    }

    // 9th pin must return false with Busy
    expect(!attachInterrupt(8, &int_test_h1, RISING), "9th interrupt slot must fail");
    expect(lastError() == Status::Busy, "9th interrupt sets Busy");
    expect(std::string_view(lastCall()) == "attachInterrupt", "lastCall is attachInterrupt");
    clearError();

    // Detach slot for pin 3
    expect(detachInterrupt(3), "detach pin 3 succeeds");

    // Now pin 8 can be attached into the freed slot
    expect(attachInterrupt(8, &int_test_h1, RISING), "attach pin 8 after detach succeeds");

    // Cleanup
    for (unsigned int p = 0; p < 9; ++p) {
        detachInterrupt(p);
    }
    test_gpio_clear_all_edges();
    clearError();
}

void interrupt_dispatch_and_edge_trigger() {
    test_gpio_clear_all_edges();
    clearError();
    int_test_h1_count = 0;

    expect(attachInterrupt(2, &int_test_h1, RISING), "attach pin 2");

    // Without edge, dispatch does not call handler
    dispatch();
    expect(int_test_h1_count == 0, "no handler call without edge");

    // Signal edge on pin 2
    test_gpio_set_edge(2, true);
    dispatch();
    expect(int_test_h1_count == 1, "handler called once on edge");

    // Without another edge, dispatch does not re-invoke
    dispatch();
    expect(int_test_h1_count == 1, "edge was consumed");

    // Signal another edge
    test_gpio_set_edge(2, true);
    dispatch();
    expect(int_test_h1_count == 2, "handler called again on new edge");

    detachInterrupt(2);
    test_gpio_clear_all_edges();
    clearError();
}

static int reentrant_h_calls = 0;
void reentrant_handler() {
    ++reentrant_h_calls;
    dispatch();
    delay(10);
}

void interrupt_reentrancy_guard() {
    test_gpio_clear_all_edges();
    clearError();
    reentrant_h_calls = 0;

    expect(attachInterrupt(2, &reentrant_handler, RISING), "attach reentrant handler");

    test_gpio_set_edge(2, true);
    dispatch();
    expect(reentrant_h_calls == 1, "handler runs exactly once despite nested dispatch and delay");

    detachInterrupt(2);
    test_gpio_clear_all_edges();
    clearError();
}

static int mut_h0_calls = 0;
static int mut_h1_calls = 0;

void mut_detach_later_h0() {
    ++mut_h0_calls;
    detachInterrupt(1);
}

void mut_target_h1() {
    ++mut_h1_calls;
}

void mut_detach_earlier_h1() {
    ++mut_h1_calls;
    detachInterrupt(0);
}

void mut_attach_later_h0() {
    ++mut_h0_calls;
    attachInterrupt(1, &mut_target_h1, RISING);
    test_gpio_set_edge(1, true);
}

void mut_attach_earlier_h1() {
    ++mut_h1_calls;
    attachInterrupt(0, &mut_target_h1, RISING);
    test_gpio_set_edge(0, true);
}

static int mut_self_calls = 0;
void mut_self_detach_h0() {
    ++mut_self_calls;
    detachInterrupt(0);
}

void interrupt_table_mutation_during_dispatch() {
    test_gpio_clear_all_edges();
    clearError();

    // 1. Detach later slot during pass
    mut_h0_calls = 0;
    mut_h1_calls = 0;
    expect(attachInterrupt(0, &mut_detach_later_h0, RISING), "attach slot 0");
    expect(attachInterrupt(1, &mut_target_h1, RISING), "attach slot 1");
    test_gpio_set_edge(0, true);
    test_gpio_set_edge(1, true);

    dispatch();
    expect(mut_h0_calls == 1, "slot 0 ran");
    expect(mut_h1_calls == 0, "slot 1 skipped because it was detached during pass");
    detachInterrupt(0);

    // 2. Detach earlier slot during pass
    mut_h0_calls = 0;
    mut_h1_calls = 0;
    expect(attachInterrupt(0, &mut_target_h1, RISING), "attach slot 0");
    expect(attachInterrupt(1, &mut_detach_earlier_h1, RISING), "attach slot 1");
    test_gpio_set_edge(0, true);
    test_gpio_set_edge(1, true);

    dispatch();
    expect(mut_h1_calls == 2, "slot 0 ran (via target_h1) and slot 1 ran");

    test_gpio_set_edge(0, true);
    test_gpio_set_edge(1, true);
    dispatch();
    expect(mut_h1_calls == 3, "only slot 1 ran in pass 2; slot 0 was detached");
    detachInterrupt(1);

    // 3. Attach later slot during pass
    mut_h0_calls = 0;
    mut_h1_calls = 0;
    expect(attachInterrupt(0, &mut_attach_later_h0, RISING), "attach slot 0");
    test_gpio_set_edge(0, true);

    dispatch();
    expect(mut_h0_calls == 1, "slot 0 ran in pass 1");
    expect(mut_h1_calls == 0, "slot 1 does not run in pass 1");

    dispatch();
    expect(mut_h1_calls == 1, "slot 1 runs in pass 2");
    detachInterrupt(0);
    detachInterrupt(1);

    // 4. Attach earlier slot during pass
    mut_h0_calls = 0;
    mut_h1_calls = 0;
    expect(attachInterrupt(0, &mut_target_h1, RISING), "fill slot 0");
    expect(attachInterrupt(1, &mut_attach_earlier_h1, RISING), "fill slot 1");
    expect(detachInterrupt(0), "empty slot 0");

    test_gpio_set_edge(1, true);
    dispatch();
    expect(mut_h1_calls == 1, "slot 1 ran in pass 1");
    expect(mut_h0_calls == 0, "slot 0 did not run in pass 1");

    dispatch();
    expect(mut_h1_calls == 2, "slot 0 ran in pass 2");
    detachInterrupt(0);
    detachInterrupt(1);

    // 5. Self-detach
    mut_self_calls = 0;
    expect(attachInterrupt(0, &mut_self_detach_h0, RISING), "attach self-detach");
    test_gpio_set_edge(0, true);
    dispatch();
    expect(mut_self_calls == 1, "self-detach handler ran in pass 1");

    test_gpio_set_edge(0, true);
    dispatch();
    expect(mut_self_calls == 1, "self-detached handler does not run in pass 2");

    test_gpio_clear_all_edges();
    clearError();
}

static int edge_latch_calls = 0;
void edge_latch_handler() {
    ++edge_latch_calls;
    if (edge_latch_calls == 1) {
        test_gpio_set_edge(2, true);
    }
}

void interrupt_edge_latched_during_handler() {
    test_gpio_clear_all_edges();
    clearError();
    edge_latch_calls = 0;

    expect(attachInterrupt(2, &edge_latch_handler, RISING), "attach handler");

    test_gpio_set_edge(2, true);
    dispatch();
    expect(edge_latch_calls == 1, "handler ran once in pass 1");

    dispatch();
    expect(edge_latch_calls == 2, "latched edge reported in pass 2");

    detachInterrupt(2);
    test_gpio_clear_all_edges();
    clearError();
}

static int int_exit_h1_calls = 0;
static int int_exit_h2_calls = 0;

void int_exit_handler1() {
    ++int_exit_h1_calls;
    requestExit(77);
}

void int_exit_handler2() {
    ++int_exit_h2_calls;
}

void interrupt_dispatch_request_exit() {
    test_gpio_clear_all_edges();
    clearError();
    int_exit_h1_calls = 0;
    int_exit_h2_calls = 0;

    expect(attachInterrupt(0, &int_exit_handler1, RISING), "attach exit handler pin 0");
    expect(attachInterrupt(1, &int_exit_handler2, RISING), "attach secondary handler pin 1");

    test_gpio_set_edge(0, true);
    test_gpio_set_edge(1, true);

    dispatch();

    expect(int_exit_h1_calls == 1, "handler 1 ran and requested exit");
    expect(int_exit_h2_calls == 0, "handler 2 aborted because requestExit was called");
    expect(exitRequested(), "exit flag is raised");
    expect(exitCode() == 77, "exit code is 77");

    detachInterrupt(0);
    detachInterrupt(1);
    test_gpio_clear_all_edges();
    run(&setup_nop, &loop_completes); // reset exit flag
    clearError();
}

static int int_preserved_calls = 0;
void int_preserved_handler() {
    ++int_preserved_calls;
}

void setup_preserved_test() {}
void loop_preserved_run1() {
    requestExit(0);
}
void loop_preserved_run2() {
    test_gpio_set_edge(2, true);
    dispatch();
    requestExit(0);
}

void interrupt_preserved_across_runs() {
    test_gpio_clear_all_edges();
    clearError();
    int_preserved_calls = 0;

    expect(attachInterrupt(2, &int_preserved_handler, RISING), "attach interrupt pin 2");

    expect(run(&setup_preserved_test, &loop_preserved_run1) == 0, "run 1 succeeds");
    expect(run(&setup_preserved_test, &loop_preserved_run2) == 0, "run 2 succeeds");
    expect(int_preserved_calls == 1, "interrupt handler was preserved across run invocations");

    detachInterrupt(2);
    test_gpio_clear_all_edges();
    clearError();
}

void spi_communication() {
    clearError();
    test_reset_spi();

    // SPI on unsupported board
    test_set_spi_present(false);
    run([]{
        clearError();
        expect(!SPI.begin(), "SPI.begin fails on board without SPI");
        expect(lastError() == Status::Unsupported, "SPI.begin latches Unsupported");
        requestExit(0);
    }, nullptr);
    test_set_spi_present(true);
    clearError();

    // SPI begin, transfer, end
    run([]{
        clearError();
        expect(SPI.begin(), "SPI.begin succeeds");
        expect(lastError() == Status::Ok, "no error after SPI.begin");

        // 8-bit transfer with xor_mask=0 is loopback
        byte result = SPI.transfer(static_cast<byte>(0xA5));
        expect(result == 0xA5, "SPI.transfer 8-bit loopback");

        SPI.endTransaction();
        expect(SPI.end(), "SPI.end succeeds");
        requestExit(0);
    }, nullptr);
    clearError();

    // SPI transfer before begin
    run([]{
        clearError();
        byte r = SPI.transfer(static_cast<byte>(0x42));
        expect(r == 0, "SPI.transfer returns 0 before begin");
        expect(lastError() == Status::NotInitialized, "SPI.transfer latches NotInitialized");
        requestExit(0);
    }, nullptr);
    clearError();

    // SPI 16-bit transfer MSB first
    run([]{
        clearError();
        expect(SPI.begin(), "SPI.begin for transfer16");
        SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
        word r16 = SPI.transfer16(static_cast<word>(0x1234));
        expect(r16 == 0x1234, "SPI.transfer16 MSB loopback");
        SPI.endTransaction();
        expect(SPI.end(), "SPI.end after transfer16");
        requestExit(0);
    }, nullptr);
    clearError();

    // SPI 16-bit transfer LSB first
    run([]{
        clearError();
        expect(SPI.begin(), "SPI.begin for LSB transfer16");
        SPI.beginTransaction(SPISettings(1000000, LSBFIRST, SPI_MODE0));
        word r16 = SPI.transfer16(static_cast<word>(0xABCD));
        expect(r16 == 0xABCD, "SPI.transfer16 LSB loopback");
        SPI.endTransaction();
        expect(SPI.end(), "SPI.end after LSB transfer16");
        requestExit(0);
    }, nullptr);
    clearError();

    // SPI buffer transfer
    test_set_spi_xor_mask(0xFF);
    run([]{
        clearError();
        expect(SPI.begin(), "SPI.begin for buffer transfer");
        byte buf[4] = {0x00, 0x55, 0xAA, 0xFF};
        SPI.transfer(buf, 4);
        expect(buf[0] == 0xFF, "buffer[0] xor 0xFF");
        expect(buf[1] == 0xAA, "buffer[1] xor 0xFF");
        expect(buf[2] == 0x55, "buffer[2] xor 0xFF");
        expect(buf[3] == 0x00, "buffer[3] xor 0xFF");
        expect(SPI.end(), "SPI.end after buffer transfer");
        requestExit(0);
    }, nullptr);
    test_set_spi_xor_mask(0);
    clearError();

    // beginTransaction before begin
    run([]{
        clearError();
        SPI.beginTransaction(SPISettings{});
        expect(lastError() == Status::NotInitialized, "beginTransaction before begin");
        requestExit(0);
    }, nullptr);
    clearError();

    test_reset_spi();
}

void wire_communication() {
    clearError();
    test_reset_i2c();

    // Wire on unsupported board
    test_set_i2c_present(false);
    run([]{
        clearError();
        expect(!Wire.begin(), "Wire.begin fails without I2C");
        expect(lastError() == Status::Unsupported, "Wire.begin latches Unsupported");
        requestExit(0);
    }, nullptr);
    test_set_i2c_present(true);
    clearError();

    // Wire begin, write, endTransmission
    run([]{
        clearError();
        expect(Wire.begin(), "Wire.begin succeeds");
        expect(lastError() == Status::Ok, "no error after Wire.begin");

        Wire.beginTransmission(static_cast<byte>(0x50));
        expect(Wire.write(static_cast<byte>(0x10)) == 1, "Wire.write byte");
        expect(Wire.write(static_cast<byte>(0x20)) == 1, "Wire.write byte 2");
        expect(Wire.endTransmission() == 0, "endTransmission succeeds");

        requestExit(0);
    }, nullptr);
    expect(test_get_i2c_written_address() == 0x50, "i2c written to address 0x50");
    expect(test_get_i2c_written_size() == 2, "i2c wrote 2 bytes");
    expect(test_get_i2c_written_byte(0) == 0x10, "i2c byte 0 is 0x10");
    expect(test_get_i2c_written_byte(1) == 0x20, "i2c byte 1 is 0x20");
    clearError();
    test_reset_i2c();

    // Wire requestFrom and read
    const unsigned char read_data[] = {0xAA, 0xBB, 0xCC};
    test_set_i2c_read_data(read_data, 3);
    run([]{
        clearError();
        expect(Wire.begin(), "Wire.begin for read");
        const std::size_t count = Wire.requestFrom(static_cast<byte>(0x68), static_cast<std::size_t>(3));
        expect(count == 3, "requestFrom returned 3");
        expect(Wire.available() == 3, "3 bytes available");

        expect(Wire.peek() == 0xAA, "peek first byte");
        expect(Wire.available() == 3, "peek does not consume");
        expect(Wire.read() == 0xAA, "read byte 0");
        expect(Wire.available() == 2, "2 bytes remaining");
        expect(Wire.read() == 0xBB, "read byte 1");
        expect(Wire.read() == 0xCC, "read byte 2");
        expect(Wire.available() == 0, "0 bytes remaining");
        expect(Wire.read() == -1, "read past end returns -1");
        expect(Wire.peek() == -1, "peek past end returns -1");

        expect(Wire.end(), "Wire.end succeeds");
        requestExit(0);
    }, nullptr);
    clearError();
    test_reset_i2c();

    // Wire buffer overflow (32-byte boundary)
    run([]{
        clearError();
        expect(Wire.begin(), "Wire.begin for overflow test");
        Wire.beginTransmission(static_cast<byte>(0x50));
        for (int i = 0; i < 32; ++i) {
            expect(Wire.write(static_cast<byte>(i)) == 1, "write within 32");
        }
        expect(Wire.write(static_cast<byte>(0xFF)) == 0, "write byte 33 returns 0");
        byte result = Wire.endTransmission();
        expect(result == 1, "endTransmission returns 1 on overflow");
        expect(lastError() == Status::BadArgument, "overflow latches BadArgument");

        expect(Wire.end(), "Wire.end after overflow");
        requestExit(0);
    }, nullptr);
    clearError();
    test_reset_i2c();

    // Wire buffer write with array
    run([]{
        clearError();
        expect(Wire.begin(), "Wire.begin for array write");
        Wire.beginTransmission(static_cast<byte>(0x50));
        const byte data[] = {0x01, 0x02, 0x03, 0x04};
        const std::size_t written = Wire.write(data, 4);
        expect(written == 4, "wrote 4 bytes");
        expect(Wire.endTransmission() == 0, "endTransmission ok");
        expect(Wire.end(), "Wire.end");
        requestExit(0);
    }, nullptr);
    expect(test_get_i2c_written_size() == 4, "i2c array wrote 4 bytes");
    clearError();
    test_reset_i2c();

    // Wire setClock before begin
    run([]{
        clearError();
        Wire.setClock(400000);
        expect(lastError() == Status::NotInitialized, "setClock before begin");
        requestExit(0);
    }, nullptr);
    clearError();

    // Wire endTransmission before begin
    run([]{
        clearError();
        Wire.beginTransmission(static_cast<byte>(0x50));
        Wire.write(static_cast<byte>(0x00));
        byte result = Wire.endTransmission();
        expect(result == 4, "endTransmission returns 4 before begin");
        expect(lastError() == Status::NotInitialized, "endTransmission latches NotInitialized");
        requestExit(0);
    }, nullptr);
    clearError();

    // Wire write_read via endTransmission(false) then requestFrom
    const unsigned char wr_read_data[] = {0xDE, 0xAD};
    test_set_i2c_read_data(wr_read_data, 2);
    run([]{
        clearError();
        expect(Wire.begin(), "Wire.begin for write_read");
        Wire.beginTransmission(static_cast<byte>(0x68));
        Wire.write(static_cast<byte>(0x0F));
        expect(Wire.endTransmission(false) == 0, "endTransmission(false) ok");
        const std::size_t count = Wire.requestFrom(static_cast<byte>(0x68), static_cast<std::size_t>(2));
        expect(count == 2, "requestFrom returned 2");
        expect(Wire.read() == 0xDE, "write_read byte 0");
        expect(Wire.read() == 0xAD, "write_read byte 1");
        expect(Wire.end(), "Wire.end after write_read");
        requestExit(0);
    }, nullptr);
    expect(test_get_i2c_written_address() == 0x68, "write_read to address 0x68");
    expect(test_get_i2c_written_size() == 1, "write_read command was 1 byte");
    expect(test_get_i2c_written_byte(0) == 0x0F, "write_read command byte");
    clearError();
    test_reset_i2c();

    // Wire write(const char*) overload
    run([]{
        clearError();
        expect(Wire.begin(), "Wire.begin for string write");
        Wire.beginTransmission(static_cast<byte>(0x50));
        const std::size_t written = Wire.write("Hi");
        expect(written == 2, "write string 2 bytes");
        expect(Wire.endTransmission() == 0, "endTransmission ok");
        expect(Wire.end(), "Wire.end");
        requestExit(0);
    }, nullptr);
    expect(test_get_i2c_written_size() == 2, "string wrote 2 bytes");
    expect(test_get_i2c_written_byte(0) == 'H', "string byte 0");
    expect(test_get_i2c_written_byte(1) == 'i', "string byte 1");
    clearError();
    test_reset_i2c();
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
    {"serial formatting", &serial_formatting},
    {"serial input and waiting", &serial_input_and_waiting},
    {"serial event callback", &serial_event_callback},
    {"math operations", &math_operations},
    {"character classifiers", &character_classifiers},
    {"random numbers", &random_numbers},
    {"bits and bytes", &bits_and_bytes},
    {"shift in and shift out", &shift_in_and_shift_out},
    {"interrupt registration and errors", &interrupt_registration_and_errors},
    {"interrupt table capacity", &interrupt_table_capacity},
    {"interrupt dispatch and edge trigger", &interrupt_dispatch_and_edge_trigger},
    {"interrupt reentrancy guard", &interrupt_reentrancy_guard},
    {"interrupt table mutation during dispatch", &interrupt_table_mutation_during_dispatch},
    {"interrupt edge latched during handler", &interrupt_edge_latched_during_handler},
    {"interrupt dispatch request exit", &interrupt_dispatch_request_exit},
    {"interrupt preserved across runs", &interrupt_preserved_across_runs},
    {"spi communication", &spi_communication},
    {"wire communication", &wire_communication},
};

const mm::test::registrar reg{"mm.sketch", cases};

} // namespace

