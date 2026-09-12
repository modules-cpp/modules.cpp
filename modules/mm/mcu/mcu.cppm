// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.mcu;

// One import for a consumer, one partition per facility. A consumer that needs
// only GPIO still imports mm.mcu: partitions are this module's internal
// structure, not a selection a caller makes.
export import :status;
export import :gpio;
export import :uart;
export import :timer;
