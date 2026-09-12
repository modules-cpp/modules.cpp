// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The platform side of mm.mcu, in plain C so that a board's sources or a vendor
// adapter can define it. One declaration site on purpose: these names have C
// language linkage and are therefore unmangled, so a signature the interface and
// the platform disagree about would link silently and misbehave at run time.
//
// A function answers one of the codes below and no others. An unrecognised answer
// is reported to callers as MM_MCU_BAD_ARGUMENT.
//
// C includes this header directly. C++ includes mcu-cxx.h instead, which wraps it
// in extern "C": including this one from C++ gives the declarations C++ linkage
// and they then conflict with the platform's own definitions.
//
// This header carries no include guard, matching the other project-authored
// headers in the tree: the specification permits #include and no other
// preprocessor directive, so each translation unit includes it once.

enum {
    MM_MCU_OK = 0,
    MM_MCU_BAD_ARGUMENT = 1,
    MM_MCU_UNSUPPORTED = 2,
    MM_MCU_BUSY = 3,
    MM_MCU_TIMEOUT = 4
};

enum { MM_MCU_DIRECTION_IN = 0, MM_MCU_DIRECTION_OUT = 1 };
enum { MM_MCU_PULL_NONE = 0, MM_MCU_PULL_UP = 1, MM_MCU_PULL_DOWN = 2 };

int mm_mcu_gpio_configure(unsigned int pin, int direction, int pull);
int mm_mcu_gpio_write(unsigned int pin, int high);
int mm_mcu_gpio_read(unsigned int pin, int* high);

int mm_mcu_uart_write(unsigned int instance, const char* text);

int mm_mcu_delay_ms(unsigned long milliseconds);
int mm_mcu_ticks_ms(unsigned long* ticks);
