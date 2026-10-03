// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.mcu:uart_types;

export namespace mm::mcu {

// A UART is two pins and a rate. The frame -- data bits, parity, stop bits --
// is eight, none, one, the shape every device this interface has met uses; a
// platform whose configuration names another frame keeps it.
//
// A platform that routes UARTs by name rather than by pin, as Linux does
// through its device map, ignores the GPIOs.
struct UartConfiguration {
    unsigned int instance = 0;
    unsigned int transmit_gpio = 0;
    unsigned int receive_gpio = 0;
    unsigned long baud = 115'200;
};

}
