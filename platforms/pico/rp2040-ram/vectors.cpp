// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026

extern "C" {

extern unsigned long __stack;
void _start();

using Handler = void (*)();

static void semihosting_puts(const char* s) {
    register int r0 asm("r0") = 0x04;
    register const char* r1 asm("r1") = s;
    asm volatile(
        "bkpt 0xab"
        : "+r"(r0)
        : "r"(r1)
        : "memory"
    );
}

[[gnu::used]] void default_fault_handler() {
    semihosting_puts("fault\n");
    while (true) {
        asm volatile("bkpt #0");
    }
}

extern Handler const vectors[];

[[gnu::naked]] void reset_handler() {
    asm volatile(
        "ldr r0, =__stack\n"
        "msr msp, r0\n"
        "ldr r0, =vectors\n"
        "ldr r1, =0xE000ED08\n"
        "str r0, [r1]\n"
        "bl _start\n"
        "1: b 1b\n"
        ".ltorg\n"
    );
}

[[gnu::section(".isr_vector"), gnu::used]]
Handler const vectors[] = {
    reinterpret_cast<Handler>(&__stack),
    reset_handler,
    default_fault_handler, // NMI
    default_fault_handler, // HardFault
    nullptr,               // Reserved
    nullptr,               // Reserved
    nullptr,               // Reserved
    nullptr,               // Reserved
    nullptr,               // Reserved
    nullptr,               // Reserved
    nullptr,               // Reserved
    default_fault_handler, // SVCall
    nullptr,               // Reserved
    nullptr,               // Reserved
    default_fault_handler, // PendSV
    default_fault_handler, // SysTick
    // 26 external interrupts
    default_fault_handler, default_fault_handler, default_fault_handler, default_fault_handler,
    default_fault_handler, default_fault_handler, default_fault_handler, default_fault_handler,
    default_fault_handler, default_fault_handler, default_fault_handler, default_fault_handler,
    default_fault_handler, default_fault_handler, default_fault_handler, default_fault_handler,
    default_fault_handler, default_fault_handler, default_fault_handler, default_fault_handler,
    default_fault_handler, default_fault_handler, default_fault_handler, default_fault_handler,
    default_fault_handler, default_fault_handler,
};

}
