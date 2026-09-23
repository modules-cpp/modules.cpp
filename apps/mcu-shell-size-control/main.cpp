// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <span>

import mm.mcu;
import mm.stdio;

int main() {
    auto& driver = mm::stdio::selected_console();
    if (driver.initialize() != mm::stdio::Status::Ok) return 1;
    std::byte input[64]{};
    for (;;) {
        std::size_t count = 0;
        const auto status = driver.read(input, count);
        if (status == mm::stdio::Status::Ok && count != 0) {
            std::size_t written = 0;
            if (driver.write(std::span<const std::byte>{input, count},
                             written) != mm::stdio::Status::Ok) {
                return 2;
            }
            if (written != count) (void)driver.flush();
        } else if (status != mm::stdio::Status::Ok &&
                   status != mm::stdio::Status::Busy) {
            return 3;
        }
        (void)mm::mcu::delay_ms(1);
    }
}
