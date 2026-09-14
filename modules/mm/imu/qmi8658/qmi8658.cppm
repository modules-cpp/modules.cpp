// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.imu.qmi8658;

import mm.mcu;
import mm.imu;

export namespace mm::imu::qmi8658 {

// The part answers at one of two addresses depending on how its SA0 pin is
// strapped. A provider that knows which may say so; one that does not leaves
// the alternate in place and the driver finds it.
struct Wiring {
    mm::mcu::I2cConfiguration i2c;
    unsigned int address = 0x6b;
    unsigned int alternate_address = 0x6a;
};

// Full-scale ranges, in the units the datasheet names them. Only the values the
// part accepts are offered; an unlisted range is not expressible rather than
// silently rounded.
enum class AccelerationRange { G2, G4, G8, G16 };
enum class RotationRange { Dps32, Dps64, Dps128, Dps256, Dps512, Dps1024, Dps2048, Dps4096 };
enum class OutputRate { Hz8000, Hz4000, Hz2000, Hz1000, Hz500, Hz250, Hz125, Hz62_5, Hz31_25 };

struct Configuration {
    AccelerationRange acceleration = AccelerationRange::G8;
    RotationRange rotation = RotationRange::Dps512;
    OutputRate acceleration_rate = OutputRate::Hz1000;
    OutputRate rotation_rate = OutputRate::Hz1000;
};

class Sensor : public mm::imu::Imu {
public:
    Sensor(const Wiring& wiring, const Configuration& configuration)
        : wiring_(wiring), configuration_(configuration) {}

    [[nodiscard]] mm::imu::Scale scale() const override;
    [[nodiscard]] mm::imu::Status initialize() override;
    [[nodiscard]] mm::imu::Status read(mm::imu::Axes& acceleration,
                                       mm::imu::Axes& rotation) override;
    [[nodiscard]] mm::imu::Status temperature(int& centidegrees) override;
    [[nodiscard]] mm::imu::Status sleep() override;

private:
    enum class State { Idle, Ready, Sleeping };

    [[nodiscard]] mm::imu::Status write_register(unsigned int register_address,
                                                 unsigned int value);
    [[nodiscard]] mm::imu::Status read_register(unsigned int register_address,
                                                unsigned char* data, unsigned int size);
    [[nodiscard]] mm::imu::Status identify(unsigned int address);
    [[nodiscard]] mm::imu::Status from_mcu(mm::mcu::Status status) const;

    Wiring wiring_;
    Configuration configuration_;
    unsigned int selected_address_ = 0;
    State state_ = State::Idle;
};

}
