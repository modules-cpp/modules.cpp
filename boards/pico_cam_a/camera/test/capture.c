/* Camera lifecycle and failure regression tests, with a recording SDK double.
 * Tests the production capture implementation; no camera or Pico SDK needed. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
typedef unsigned int uint;
typedef struct { uint32_t rxf[4]; uint32_t fdebug; } pio_hw_t;
typedef pio_hw_t* PIO;
static pio_hw_t pios[2];
#define pio0 (&pios[0])
#define pio1 (&pios[1])
struct pio_program { const uint16_t* instructions; uint length; int origin; };
typedef struct { uint wrap_start, wrap_end, base, bits; bool shift_right; } pio_sm_config;
typedef struct { uint size, dreq; bool read_increment, write_increment; } dma_channel_config;
typedef unsigned int absolute_time_t;
enum { GPIO_IN, GPIO_FUNC_I2C, PIO_FIFO_JOIN_RX, DMA_SIZE_8, clk_sys,
       pio_pins, PIO_FDEBUG_RXSTALL_LSB = 24 };
enum { MM_PICO_OWNER_NONE, MM_PICO_OWNER_GPIO, MM_PICO_OWNER_CAMERA };
#define PICO_ERROR_TIMEOUT (-2)
static unsigned char mm_pico_pin_owner[30];
static int mm_pico_camera_claimed, mm_pico_i2c_ready[2];
static int i2c0;
static unsigned int ticks, sys_clock = 125000000, program_claims, dma_claims;
static int pio_full[2], dma_unavailable, i2c_error, dma_hang, fifo_stall;
static bool pio_enabled, dma_running;
static unsigned char* dma_buffer;
static size_t dma_size;
static unsigned int qvga, clock_control, initialized_gpio_mask, identity_register;
static int wrong_identity;
static int voltage_set, clock_set;
static pio_sm_config recorded_pio;
static dma_channel_config recorded_dma;
static int pio_index(PIO p) { return p == pio0 ? 0 : 1; }
enum { VREG_VOLTAGE_1_15 = 12 };
static void vreg_set_voltage(int voltage) {
    assert(voltage == VREG_VOLTAGE_1_15); voltage_set = 1;
}
static bool set_sys_clock_khz(uint khz, bool required) {
    assert(voltage_set && ticks >= 10 && khz == 200000 && !required);
    sys_clock = khz * 1000u; clock_set = 1; return true;
}
static uint clock_get_hz(int clock) { (void)clock; return sys_clock; }
static uint16_t pio_encode_wait_pin(bool level, uint pin) { return (uint16_t)(pin | (level ? 128 : 0)); }
static uint16_t pio_encode_in(int source, uint bits) { (void)source; return (uint16_t)(256 | bits); }
static uint16_t pio_encode_jmp(uint address) { return (uint16_t)address; }
static bool pio_can_add_program(PIO p, const struct pio_program* program) {
    assert(program->length == 6); return !pio_full[pio_index(p)];
}
static int pio_claim_unused_sm(PIO p, bool required) { (void)p; assert(!required); return 2; }
static uint pio_add_program(PIO p, const struct pio_program* program) {
    (void)p; assert(program->instructions[0] == 10); assert(program->instructions[1] == 138);
    assert(program->instructions[2] == 137); // LVLD high.
    assert(program->instructions[3] == 136); // PCLK high.
    assert(program->instructions[4] == 257); // IN one bit.
    assert(program->instructions[5] == 8);   // PCLK low.
    ++program_claims; return 7;
}
static void pio_remove_program(PIO p, const struct pio_program* program, uint offset) {
    (void)p; (void)program; assert(offset == 7); assert(program_claims == 1); --program_claims;
}
static void pio_sm_unclaim(PIO p, uint sm) { (void)p; assert(sm == 2); }
static void pio_sm_clear_fifos(PIO p, uint sm) { (void)p; assert(sm == 2); }
static void pio_sm_restart(PIO p, uint sm) { (void)p; assert(sm == 2); }
static void pio_sm_exec(PIO p, uint sm, uint16_t instruction) { (void)p; assert(sm == 2 && instruction == 7); }
static void pio_sm_set_enabled(PIO p, uint sm, bool enabled) {
    assert(sm == 2); pio_enabled = enabled;
    // Model the prior W1C FDEBUG write, which a plain host variable cannot.
    if (enabled) p->fdebug = fifo_stall ? (1u << (PIO_FDEBUG_RXSTALL_LSB + sm)) : 0;
}
static pio_sm_config pio_get_default_sm_config(void) { return (pio_sm_config){0}; }
static void sm_config_set_wrap(pio_sm_config* c, uint first, uint last) { c->wrap_start = first; c->wrap_end = last; }
static void sm_config_set_in_pins(pio_sm_config* c, uint base) { c->base = base; }
static void sm_config_set_in_shift(pio_sm_config* c, bool right, bool automatic, uint bits) {
    assert(automatic); c->shift_right = right; c->bits = bits;
}
static void sm_config_set_fifo_join(pio_sm_config* c, int mode) { (void)c; assert(mode == PIO_FIFO_JOIN_RX); }
static void pio_gpio_init(PIO p, uint pin) { (void)p; assert(pin == 6 || pin == 14); }
static void pio_sm_set_consecutive_pindirs(PIO p, uint sm, uint pin, uint count, bool out) {
    (void)p; assert(sm == 2 && (pin == 6 || pin == 14) && count == 1 && !out);
}
static void pio_sm_init(PIO p, uint sm, uint offset, const pio_sm_config* c) {
    (void)p; assert(sm == 2 && offset == 7); recorded_pio = *c;
}
static int dma_claim_unused_channel(bool required) {
    assert(!required); if (dma_unavailable) return -1; ++dma_claims; return 3;
}
static void dma_channel_abort(uint channel) { assert(channel == 3); dma_running = false; }
static void dma_channel_unclaim(uint channel) { assert(channel == 3 && dma_claims == 1); --dma_claims; }
static dma_channel_config dma_channel_get_default_config(uint channel) { assert(channel == 3); return (dma_channel_config){0}; }
static void channel_config_set_transfer_data_size(dma_channel_config* c, int size) { c->size = (uint)size; }
static void channel_config_set_read_increment(dma_channel_config* c, bool increment) { c->read_increment = increment; }
static void channel_config_set_write_increment(dma_channel_config* c, bool increment) { c->write_increment = increment; }
static void channel_config_set_dreq(dma_channel_config* c, uint dreq) { c->dreq = dreq; }
static uint pio_get_dreq(PIO p, uint sm, bool tx) { (void)p; assert(sm == 2 && !tx); return 17; }
static void dma_channel_configure(uint channel, const dma_channel_config* c,
    void* dest, const volatile void* source, size_t size, bool start) {
    (void)source; assert(channel == 3 && !start); recorded_dma = *c; dma_buffer = dest; dma_size = size;
}
static void dma_channel_start(uint channel) { assert(channel == 3); dma_running = true; }
static bool dma_channel_is_busy(uint channel) {
    assert(channel == 3);
    if (dma_hang) return dma_running;
    memset(dma_buffer, 0x5a, dma_size); dma_running = false; return false;
}
static absolute_time_t make_timeout_time_ms(uint ms) { return ticks + ms; }
static bool time_reached(absolute_time_t deadline) { return ticks >= deadline; }
static void tight_loop_contents(void) { ++ticks; }
static void gpio_init(uint pin) { initialized_gpio_mask |= 1u << pin; }
static void gpio_set_dir(uint pin, int direction) { (void)pin; assert(direction == GPIO_IN); }
static void gpio_disable_pulls(uint pin) { (void)pin; }
static void gpio_deinit(uint pin) { initialized_gpio_mask &= ~(1u << pin); }
static void gpio_set_function(uint pin, int function) { assert((pin == 4 || pin == 5) && function == GPIO_FUNC_I2C); }
static void gpio_pull_up(uint pin) { assert(pin == 4 || pin == 5); }
static void i2c_init(int bus, uint baud) { assert(bus == i2c0 && baud == 100000); }
static void i2c_deinit(int bus) { assert(bus == i2c0); }
static int i2c_write_timeout_us(int bus, uint address, const uint8_t* data, size_t size, bool nostop, uint us) {
    assert(bus == i2c0 && address == 0x24 && us == 10000);
    if (i2c_error) return i2c_error;
    if (size == 3) {
        assert(!nostop);
        if (data[0] == 0x30 && data[1] == 0x10) qvga = data[2];
        if (data[0] == 0x30 && data[1] == 0x60) clock_control = data[2];
    } else { assert(size == 2 && nostop); identity_register = data[1]; }
    return (int)size;
}
static int i2c_read_timeout_us(int bus, uint address, uint8_t* data, size_t size, bool nostop, uint us) {
    assert(bus == i2c0 && address == 0x24 && size == 1 && !nostop && us == 10000);
    data[0] = wrong_identity ? 0 : identity_register == 0 ? 0x01 : 0xb0; return 1;
}
static void sleep_ms(uint ms) { ticks += ms; }
#include "../capture.inc.c"

static unsigned char frame[324 * 244];
static void released(void) {
    assert(!program_claims && !dma_claims && !mm_pico_camera_claimed && !mm_cam_ready);
    assert(!pio_enabled && !dma_running && initialized_gpio_mask == 0);
    for (uint i = 0; i < 30; ++i) assert(mm_pico_pin_owner[i] == MM_PICO_OWNER_NONE);
}
// Exercise the actual encoded instruction sequence against a serial waveform.
// One bit lasts 250 units and a 200 MHz PIO cycle 45 units (9 GHz model
// timebase). Figure 6.5: data changes at the low boundary, clock rises halfway
// through the bit. Sweep sub-cycle phase, full 324-pixel lines and blanking.
// Equal data/clock synchronizer latency is represented by the phase sweep.
static void timing(void) {
    const uint line_bits = 324u * 8u;
    const uint line_time = (line_bits + 40u) * 250u;
    for (uint phase = 0; phase < 250; ++phase) {
        const uint frame_start = 500u + phase;
        const uint data_start = frame_start + line_time; // FVLD advances one row.
        uint pc = 0, samples = 0;
        for (uint time = 0; time < data_start + 4u * line_time; time += 45u) {
            const bool fvld = time >= frame_start;
            bool lvld = false, pclk = false;
            uint bit = UINT_MAX;
            if (time >= data_start) {
                const uint row = (time - data_start) / line_time;
                const uint at = (time - data_start) % line_time;
                if (row < 3 && at < line_bits * 250u) {
                    lvld = true;
                    pclk = (at % 250u) >= 125u;
                    bit = row * line_bits + at / 250u;
                }
            }
            const uint16_t instruction = mm_cam_instructions[pc];
            if (instruction == 257) {
                assert(bit == samples); // No late, blanking, missing or duplicate bit.
                if (++samples == 3u * line_bits) break;
                ++pc;
            } else {
                const uint pin = instruction & 127u;
                const bool level = pin == 10 ? fvld : pin == 9 ? lvld : pclk;
                if (level == ((instruction & 128u) != 0)) ++pc;
            }
            if (pc == 6) pc = 2;
        }
        assert(samples == 3u * line_bits);
    }
}
int main(void) {
    assert(voltage_set && clock_set && sys_clock == 200000000);
    assert(mm_pico_cam_capture(frame, sizeof(frame), 10) == MM_CAM_NOT_INITIALIZED);
    assert(mm_pico_cam_capture(frame, sizeof(frame) - 1, 10) == MM_CAM_BAD_ARGUMENT);
    assert(mm_pico_cam_capture(NULL, sizeof(frame), 10) == MM_CAM_BAD_ARGUMENT);
    assert(mm_pico_cam_capture(frame, sizeof(frame), 0) == MM_CAM_BAD_ARGUMENT);
    assert(mm_pico_cam_sleep() == MM_CAM_OK); released();
    sys_clock = 125000000; assert(mm_pico_cam_initialize() == MM_CAM_UNSUPPORTED); released(); sys_clock = 200000000;
    mm_pico_pin_owner[14] = MM_PICO_OWNER_GPIO;
    assert(mm_pico_cam_initialize() == MM_CAM_BUSY); mm_pico_pin_owner[14] = MM_PICO_OWNER_NONE; released();
    pio_full[0] = pio_full[1] = 1; assert(mm_pico_cam_initialize() == MM_CAM_BUSY); released();
    pio_full[1] = 0; dma_unavailable = 1; assert(mm_pico_cam_initialize() == MM_CAM_BUSY); released(); dma_unavailable = 0;
    i2c_error = PICO_ERROR_TIMEOUT; assert(mm_pico_cam_initialize() == MM_CAM_TIMEOUT); released(); i2c_error = 0;
    wrong_identity = 1; assert(mm_pico_cam_initialize() == MM_CAM_TRANSPORT_ERROR); released(); wrong_identity = 0;
    assert(mm_pico_cam_initialize() == MM_CAM_OK); assert(mm_cam_pio == pio1);
    assert(mm_pico_cam_initialize() == MM_CAM_OK && program_claims == 1 && dma_claims == 1);
    // Match ArduCAM: serial gated clock and msb_en must both be set.
    assert(qvga == 1 && clock_control == 0x30);
    assert(recorded_pio.base == 6 && recorded_pio.bits == 8 && !recorded_pio.shift_right);
    assert(recorded_pio.wrap_start == 9 && recorded_pio.wrap_end == 12);
    timing();
    sys_clock = 125000000;
    assert(mm_pico_cam_capture(frame, sizeof(frame), 7) == MM_CAM_UNSUPPORTED);
    sys_clock = 200000000;
    dma_hang = 1; uint start = ticks;
    assert(mm_pico_cam_capture(frame, sizeof(frame), 7) == MM_CAM_TIMEOUT && ticks == start + 7);
    assert(!pio_enabled && !dma_running); dma_hang = 0;
    assert(mm_pico_cam_capture(frame, sizeof(frame), 7) == MM_CAM_OK);
    assert(frame[0] == 0x5a && frame[sizeof(frame)-1] == 0x5a);
    assert(recorded_dma.size == DMA_SIZE_8 && !recorded_dma.read_increment && recorded_dma.write_increment);
    fifo_stall = 1; assert(mm_pico_cam_capture(frame, sizeof(frame), 7) == MM_CAM_TRANSPORT_ERROR); fifo_stall = 0;
    i2c_error = -1; assert(mm_pico_cam_sleep() == MM_CAM_TRANSPORT_ERROR); released(); i2c_error = 0;
    assert(mm_pico_cam_initialize() == MM_CAM_OK); assert(mm_pico_cam_sleep() == MM_CAM_OK); released();
    puts("PASS: PICO-Cam-A timing phases, clock setup, lifecycle, and failure recovery");
}
