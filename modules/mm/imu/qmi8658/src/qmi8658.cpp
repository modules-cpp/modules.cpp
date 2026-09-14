// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <array>
#include <cstddef>
#include <span>

module mm.imu.qmi8658;

namespace mm::imu::qmi8658 {

namespace {

// Eight-bit register addresses, named for what this driver uses rather than
// reproducing the part's whole map.
constexpr unsigned int who_am_i = 0;
constexpr unsigned int revision = 1;
constexpr unsigned int ctrl1 = 2;
constexpr unsigned int ctrl2 = 3;
constexpr unsigned int ctrl3 = 4;
constexpr unsigned int ctrl7 = 8;
constexpr unsigned int temperature_low = 51;
constexpr unsigned int acceleration_x_low = 53;

// The part identifies itself here, and answers nothing else.
constexpr unsigned int device_identifier = 0x05;

// Address auto-increment on, so one read walks consecutive registers.
constexpr unsigned int ctrl1_auto_increment = 0x60;

// Accelerometer and gyroscope on; the magnetometer and AttitudeEngine this
// board does not wire stay off.
constexpr unsigned int ctrl7_acceleration_and_rotation = 0x03;
constexpr unsigned int ctrl7_disable_all = 0x00;

constexpr unsigned int sample_bytes = 12;
constexpr unsigned int full_scale = 32768;

// The identification retry the reference driver performs, kept because the part
// can be slow to answer after power-on.
constexpr unsigned int identify_attempts = 5;

[[nodiscard]] unsigned int acceleration_code(AccelerationRange range) {
    switch (range) {
        case AccelerationRange::G2: return 0x00 << 4;
        case AccelerationRange::G4: return 0x01 << 4;
        case AccelerationRange::G8: return 0x02 << 4;
        case AccelerationRange::G16: return 0x03 << 4;
    }
    return 0x02 << 4;
}

[[nodiscard]] unsigned int acceleration_g(AccelerationRange range) {
    switch (range) {
        case AccelerationRange::G2: return 2;
        case AccelerationRange::G4: return 4;
        case AccelerationRange::G8: return 8;
        case AccelerationRange::G16: return 16;
    }
    return 8;
}

[[nodiscard]] unsigned int rotation_code(RotationRange range) {
    switch (range) {
        case RotationRange::Dps32: return 0 << 4;
        case RotationRange::Dps64: return 1 << 4;
        case RotationRange::Dps128: return 2 << 4;
        case RotationRange::Dps256: return 3 << 4;
        case RotationRange::Dps512: return 4 << 4;
        case RotationRange::Dps1024: return 5 << 4;
        case RotationRange::Dps2048: return 6 << 4;
        case RotationRange::Dps4096: return 7 << 4;
    }
    return 4 << 4;
}

[[nodiscard]] unsigned int rotation_dps(RotationRange range) {
    switch (range) {
        case RotationRange::Dps32: return 32;
        case RotationRange::Dps64: return 64;
        case RotationRange::Dps128: return 128;
        case RotationRange::Dps256: return 256;
        case RotationRange::Dps512: return 512;
        case RotationRange::Dps1024: return 1024;
        case RotationRange::Dps2048: return 2048;
        case RotationRange::Dps4096: return 4096;
    }
    return 512;
}

[[nodiscard]] unsigned int rate_code(OutputRate rate) {
    switch (rate) {
        case OutputRate::Hz8000: return 0x00;
        case OutputRate::Hz4000: return 0x01;
        case OutputRate::Hz2000: return 0x02;
        case OutputRate::Hz1000: return 0x03;
        case OutputRate::Hz500: return 0x04;
        case OutputRate::Hz250: return 0x05;
        case OutputRate::Hz125: return 0x06;
        case OutputRate::Hz62_5: return 0x07;
        case OutputRate::Hz31_25: return 0x08;
    }
    return 0x03;
}

// Little-endian sixteen-bit two's complement, which is what the part reports
// and what a plain cast to a signed type must not be trusted to reproduce.
[[nodiscard]] int signed_16(unsigned char low, unsigned char high) {
    const auto raw = static_cast<unsigned int>(low) | (static_cast<unsigned int>(high) << 8);
    return raw >= 0x8000u ? static_cast<int>(raw) - 0x10000 : static_cast<int>(raw);
}

}  // namespace

mm::imu::Status Sensor::from_mcu(mm::mcu::Status status) const {
    switch (status) {
        case mm::mcu::Status::Ok: return mm::imu::Status::Ok;
        case mm::mcu::Status::BadArgument: return mm::imu::Status::BadArgument;
        case mm::mcu::Status::Unsupported: return mm::imu::Status::Unsupported;
        case mm::mcu::Status::Busy: return mm::imu::Status::Busy;
        case mm::mcu::Status::Timeout: return mm::imu::Status::Timeout;
    }
    return mm::imu::Status::TransportError;
}

mm::imu::Scale Sensor::scale() const {
    return {acceleration_g(configuration_.acceleration),
            rotation_dps(configuration_.rotation), full_scale};
}

mm::imu::Status Sensor::write_register(unsigned int register_address, unsigned int value) {
    const std::array data{static_cast<std::byte>(register_address & 0xff),
                          static_cast<std::byte>(value & 0xff)};
    return from_mcu(mm::mcu::i2c_write(wiring_.i2c.instance, selected_address_, data));
}

mm::imu::Status Sensor::read_register(unsigned int register_address, unsigned char* data,
                                      unsigned int size) {
    const std::array command{static_cast<std::byte>(register_address & 0xff)};
    std::byte buffer[sample_bytes]{};
    if (size == 0 || size > sample_bytes) return mm::imu::Status::BadArgument;
    const auto status = from_mcu(mm::mcu::i2c_write_read(
        wiring_.i2c.instance, selected_address_, command,
        std::span<std::byte>{buffer, size}));
    if (status != mm::imu::Status::Ok) return status;
    for (unsigned int index = 0; index < size; ++index)
        data[index] = static_cast<unsigned char>(buffer[index]);
    return mm::imu::Status::Ok;
}

// The part can be slow to answer after power-on, so each address is tried more
// than once before moving on to the other one.
mm::imu::Status Sensor::identify(unsigned int address) {
    selected_address_ = address;
    for (unsigned int attempt = 0; attempt < identify_attempts; ++attempt) {
        unsigned char identifier = 0;
        const auto status = read_register(who_am_i, &identifier, 1);
        if (status == mm::imu::Status::Ok && identifier == device_identifier)
            return mm::imu::Status::Ok;
    }
    return mm::imu::Status::Unsupported;
}

mm::imu::Status Sensor::initialize() {
    auto status = from_mcu(mm::mcu::i2c_configure(wiring_.i2c));
    if (status != mm::imu::Status::Ok) return status;

    status = identify(wiring_.address);
    if (status != mm::imu::Status::Ok && wiring_.alternate_address != wiring_.address)
        status = identify(wiring_.alternate_address);
    if (status != mm::imu::Status::Ok) {
        selected_address_ = 0;
        return status;
    }

    // Read so a provider that wants it can log it; the driver itself does not
    // branch on the revision, because no behaviour here depends on one.
    unsigned char part_revision = 0;
    status = read_register(revision, &part_revision, 1);
    if (status != mm::imu::Status::Ok) return status;

    status = write_register(ctrl1, ctrl1_auto_increment);
    if (status != mm::imu::Status::Ok) return status;
    status = write_register(ctrl2, acceleration_code(configuration_.acceleration) |
                                       rate_code(configuration_.acceleration_rate));
    if (status != mm::imu::Status::Ok) return status;
    status = write_register(ctrl3, rotation_code(configuration_.rotation) |
                                       rate_code(configuration_.rotation_rate));
    if (status != mm::imu::Status::Ok) return status;
    status = write_register(ctrl7, ctrl7_acceleration_and_rotation);
    if (status != mm::imu::Status::Ok) return status;

    state_ = State::Ready;
    return mm::imu::Status::Ok;
}

mm::imu::Status Sensor::read(mm::imu::Axes& acceleration, mm::imu::Axes& rotation) {
    if (state_ != State::Ready) return mm::imu::Status::NotInitialized;

    // One read of twelve consecutive registers, so the six axes belong to one
    // sample rather than to whatever the part was measuring between two reads.
    unsigned char sample[sample_bytes]{};
    const auto status = read_register(acceleration_x_low, sample, sample_bytes);
    if (status != mm::imu::Status::Ok) return status;

    acceleration.x = signed_16(sample[0], sample[1]);
    acceleration.y = signed_16(sample[2], sample[3]);
    acceleration.z = signed_16(sample[4], sample[5]);
    rotation.x = signed_16(sample[6], sample[7]);
    rotation.y = signed_16(sample[8], sample[9]);
    rotation.z = signed_16(sample[10], sample[11]);
    return mm::imu::Status::Ok;
}

mm::imu::Status Sensor::temperature(int& centidegrees) {
    if (state_ != State::Ready) return mm::imu::Status::NotInitialized;

    unsigned char raw[2]{};
    const auto status = read_register(temperature_low, raw, 2);
    if (status != mm::imu::Status::Ok) return status;

    // The part reports degrees Celsius in 1/256ths. Scaling by 100 before
    // dividing keeps the hundredths mm.imu promises without floating point.
    centidegrees = signed_16(raw[0], raw[1]) * 100 / 256;
    return mm::imu::Status::Ok;
}

mm::imu::Status Sensor::sleep() {
    if (state_ != State::Ready) return mm::imu::Status::NotInitialized;
    const auto status = write_register(ctrl7, ctrl7_disable_all);
    if (status != mm::imu::Status::Ok) return status;
    state_ = State::Sleeping;
    return mm::imu::Status::Ok;
}

}  // namespace mm::imu::qmi8658
