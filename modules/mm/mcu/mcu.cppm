// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.mcu;

// One import for a consumer, one partition per facility. A consumer that needs
// only GPIO still imports mm.mcu: partitions are this module's internal
// structure, not a selection a caller makes. :platform is exported too, because
// a platform module implements Platform and must see it.
export import :status;
export import :board;
export import :spi_types;
export import :i2c_types;
export import :adc_types;
export import :pwm_types;
export import :platform;
export import :gpio;
export import :spi;
export import :i2c;
export import :uart;
export import :timer;
export import :adc;
export import :pwm;
