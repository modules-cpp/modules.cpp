// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.touch:types;

export namespace mm::touch {

// The panel's own coordinate space, which a caller needs to map a contact onto
// whatever it drew. A panel laminated to a display need not share its geometry.
struct Geometry {
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int points = 0;  // the most simultaneous contacts the panel reports
};

struct Point {
    unsigned int x = 0;
    unsigned int y = 0;
};

}
