// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
import ext.pico;

int main() {
    ext::pico::initialize();
    ext::pico::write("modules.cpp pico2 riscv smoke");
    ext::pico::delay_ms(1);
    ext::pico::gpio_write(25, true);
    return 0;
}
