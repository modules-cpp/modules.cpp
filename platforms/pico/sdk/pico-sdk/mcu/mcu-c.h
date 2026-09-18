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

enum {
    MM_PICO_MCU_OK = 0,
    MM_PICO_MCU_BAD_ARGUMENT = 1,
    MM_PICO_MCU_UNSUPPORTED = 2,
    MM_PICO_MCU_BUSY = 3,
    MM_PICO_MCU_TIMEOUT = 4
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
int mm_pico_mcu_delay_ms(unsigned long milliseconds);
int mm_pico_mcu_ticks_ms(unsigned long* ticks);
const char* mm_pico_mcu_board_name(void);
unsigned int mm_pico_mcu_gpio_count(void);
int mm_pico_mcu_has_led(void);
unsigned int mm_pico_mcu_led_gpio(void);
int mm_pico_mcu_led_active_high(void);
