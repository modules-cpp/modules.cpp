// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The Pico platform's private ABI between platform.pico.mcu and the SDK adapter.
// It exists because only the adapter may include Pico SDK headers, and the
// adapter is C. Nothing outside this platform declares or calls these.
//
// C includes this header directly. C++ includes mcu-cxx.h, which wraps it in
// extern "C"; including this one from C++ would give the declarations C++ linkage
// and they would then not match the adapter's definitions.

#include <stddef.h>
#include <stdint.h>

enum {
    MM_PICO_MCU_OK = 0,
    MM_PICO_MCU_BAD_ARGUMENT = 1,
    MM_PICO_MCU_UNSUPPORTED = 2,
    MM_PICO_MCU_BUSY = 3,
    MM_PICO_MCU_TIMEOUT = 4,
    MM_PICO_MCU_TRANSPORT_ERROR = 5
};

enum { MM_PICO_MCU_DIRECTION_IN = 0, MM_PICO_MCU_DIRECTION_OUT = 1 };
enum { MM_PICO_MCU_PULL_NONE = 0, MM_PICO_MCU_PULL_UP = 1, MM_PICO_MCU_PULL_DOWN = 2 };
enum { MM_PICO_MCU_EDGE_RISING = 0, MM_PICO_MCU_EDGE_FALLING = 1,
       MM_PICO_MCU_EDGE_BOTH = 2 };

int mm_pico_mcu_gpio_configure(unsigned int pin, int direction, int pull);
int mm_pico_mcu_gpio_write(unsigned int pin, int high);
int mm_pico_mcu_gpio_read(unsigned int pin, int* high);
int mm_pico_mcu_gpio_watch(unsigned int pin, int pull, int edge);
int mm_pico_mcu_gpio_take(unsigned int pin, int* pending);
int mm_pico_mcu_gpio_unwatch(unsigned int pin);
int mm_pico_mcu_gpio_wait(unsigned int pin, unsigned long timeout_ms, int* pending);
int mm_pico_mcu_spi_configure(unsigned int instance, unsigned int clock_pin,
                              unsigned int transmit_pin, unsigned int receive_pin,
                              int has_receive, unsigned long baud, int mode,
                              int least_significant_first);
int mm_pico_mcu_spi_write(unsigned int instance, const unsigned char* data, size_t size);
int mm_pico_mcu_spi_transfer(unsigned int instance, const unsigned char* transmit,
                             unsigned char* receive, size_t size);
int mm_pico_mcu_i2c_configure(unsigned int instance, unsigned int data_pin,
                              unsigned int clock_pin, unsigned long baud);
int mm_pico_mcu_i2c_write(unsigned int instance, unsigned int address,
                          const unsigned char* data, size_t size);
int mm_pico_mcu_i2c_read(unsigned int instance, unsigned int address,
                         unsigned char* data, size_t size);
int mm_pico_mcu_i2c_write_read(unsigned int instance, unsigned int address,
                               const unsigned char* command, size_t command_size,
                               unsigned char* data, size_t size);
int mm_pico_mcu_uart_write(unsigned int instance, const char* text);
int mm_pico_mcu_uart_configure(unsigned int instance, unsigned int transmit_pin,
                               unsigned int receive_pin, unsigned long baud);
int mm_pico_mcu_uart_send(unsigned int instance, const unsigned char* data, size_t size,
                          size_t* accepted);
int mm_pico_mcu_uart_receive(unsigned int instance, unsigned char* data, size_t size,
                             size_t* count);
int mm_pico_mcu_uart_release(unsigned int instance);

// Block storage: a USB mass-storage device on the PIO USB host port of a board
// with one (MM_BOARD_HAS_USB_HOST). Every call answers
// MM_PICO_MCU_UNSUPPORTED on any other board. size is in bytes, a whole
// number of blocks.
int mm_pico_mcu_has_storage(void);
int mm_pico_mcu_storage_poll(int* present);
int mm_pico_mcu_storage_geometry(unsigned long long* block_count, unsigned int* block_size);
int mm_pico_mcu_storage_read(unsigned long long block, void* data, unsigned long size);
int mm_pico_mcu_storage_write(unsigned long long block, const void* data, unsigned long size);
int mm_pico_mcu_default_uart(unsigned int* instance, unsigned int* transmit_pin,
                             unsigned int* receive_pin);
int mm_pico_mcu_has_second_uart(void);
int mm_pico_mcu_delay_ms(unsigned long milliseconds);
int mm_pico_mcu_ticks_ms(unsigned long* ticks);
int mm_pico_mcu_delay_us(unsigned long microseconds);
int mm_pico_mcu_ticks_us(unsigned long* ticks);

// I2S over PIO and DMA, instance zero only. Frames cross as PIO words: one
// word a frame with sixteen-bit slots, the left slot in the high half; two
// words a frame, left then right, with thirty-two-bit slots. receive selects
// the direction for start, progress, and stop.
int mm_pico_mcu_i2s_configure(unsigned int instance, unsigned int bit_clock,
                              unsigned int word_clock, int has_transmit, unsigned int transmit,
                              int has_receive, unsigned int receive, unsigned long rate_hz,
                              unsigned int slot_bits);
int mm_pico_mcu_i2s_rate(unsigned int instance, unsigned long long* numerator,
                         unsigned long long* denominator);
int mm_pico_mcu_i2s_start(unsigned int instance, int receive);
int mm_pico_mcu_i2s_write(unsigned int instance, const uint32_t* words, size_t frames,
                          size_t* accepted);
int mm_pico_mcu_i2s_read(unsigned int instance, uint32_t* words, size_t frames,
                         size_t* count);
int mm_pico_mcu_i2s_progress(unsigned int instance, int receive,
                             unsigned long long* completed, size_t* queued,
                             unsigned long* missed);
int mm_pico_mcu_i2s_stop(unsigned int instance, int receive);
int mm_pico_mcu_i2s_release(unsigned int instance);

// A one-wire pulse-width-coded output over PIO, instances zero and one: each
// bit is bit_period_ns, high for zero_high_ns or one_high_ns, most significant
// bit first. write blocks until the frame is out and reset_ns has passed.
int mm_pico_mcu_pulse_configure(unsigned int instance, unsigned int pin,
                                unsigned long bit_period_ns, unsigned long zero_high_ns,
                                unsigned long one_high_ns, unsigned long reset_ns);
int mm_pico_mcu_pulse_write(unsigned int instance, const unsigned char* data, size_t size);
int mm_pico_mcu_pulse_release(unsigned int instance);

// The data region at the top of program flash, MM_BOARD_FLASH_REGION_BYTES
// long and ending at PICO_FLASH_SIZE_BYTES. Offsets are bytes from its start.
// UNSUPPORTED on a board whose region is zero bytes, or whose image reaches
// into it, which the bridge's UF2 check refuses at build time already.
int mm_pico_mcu_flash_region_geometry(unsigned long long* size, unsigned int* read_size,
                                      unsigned int* program_size, unsigned int* erase_size);
int mm_pico_mcu_flash_region_read(unsigned long long offset, void* data, size_t size);
int mm_pico_mcu_flash_region_program(unsigned long long offset, const void* data, size_t size);
int mm_pico_mcu_flash_region_erase(unsigned long long offset, unsigned long long size);

// A critical section on the calling core: the SDK's save_and_disable_interrupts
// and restore_interrupts. saved is the core's mask word, PRIMASK on Arm and
// mstatus.MIE on RISC-V, handed back unchanged.
int mm_pico_mcu_interrupts_disable(unsigned int* saved);
int mm_pico_mcu_interrupts_enable(unsigned int saved);

// The analog facilities. Channels and outputs are the SDK's own numbering:
// ADC channel n is GPIO base + n up to the temperature channel, which has no
// pin; a PWM output is its GPIO, and the slice and comparator queries say
// which counter and compare register it lands on. The reference is the
// selected board's row in resolve-board.cmake, zero for a board without one.
// pwm_configure takes the plan the C++ side made -- top and a divider in
// sixteenths -- because the arithmetic is portable and lives above this ABI.
unsigned int mm_pico_mcu_adc_channel_count(void);
unsigned int mm_pico_mcu_adc_base_pin(void);
unsigned int mm_pico_mcu_adc_temperature_channel(void);
unsigned int mm_pico_mcu_adc_reference_mv(void);
int mm_pico_mcu_adc_configure(unsigned int channel);
int mm_pico_mcu_adc_read(unsigned int channel, unsigned int* count);
int mm_pico_mcu_adc_release(unsigned int channel);
unsigned long mm_pico_mcu_system_clock_hz(void);
unsigned int mm_pico_mcu_pwm_slice(unsigned int pin);
unsigned int mm_pico_mcu_pwm_comparator(unsigned int pin);
int mm_pico_mcu_pwm_configure(unsigned int pin, unsigned int top, unsigned int divider_x16);
int mm_pico_mcu_pwm_write(unsigned int pin, unsigned int level);
int mm_pico_mcu_pwm_release(unsigned int pin);
const char* mm_pico_mcu_board_name(void);
unsigned int mm_pico_mcu_gpio_count(void);
int mm_pico_mcu_has_led(void);
int mm_pico_mcu_has_second_i2c(void);
unsigned int mm_pico_mcu_led_gpio(void);
int mm_pico_mcu_led_active_high(void);
