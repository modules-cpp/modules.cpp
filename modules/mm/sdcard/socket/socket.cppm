// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.sdcard.socket;

import mm.fs;

export namespace mm::sdcard::socket {

// The seam. A board's provider answers its socket's card; the default
// answers a device that is Unsupported throughout.
class Provider {
public:
    virtual ~Provider() = default;
    [[nodiscard]] virtual mm::fs::BlockDevice& card();
};

void set_provider(Provider& provider);
[[nodiscard]] Provider& selected_provider();

// The board's socket.
[[nodiscard]] mm::fs::BlockDevice& card();

}
