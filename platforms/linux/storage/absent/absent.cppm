// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module platform.linux.storage.absent;

import mm.fs;
import mm.fs.fat;
import mm.fs.littlefs;
import mm.sdcard.socket;

namespace platform::linux::storage_absent_provider {

mm::fs::BlockDevice unavailable;

class Socket final : public mm::sdcard::socket::Provider {
public:
    [[nodiscard]] mm::fs::BlockDevice& card() override { return unavailable; }
};

Socket socket;
mm::fs::fat::Provider fat;
mm::fs::littlefs::Provider littlefs;

struct Register {
    Register() {
        mm::sdcard::socket::set_provider(socket);
        mm::fs::fat::set_provider(fat);
        mm::fs::littlefs::set_provider(littlefs);
    }
};

const Register registered;

}
