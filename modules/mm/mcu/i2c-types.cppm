// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.mcu:i2c_types;

export namespace mm::mcu {

// Seven-bit addressing only. Ten-bit addressing is a real feature of the bus,
// but no device this interface has met uses it, and inventing an encoding for
// it before one appears would be guessing at the shape of the answer.
//
// Unlike SPI there is no mode or bit order: I2C fixes both. Unlike SPI there is
// no chip select, because the address is the selection, which is why every
// operation below takes one and the configuration does not.
struct I2cConfiguration {
    unsigned int instance = 0;
    unsigned int data_gpio = 0;
    unsigned int clock_gpio = 0;
    unsigned long baud = 400'000;
};

}
