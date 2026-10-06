/* Adapted HM01B0 camera support using ArduCAM's initialization and PIO/DMA
 * capture approach, as distributed in Waveshare's PICO-Cam-A example.
 * Original support code: https://github.com/ArduCAM/RPI-Pico-Cam
 * https://github.com/ArduCAM/RPI-Pico-Cam/blob/master/rp2040_hm01b0/arducam/arducam.c
 * https://github.com/ArduCAM/RPI-Pico-Cam/blob/master/rp2040_hm01b0/image.pio
 * Local adaptation adds board-specific wiring, bounded transfers, PIO frame
 * synchronization, resource allocation/release, and the private C ABI.
 * Included by the Pico SDK adapter only. Single-core synchronous API;
 * no SDK types cross the C++ module boundary. */
#include "camera-c.h"
#include "hm01b0_init.h"

#define MM_CAM_PIXELS (324u * 244u)
static const unsigned int mm_cam_pins[] = {4, 5, 6, 14, 15, 16};
static PIO mm_cam_pio;
static int mm_cam_sm = -1;
static int mm_cam_dma = -1;
static unsigned int mm_cam_offset;
static int mm_cam_ready;
static uint16_t mm_cam_instructions[5];
static const struct pio_program mm_cam_program = {
    .instructions = mm_cam_instructions, .length = 5, .origin = -1
};

static int mm_cam_write(uint16_t address, uint8_t value) {
    const uint8_t bytes[] = {(uint8_t)(address >> 8), (uint8_t)address, value};
    const int result = i2c_write_timeout_us(i2c0, 0x24, bytes, sizeof(bytes), false, 10000);
    return result == (int)sizeof(bytes) ? MM_CAM_OK :
           result == PICO_ERROR_TIMEOUT ? MM_CAM_TIMEOUT : MM_CAM_TRANSPORT_ERROR;
}

static void mm_cam_release(void) {
    if (mm_cam_dma >= 0) {
        dma_channel_abort((uint)mm_cam_dma);
        dma_channel_unclaim((uint)mm_cam_dma);
        mm_cam_dma = -1;
    }
    if (mm_cam_sm >= 0) {
        pio_sm_set_enabled(mm_cam_pio, (uint)mm_cam_sm, false);
        pio_sm_clear_fifos(mm_cam_pio, (uint)mm_cam_sm);
        pio_remove_program(mm_cam_pio, &mm_cam_program, mm_cam_offset);
        pio_sm_unclaim(mm_cam_pio, (uint)mm_cam_sm);
        mm_cam_sm = -1;
    }
    if (mm_pico_camera_claimed) {
        i2c_deinit(i2c0);
        for (size_t i = 0; i < sizeof(mm_cam_pins) / sizeof(mm_cam_pins[0]); ++i) {
            gpio_deinit(mm_cam_pins[i]);
            mm_pico_pin_owner[mm_cam_pins[i]] = MM_PICO_OWNER_NONE;
        }
        mm_pico_camera_claimed = 0;
    }
    mm_cam_ready = 0;
}

int mm_pico_cam_initialize(void) {
    if (mm_cam_ready) return MM_CAM_OK;
    if (mm_pico_i2c_ready[0]) return MM_CAM_BUSY;
    for (size_t i = 0; i < sizeof(mm_cam_pins) / sizeof(mm_cam_pins[0]); ++i) {
        const unsigned int pin = mm_cam_pins[i];
        if (mm_pico_pin_owner[pin] != MM_PICO_OWNER_NONE) return MM_CAM_BUSY;
    }
    // Gated PCLK supplies exactly the active bits, so no per-bit LVLD wait
    // is needed. Three instructions keep up with the board's 36 MHz PCLK
    // at the SDK's default 125 MHz system clock, without an overclock.
    if (clock_get_hz(clk_sys) < 125000000u) return MM_CAM_UNSUPPORTED;
    mm_cam_instructions[0] = pio_encode_wait_pin(false, 10); // Fresh FVLD edge.
    mm_cam_instructions[1] = pio_encode_wait_pin(true, 10);
    mm_cam_instructions[2] = pio_encode_wait_pin(false, 8);
    mm_cam_instructions[3] = pio_encode_wait_pin(true, 8);
    mm_cam_instructions[4] = pio_encode_in(pio_pins, 1);
    const PIO candidates[] = {pio0, pio1};
    for (size_t i = 0; i < 2; ++i) {
        if (!pio_can_add_program(candidates[i], &mm_cam_program)) continue;
        const int sm = pio_claim_unused_sm(candidates[i], false);
        if (sm < 0) continue;
        mm_cam_pio = candidates[i];
        mm_cam_sm = sm;
        mm_cam_offset = pio_add_program(mm_cam_pio, &mm_cam_program);
        break;
    }
    if (mm_cam_sm < 0) return MM_CAM_BUSY;
    mm_cam_dma = dma_claim_unused_channel(false);
    if (mm_cam_dma < 0) { mm_cam_release(); return MM_CAM_BUSY; }
    for (size_t i = 0; i < sizeof(mm_cam_pins) / sizeof(mm_cam_pins[0]); ++i) {
        gpio_init(mm_cam_pins[i]);
        gpio_set_dir(mm_cam_pins[i], GPIO_IN);
        gpio_disable_pulls(mm_cam_pins[i]);
        mm_pico_pin_owner[mm_cam_pins[i]] = MM_PICO_OWNER_CAMERA;
    }
    mm_pico_camera_claimed = 1;
    i2c_init(i2c0, 100000);
    gpio_set_function(4, GPIO_FUNC_I2C);
    gpio_set_function(5, GPIO_FUNC_I2C);
    gpio_pull_up(4);
    gpio_pull_up(5);
    int status = mm_cam_write(0x0103, 0x00); // Software reset; no reset GPIO.
    if (status == MM_CAM_OK) sleep_ms(10);
    // Read the two model registers separately; no auto-increment assumption.
    const uint8_t expected[] = {0x01, 0xb0};
    for (uint8_t reg = 0; status == MM_CAM_OK && reg < 2; ++reg) {
        const uint8_t command[] = {0, reg};
        uint8_t identity = 0;
        const int written = i2c_write_timeout_us(i2c0, 0x24, command, 2, true, 10000);
        const int read = written == 2 ? i2c_read_timeout_us(i2c0, 0x24, &identity, 1, false, 10000) : written;
        status = written == 2 && read == 1 && identity == expected[reg]
                     ? MM_CAM_OK : (written == PICO_ERROR_TIMEOUT || read == PICO_ERROR_TIMEOUT)
                     ? MM_CAM_TIMEOUT : MM_CAM_TRANSPORT_ERROR;
    }
    for (size_t i = 0; status == MM_CAM_OK &&
         i < sizeof(mm_cam_registers) / sizeof(mm_cam_registers[0]); ++i) {
        status = mm_cam_write(mm_cam_registers[i].address, mm_cam_registers[i].value);
    }
    if (status != MM_CAM_OK) { mm_cam_release(); return status; }
    pio_sm_config config = pio_get_default_sm_config();
    sm_config_set_wrap(&config, mm_cam_offset + 2, mm_cam_offset + 4);
    sm_config_set_in_pins(&config, 6);
    sm_config_set_in_shift(&config, false, true, 8); // MSB first; byte in low FIFO bits.
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
    pio_gpio_init(mm_cam_pio, 6);
    pio_gpio_init(mm_cam_pio, 14);
    pio_sm_set_consecutive_pindirs(mm_cam_pio, (uint)mm_cam_sm, 6, 1, false);
    pio_sm_set_consecutive_pindirs(mm_cam_pio, (uint)mm_cam_sm, 14, 1, false);
    pio_sm_init(mm_cam_pio, (uint)mm_cam_sm, mm_cam_offset, &config);
    mm_cam_ready = 1;
    return MM_CAM_OK;
}

int mm_pico_cam_capture(unsigned char* data, size_t size, unsigned long timeout_ms) {
    if (data == NULL || size != MM_CAM_PIXELS || timeout_ms == 0)
        return MM_CAM_BAD_ARGUMENT;
#if ULONG_MAX > UINT32_MAX
    if (timeout_ms > UINT32_MAX) return MM_CAM_BAD_ARGUMENT;
#endif
    if (!mm_cam_ready) return MM_CAM_NOT_INITIALIZED;
    const absolute_time_t deadline = make_timeout_time_ms((uint32_t)timeout_ms);
    const uint sm = (uint)mm_cam_sm;
    const uint channel = (uint)mm_cam_dma;
    pio_sm_set_enabled(mm_cam_pio, sm, false);
    pio_sm_clear_fifos(mm_cam_pio, sm);
    pio_sm_restart(mm_cam_pio, sm);
    pio_sm_exec(mm_cam_pio, sm, pio_encode_jmp(mm_cam_offset));
    dma_channel_config config = dma_channel_get_default_config(channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
    channel_config_set_read_increment(&config, false);
    channel_config_set_write_increment(&config, true);
    channel_config_set_dreq(&config, pio_get_dreq(mm_cam_pio, sm, false));
    dma_channel_configure(channel, &config, data, &mm_cam_pio->rxf[sm], size, false);
    // FVLD synchronization is in PIO, so an interrupt on this core cannot
    // miss the first pixels between seeing the edge and enabling the receiver.
    const uint32_t stalled = 1u << (PIO_FDEBUG_RXSTALL_LSB + sm);
    mm_cam_pio->fdebug = stalled;
    dma_channel_start(channel);
    pio_sm_set_enabled(mm_cam_pio, sm, true);
    while (dma_channel_is_busy(channel)) {
        if (time_reached(deadline)) {
            pio_sm_set_enabled(mm_cam_pio, sm, false);
            dma_channel_abort(channel);
            pio_sm_clear_fifos(mm_cam_pio, sm);
            return MM_CAM_TIMEOUT;
        }
        tight_loop_contents();
    }
    pio_sm_set_enabled(mm_cam_pio, sm, false);
    return (mm_cam_pio->fdebug & stalled) ? MM_CAM_TRANSPORT_ERROR : MM_CAM_OK;
}

int mm_pico_cam_sleep(void) {
    const int status = mm_cam_ready ? mm_cam_write(0x0100, 0x00) : MM_CAM_OK;
    mm_cam_release();
    return status;
}
