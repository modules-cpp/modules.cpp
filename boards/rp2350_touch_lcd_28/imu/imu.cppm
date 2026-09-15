// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module platform.rp2350_touch_lcd_28.imu;

import mm.mcu;
import mm.imu;
import mm.imu.qmi8658;

namespace {

constexpr mm::imu::qmi8658::Wiring wiring{
    .i2c = {.instance = 1, .data_gpio = 6, .clock_gpio = 7, .baud = 400'000},
    .address = 0x6b,
    .alternate_address = 0x6a,
};

// The ranges Waveshare's own firmware selects for this board. A caller that
// wants different ones needs a different provider, because the range is what
// makes a count mean something and mm.imu reports counts.
constexpr mm::imu::qmi8658::Configuration configuration{
    .acceleration = mm::imu::qmi8658::AccelerationRange::G8,
    .rotation = mm::imu::qmi8658::RotationRange::Dps512,
    .acceleration_rate = mm::imu::qmi8658::OutputRate::Hz1000,
    .rotation_rate = mm::imu::qmi8658::OutputRate::Hz1000,
};

mm::imu::qmi8658::Sensor sensor{wiring, configuration};

struct Register {
    Register() { mm::imu::set_imu(sensor); }
};

const Register registered;

}  // namespace
