// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// Block storage against the stand-in's eight-block drive.
#include <array>
#include <cstddef>
#include <span>

import mm.mcu;
import mm.test;

void mm_test_reset();
void mm_test_force(mm::mcu::Status status);
void mm_test_storage_present(bool present);
unsigned int mm_test_storage_polls();

namespace {

using mm::mcu::Status;
using mm::mcu::StorageGeometry;
using mm::test::expect;

// A platform that serves no storage, for the defaults.
class Bare final : public mm::mcu::Platform {};

void defaults_answer_unsupported() {
    Bare bare;
    mm::mcu::set_platform(bare);
    bool present = true;
    StorageGeometry geometry{5, 7};
    std::array<std::byte, 512> block{};
    expect(!bare.capabilities().storage, "a platform declares no storage by default");
    expect(mm::mcu::storage_poll(present) == Status::Unsupported && present,
           "poll is Unsupported and leaves present");
    expect(mm::mcu::storage_geometry(geometry) == Status::Unsupported &&
               geometry.block_count == 5 && geometry.block_size == 7,
           "geometry is Unsupported and leaves the geometry");
    expect(mm::mcu::storage_read(0, block) == Status::Unsupported &&
               mm::mcu::storage_write(0, block) == Status::Unsupported,
           "read and write are Unsupported");
    mm_test_reset();
}

void a_drive_arrives_and_is_read_and_written() {
    mm_test_reset();
    bool present = true;
    expect(mm::mcu::storage_poll(present) == Status::Ok && !present,
           "no drive: poll answers Ok, not present");
    StorageGeometry geometry{};
    expect(mm::mcu::storage_geometry(geometry) == Status::TransportError &&
               geometry.block_count == 0,
           "geometry without a drive is TransportError");

    mm_test_storage_present(true);
    expect(mm::mcu::storage_poll(present) == Status::Ok && present &&
               mm_test_storage_polls() == 2,
           "the drive is noticed on the next poll");
    expect(mm::mcu::storage_geometry(geometry) == Status::Ok && geometry.block_count == 8 &&
               geometry.block_size == 512,
           "its geometry is reported");

    std::array<std::byte, 1024> out{};
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<std::byte>(i & 0xff);
    expect(mm::mcu::storage_write(3, out) == Status::Ok, "two blocks are written");
    std::array<std::byte, 1024> in{};
    expect(mm::mcu::storage_read(3, in) == Status::Ok && in == out,
           "and read back");
    std::array<std::byte, 512> one{};
    expect(mm::mcu::storage_read(4, one) == Status::Ok && one[0] == out[512],
           "a single block is addressed by its number");

    expect(mm::mcu::storage_read(0, std::span<std::byte>{}) == Status::BadArgument,
           "an empty span is refused before the platform");
    std::array<std::byte, 100> partial{};
    expect(mm::mcu::storage_read(0, partial) == Status::BadArgument,
           "a span that is not a whole number of blocks is refused");
    expect(mm::mcu::storage_write(7, out) == Status::BadArgument,
           "blocks past the drive's end are refused");

    mm_test_force(Status::TransportError);
    expect(mm::mcu::storage_poll(present) == Status::TransportError && present,
           "a failed poll leaves present");
    mm_test_reset();
}

const mm::test::case_ cases[] = {
    {"storage defaults answer unsupported", defaults_answer_unsupported},
    {"a drive arrives and is read and written", a_drive_arrives_and_is_read_and_written},
};
const mm::test::registrar reg{"mm.mcu storage", cases};

}  // namespace
