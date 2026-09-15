// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// Private ABI between platform.pico.stdio and the Pico SDK adapter. C includes
// this file directly; C++ includes stdio-cxx.h to give these names C linkage.

#include <stddef.h>

enum {
    MM_PICO_STDIO_OK = 0,
    MM_PICO_STDIO_BAD_ARGUMENT = 1,
    MM_PICO_STDIO_UNSUPPORTED = 2,
    MM_PICO_STDIO_NOT_INITIALIZED = 3,
    MM_PICO_STDIO_BUSY = 4,
    MM_PICO_STDIO_TIMEOUT = 5,
    MM_PICO_STDIO_TRANSPORT_ERROR = 6
};

int mm_pico_stdio_initialize(void);
int mm_pico_stdio_write(const unsigned char* data, size_t size, size_t* written);
int mm_pico_stdio_read(unsigned char* data, size_t size, size_t* count);
int mm_pico_stdio_flush(void);
int mm_pico_stdio_connected(int* connected);
