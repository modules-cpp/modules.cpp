// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <string_view>

import mm.sketch;
import mm.test;

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
    setup1_calls = 0;
    loop1_calls = 0;
    const int code1 = run(&setup1, &loop1);
    expect(code1 == 42, "first run should return exit code 42");
    expect(setup1_calls == 1, "setup1 should run once");
    expect(loop1_calls == 1, "loop1 should run once");

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

const mm::test::case_ cases[] = {
    {"runner clean start twice", &runner_clean_start_twice},
    {"status latch first failure", &status_latch_first_failure},
    {"delay cancellation preserves latch", &delay_cancellation_preserves_latch},
    {"exit from setup skips loop", &exit_from_setup_skips_loop},
    {"exit from loop completes that loop", &exit_from_loop_completes_that_loop},
    {"digital io and led", &digital_io_and_led},
    {"time and delay", &time_and_delay},
    {"serial communication", &serial_communication},
};

const mm::test::registrar reg{"mm.sketch", cases};

} // namespace
