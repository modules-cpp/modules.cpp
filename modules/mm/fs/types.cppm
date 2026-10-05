// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>

export module mm.fs:types;

export namespace mm::fs {

enum class Kind { File, Directory };

enum class Access {
    Read,
    Write,
    ReadWrite,
    Append,          // write only; every write goes to the end
};

enum class Disposition {
    OpenExisting,    // NotFound when absent
    OpenOrCreate,
    CreateNew,       // Exists when present
    CreateOrTruncate,
};

struct Stat {
    Kind kind = Kind::File;
    std::uint64_t size = 0;      // bytes; zero for a directory
    std::uint64_t modified = 0;  // seconds since 1970-01-01, 0 when unknown
    bool read_only = false;
};

struct Space {
    std::uint64_t total = 0;     // bytes
    std::uint64_t free = 0;      // bytes
};

// A driver's own number for an open file or directory, from pools it sizes.
using Handle = unsigned int;

inline constexpr std::size_t max_name = 255;   // bytes of UTF-8 in one component
inline constexpr std::size_t max_path = 255;   // bytes of a normalised path
inline constexpr std::size_t max_mounts = 8;

}
