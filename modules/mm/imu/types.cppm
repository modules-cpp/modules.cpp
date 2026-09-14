// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.imu:types;

export namespace mm::imu {

// Signed counts in the sensor's own axes. What a count is worth is Scale's
// business, and which way the axes point on a particular board is the board's.
struct Axes {
    int x = 0;
    int y = 0;
    int z = 0;
};

// Enough to convert a count exactly, without this module choosing a unit.
// An acceleration count is acceleration_range_g / full_scale of a g; a rotation
// count is rotation_range_dps / full_scale of a degree per second.
struct Scale {
    unsigned int acceleration_range_g = 0;
    unsigned int rotation_range_dps = 0;
    unsigned int full_scale = 0;
};

}
