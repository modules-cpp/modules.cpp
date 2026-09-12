// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
import lib.pico;

int main() {
    lib::pico::initialize();
    lib::pico::write("modules.cpp pico2 riscv smoke");
    lib::pico::delay_ms(1);
    lib::pico::gpio_write(25, true);
    return 0;
}
