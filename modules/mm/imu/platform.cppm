// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.imu:platform;

import :status;
import :types;

export namespace mm::imu {

class Imu {
public:
    virtual ~Imu() = default;

    [[nodiscard]] virtual Scale scale() const { return {}; }
    [[nodiscard]] virtual Status initialize() { return Status::Unsupported; }

    // Both sets of axes come from one reading, so a caller cannot pair an
    // acceleration with a rotation the sensor measured at a different moment.
    [[nodiscard]] virtual Status read(Axes&, Axes&) { return Status::Unsupported; }

    // Hundredths of a degree Celsius. Written only when the call answers Ok.
    [[nodiscard]] virtual Status temperature(int&) { return Status::Unsupported; }

    [[nodiscard]] virtual Status sleep() { return Status::Unsupported; }
};

void set_imu(Imu& imu);
[[nodiscard]] Imu& selected_imu();

}
