// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cerrno>
#include <cstddef>

// Newlib may reference this syscall even when a firmware never asks for
// entropy. MPS2 semihosting has no entropy source: report that fact rather
// than silently fabricating random bytes.
extern "C" int _getentropy(void*, std::size_t) {
    errno = ENOSYS;
    return -1;
}
