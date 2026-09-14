// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>

import mm.mcu;
import mm.imu;
import mm.imu.qmi8658;
import mm.test;

void mm_test_imu_reset();
void mm_test_imu_force(mm::mcu::Status status);
void mm_test_imu_address(unsigned int address);
void mm_test_imu_identifier(unsigned int value);
void mm_test_imu_set_register(unsigned int reg, unsigned int value);
std::size_t mm_test_imu_write_count();
unsigned int mm_test_imu_write_register(std::size_t index);
unsigned int mm_test_imu_write_value(std::size_t index);
std::size_t mm_test_imu_read_count();
unsigned int mm_test_imu_read(std::size_t index);
std::size_t mm_test_imu_transactions();

namespace {

using mm::test::expect;
using mm::imu::Status;

mm::imu::qmi8658::Wiring wiring() {
    return {.i2c = {.instance = 1, .data_gpio = 6, .clock_gpio = 7, .baud = 400'000},
            .address = 0x6b,
            .alternate_address = 0x6a};
}

mm::imu::qmi8658::Configuration configuration() { return {}; }

void initializes_and_writes_the_control_sequence() {
    mm_test_imu_reset();
    mm::imu::qmi8658::Sensor sensor{wiring(), configuration()};
    expect(sensor.initialize() == Status::Ok, "a recognised sensor initializes");

    expect(mm_test_imu_write_count() == 4, "four control registers are written");
    expect(mm_test_imu_write_register(0) == 2 && mm_test_imu_write_value(0) == 0x60,
           "address auto-increment is enabled first");
    // 8g is 0x02 << 4 and 1000Hz is 0x03.
    expect(mm_test_imu_write_register(1) == 3 && mm_test_imu_write_value(1) == 0x23,
           "the accelerometer range and rate are one register write");
    // 512dps is 4 << 4 and 1000Hz is 0x03.
    expect(mm_test_imu_write_register(2) == 4 && mm_test_imu_write_value(2) == 0x43,
           "the gyroscope range and rate are one register write");
    expect(mm_test_imu_write_register(3) == 8 && mm_test_imu_write_value(3) == 0x03,
           "only the accelerometer and gyroscope are enabled");

    const auto scale = sensor.scale();
    expect(scale.acceleration_range_g == 8 && scale.rotation_range_dps == 512 &&
               scale.full_scale == 32768,
           "scale reports what a count is worth without choosing a unit");
}

void finds_the_part_at_its_alternate_address() {
    mm_test_imu_reset();
    mm_test_imu_address(0x6a);
    mm::imu::qmi8658::Sensor sensor{wiring(), configuration()};
    expect(sensor.initialize() == Status::Ok,
           "a part strapped to the other address is still found");

    mm_test_imu_reset();
    mm_test_imu_identifier(0x42);
    mm::imu::qmi8658::Sensor foreign{wiring(), configuration()};
    expect(foreign.initialize() == Status::Unsupported,
           "a device that is not this part is Unsupported at either address");
}

void reads_six_axes_from_one_sample() {
    mm_test_imu_reset();
    mm::imu::qmi8658::Sensor sensor{wiring(), configuration()};
    expect(sensor.initialize() == Status::Ok, "the sensor initializes");

    // Little-endian sixteen-bit two's complement, from register 53.
    mm_test_imu_set_register(53, 0x10);
    mm_test_imu_set_register(54, 0x27);  // 10000
    mm_test_imu_set_register(55, 0xf0);
    mm_test_imu_set_register(56, 0xd8);  // -10000
    mm_test_imu_set_register(57, 0x00);
    mm_test_imu_set_register(58, 0x00);
    mm_test_imu_set_register(59, 0x01);
    mm_test_imu_set_register(60, 0x00);  // 1
    mm_test_imu_set_register(61, 0xff);
    mm_test_imu_set_register(62, 0xff);  // -1
    mm_test_imu_set_register(63, 0x00);
    mm_test_imu_set_register(64, 0x80);  // beyond the fixture's register file

    const auto before = mm_test_imu_transactions();
    mm::imu::Axes acceleration;
    mm::imu::Axes rotation;
    expect(sensor.read(acceleration, rotation) == Status::Ok, "a sample reads");
    expect(mm_test_imu_transactions() == before + 1,
           "all six axes come from one transaction rather than six");

    expect(acceleration.x == 10000 && acceleration.y == -10000 && acceleration.z == 0,
           "acceleration decodes, including a negative axis");
    expect(rotation.x == 1 && rotation.y == -1,
           "rotation decodes, including a negative axis");
}

void temperature_is_hundredths_of_a_degree() {
    mm_test_imu_reset();
    mm::imu::qmi8658::Sensor sensor{wiring(), configuration()};
    expect(sensor.initialize() == Status::Ok, "the sensor initializes");

    // The part reports 1/256ths of a degree: 0x1900 is 25.0 C.
    mm_test_imu_set_register(51, 0x00);
    mm_test_imu_set_register(52, 0x19);
    int centidegrees = 0;
    expect(sensor.temperature(centidegrees) == Status::Ok && centidegrees == 2500,
           "a positive temperature scales to hundredths");

    // 0xF700 is -9.0 C.
    mm_test_imu_set_register(51, 0x00);
    mm_test_imu_set_register(52, 0xf7);
    expect(sensor.temperature(centidegrees) == Status::Ok && centidegrees == -900,
           "a negative temperature keeps its sign");
}

void the_lifecycle_is_explicit() {
    mm_test_imu_reset();
    mm::imu::qmi8658::Sensor sensor{wiring(), configuration()};

    mm::imu::Axes acceleration;
    mm::imu::Axes rotation;
    int centidegrees = 0;
    expect(sensor.read(acceleration, rotation) == Status::NotInitialized,
           "reading before initialization reports so");
    expect(sensor.temperature(centidegrees) == Status::NotInitialized,
           "so does reading temperature");
    expect(sensor.sleep() == Status::NotInitialized, "so does sleeping");

    expect(sensor.initialize() == Status::Ok, "the sensor initializes");
    expect(sensor.sleep() == Status::Ok, "the sensor sleeps");
    expect(mm_test_imu_write_register(mm_test_imu_write_count() - 1) == 8 &&
               mm_test_imu_write_value(mm_test_imu_write_count() - 1) == 0x00,
           "sleep disables every sensor through the enable register");
    expect(sensor.read(acceleration, rotation) == Status::NotInitialized,
           "a sleeping sensor requires reinitialization before it reads");
}

void transport_failure_reaches_the_caller() {
    mm_test_imu_reset();
    mm::imu::qmi8658::Sensor sensor{wiring(), configuration()};
    expect(sensor.initialize() == Status::Ok, "the sensor initializes");
    mm_test_imu_force(mm::mcu::Status::Busy);

    mm::imu::Axes acceleration;
    mm::imu::Axes rotation;
    expect(sensor.read(acceleration, rotation) == Status::Busy,
           "a busy bus is reported rather than returning a stale sample");
    mm_test_imu_force(mm::mcu::Status::Ok);
}

void an_unserved_platform_answers_unsupported() {
    mm::imu::Imu bare;
    mm::imu::Axes acceleration;
    mm::imu::Axes rotation;
    int centidegrees = 0;
    expect(bare.initialize() == Status::Unsupported,
           "an unserved platform answers rather than failing to link");
    expect(bare.read(acceleration, rotation) == Status::Unsupported, "so does a read");
    expect(bare.temperature(centidegrees) == Status::Unsupported, "so does a temperature");
    expect(bare.scale().full_scale == 0, "and it claims no scale");
}

const mm::test::case_ cases[] = {
    {"initializes and writes control", &initializes_and_writes_the_control_sequence},
    {"finds the alternate address", &finds_the_part_at_its_alternate_address},
    {"reads six axes from one sample", &reads_six_axes_from_one_sample},
    {"temperature is hundredths", &temperature_is_hundredths_of_a_degree},
    {"the lifecycle is explicit", &the_lifecycle_is_explicit},
    {"transport failure reaches caller", &transport_failure_reaches_the_caller},
    {"unserved answers Unsupported", &an_unserved_platform_answers_unsupported},
};

const mm::test::registrar reg{"mm.imu qmi8658", cases};

}  // namespace
