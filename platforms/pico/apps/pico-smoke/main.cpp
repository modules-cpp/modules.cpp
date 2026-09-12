import ext.pico;

int main() {
    ext::pico::initialize();
    ext::pico::write("modules.cpp pico smoke");
    ext::pico::gpio_write(25, true);
    ext::pico::delay_ms(100);
    ext::pico::gpio_write(25, false);
    return 0;
}
