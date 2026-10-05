// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The private ABI between platform.pico.fs.fat and the FatFs adapter.
// It exists because FatFs is C and only the adapter includes its headers.
// Nothing outside platforms/pico/sdk/pico-sdk/fatfs declares or calls it.
//
// C includes this header directly; C++ includes fat-cxx.h, which wraps it in
// extern "C".


#include <stddef.h>
#include <stdint.h>

// Results, numbered in mm::fs::Status's order. The provider maps them by
// name, never by casting.
enum {
    MM_PICO_FAT_OK = 0,
    MM_PICO_FAT_BAD_ARGUMENT,
    MM_PICO_FAT_UNSUPPORTED,
    MM_PICO_FAT_NOT_FOUND,
    MM_PICO_FAT_EXISTS,
    MM_PICO_FAT_NOT_DIRECTORY,
    MM_PICO_FAT_IS_DIRECTORY,
    MM_PICO_FAT_NOT_EMPTY,
    MM_PICO_FAT_NO_SPACE,
    MM_PICO_FAT_READ_ONLY,
    MM_PICO_FAT_NAME_TOO_LONG,
    MM_PICO_FAT_TOO_MANY,
    MM_PICO_FAT_BUSY,
    MM_PICO_FAT_CROSS_VOLUME,
    MM_PICO_FAT_CORRUPT,
    MM_PICO_FAT_TIMEOUT,
    MM_PICO_FAT_TRANSPORT_ERROR
};

// mm.fs's Access and Disposition, in their order; mm.fs.fat's Format.
enum { MM_PICO_FAT_READ = 0, MM_PICO_FAT_WRITE, MM_PICO_FAT_READ_WRITE, MM_PICO_FAT_APPEND };
enum {
    MM_PICO_FAT_OPEN_EXISTING = 0,
    MM_PICO_FAT_OPEN_OR_CREATE,
    MM_PICO_FAT_CREATE_NEW,
    MM_PICO_FAT_CREATE_OR_TRUNCATE
};
enum { MM_PICO_FAT_FORMAT_AUTOMATIC = 0, MM_PICO_FAT_FORMAT_FAT, MM_PICO_FAT_FORMAT_FAT32 };

// A block device: an mm.fs BlockDevice seen from C, in 512-byte blocks.
// Every callback answers one of the results above.
typedef struct {
    void* context;
    int (*geometry)(void* context, uint64_t* count, unsigned int* size);
    int (*read)(void* context, uint64_t block, void* data, size_t size);
    int (*write)(void* context, uint64_t block, const void* data, size_t size);
    int (*sync)(void* context);
} mm_pico_fat_device;

typedef struct {
    int directory;          // 0 for a file
    uint64_t size;
    uint64_t modified;      // seconds since 1970-01-01, 0 when unknown
    int read_only;
} mm_pico_fat_entry;

// Volumes. device must stay valid until detach. A device holding no FAT
// volume is CORRUPT and is never written; format makes one.
int mm_pico_fat_attach(const mm_pico_fat_device* device, int read_only, unsigned int* volume);
int mm_pico_fat_detach(unsigned int volume);
int mm_pico_fat_format(const mm_pico_fat_device* device, int format);

// Files. Paths are volume-relative, normalised, pointer and length; "" is the
// root.
int mm_pico_fat_open(unsigned int volume, const char* path, size_t length, int access,
                     int disposition, unsigned int* handle);
int mm_pico_fat_read(unsigned int handle, void* data, size_t size, size_t* count);
int mm_pico_fat_write(unsigned int handle, const void* data, size_t size, size_t* count);
int mm_pico_fat_seek(unsigned int handle, uint64_t offset);
int mm_pico_fat_tell(unsigned int handle, uint64_t* offset);
int mm_pico_fat_truncate(unsigned int handle);
int mm_pico_fat_sync(unsigned int handle);
int mm_pico_fat_file_stat(unsigned int handle, mm_pico_fat_entry* stat);
int mm_pico_fat_close(unsigned int handle);

// Directories: next skips "." and "..", and leaves an entry whose name does
// not fit capacity for the next call, answering NAME_TOO_LONG.
int mm_pico_fat_open_directory(unsigned int volume, const char* path, size_t length,
                               unsigned int* handle);
int mm_pico_fat_next(unsigned int handle, char* name, size_t capacity, size_t* length,
                     mm_pico_fat_entry* stat, int* done);
int mm_pico_fat_close_directory(unsigned int handle);

// Paths.
int mm_pico_fat_stat(unsigned int volume, const char* path, size_t length,
                     mm_pico_fat_entry* stat);
int mm_pico_fat_make_directory(unsigned int volume, const char* path, size_t length);
int mm_pico_fat_remove(unsigned int volume, const char* path, size_t length);
int mm_pico_fat_rename(unsigned int volume, const char* from, size_t from_length,
                       const char* to, size_t to_length);
int mm_pico_fat_space(unsigned int volume, uint64_t* total, uint64_t* free);
int mm_pico_fat_flush(unsigned int volume);

// The time written files are stamped with, seconds since 1970-01-01; NULL, or
// a clock answering 0, writes 1 January 2026.
void mm_pico_fat_set_clock(uint64_t (*now)(void));
