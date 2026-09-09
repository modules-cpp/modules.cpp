// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026

extern "C" {
extern unsigned long __stack;
void _start();

using Handler = void (*)();

[[gnu::section(".isr_vector"), gnu::used]]
Handler const vectors[] = {
    reinterpret_cast<Handler>(&__stack),
    _start,
};
}
