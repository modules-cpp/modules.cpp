// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.mcu:i2s_types;

export namespace mm::mcu {

// I2S is a serial audio link, so unlike I2C it carries no address: the link is
// point-to-point and there is nothing to select on it. Unlike SPI it carries no
// mode or bit order and no chip select: the protocol fixes the framing, and a
// device's chip select, like its reset, belongs to the device transaction.
//
// The link has three lines where I2C has two: a serial data line, a bit clock
// that paces each bit, and a word-select (frame) line that names which half of
// a frame is going. The bit clock is what baud paces.
struct I2sConfiguration {
    unsigned int instance = 0;
    unsigned int data_gpio = 0;
    unsigned int clock_gpio = 0;
    unsigned int word_select_gpio = 0;
    unsigned long baud = 1'000'000;
};

}
