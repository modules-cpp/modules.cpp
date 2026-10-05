// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The private ABI between platform.pico.fs.littlefs and the littlefs adapter.
// It exists because littlefs is C and only the adapter includes its headers.
// Nothing outside platforms/pico/sdk/pico-sdk/littlefs declares or calls it.
//
// C includes this header directly; C++ includes lfs-cxx.h, which wraps it in
// extern "C".

#include <stddef.h>
#include <stdint.h>

// Results, numbered in mm::fs::Status's order. The provider maps them by
// name, never by casting.
enum {
    MM_PICO_LFS_OK = 0,
    MM_PICO_LFS_BAD_ARGUMENT,
    MM_PICO_LFS_UNSUPPORTED,
    MM_PICO_LFS_NOT_FOUND,
    MM_PICO_LFS_EXISTS,
    MM_PICO_LFS_NOT_DIRECTORY,
    MM_PICO_LFS_IS_DIRECTORY,
    MM_PICO_LFS_NOT_EMPTY,
    MM_PICO_LFS_NO_SPACE,
    MM_PICO_LFS_READ_ONLY,
    MM_PICO_LFS_NAME_TOO_LONG,
    MM_PICO_LFS_TOO_MANY,
    MM_PICO_LFS_BUSY,
    MM_PICO_LFS_CROSS_VOLUME,
    MM_PICO_LFS_CORRUPT,
    MM_PICO_LFS_TIMEOUT,
    MM_PICO_LFS_TRANSPORT_ERROR
};

// mm.fs's Access and Disposition, in their order.
enum { MM_PICO_LFS_READ = 0, MM_PICO_LFS_WRITE, MM_PICO_LFS_READ_WRITE, MM_PICO_LFS_APPEND };
enum {
    MM_PICO_LFS_OPEN_EXISTING = 0,
    MM_PICO_LFS_OPEN_OR_CREATE,
    MM_PICO_LFS_CREATE_NEW,
    MM_PICO_LFS_CREATE_OR_TRUNCATE
};

// A flash device: an mm.fs FlashDevice seen from C. Every callback answers
// one of the results above.
typedef struct {
    void* context;
    int (*geometry)(void* context, uint32_t* read_size, uint32_t* program_size,
                    uint32_t* erase_size, uint32_t* erase_count);
    int (*read)(void* context, uint64_t offset, void* data, size_t size);
    int (*program)(void* context, uint64_t offset, const void* data, size_t size);
    int (*erase)(void* context, uint64_t offset, uint64_t size);
    int (*sync)(void* context);
} mm_pico_lfs_device;

typedef struct {
    int directory;          // 0 for a file
    uint64_t size;
    uint64_t modified;      // seconds since 1970-01-01, 0 when unknown
    int read_only;
} mm_pico_lfs_entry;

// The pool sizes this build has: volumes attached at once, files and
// directories open at once. Any pointer may be NULL.
void mm_pico_lfs_limits(unsigned int* volumes, unsigned int* files, unsigned int* directories);

// Volumes. device must stay valid until detach. With format_if_blank, a
// device whose first two erase blocks are all 0xFF is formatted and mounted;
// any other device that does not mount is CORRUPT, never erased.
int mm_pico_lfs_attach(const mm_pico_lfs_device* device, int read_only, int format_if_blank,
                       int32_t block_cycles, unsigned int* volume);
int mm_pico_lfs_detach(unsigned int volume);
int mm_pico_lfs_format(const mm_pico_lfs_device* device);

// Files. Paths are volume-relative, normalised, pointer and length; "" is the
// root.
int mm_pico_lfs_open(unsigned int volume, const char* path, size_t length, int access,
                     int disposition, unsigned int* handle);
int mm_pico_lfs_read(unsigned int handle, void* data, size_t size, size_t* count);
int mm_pico_lfs_write(unsigned int handle, const void* data, size_t size, size_t* count);
int mm_pico_lfs_seek(unsigned int handle, uint64_t offset);
int mm_pico_lfs_tell(unsigned int handle, uint64_t* offset);
int mm_pico_lfs_truncate(unsigned int handle);
int mm_pico_lfs_sync(unsigned int handle);
int mm_pico_lfs_file_stat(unsigned int handle, mm_pico_lfs_entry* stat);
int mm_pico_lfs_close(unsigned int handle);

// Directories: next skips "." and "..", and leaves an entry whose name does
// not fit capacity for the next call, answering NAME_TOO_LONG.
int mm_pico_lfs_open_directory(unsigned int volume, const char* path, size_t length,
                               unsigned int* handle);
int mm_pico_lfs_next(unsigned int handle, char* name, size_t capacity, size_t* length,
                     mm_pico_lfs_entry* stat, int* done);
int mm_pico_lfs_close_directory(unsigned int handle);

// Paths.
int mm_pico_lfs_stat(unsigned int volume, const char* path, size_t length,
                     mm_pico_lfs_entry* stat);
int mm_pico_lfs_make_directory(unsigned int volume, const char* path, size_t length);
int mm_pico_lfs_remove(unsigned int volume, const char* path, size_t length);
int mm_pico_lfs_rename(unsigned int volume, const char* from, size_t from_length,
                       const char* to, size_t to_length);
int mm_pico_lfs_space(unsigned int volume, uint64_t* total, uint64_t* free);
int mm_pico_lfs_flush(unsigned int volume);

// The timestamp a written file records, seconds since 1970-01-01; NULL for
// none, which records 0.
void mm_pico_lfs_set_clock(uint64_t (*now)(void));
