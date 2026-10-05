// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// FatFs behind the private ABI platform.linux.fs.fat calls, and FatFs's disk
// I/O layer over the block devices the provider registers. This file owns
// FatFs's state -- two FATFS work areas, open files, open directories -- in
// static pools, and keeps the rules mm.fs's Volume documents that FatFs does
// not keep by itself:
//
// - a file open for writing cannot be opened again, removed, or renamed, and
//   an open directory cannot be removed or renamed (FatFs's own lock table,
//   FF_FS_LOCK, catches the same under another spelling of the name);
// - rename never replaces a target, and a directory never moves into itself;
// - every Append write goes to the end, not only the first;
// - seeking past the end and writing fills the gap with zeros, where FatFs
//   would leave it undefined;
// - the root has a stat, which FatFs refuses to give.
//
// Paths cross as "N:/..." with N the FatFs drive, which is the volume's slot.

#include "fat-c.h"
#include "ff.h"
#include "diskio.h"

#include <stdint.h>
#include <string.h>

#define MM_LINUX_FAT_VOLUMES FF_VOLUMES
#define MM_LINUX_FAT_FILES 8u
#define MM_LINUX_FAT_DIRECTORIES 4u
#define MM_LINUX_FAT_BLOCK 512u
// "N:/", a normalised volume-relative path of at most 255 bytes, a NUL.
#define MM_LINUX_FAT_PATH 260u
// A directory path, "/", and an entry name.
#define MM_LINUX_FAT_CHILD (MM_LINUX_FAT_PATH + 1u + FF_LFN_BUF)

typedef struct {
    int used;
    int read_only;
    const mm_linux_fat_device* device;
    int last_device_status;
    FATFS fs;
} mm_linux_fat_volume_t;

typedef struct {
    int used;
    unsigned int volume;
    int access;
    FIL file;
    // A position past the end that a seek asked for, applied, with zeros,
    // by the next write; zero when none is pending.
    int pending;
    uint64_t pending_offset;
    char path[MM_LINUX_FAT_PATH];
} mm_linux_fat_file_t;

typedef struct {
    int used;
    unsigned int volume;
    DIR directory;
    FILINFO pending;
    int has_pending;
    char path[MM_LINUX_FAT_PATH];
} mm_linux_fat_directory_t;

// Disk records by FatFs drive: the attached volumes', or one being formatted.
static const mm_linux_fat_device* mm_linux_fat_drives[MM_LINUX_FAT_VOLUMES];
static int mm_linux_fat_drive_status[MM_LINUX_FAT_VOLUMES];

static mm_linux_fat_volume_t mm_linux_fat_volumes[MM_LINUX_FAT_VOLUMES];
static mm_linux_fat_file_t mm_linux_fat_files[MM_LINUX_FAT_FILES];
static mm_linux_fat_directory_t mm_linux_fat_directories[MM_LINUX_FAT_DIRECTORIES];
static uint64_t (*mm_linux_fat_clock)(void);

void mm_linux_fat_set_clock(uint64_t (*now)(void)) { mm_linux_fat_clock = now; }

// Days since 1970-01-01 for a civil date, and back: Howard Hinnant's
// algorithms, exact for every date FAT can hold.
static int64_t mm_linux_fat_days_from_civil(int64_t year, unsigned int month, unsigned int day) {
    year -= month <= 2u;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const int64_t year_of_era = year - era * 400;
    const int64_t day_of_year = (153 * (month + (month > 2u ? -3 : 9)) + 2) / 5 + day - 1;
    const int64_t day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + day_of_era - 719468;
}

static void mm_linux_fat_civil_from_days(int64_t days, int64_t* year, unsigned int* month,
                                        unsigned int* day) {
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int64_t day_of_era = days - era * 146097;
    const int64_t year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const int64_t month_index = (5 * day_of_year + 2) / 153;
    *day = (unsigned int)(day_of_year - (153 * month_index + 2) / 5 + 1);
    *month = (unsigned int)(month_index < 10 ? month_index + 3 : month_index - 9);
    *year = year_of_era + era * 400 + (*month <= 2u);
}

// FAT stores local time with no zone; mm.fs treats it as UTC.
static uint64_t mm_linux_fat_seconds(WORD date, WORD time) {
    if (date == 0) return 0;
    const int64_t year = 1980 + (date >> 9);
    const unsigned int month = (date >> 5) & 15u;
    const unsigned int day = date & 31u;
    if (month < 1u || month > 12u || day < 1u) return 0;
    const int64_t days = mm_linux_fat_days_from_civil(year, month, day);
    const int64_t seconds =
        days * 86400 + (time >> 11) * 3600 + ((time >> 5) & 63u) * 60 + (time & 31u) * 2;
    return seconds > 0 ? (uint64_t)seconds : 0u;
}

// FatFs's clock: the mm.fs clock, or 1 January 2026 when there is none.
DWORD get_fattime(void) {
    uint64_t now = mm_linux_fat_clock != NULL ? mm_linux_fat_clock() : 0u;
    if (now == 0) now = (uint64_t)mm_linux_fat_days_from_civil(2026, 1, 1) * 86400u;
    int64_t year = 0;
    unsigned int month = 0, day = 0;
    mm_linux_fat_civil_from_days((int64_t)(now / 86400u), &year, &month, &day);
    if (year < 1980) return (DWORD)((1u << 21) | (1u << 16));     // 1980-01-01
    if (year > 2107) year = 2107;
    const unsigned int second_of_day = (unsigned int)(now % 86400u);
    return (DWORD)((uint32_t)(year - 1980) << 25 | (uint32_t)month << 21 | (uint32_t)day << 16 |
                   (uint32_t)(second_of_day / 3600u) << 11 |
                   (uint32_t)(second_of_day / 60u % 60u) << 5 | (uint32_t)(second_of_day % 60u / 2u));
}

DSTATUS disk_status(BYTE drive) {
    return drive < MM_LINUX_FAT_VOLUMES && mm_linux_fat_drives[drive] != NULL ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE drive) { return disk_status(drive); }

DRESULT disk_read(BYTE drive, BYTE* buffer, LBA_t sector, UINT count) {
    if (disk_status(drive) != 0) return RES_NOTRDY;
    const mm_linux_fat_device* device = mm_linux_fat_drives[drive];
    const int status = device->read(device->context, sector, buffer, (size_t)count * MM_LINUX_FAT_BLOCK);
    mm_linux_fat_drive_status[drive] = status;
    return status == MM_LINUX_FAT_OK ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE drive, const BYTE* buffer, LBA_t sector, UINT count) {
    if (disk_status(drive) != 0) return RES_NOTRDY;
    const mm_linux_fat_device* device = mm_linux_fat_drives[drive];
    const int status = device->write(device->context, sector, buffer, (size_t)count * MM_LINUX_FAT_BLOCK);
    mm_linux_fat_drive_status[drive] = status;
    return status == MM_LINUX_FAT_OK ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE drive, BYTE command, void* buffer) {
    if (disk_status(drive) != 0) return RES_NOTRDY;
    const mm_linux_fat_device* device = mm_linux_fat_drives[drive];
    switch (command) {
        case CTRL_SYNC: {
            const int status = device->sync(device->context);
            mm_linux_fat_drive_status[drive] = status;
            return status == MM_LINUX_FAT_OK ? RES_OK : RES_ERROR;
        }
        case GET_SECTOR_COUNT: {
            uint64_t count = 0;
            unsigned int size = 0;
            const int status = device->geometry(device->context, &count, &size);
            mm_linux_fat_drive_status[drive] = status;
            if (status != MM_LINUX_FAT_OK) return RES_ERROR;
            *(LBA_t*)buffer = count > 0xffffffffu ? 0xffffffffu : (LBA_t)count;
            return RES_OK;
        }
        case GET_SECTOR_SIZE: *(WORD*)buffer = MM_LINUX_FAT_BLOCK; return RES_OK;
        case GET_BLOCK_SIZE: *(DWORD*)buffer = 1; return RES_OK;    // unknown
        default: return RES_PARERR;
    }
}

// FatFs's results. FR_DENIED means different things by operation, so the
// caller says what it was doing: making something (the directory or volume
// is full), removing a directory (it has entries), or anything else.
enum { MM_LINUX_FAT_DENIED_READ_ONLY = 0, MM_LINUX_FAT_DENIED_NO_SPACE, MM_LINUX_FAT_DENIED_NOT_EMPTY };

static int mm_linux_fat_status_as(unsigned int drive, FRESULT result, int denied) {
    switch (result) {
        case FR_OK: return MM_LINUX_FAT_OK;
        case FR_DISK_ERR:
            if (drive < MM_LINUX_FAT_VOLUMES && mm_linux_fat_drive_status[drive] != MM_LINUX_FAT_OK)
                return mm_linux_fat_drive_status[drive];
            return MM_LINUX_FAT_TRANSPORT_ERROR;
        case FR_INT_ERR: return MM_LINUX_FAT_CORRUPT;
        case FR_NOT_READY: return MM_LINUX_FAT_TRANSPORT_ERROR;
        case FR_NO_FILE: return MM_LINUX_FAT_NOT_FOUND;
        case FR_NO_PATH: return MM_LINUX_FAT_NOT_FOUND;
        case FR_INVALID_NAME: return MM_LINUX_FAT_BAD_ARGUMENT;
        case FR_DENIED:
            if (denied == MM_LINUX_FAT_DENIED_NO_SPACE) return MM_LINUX_FAT_NO_SPACE;
            if (denied == MM_LINUX_FAT_DENIED_NOT_EMPTY) return MM_LINUX_FAT_NOT_EMPTY;
            return MM_LINUX_FAT_READ_ONLY;
        case FR_EXIST: return MM_LINUX_FAT_EXISTS;
        case FR_INVALID_OBJECT: return MM_LINUX_FAT_BAD_ARGUMENT;
        case FR_WRITE_PROTECTED: return MM_LINUX_FAT_READ_ONLY;
        case FR_INVALID_DRIVE: return MM_LINUX_FAT_BAD_ARGUMENT;
        case FR_NOT_ENABLED: return MM_LINUX_FAT_BAD_ARGUMENT;
        case FR_NO_FILESYSTEM: return MM_LINUX_FAT_CORRUPT;
        case FR_MKFS_ABORTED: return MM_LINUX_FAT_BAD_ARGUMENT;
        case FR_TIMEOUT: return MM_LINUX_FAT_TIMEOUT;
        case FR_LOCKED: return MM_LINUX_FAT_BUSY;
        case FR_NOT_ENOUGH_CORE: return MM_LINUX_FAT_TOO_MANY;
        case FR_TOO_MANY_OPEN_FILES: return MM_LINUX_FAT_TOO_MANY;
        case FR_INVALID_PARAMETER: return MM_LINUX_FAT_BAD_ARGUMENT;
        default: return MM_LINUX_FAT_TRANSPORT_ERROR;
    }
}

static int mm_linux_fat_status(unsigned int drive, FRESULT result) {
    return mm_linux_fat_status_as(drive, result, MM_LINUX_FAT_DENIED_READ_ONLY);
}

static mm_linux_fat_volume_t* mm_linux_fat_volume_at(unsigned int volume) {
    if (volume >= MM_LINUX_FAT_VOLUMES || !mm_linux_fat_volumes[volume].used) return NULL;
    return &mm_linux_fat_volumes[volume];
}

static mm_linux_fat_file_t* mm_linux_fat_file_at(unsigned int handle) {
    if (handle >= MM_LINUX_FAT_FILES || !mm_linux_fat_files[handle].used) return NULL;
    return &mm_linux_fat_files[handle];
}

// "N:/" and the volume-relative path, NUL-terminated.
static int mm_linux_fat_path(char* out, unsigned int drive, const char* path, size_t length) {
    if (length > MM_LINUX_FAT_PATH - 4u) return MM_LINUX_FAT_NAME_TOO_LONG;
    if (length != 0 && path == NULL) return MM_LINUX_FAT_BAD_ARGUMENT;
    out[0] = (char)('0' + drive);
    out[1] = ':';
    out[2] = '/';
    if (length != 0) memcpy(out + 3, path, length);
    out[length + 3] = '\0';
    if (strlen(out) != length + 3) return MM_LINUX_FAT_BAD_ARGUMENT;    // an embedded NUL
    return MM_LINUX_FAT_OK;
}

static int mm_linux_fat_same_or_beneath(const char* candidate, const char* directory) {
    const size_t length = strlen(directory);
    return strncmp(candidate, directory, length) == 0 &&
           (candidate[length] == '\0' || candidate[length] == '/');
}

static int mm_linux_fat_open_on(unsigned int volume, const char* full, int writers_only) {
    for (unsigned int i = 0; i < MM_LINUX_FAT_FILES; ++i) {
        const mm_linux_fat_file_t* file = &mm_linux_fat_files[i];
        if (file->used && file->volume == volume && strcmp(file->path, full) == 0 &&
            (!writers_only || file->access != MM_LINUX_FAT_READ))
            return 1;
    }
    return 0;
}

static int mm_linux_fat_open_beneath(unsigned int volume, const char* full) {
    for (unsigned int i = 0; i < MM_LINUX_FAT_FILES; ++i) {
        const mm_linux_fat_file_t* file = &mm_linux_fat_files[i];
        if (file->used && file->volume == volume && mm_linux_fat_same_or_beneath(file->path, full))
            return 1;
    }
    for (unsigned int i = 0; i < MM_LINUX_FAT_DIRECTORIES; ++i) {
        const mm_linux_fat_directory_t* directory = &mm_linux_fat_directories[i];
        if (directory->used && directory->volume == volume &&
            mm_linux_fat_same_or_beneath(directory->path, full))
            return 1;
    }
    return 0;
}

// The parent of full must be a directory for anything to be made in it.
static int mm_linux_fat_parent_is_directory(unsigned int drive, const char* full) {
    char parent[MM_LINUX_FAT_PATH];
    const char* slash = strrchr(full, '/');
    if (slash == full + 2) return MM_LINUX_FAT_OK;      // "N:/name": the root
    const size_t length = (size_t)(slash - full);
    memcpy(parent, full, length);
    parent[length] = '\0';
    FILINFO info;
    const FRESULT result = f_stat(parent, &info);
    if (result != FR_OK) return mm_linux_fat_status(drive, result);
    return (info.fattrib & AM_DIR) ? MM_LINUX_FAT_OK : MM_LINUX_FAT_NOT_DIRECTORY;
}

static void mm_linux_fat_describe(const mm_linux_fat_volume_t* volume, const FILINFO* info,
                                 mm_linux_fat_entry* stat) {
    stat->directory = (info->fattrib & AM_DIR) != 0;
    stat->size = stat->directory ? 0u : info->fsize;
    stat->modified = mm_linux_fat_seconds(info->fdate, info->ftime);
    stat->read_only = volume->read_only || (info->fattrib & AM_RDO) != 0;
}

static int mm_linux_fat_mount(unsigned int drive) {
    char root[4] = {(char)('0' + drive), ':', '\0', '\0'};
    return mm_linux_fat_status(drive, f_mount(&mm_linux_fat_volumes[drive].fs, root, 1));
}

static void mm_linux_fat_unmount(unsigned int drive) {
    char root[4] = {(char)('0' + drive), ':', '\0', '\0'};
    (void)f_mount(NULL, root, 0);
}

// A device FAT can use: 512-byte blocks.
static int mm_linux_fat_check_device(const mm_linux_fat_device* device) {
    uint64_t count = 0;
    unsigned int size = 0;
    const int status = device->geometry(device->context, &count, &size);
    if (status != MM_LINUX_FAT_OK) return status;
    if (size != MM_LINUX_FAT_BLOCK || count == 0) return MM_LINUX_FAT_UNSUPPORTED;
    return MM_LINUX_FAT_OK;
}

// Mounting reads only; a device that does not mount is left as it is.
int mm_linux_fat_attach(const mm_linux_fat_device* device, int read_only, unsigned int* volume) {
    if (device == NULL || volume == NULL) return MM_LINUX_FAT_BAD_ARGUMENT;
    int status = mm_linux_fat_check_device(device);
    if (status != MM_LINUX_FAT_OK) return status;
    for (unsigned int drive = 0; drive < MM_LINUX_FAT_VOLUMES; ++drive) {
        if (mm_linux_fat_volumes[drive].used || mm_linux_fat_drives[drive] != NULL) continue;
        mm_linux_fat_drives[drive] = device;
        mm_linux_fat_drive_status[drive] = MM_LINUX_FAT_OK;
        status = mm_linux_fat_mount(drive);
        if (status != MM_LINUX_FAT_OK) {
            mm_linux_fat_unmount(drive);
            mm_linux_fat_drives[drive] = NULL;
            return status;
        }
        mm_linux_fat_volumes[drive].used = 1;
        mm_linux_fat_volumes[drive].read_only = read_only;
        mm_linux_fat_volumes[drive].device = device;
        *volume = drive;
        return MM_LINUX_FAT_OK;
    }
    return MM_LINUX_FAT_TOO_MANY;
}

int mm_linux_fat_detach(unsigned int volume) {
    mm_linux_fat_volume_t* v = mm_linux_fat_volume_at(volume);
    if (v == NULL) return MM_LINUX_FAT_BAD_ARGUMENT;
    int result = MM_LINUX_FAT_OK;
    for (unsigned int i = 0; i < MM_LINUX_FAT_FILES; ++i) {
        mm_linux_fat_file_t* file = &mm_linux_fat_files[i];
        if (!file->used || file->volume != volume) continue;
        const FRESULT closed = f_close(&file->file);
        if (closed != FR_OK) result = mm_linux_fat_status(volume, closed);
        file->used = 0;
    }
    for (unsigned int i = 0; i < MM_LINUX_FAT_DIRECTORIES; ++i) {
        mm_linux_fat_directory_t* directory = &mm_linux_fat_directories[i];
        if (!directory->used || directory->volume != volume) continue;
        (void)f_closedir(&directory->directory);
        directory->used = 0;
    }
    mm_linux_fat_unmount(volume);
    mm_linux_fat_drives[volume] = NULL;
    v->used = 0;
    return result;
}

// Formatting borrows a free drive for the length of f_mkfs.
int mm_linux_fat_format(const mm_linux_fat_device* device, int format) {
    if (device == NULL) return MM_LINUX_FAT_BAD_ARGUMENT;
    int status = mm_linux_fat_check_device(device);
    if (status != MM_LINUX_FAT_OK) return status;
    BYTE options = FM_ANY & ~FM_EXFAT;
    if (format == MM_LINUX_FAT_FORMAT_FAT) options = FM_FAT;
    else if (format == MM_LINUX_FAT_FORMAT_FAT32) options = FM_FAT32;
    else if (format != MM_LINUX_FAT_FORMAT_AUTOMATIC) return MM_LINUX_FAT_BAD_ARGUMENT;
    for (unsigned int drive = 0; drive < MM_LINUX_FAT_VOLUMES; ++drive) {
        if (mm_linux_fat_volumes[drive].used || mm_linux_fat_drives[drive] != NULL) continue;
        mm_linux_fat_drives[drive] = device;
        mm_linux_fat_drive_status[drive] = MM_LINUX_FAT_OK;
        static BYTE work[FF_MAX_SS];
        const MKFS_PARM parameters = {options, 0, 0, 0, 0};
        char root[4] = {(char)('0' + drive), ':', '\0', '\0'};
        const FRESULT result = f_mkfs(root, &parameters, work, sizeof work);
        status = mm_linux_fat_status(drive, result);
        mm_linux_fat_drives[drive] = NULL;
        return status;
    }
    return MM_LINUX_FAT_TOO_MANY;
}

int mm_linux_fat_open(unsigned int volume, const char* path, size_t length, int access,
                     int disposition, unsigned int* handle) {
    mm_linux_fat_volume_t* v = mm_linux_fat_volume_at(volume);
    if (v == NULL || handle == NULL || access < MM_LINUX_FAT_READ || access > MM_LINUX_FAT_APPEND)
        return MM_LINUX_FAT_BAD_ARGUMENT;
    if (length == 0) return MM_LINUX_FAT_IS_DIRECTORY;
    const int writable = access != MM_LINUX_FAT_READ;
    const int truncating = disposition == MM_LINUX_FAT_CREATE_OR_TRUNCATE;
    if ((writable || truncating) && v->read_only) return MM_LINUX_FAT_READ_ONLY;

    char full[MM_LINUX_FAT_PATH];
    int status = mm_linux_fat_path(full, volume, path, length);
    if (status != MM_LINUX_FAT_OK) return status;
    status = mm_linux_fat_parent_is_directory(volume, full);
    if (status != MM_LINUX_FAT_OK) return status;
    FILINFO info;
    const FRESULT found = f_stat(full, &info);
    const int present = found == FR_OK;
    if (!present && found != FR_NO_FILE) return mm_linux_fat_status(volume, found);
    if (present) {
        if (info.fattrib & AM_DIR) return MM_LINUX_FAT_IS_DIRECTORY;
        if (disposition == MM_LINUX_FAT_CREATE_NEW) return MM_LINUX_FAT_EXISTS;
        if (mm_linux_fat_open_on(volume, full, 0) &&
            (writable || truncating || mm_linux_fat_open_on(volume, full, 1)))
            return MM_LINUX_FAT_BUSY;
    } else {
        if (disposition == MM_LINUX_FAT_OPEN_EXISTING) return MM_LINUX_FAT_NOT_FOUND;
        if (v->read_only) return MM_LINUX_FAT_READ_ONLY;
    }

    mm_linux_fat_file_t* file = NULL;
    unsigned int index = 0;
    for (; index < MM_LINUX_FAT_FILES; ++index) {
        if (!mm_linux_fat_files[index].used) {
            file = &mm_linux_fat_files[index];
            break;
        }
    }
    if (file == NULL) return MM_LINUX_FAT_TOO_MANY;

    BYTE mode = access == MM_LINUX_FAT_READ ? FA_READ
              : access == MM_LINUX_FAT_READ_WRITE ? (BYTE)(FA_READ | FA_WRITE)
                                                 : FA_WRITE;
    if (!present || truncating) {
        if (access == MM_LINUX_FAT_READ) {
            // A file made or emptied for reading is made writable first.
            FIL maker;
            const FRESULT made = f_open(&maker, full, FA_WRITE | FA_CREATE_ALWAYS);
            if (made != FR_OK)
                return mm_linux_fat_status_as(volume, made, MM_LINUX_FAT_DENIED_NO_SPACE);
            const FRESULT closed = f_close(&maker);
            if (closed != FR_OK) return mm_linux_fat_status(volume, closed);
        } else {
            mode |= truncating ? FA_CREATE_ALWAYS : FA_OPEN_ALWAYS;
        }
    }
    const FRESULT opened = f_open(&file->file, full, mode);
    if (opened != FR_OK)
        return mm_linux_fat_status_as(volume, opened,
                                     present ? MM_LINUX_FAT_DENIED_READ_ONLY
                                             : MM_LINUX_FAT_DENIED_NO_SPACE);
    file->used = 1;
    file->volume = volume;
    file->access = access;
    file->pending = 0;
    file->pending_offset = 0;
    memcpy(file->path, full, strlen(full) + 1u);
    *handle = index;
    return MM_LINUX_FAT_OK;
}

int mm_linux_fat_read(unsigned int handle, void* data, size_t size, size_t* count) {
    mm_linux_fat_file_t* file = mm_linux_fat_file_at(handle);
    if (file == NULL || count == NULL || (data == NULL && size != 0))
        return MM_LINUX_FAT_BAD_ARGUMENT;
    if (file->pending) {    // past the end: nothing to read
        *count = 0;
        return MM_LINUX_FAT_OK;
    }
    UINT moved = 0;
    const FRESULT result = f_read(&file->file, data, (UINT)size, &moved);
    if (result != FR_OK) return mm_linux_fat_status(file->volume, result);
    *count = moved;
    return MM_LINUX_FAT_OK;
}

// Zeros from the end to where a seek past it asked to be, so the gap reads as
// zeros on every volume.
static int mm_linux_fat_fill_gap(mm_linux_fat_file_t* file) {
    static const BYTE zeros[64];
    FRESULT result = f_lseek(&file->file, f_size(&file->file));
    if (result != FR_OK) return mm_linux_fat_status(file->volume, result);
    while (f_size(&file->file) < file->pending_offset) {
        const uint64_t gap = file->pending_offset - f_size(&file->file);
        const UINT chunk = gap < sizeof zeros ? (UINT)gap : (UINT)sizeof zeros;
        UINT moved = 0;
        result = f_write(&file->file, zeros, chunk, &moved);
        if (result != FR_OK) return mm_linux_fat_status(file->volume, result);
        if (moved < chunk) return MM_LINUX_FAT_NO_SPACE;
    }
    file->pending = 0;
    return MM_LINUX_FAT_OK;
}

int mm_linux_fat_write(unsigned int handle, const void* data, size_t size, size_t* count) {
    mm_linux_fat_file_t* file = mm_linux_fat_file_at(handle);
    if (file == NULL || count == NULL || (data == NULL && size != 0))
        return MM_LINUX_FAT_BAD_ARGUMENT;
    *count = 0;
    if (file->access == MM_LINUX_FAT_APPEND) {
        file->pending = 0;
        const FRESULT result = f_lseek(&file->file, f_size(&file->file));
        if (result != FR_OK) return mm_linux_fat_status(file->volume, result);
    } else if (file->pending) {
        const int status = mm_linux_fat_fill_gap(file);
        if (status != MM_LINUX_FAT_OK) return status;
    }
    UINT moved = 0;
    const FRESULT result = f_write(&file->file, data, (UINT)size, &moved);
    *count = moved;
    if (result != FR_OK)
        return mm_linux_fat_status_as(file->volume, result, MM_LINUX_FAT_DENIED_NO_SPACE);
    return moved < size ? MM_LINUX_FAT_NO_SPACE : MM_LINUX_FAT_OK;
}

// A seek past the end is remembered rather than applied: f_lseek would
// extend the file with undefined contents.
int mm_linux_fat_seek(unsigned int handle, uint64_t offset) {
    mm_linux_fat_file_t* file = mm_linux_fat_file_at(handle);
    if (file == NULL) return MM_LINUX_FAT_BAD_ARGUMENT;
    if (offset > 0xffffffffu) return MM_LINUX_FAT_BAD_ARGUMENT;
    const uint64_t size = f_size(&file->file);
    if (offset > size) {
        const FRESULT result = f_lseek(&file->file, (FSIZE_t)size);
        if (result != FR_OK) return mm_linux_fat_status(file->volume, result);
        file->pending = 1;
        file->pending_offset = offset;
        return MM_LINUX_FAT_OK;
    }
    file->pending = 0;
    return mm_linux_fat_status(file->volume, f_lseek(&file->file, (FSIZE_t)offset));
}

int mm_linux_fat_tell(unsigned int handle, uint64_t* offset) {
    mm_linux_fat_file_t* file = mm_linux_fat_file_at(handle);
    if (file == NULL || offset == NULL) return MM_LINUX_FAT_BAD_ARGUMENT;
    *offset = file->pending ? file->pending_offset : (uint64_t)f_tell(&file->file);
    return MM_LINUX_FAT_OK;
}

// At the current offset; one past the end changes nothing.
int mm_linux_fat_truncate(unsigned int handle) {
    mm_linux_fat_file_t* file = mm_linux_fat_file_at(handle);
    if (file == NULL) return MM_LINUX_FAT_BAD_ARGUMENT;
    if (file->pending) return MM_LINUX_FAT_OK;
    return mm_linux_fat_status(file->volume, f_truncate(&file->file));
}

int mm_linux_fat_sync(unsigned int handle) {
    mm_linux_fat_file_t* file = mm_linux_fat_file_at(handle);
    if (file == NULL) return MM_LINUX_FAT_BAD_ARGUMENT;
    return mm_linux_fat_status(file->volume, f_sync(&file->file));
}

int mm_linux_fat_file_stat(unsigned int handle, mm_linux_fat_entry* stat) {
    mm_linux_fat_file_t* file = mm_linux_fat_file_at(handle);
    if (file == NULL || stat == NULL) return MM_LINUX_FAT_BAD_ARGUMENT;
    // The directory entry is current after a sync; a reader's sync is a
    // no-op.
    if (file->access != MM_LINUX_FAT_READ) {
        const FRESULT synced = f_sync(&file->file);
        if (synced != FR_OK) return mm_linux_fat_status(file->volume, synced);
    }
    FILINFO info;
    const FRESULT result = f_stat(file->path, &info);
    if (result != FR_OK) return mm_linux_fat_status(file->volume, result);
    mm_linux_fat_describe(&mm_linux_fat_volumes[file->volume], &info, stat);
    stat->size = f_size(&file->file);
    return MM_LINUX_FAT_OK;
}

int mm_linux_fat_close(unsigned int handle) {
    mm_linux_fat_file_t* file = mm_linux_fat_file_at(handle);
    if (file == NULL) return MM_LINUX_FAT_BAD_ARGUMENT;
    const FRESULT result = f_close(&file->file);
    file->used = 0;
    return mm_linux_fat_status(file->volume, result);
}

int mm_linux_fat_open_directory(unsigned int volume, const char* path, size_t length,
                               unsigned int* handle) {
    mm_linux_fat_volume_t* v = mm_linux_fat_volume_at(volume);
    if (v == NULL || handle == NULL) return MM_LINUX_FAT_BAD_ARGUMENT;
    char full[MM_LINUX_FAT_PATH];
    const int status = mm_linux_fat_path(full, volume, path, length);
    if (status != MM_LINUX_FAT_OK) return status;
    if (length != 0) {
        FILINFO info;
        const FRESULT found = f_stat(full, &info);
        if (found != FR_OK) return mm_linux_fat_status(volume, found);
        if (!(info.fattrib & AM_DIR)) return MM_LINUX_FAT_NOT_DIRECTORY;
    }
    for (unsigned int i = 0; i < MM_LINUX_FAT_DIRECTORIES; ++i) {
        mm_linux_fat_directory_t* directory = &mm_linux_fat_directories[i];
        if (directory->used) continue;
        const FRESULT result = f_opendir(&directory->directory, full);
        if (result != FR_OK) return mm_linux_fat_status(volume, result);
        directory->used = 1;
        directory->volume = volume;
        directory->has_pending = 0;
        memcpy(directory->path, full, strlen(full) + 1u);
        *handle = i;
        return MM_LINUX_FAT_OK;
    }
    return MM_LINUX_FAT_TOO_MANY;
}

int mm_linux_fat_next(unsigned int handle, char* name, size_t capacity, size_t* length,
                     mm_linux_fat_entry* stat, int* done) {
    if (handle >= MM_LINUX_FAT_DIRECTORIES || !mm_linux_fat_directories[handle].used ||
        length == NULL || stat == NULL || done == NULL || (name == NULL && capacity != 0))
        return MM_LINUX_FAT_BAD_ARGUMENT;
    mm_linux_fat_directory_t* directory = &mm_linux_fat_directories[handle];
    while (!directory->has_pending) {
        const FRESULT result = f_readdir(&directory->directory, &directory->pending);
        if (result != FR_OK) return mm_linux_fat_status(directory->volume, result);
        if (directory->pending.fname[0] == '\0') {
            *done = 1;
            return MM_LINUX_FAT_OK;
        }
        // FatFs filters these already; this keeps the contract if it ever
        // does not.
        if (strcmp(directory->pending.fname, ".") == 0 ||
            strcmp(directory->pending.fname, "..") == 0)
            continue;
        directory->has_pending = 1;
    }
    const size_t name_length = strlen(directory->pending.fname);
    if (name_length > capacity) return MM_LINUX_FAT_NAME_TOO_LONG;
    memcpy(name, directory->pending.fname, name_length);
    mm_linux_fat_describe(&mm_linux_fat_volumes[directory->volume], &directory->pending, stat);
    *length = name_length;
    *done = 0;
    directory->has_pending = 0;
    return MM_LINUX_FAT_OK;
}

int mm_linux_fat_close_directory(unsigned int handle) {
    if (handle >= MM_LINUX_FAT_DIRECTORIES || !mm_linux_fat_directories[handle].used)
        return MM_LINUX_FAT_BAD_ARGUMENT;
    mm_linux_fat_directory_t* directory = &mm_linux_fat_directories[handle];
    const FRESULT result = f_closedir(&directory->directory);
    directory->used = 0;
    return mm_linux_fat_status(directory->volume, result);
}

int mm_linux_fat_stat(unsigned int volume, const char* path, size_t length,
                     mm_linux_fat_entry* stat) {
    mm_linux_fat_volume_t* v = mm_linux_fat_volume_at(volume);
    if (v == NULL || stat == NULL) return MM_LINUX_FAT_BAD_ARGUMENT;
    if (length == 0) {
        stat->directory = 1;
        stat->size = 0;
        stat->modified = 0;
        stat->read_only = v->read_only;
        return MM_LINUX_FAT_OK;
    }
    char full[MM_LINUX_FAT_PATH];
    const int status = mm_linux_fat_path(full, volume, path, length);
    if (status != MM_LINUX_FAT_OK) return status;
    FILINFO info;
    const FRESULT result = f_stat(full, &info);
    if (result != FR_OK) return mm_linux_fat_status(volume, result);
    mm_linux_fat_describe(v, &info, stat);
    return MM_LINUX_FAT_OK;
}

int mm_linux_fat_make_directory(unsigned int volume, const char* path, size_t length) {
    mm_linux_fat_volume_t* v = mm_linux_fat_volume_at(volume);
    if (v == NULL) return MM_LINUX_FAT_BAD_ARGUMENT;
    if (v->read_only) return MM_LINUX_FAT_READ_ONLY;
    if (length == 0) return MM_LINUX_FAT_EXISTS;
    char full[MM_LINUX_FAT_PATH];
    int status = mm_linux_fat_path(full, volume, path, length);
    if (status != MM_LINUX_FAT_OK) return status;
    status = mm_linux_fat_parent_is_directory(volume, full);
    if (status != MM_LINUX_FAT_OK) return status;
    return mm_linux_fat_status_as(volume, f_mkdir(full), MM_LINUX_FAT_DENIED_NO_SPACE);
}

int mm_linux_fat_remove(unsigned int volume, const char* path, size_t length) {
    mm_linux_fat_volume_t* v = mm_linux_fat_volume_at(volume);
    if (v == NULL || length == 0) return MM_LINUX_FAT_BAD_ARGUMENT;
    if (v->read_only) return MM_LINUX_FAT_READ_ONLY;
    char full[MM_LINUX_FAT_PATH];
    const int status = mm_linux_fat_path(full, volume, path, length);
    if (status != MM_LINUX_FAT_OK) return status;
    FILINFO info;
    const FRESULT found = f_stat(full, &info);
    if (found != FR_OK) return mm_linux_fat_status(volume, found);
    if (mm_linux_fat_open_beneath(volume, full)) return MM_LINUX_FAT_BUSY;
    return mm_linux_fat_status_as(volume, f_unlink(full),
                                 (info.fattrib & AM_DIR) ? MM_LINUX_FAT_DENIED_NOT_EMPTY
                                                         : MM_LINUX_FAT_DENIED_READ_ONLY);
}

int mm_linux_fat_rename(unsigned int volume, const char* from, size_t from_length,
                       const char* to, size_t to_length) {
    mm_linux_fat_volume_t* v = mm_linux_fat_volume_at(volume);
    if (v == NULL || from_length == 0 || to_length == 0) return MM_LINUX_FAT_BAD_ARGUMENT;
    if (v->read_only) return MM_LINUX_FAT_READ_ONLY;
    char source[MM_LINUX_FAT_PATH];
    char target[MM_LINUX_FAT_PATH];
    int status = mm_linux_fat_path(source, volume, from, from_length);
    if (status != MM_LINUX_FAT_OK) return status;
    status = mm_linux_fat_path(target, volume, to, to_length);
    if (status != MM_LINUX_FAT_OK) return status;
    FILINFO info;
    FRESULT found = f_stat(source, &info);
    if (found != FR_OK) return mm_linux_fat_status(volume, found);
    found = f_stat(target, &info);
    if (found == FR_OK) return MM_LINUX_FAT_EXISTS;
    if (found != FR_NO_FILE) return mm_linux_fat_status(volume, found);
    status = mm_linux_fat_parent_is_directory(volume, target);
    if (status != MM_LINUX_FAT_OK) return status;
    if (mm_linux_fat_same_or_beneath(target, source)) return MM_LINUX_FAT_BAD_ARGUMENT;
    if (mm_linux_fat_open_beneath(volume, source)) return MM_LINUX_FAT_BUSY;
    // f_rename takes the new name without its drive.
    return mm_linux_fat_status_as(volume, f_rename(source, target + 2),
                                 MM_LINUX_FAT_DENIED_NO_SPACE);
}

int mm_linux_fat_space(unsigned int volume, uint64_t* total, uint64_t* free) {
    mm_linux_fat_volume_t* v = mm_linux_fat_volume_at(volume);
    if (v == NULL || total == NULL || free == NULL) return MM_LINUX_FAT_BAD_ARGUMENT;
    char root[4] = {(char)('0' + volume), ':', '\0', '\0'};
    DWORD clusters = 0;
    FATFS* fs = NULL;
    const FRESULT result = f_getfree(root, &clusters, &fs);
    if (result != FR_OK) return mm_linux_fat_status(volume, result);
    const uint64_t cluster = (uint64_t)fs->csize * MM_LINUX_FAT_BLOCK;
    *total = (uint64_t)(fs->n_fatent - 2u) * cluster;
    *free = (uint64_t)clusters * cluster;
    return MM_LINUX_FAT_OK;
}

int mm_linux_fat_flush(unsigned int volume) {
    mm_linux_fat_volume_t* v = mm_linux_fat_volume_at(volume);
    if (v == NULL) return MM_LINUX_FAT_BAD_ARGUMENT;
    int result = MM_LINUX_FAT_OK;
    for (unsigned int i = 0; i < MM_LINUX_FAT_FILES; ++i) {
        mm_linux_fat_file_t* file = &mm_linux_fat_files[i];
        if (!file->used || file->volume != volume) continue;
        const FRESULT synced = f_sync(&file->file);
        if (synced != FR_OK) result = mm_linux_fat_status(volume, synced);
    }
    return result;
}
