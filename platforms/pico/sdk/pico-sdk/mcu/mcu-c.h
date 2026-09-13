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

enum {
    MM_PICO_MCU_OK = 0,
    MM_PICO_MCU_BAD_ARGUMENT = 1,
    MM_PICO_MCU_UNSUPPORTED = 2,
    MM_PICO_MCU_BUSY = 3,
    MM_PICO_MCU_TIMEOUT = 4
};

enum { MM_PICO_MCU_DIRECTION_IN = 0, MM_PICO_MCU_DIRECTION_OUT = 1 };
enum { MM_PICO_MCU_PULL_NONE = 0, MM_PICO_MCU_PULL_UP = 1, MM_PICO_MCU_PULL_DOWN = 2 };

int mm_pico_mcu_gpio_configure(unsigned int pin, int direction, int pull);
int mm_pico_mcu_gpio_write(unsigned int pin, int high);
int mm_pico_mcu_gpio_read(unsigned int pin, int* high);
int mm_pico_mcu_uart_write(unsigned int instance, const char* text);
int mm_pico_mcu_delay_ms(unsigned long milliseconds);
int mm_pico_mcu_ticks_ms(unsigned long* ticks);
