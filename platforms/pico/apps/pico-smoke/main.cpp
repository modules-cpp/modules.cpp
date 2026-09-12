import lib.pico;

int main() {
    lib::pico::initialize();
    lib::pico::write("modules.cpp pico smoke");
    lib::pico::gpio_write(25, true);
    lib::pico::delay_ms(100);
    lib::pico::gpio_write(25, false);
    return 0;
}
