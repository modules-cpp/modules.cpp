// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// littlefs behind the private ABI platform.linux.fs.littlefs calls. This file
// owns littlefs's state -- volumes, open files, open directories, their
// caches -- in static pools, and keeps the rules mm.fs's Volume documents that
// littlefs itself does not: a file open for writing cannot be opened again,
// removed, or renamed; rename never replaces a target; directory listings
// leave out "." and "..". littlefs is built with LFS_NO_MALLOC, so every
// buffer it uses is one of these.

#include "lfs-c.h"
#include "lfs.h"

#include <stdint.h>
#include <string.h>

#define MM_LINUX_LFS_VOLUMES 2u
#define MM_LINUX_LFS_FILES 8u
#define MM_LINUX_LFS_DIRECTORIES 4u
#define MM_LINUX_LFS_CACHE 256u
#define MM_LINUX_LFS_LOOKAHEAD 32u
// "/", a normalised volume-relative path of at most 255 bytes, a NUL.
#define MM_LINUX_LFS_PATH 258u
// A directory path, "/", and an entry name.
#define MM_LINUX_LFS_CHILD (MM_LINUX_LFS_PATH + 1u + LFS_NAME_MAX)
// The custom attribute a file's modification time lives in, eight bytes of
// seconds since 1970-01-01, little-endian on every RP2 core.
#define MM_LINUX_LFS_TIME 0x74u

typedef struct {
    int used;
    int read_only;
    const mm_linux_lfs_device* device;
    int last_device_status;
    lfs_t lfs;
    struct lfs_config config;
    uint8_t read_buffer[MM_LINUX_LFS_CACHE];
    uint8_t program_buffer[MM_LINUX_LFS_CACHE];
    uint8_t lookahead_buffer[MM_LINUX_LFS_LOOKAHEAD];
} mm_linux_lfs_volume_t;

typedef struct {
    int used;
    unsigned int volume;
    int access;
    lfs_file_t file;
    struct lfs_file_config config;
    struct lfs_attr attribute;
    uint64_t modified;
    uint8_t buffer[MM_LINUX_LFS_CACHE];
    char path[MM_LINUX_LFS_PATH];
} mm_linux_lfs_file_t;

typedef struct {
    int used;
    unsigned int volume;
    lfs_dir_t directory;
    struct lfs_info pending;
    int has_pending;
    char path[MM_LINUX_LFS_PATH];
} mm_linux_lfs_directory_t;

static mm_linux_lfs_volume_t mm_linux_lfs_volumes[MM_LINUX_LFS_VOLUMES];
static mm_linux_lfs_volume_t mm_linux_lfs_formatting;
static mm_linux_lfs_file_t mm_linux_lfs_files[MM_LINUX_LFS_FILES];
static mm_linux_lfs_directory_t mm_linux_lfs_directories[MM_LINUX_LFS_DIRECTORIES];
static uint64_t (*mm_linux_lfs_clock)(void);

static uint64_t mm_linux_lfs_now(void) {
    return mm_linux_lfs_clock != NULL ? mm_linux_lfs_clock() : 0u;
}

void mm_linux_lfs_set_clock(uint64_t (*now)(void)) { mm_linux_lfs_clock = now; }

// littlefs's errors, except that an I/O error the device itself explained is
// reported as the device's own answer -- a timeout stays a timeout.
static int mm_linux_lfs_status(const mm_linux_lfs_volume_t* volume, int error) {
    switch (error) {
        case LFS_ERR_OK: return MM_LINUX_LFS_OK;
        case LFS_ERR_NOENT: return MM_LINUX_LFS_NOT_FOUND;
        case LFS_ERR_EXIST: return MM_LINUX_LFS_EXISTS;
        case LFS_ERR_NOTDIR: return MM_LINUX_LFS_NOT_DIRECTORY;
        case LFS_ERR_ISDIR: return MM_LINUX_LFS_IS_DIRECTORY;
        case LFS_ERR_NOTEMPTY: return MM_LINUX_LFS_NOT_EMPTY;
        case LFS_ERR_NOSPC: return MM_LINUX_LFS_NO_SPACE;
        case LFS_ERR_FBIG: return MM_LINUX_LFS_NO_SPACE;
        case LFS_ERR_NAMETOOLONG: return MM_LINUX_LFS_NAME_TOO_LONG;
        case LFS_ERR_NOMEM: return MM_LINUX_LFS_TOO_MANY;
        case LFS_ERR_CORRUPT: return MM_LINUX_LFS_CORRUPT;
        case LFS_ERR_BADF: return MM_LINUX_LFS_BAD_ARGUMENT;
        case LFS_ERR_INVAL: return MM_LINUX_LFS_BAD_ARGUMENT;
        case LFS_ERR_IO:
            if (volume != NULL && volume->last_device_status != MM_LINUX_LFS_OK)
                return volume->last_device_status;
            return MM_LINUX_LFS_TRANSPORT_ERROR;
        default: return MM_LINUX_LFS_TRANSPORT_ERROR;
    }
}

static int mm_linux_lfs_block_read(const struct lfs_config* config, lfs_block_t block,
                                  lfs_off_t offset, void* buffer, lfs_size_t size) {
    mm_linux_lfs_volume_t* volume = (mm_linux_lfs_volume_t*)config->context;
    const int status = volume->device->read(
        volume->device->context, (uint64_t)block * config->block_size + offset, buffer, size);
    volume->last_device_status = status;
    return status == MM_LINUX_LFS_OK ? LFS_ERR_OK : LFS_ERR_IO;
}

static int mm_linux_lfs_block_program(const struct lfs_config* config, lfs_block_t block,
                                     lfs_off_t offset, const void* buffer, lfs_size_t size) {
    mm_linux_lfs_volume_t* volume = (mm_linux_lfs_volume_t*)config->context;
    const int status = volume->device->program(
        volume->device->context, (uint64_t)block * config->block_size + offset, buffer, size);
    volume->last_device_status = status;
    return status == MM_LINUX_LFS_OK ? LFS_ERR_OK : LFS_ERR_IO;
}

static int mm_linux_lfs_block_erase(const struct lfs_config* config, lfs_block_t block) {
    mm_linux_lfs_volume_t* volume = (mm_linux_lfs_volume_t*)config->context;
    const int status = volume->device->erase(
        volume->device->context, (uint64_t)block * config->block_size, config->block_size);
    volume->last_device_status = status;
    return status == MM_LINUX_LFS_OK ? LFS_ERR_OK : LFS_ERR_IO;
}

static int mm_linux_lfs_block_sync(const struct lfs_config* config) {
    mm_linux_lfs_volume_t* volume = (mm_linux_lfs_volume_t*)config->context;
    const int status = volume->device->sync(volume->device->context);
    volume->last_device_status = status;
    return status == MM_LINUX_LFS_OK ? LFS_ERR_OK : LFS_ERR_IO;
}

// littlefs's configuration for a device: an erase block is a littlefs block,
// and the caches are one program unit, which must fit the static buffers.
static int mm_linux_lfs_configure(mm_linux_lfs_volume_t* volume, const mm_linux_lfs_device* device,
                                 int32_t block_cycles) {
    uint32_t read_size = 0, program_size = 0, erase_size = 0, erase_count = 0;
    const int status =
        device->geometry(device->context, &read_size, &program_size, &erase_size, &erase_count);
    if (status != MM_LINUX_LFS_OK) return status;
    if (read_size == 0 || program_size == 0 || program_size > MM_LINUX_LFS_CACHE ||
        program_size % read_size != 0 || erase_size % program_size != 0 || erase_count < 2)
        return MM_LINUX_LFS_UNSUPPORTED;
    memset(&volume->lfs, 0, sizeof volume->lfs);
    memset(&volume->config, 0, sizeof volume->config);
    volume->device = device;
    volume->last_device_status = MM_LINUX_LFS_OK;
    volume->config.context = volume;
    volume->config.read = mm_linux_lfs_block_read;
    volume->config.prog = mm_linux_lfs_block_program;
    volume->config.erase = mm_linux_lfs_block_erase;
    volume->config.sync = mm_linux_lfs_block_sync;
    volume->config.read_size = read_size;
    volume->config.prog_size = program_size;
    volume->config.block_size = erase_size;
    volume->config.block_count = erase_count;
    volume->config.block_cycles = block_cycles;
    volume->config.cache_size = program_size;
    volume->config.lookahead_size = MM_LINUX_LFS_LOOKAHEAD;
    volume->config.read_buffer = volume->read_buffer;
    volume->config.prog_buffer = volume->program_buffer;
    volume->config.lookahead_buffer = volume->lookahead_buffer;
    volume->config.name_max = LFS_NAME_MAX;
    return MM_LINUX_LFS_OK;
}

// A device never written: littlefs keeps its superblock pair in blocks 0 and
// 1, so both all 0xFF means nothing was ever there.
static int mm_linux_lfs_blank(mm_linux_lfs_volume_t* volume) {
    uint8_t chunk[64];
    const uint64_t span = 2u * (uint64_t)volume->config.block_size;
    for (uint64_t offset = 0; offset < span; offset += sizeof chunk) {
        if (volume->device->read(volume->device->context, offset, chunk, sizeof chunk) !=
            MM_LINUX_LFS_OK)
            return 0;
        for (size_t i = 0; i < sizeof chunk; ++i)
            if (chunk[i] != 0xffu) return 0;
    }
    return 1;
}

static mm_linux_lfs_volume_t* mm_linux_lfs_volume_at(unsigned int volume) {
    if (volume >= MM_LINUX_LFS_VOLUMES || !mm_linux_lfs_volumes[volume].used) return NULL;
    return &mm_linux_lfs_volumes[volume];
}

static mm_linux_lfs_file_t* mm_linux_lfs_file_at(unsigned int handle) {
    if (handle >= MM_LINUX_LFS_FILES || !mm_linux_lfs_files[handle].used) return NULL;
    return &mm_linux_lfs_files[handle];
}

// "/" and the volume-relative path, NUL-terminated.
static int mm_linux_lfs_path(char* out, const char* path, size_t length) {
    if (length > MM_LINUX_LFS_PATH - 2u) return MM_LINUX_LFS_NAME_TOO_LONG;
    if (length != 0 && path == NULL) return MM_LINUX_LFS_BAD_ARGUMENT;
    out[0] = '/';
    if (length != 0) memcpy(out + 1, path, length);
    out[length + 1] = '\0';
    if (strlen(out) != length + 1) return MM_LINUX_LFS_BAD_ARGUMENT;   // an embedded NUL
    return MM_LINUX_LFS_OK;
}

static int mm_linux_lfs_same_or_beneath(const char* candidate, const char* directory) {
    const size_t length = strlen(directory);
    return strncmp(candidate, directory, length) == 0 &&
           (candidate[length] == '\0' || candidate[length] == '/');
}

static int mm_linux_lfs_open_on(unsigned int volume, const char* full, int writers_only) {
    for (unsigned int i = 0; i < MM_LINUX_LFS_FILES; ++i) {
        const mm_linux_lfs_file_t* file = &mm_linux_lfs_files[i];
        if (file->used && file->volume == volume && strcmp(file->path, full) == 0 &&
            (!writers_only || file->access != MM_LINUX_LFS_READ))
            return 1;
    }
    return 0;
}

static int mm_linux_lfs_open_beneath(unsigned int volume, const char* full) {
    for (unsigned int i = 0; i < MM_LINUX_LFS_FILES; ++i) {
        const mm_linux_lfs_file_t* file = &mm_linux_lfs_files[i];
        if (file->used && file->volume == volume && mm_linux_lfs_same_or_beneath(file->path, full))
            return 1;
    }
    for (unsigned int i = 0; i < MM_LINUX_LFS_DIRECTORIES; ++i) {
        const mm_linux_lfs_directory_t* directory = &mm_linux_lfs_directories[i];
        if (directory->used && directory->volume == volume &&
            mm_linux_lfs_same_or_beneath(directory->path, full))
            return 1;
    }
    return 0;
}

// The parent of full must be a directory for anything to be made in it.
static int mm_linux_lfs_parent_is_directory(mm_linux_lfs_volume_t* volume, const char* full) {
    char parent[MM_LINUX_LFS_PATH];
    const char* slash = strrchr(full, '/');
    if (slash == full) return MM_LINUX_LFS_OK;       // the root
    const size_t length = (size_t)(slash - full);
    memcpy(parent, full, length);
    parent[length] = '\0';
    struct lfs_info info;
    const int error = lfs_stat(&volume->lfs, parent, &info);
    if (error != LFS_ERR_OK) return mm_linux_lfs_status(volume, error);
    return info.type == LFS_TYPE_DIR ? MM_LINUX_LFS_OK : MM_LINUX_LFS_NOT_DIRECTORY;
}

static uint64_t mm_linux_lfs_path_modified(mm_linux_lfs_volume_t* volume, const char* full) {
    uint64_t modified = 0;
    const lfs_ssize_t size = lfs_getattr(&volume->lfs, full, MM_LINUX_LFS_TIME, &modified,
                                         sizeof modified);
    return size == (lfs_ssize_t)sizeof modified ? modified : 0u;
}

static void mm_linux_lfs_describe(mm_linux_lfs_volume_t* volume, const char* full,
                                 const struct lfs_info* info, mm_linux_lfs_entry* stat) {
    stat->directory = info->type == LFS_TYPE_DIR;
    stat->size = info->type == LFS_TYPE_DIR ? 0u : info->size;
    stat->modified = info->type == LFS_TYPE_DIR ? 0u : mm_linux_lfs_path_modified(volume, full);
    stat->read_only = volume->read_only;
}

int mm_linux_lfs_attach(const mm_linux_lfs_device* device, int read_only, int format_if_blank,
                       int32_t block_cycles, unsigned int* volume) {
    if (device == NULL || volume == NULL) return MM_LINUX_LFS_BAD_ARGUMENT;
    for (unsigned int i = 0; i < MM_LINUX_LFS_VOLUMES; ++i) {
        mm_linux_lfs_volume_t* candidate = &mm_linux_lfs_volumes[i];
        if (candidate->used) continue;
        int status = mm_linux_lfs_configure(candidate, device, block_cycles);
        if (status != MM_LINUX_LFS_OK) return status;
        int error = lfs_mount(&candidate->lfs, &candidate->config);
        if (error != LFS_ERR_OK) {
            if (candidate->last_device_status != MM_LINUX_LFS_OK)
                return candidate->last_device_status;
            if (!format_if_blank || !mm_linux_lfs_blank(candidate)) return MM_LINUX_LFS_CORRUPT;
            if (read_only) return MM_LINUX_LFS_READ_ONLY;
            error = lfs_format(&candidate->lfs, &candidate->config);
            if (error == LFS_ERR_OK) error = lfs_mount(&candidate->lfs, &candidate->config);
            if (error != LFS_ERR_OK) return mm_linux_lfs_status(candidate, error);
        }
        candidate->used = 1;
        candidate->read_only = read_only;
        *volume = i;
        return MM_LINUX_LFS_OK;
    }
    return MM_LINUX_LFS_TOO_MANY;
}

int mm_linux_lfs_detach(unsigned int volume) {
    mm_linux_lfs_volume_t* v = mm_linux_lfs_volume_at(volume);
    if (v == NULL) return MM_LINUX_LFS_BAD_ARGUMENT;
    for (unsigned int i = 0; i < MM_LINUX_LFS_FILES; ++i) {
        mm_linux_lfs_file_t* file = &mm_linux_lfs_files[i];
        if (!file->used || file->volume != volume) continue;
        (void)lfs_file_close(&v->lfs, &file->file);
        file->used = 0;
    }
    for (unsigned int i = 0; i < MM_LINUX_LFS_DIRECTORIES; ++i) {
        mm_linux_lfs_directory_t* directory = &mm_linux_lfs_directories[i];
        if (!directory->used || directory->volume != volume) continue;
        (void)lfs_dir_close(&v->lfs, &directory->directory);
        directory->used = 0;
    }
    const int error = lfs_unmount(&v->lfs);
    v->used = 0;
    return mm_linux_lfs_status(v, error);
}

int mm_linux_lfs_format(const mm_linux_lfs_device* device) {
    if (device == NULL) return MM_LINUX_LFS_BAD_ARGUMENT;
    mm_linux_lfs_volume_t* v = &mm_linux_lfs_formatting;
    const int status = mm_linux_lfs_configure(v, device, 500);
    if (status != MM_LINUX_LFS_OK) return status;
    return mm_linux_lfs_status(v, lfs_format(&v->lfs, &v->config));
}

// What exists is looked up first and littlefs's flags chosen to match, so
// each disposition and the Busy rules are decided here rather than inferred
// from littlefs's errors.
int mm_linux_lfs_open(unsigned int volume, const char* path, size_t length, int access,
                     int disposition, unsigned int* handle) {
    mm_linux_lfs_volume_t* v = mm_linux_lfs_volume_at(volume);
    if (v == NULL || handle == NULL || access < MM_LINUX_LFS_READ || access > MM_LINUX_LFS_APPEND)
        return MM_LINUX_LFS_BAD_ARGUMENT;
    if (length == 0) return MM_LINUX_LFS_IS_DIRECTORY;
    const int writable = access != MM_LINUX_LFS_READ;
    const int truncating = disposition == MM_LINUX_LFS_CREATE_OR_TRUNCATE;
    if ((writable || truncating) && v->read_only) return MM_LINUX_LFS_READ_ONLY;

    char full[MM_LINUX_LFS_PATH];
    int status = mm_linux_lfs_path(full, path, length);
    if (status != MM_LINUX_LFS_OK) return status;
    status = mm_linux_lfs_parent_is_directory(v, full);
    if (status != MM_LINUX_LFS_OK) return status;
    struct lfs_info info;
    const int found = lfs_stat(&v->lfs, full, &info);
    const int present = found == LFS_ERR_OK;
    if (!present && found != LFS_ERR_NOENT) return mm_linux_lfs_status(v, found);
    if (present) {
        if (info.type == LFS_TYPE_DIR) return MM_LINUX_LFS_IS_DIRECTORY;
        if (disposition == MM_LINUX_LFS_CREATE_NEW) return MM_LINUX_LFS_EXISTS;
        if (mm_linux_lfs_open_on(volume, full, 0) &&
            (writable || truncating || mm_linux_lfs_open_on(volume, full, 1)))
            return MM_LINUX_LFS_BUSY;
    } else {
        if (disposition == MM_LINUX_LFS_OPEN_EXISTING) return MM_LINUX_LFS_NOT_FOUND;
        if (v->read_only) return MM_LINUX_LFS_READ_ONLY;
    }

    mm_linux_lfs_file_t* file = NULL;
    unsigned int index = 0;
    for (; index < MM_LINUX_LFS_FILES; ++index) {
        if (!mm_linux_lfs_files[index].used) {
            file = &mm_linux_lfs_files[index];
            break;
        }
    }
    if (file == NULL) return MM_LINUX_LFS_TOO_MANY;

    memset(&file->config, 0, sizeof file->config);
    file->attribute.type = MM_LINUX_LFS_TIME;
    file->attribute.buffer = &file->modified;
    file->attribute.size = sizeof file->modified;
    file->config.buffer = file->buffer;
    file->config.attrs = &file->attribute;
    file->config.attr_count = 1;
    // littlefs reads attributes only for an open with read access, but
    // writes them at every close of a writable one, so the stored time is
    // loaded first: a file opened for writing and closed unchanged keeps it.
    file->modified = present ? mm_linux_lfs_path_modified(v, full) : 0u;

    int flags = LFS_O_RDONLY;
    switch (access) {
        case MM_LINUX_LFS_WRITE: flags = LFS_O_WRONLY; break;
        case MM_LINUX_LFS_READ_WRITE: flags = LFS_O_RDWR; break;
        case MM_LINUX_LFS_APPEND: flags = LFS_O_WRONLY | LFS_O_APPEND; break;
        default: break;
    }
    if (!present || truncating) {
        file->modified = mm_linux_lfs_now();
        if (access == MM_LINUX_LFS_READ) {
            // littlefs cannot create or truncate through a read-only open, so
            // a writable one does it first.
            const int made = lfs_file_opencfg(&v->lfs, &file->file, full,
                                              LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC,
                                              &file->config);
            if (made != LFS_ERR_OK) return mm_linux_lfs_status(v, made);
            const int closed = lfs_file_close(&v->lfs, &file->file);
            if (closed != LFS_ERR_OK) return mm_linux_lfs_status(v, closed);
        } else {
            flags |= LFS_O_CREAT;
            if (truncating) flags |= LFS_O_TRUNC;
        }
    }
    const int opened = lfs_file_opencfg(&v->lfs, &file->file, full, flags, &file->config);
    if (opened != LFS_ERR_OK) return mm_linux_lfs_status(v, opened);
    file->used = 1;
    file->volume = volume;
    file->access = access;
    memcpy(file->path, full, strlen(full) + 1u);
    *handle = index;
    return MM_LINUX_LFS_OK;
}

int mm_linux_lfs_read(unsigned int handle, void* data, size_t size, size_t* count) {
    mm_linux_lfs_file_t* file = mm_linux_lfs_file_at(handle);
    if (file == NULL || count == NULL || (data == NULL && size != 0))
        return MM_LINUX_LFS_BAD_ARGUMENT;
    mm_linux_lfs_volume_t* v = &mm_linux_lfs_volumes[file->volume];
    const lfs_ssize_t moved = lfs_file_read(&v->lfs, &file->file, data, (lfs_size_t)size);
    if (moved < 0) return mm_linux_lfs_status(v, (int)moved);
    *count = (size_t)moved;
    return MM_LINUX_LFS_OK;
}

int mm_linux_lfs_write(unsigned int handle, const void* data, size_t size, size_t* count) {
    mm_linux_lfs_file_t* file = mm_linux_lfs_file_at(handle);
    if (file == NULL || count == NULL || (data == NULL && size != 0))
        return MM_LINUX_LFS_BAD_ARGUMENT;
    mm_linux_lfs_volume_t* v = &mm_linux_lfs_volumes[file->volume];
    const lfs_ssize_t moved = lfs_file_write(&v->lfs, &file->file, data, (lfs_size_t)size);
    if (moved < 0) {
        *count = 0;
        return mm_linux_lfs_status(v, (int)moved);
    }
    file->modified = mm_linux_lfs_now();
    *count = (size_t)moved;
    return (size_t)moved < size ? MM_LINUX_LFS_NO_SPACE : MM_LINUX_LFS_OK;
}

int mm_linux_lfs_seek(unsigned int handle, uint64_t offset) {
    mm_linux_lfs_file_t* file = mm_linux_lfs_file_at(handle);
    if (file == NULL) return MM_LINUX_LFS_BAD_ARGUMENT;
    if (offset > (uint64_t)INT32_MAX) return MM_LINUX_LFS_BAD_ARGUMENT;
    mm_linux_lfs_volume_t* v = &mm_linux_lfs_volumes[file->volume];
    const lfs_soff_t at = lfs_file_seek(&v->lfs, &file->file, (lfs_soff_t)offset, LFS_SEEK_SET);
    return at < 0 ? mm_linux_lfs_status(v, (int)at) : MM_LINUX_LFS_OK;
}

int mm_linux_lfs_tell(unsigned int handle, uint64_t* offset) {
    mm_linux_lfs_file_t* file = mm_linux_lfs_file_at(handle);
    if (file == NULL || offset == NULL) return MM_LINUX_LFS_BAD_ARGUMENT;
    mm_linux_lfs_volume_t* v = &mm_linux_lfs_volumes[file->volume];
    const lfs_soff_t at = lfs_file_tell(&v->lfs, &file->file);
    if (at < 0) return mm_linux_lfs_status(v, (int)at);
    *offset = (uint64_t)at;
    return MM_LINUX_LFS_OK;
}

int mm_linux_lfs_truncate(unsigned int handle) {
    mm_linux_lfs_file_t* file = mm_linux_lfs_file_at(handle);
    if (file == NULL) return MM_LINUX_LFS_BAD_ARGUMENT;
    mm_linux_lfs_volume_t* v = &mm_linux_lfs_volumes[file->volume];
    const lfs_soff_t at = lfs_file_tell(&v->lfs, &file->file);
    if (at < 0) return mm_linux_lfs_status(v, (int)at);
    const int error = lfs_file_truncate(&v->lfs, &file->file, (lfs_off_t)at);
    if (error != LFS_ERR_OK) return mm_linux_lfs_status(v, error);
    file->modified = mm_linux_lfs_now();
    return MM_LINUX_LFS_OK;
}

int mm_linux_lfs_sync(unsigned int handle) {
    mm_linux_lfs_file_t* file = mm_linux_lfs_file_at(handle);
    if (file == NULL) return MM_LINUX_LFS_BAD_ARGUMENT;
    mm_linux_lfs_volume_t* v = &mm_linux_lfs_volumes[file->volume];
    return mm_linux_lfs_status(v, lfs_file_sync(&v->lfs, &file->file));
}

int mm_linux_lfs_file_stat(unsigned int handle, mm_linux_lfs_entry* stat) {
    mm_linux_lfs_file_t* file = mm_linux_lfs_file_at(handle);
    if (file == NULL || stat == NULL) return MM_LINUX_LFS_BAD_ARGUMENT;
    mm_linux_lfs_volume_t* v = &mm_linux_lfs_volumes[file->volume];
    const lfs_soff_t size = lfs_file_size(&v->lfs, &file->file);
    if (size < 0) return mm_linux_lfs_status(v, (int)size);
    stat->directory = 0;
    stat->size = (uint64_t)size;
    stat->modified = file->modified;
    stat->read_only = v->read_only;
    return MM_LINUX_LFS_OK;
}

int mm_linux_lfs_close(unsigned int handle) {
    mm_linux_lfs_file_t* file = mm_linux_lfs_file_at(handle);
    if (file == NULL) return MM_LINUX_LFS_BAD_ARGUMENT;
    mm_linux_lfs_volume_t* v = &mm_linux_lfs_volumes[file->volume];
    const int error = lfs_file_close(&v->lfs, &file->file);
    file->used = 0;
    return mm_linux_lfs_status(v, error);
}

int mm_linux_lfs_open_directory(unsigned int volume, const char* path, size_t length,
                               unsigned int* handle) {
    mm_linux_lfs_volume_t* v = mm_linux_lfs_volume_at(volume);
    if (v == NULL || handle == NULL) return MM_LINUX_LFS_BAD_ARGUMENT;
    char full[MM_LINUX_LFS_PATH];
    const int status = mm_linux_lfs_path(full, path, length);
    if (status != MM_LINUX_LFS_OK) return status;
    if (length != 0) {
        struct lfs_info info;
        const int found = lfs_stat(&v->lfs, full, &info);
        if (found != LFS_ERR_OK) return mm_linux_lfs_status(v, found);
        if (info.type != LFS_TYPE_DIR) return MM_LINUX_LFS_NOT_DIRECTORY;
    }
    for (unsigned int i = 0; i < MM_LINUX_LFS_DIRECTORIES; ++i) {
        mm_linux_lfs_directory_t* directory = &mm_linux_lfs_directories[i];
        if (directory->used) continue;
        const int error = lfs_dir_open(&v->lfs, &directory->directory, full);
        if (error != LFS_ERR_OK) return mm_linux_lfs_status(v, error);
        directory->used = 1;
        directory->volume = volume;
        directory->has_pending = 0;
        memcpy(directory->path, full, strlen(full) + 1u);
        *handle = i;
        return MM_LINUX_LFS_OK;
    }
    return MM_LINUX_LFS_TOO_MANY;
}

int mm_linux_lfs_next(unsigned int handle, char* name, size_t capacity, size_t* length,
                     mm_linux_lfs_entry* stat, int* done) {
    if (handle >= MM_LINUX_LFS_DIRECTORIES || !mm_linux_lfs_directories[handle].used ||
        length == NULL || stat == NULL || done == NULL || (name == NULL && capacity != 0))
        return MM_LINUX_LFS_BAD_ARGUMENT;
    mm_linux_lfs_directory_t* directory = &mm_linux_lfs_directories[handle];
    mm_linux_lfs_volume_t* v = &mm_linux_lfs_volumes[directory->volume];
    while (!directory->has_pending) {
        const int read = lfs_dir_read(&v->lfs, &directory->directory, &directory->pending);
        if (read < 0) return mm_linux_lfs_status(v, read);
        if (read == 0) {
            *done = 1;
            return MM_LINUX_LFS_OK;
        }
        if (strcmp(directory->pending.name, ".") == 0 ||
            strcmp(directory->pending.name, "..") == 0)
            continue;
        directory->has_pending = 1;
    }
    const size_t name_length = strlen(directory->pending.name);
    if (name_length > capacity) return MM_LINUX_LFS_NAME_TOO_LONG;
    memcpy(name, directory->pending.name, name_length);
    char child[MM_LINUX_LFS_CHILD];
    const size_t base = strlen(directory->path);
    memcpy(child, directory->path, base);
    size_t at = base;
    if (base > 1) child[at++] = '/';
    memcpy(child + at, directory->pending.name, name_length + 1u);
    mm_linux_lfs_describe(v, child, &directory->pending, stat);
    *length = name_length;
    *done = 0;
    directory->has_pending = 0;
    return MM_LINUX_LFS_OK;
}

int mm_linux_lfs_close_directory(unsigned int handle) {
    if (handle >= MM_LINUX_LFS_DIRECTORIES || !mm_linux_lfs_directories[handle].used)
        return MM_LINUX_LFS_BAD_ARGUMENT;
    mm_linux_lfs_directory_t* directory = &mm_linux_lfs_directories[handle];
    mm_linux_lfs_volume_t* v = &mm_linux_lfs_volumes[directory->volume];
    const int error = lfs_dir_close(&v->lfs, &directory->directory);
    directory->used = 0;
    return mm_linux_lfs_status(v, error);
}

int mm_linux_lfs_stat(unsigned int volume, const char* path, size_t length,
                     mm_linux_lfs_entry* stat) {
    mm_linux_lfs_volume_t* v = mm_linux_lfs_volume_at(volume);
    if (v == NULL || stat == NULL) return MM_LINUX_LFS_BAD_ARGUMENT;
    if (length == 0) {
        stat->directory = 1;
        stat->size = 0;
        stat->modified = 0;
        stat->read_only = v->read_only;
        return MM_LINUX_LFS_OK;
    }
    char full[MM_LINUX_LFS_PATH];
    const int status = mm_linux_lfs_path(full, path, length);
    if (status != MM_LINUX_LFS_OK) return status;
    struct lfs_info info;
    const int found = lfs_stat(&v->lfs, full, &info);
    if (found != LFS_ERR_OK) return mm_linux_lfs_status(v, found);
    mm_linux_lfs_describe(v, full, &info, stat);
    return MM_LINUX_LFS_OK;
}

int mm_linux_lfs_make_directory(unsigned int volume, const char* path, size_t length) {
    mm_linux_lfs_volume_t* v = mm_linux_lfs_volume_at(volume);
    if (v == NULL) return MM_LINUX_LFS_BAD_ARGUMENT;
    if (v->read_only) return MM_LINUX_LFS_READ_ONLY;
    if (length == 0) return MM_LINUX_LFS_EXISTS;
    char full[MM_LINUX_LFS_PATH];
    int status = mm_linux_lfs_path(full, path, length);
    if (status != MM_LINUX_LFS_OK) return status;
    status = mm_linux_lfs_parent_is_directory(v, full);
    if (status != MM_LINUX_LFS_OK) return status;
    return mm_linux_lfs_status(v, lfs_mkdir(&v->lfs, full));
}

int mm_linux_lfs_remove(unsigned int volume, const char* path, size_t length) {
    mm_linux_lfs_volume_t* v = mm_linux_lfs_volume_at(volume);
    if (v == NULL || length == 0) return MM_LINUX_LFS_BAD_ARGUMENT;
    if (v->read_only) return MM_LINUX_LFS_READ_ONLY;
    char full[MM_LINUX_LFS_PATH];
    const int status = mm_linux_lfs_path(full, path, length);
    if (status != MM_LINUX_LFS_OK) return status;
    struct lfs_info info;
    const int found = lfs_stat(&v->lfs, full, &info);
    if (found != LFS_ERR_OK) return mm_linux_lfs_status(v, found);
    if (mm_linux_lfs_open_beneath(volume, full)) return MM_LINUX_LFS_BUSY;
    return mm_linux_lfs_status(v, lfs_remove(&v->lfs, full));
}

// lfs_rename replaces an existing target; mm.fs's contract does not, so the
// target is looked up first.
int mm_linux_lfs_rename(unsigned int volume, const char* from, size_t from_length,
                       const char* to, size_t to_length) {
    mm_linux_lfs_volume_t* v = mm_linux_lfs_volume_at(volume);
    if (v == NULL || from_length == 0 || to_length == 0) return MM_LINUX_LFS_BAD_ARGUMENT;
    if (v->read_only) return MM_LINUX_LFS_READ_ONLY;
    char source[MM_LINUX_LFS_PATH];
    char target[MM_LINUX_LFS_PATH];
    int status = mm_linux_lfs_path(source, from, from_length);
    if (status != MM_LINUX_LFS_OK) return status;
    status = mm_linux_lfs_path(target, to, to_length);
    if (status != MM_LINUX_LFS_OK) return status;
    struct lfs_info info;
    int found = lfs_stat(&v->lfs, source, &info);
    if (found != LFS_ERR_OK) return mm_linux_lfs_status(v, found);
    found = lfs_stat(&v->lfs, target, &info);
    if (found == LFS_ERR_OK) return MM_LINUX_LFS_EXISTS;
    if (found != LFS_ERR_NOENT) return mm_linux_lfs_status(v, found);
    status = mm_linux_lfs_parent_is_directory(v, target);
    if (status != MM_LINUX_LFS_OK) return status;
    if (mm_linux_lfs_same_or_beneath(target, source)) return MM_LINUX_LFS_BAD_ARGUMENT;
    if (mm_linux_lfs_open_beneath(volume, source)) return MM_LINUX_LFS_BUSY;
    return mm_linux_lfs_status(v, lfs_rename(&v->lfs, source, target));
}

int mm_linux_lfs_space(unsigned int volume, uint64_t* total, uint64_t* free) {
    mm_linux_lfs_volume_t* v = mm_linux_lfs_volume_at(volume);
    if (v == NULL || total == NULL || free == NULL) return MM_LINUX_LFS_BAD_ARGUMENT;
    const lfs_ssize_t used = lfs_fs_size(&v->lfs);
    if (used < 0) return mm_linux_lfs_status(v, (int)used);
    const uint64_t block = v->config.block_size;
    const uint64_t all = (uint64_t)v->config.block_count * block;
    const uint64_t taken = (uint64_t)used * block;
    *total = all;
    *free = taken < all ? all - taken : 0u;
    return MM_LINUX_LFS_OK;
}

int mm_linux_lfs_flush(unsigned int volume) {
    mm_linux_lfs_volume_t* v = mm_linux_lfs_volume_at(volume);
    if (v == NULL) return MM_LINUX_LFS_BAD_ARGUMENT;
    int result = MM_LINUX_LFS_OK;
    for (unsigned int i = 0; i < MM_LINUX_LFS_FILES; ++i) {
        mm_linux_lfs_file_t* file = &mm_linux_lfs_files[i];
        if (!file->used || file->volume != volume) continue;
        const int error = lfs_file_sync(&v->lfs, &file->file);
        if (error != LFS_ERR_OK) result = mm_linux_lfs_status(v, error);
    }
    return result;
}
