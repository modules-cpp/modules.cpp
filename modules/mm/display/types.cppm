// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.display:types;

export namespace mm::display {

enum class Color { White, Black, Red };
enum class Refresh { Full, Partial };

struct Geometry {
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int bits_per_pixel = 0;
};

struct Rectangle {
    unsigned int x = 0;
    unsigned int y = 0;
    unsigned int width = 0;
    unsigned int height = 0;
};

}
