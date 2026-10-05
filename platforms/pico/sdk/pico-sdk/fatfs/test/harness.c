// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A native harness for the FatFs adapter: adapter.c and the pinned FatFs
// compiled for the build machine against a RAM block device, and mm.fs's
// Volume contract checked through the private ABI, with the FAT-specific
// cases besides. scripts/test-fatfs.sh builds and runs it; the module build
// compiles no C, so test.sh cannot.
#include "fat-c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK 512u
static unsigned char* disk;
static uint64_t blocks;
static unsigned long writes;
static int fail_reads;

static int geometry(void* c, uint64_t* n, unsigned int* s) {
    (void)c;
    *n = blocks;
    *s = BLOCK;
    return MM_PICO_FAT_OK;
}
static int rd(void* c, uint64_t block, void* d, size_t s) {
    (void)c;
    if (fail_reads) return MM_PICO_FAT_TIMEOUT;
    if (s % BLOCK || block + s / BLOCK > blocks) return MM_PICO_FAT_BAD_ARGUMENT;
    memcpy(d, disk + block * BLOCK, s);
    return MM_PICO_FAT_OK;
}
static int wr(void* c, uint64_t block, const void* d, size_t s) {
    (void)c;
    if (s % BLOCK || block + s / BLOCK > blocks) return MM_PICO_FAT_BAD_ARGUMENT;
    memcpy(disk + block * BLOCK, d, s);
    ++writes;
    return MM_PICO_FAT_OK;
}
static int sy(void* c) {
    (void)c;
    return MM_PICO_FAT_OK;
}
static const mm_pico_fat_device device = {0, geometry, rd, wr, sy};
static uint64_t clock_value = 1780000000u;
static uint64_t now(void) { return clock_value; }

static int failures, checks;
#define CHECK(cond, what)                                                        \
    do {                                                                         \
        ++checks;                                                                \
        if (!(cond)) {                                                           \
            ++failures;                                                          \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what);                 \
        }                                                                        \
    } while (0)
#define P(s) s, strlen(s)

static unsigned vol;
static int write_file(const char* path, const char* text, int disposition) {
    unsigned h;
    int s = mm_pico_fat_open(vol, P(path), MM_PICO_FAT_WRITE, disposition, &h);
    if (s) return s;
    size_t n = 0;
    s = mm_pico_fat_write(h, text, strlen(text), &n);
    if (s || n != strlen(text)) return 99;
    return mm_pico_fat_close(h);
}
static int holds(const char* path, const char* text) {
    unsigned h;
    char buf[128];
    size_t n = 0;
    if (mm_pico_fat_open(vol, P(path), MM_PICO_FAT_READ, MM_PICO_FAT_OPEN_EXISTING, &h)) return 0;
    if (mm_pico_fat_read(h, buf, sizeof buf, &n)) return 0;
    mm_pico_fat_close(h);
    return n == strlen(text) && memcmp(buf, text, n) == 0;
}

// A disk full of 0xA5, as a used card is full of old data: formatting writes
// only FAT's own structures, so any byte a file has that nothing wrote reads
// as 0xA5, and a gap FatFs left undefined cannot pass for zeros.
static void use_disk(uint64_t count) {
    free(disk);
    blocks = count;
    disk = malloc(blocks * BLOCK);
    memset(disk, 0xa5, blocks * BLOCK);
}

int main(void) {
    mm_pico_fat_set_clock(now);
    use_disk(16384);    // 8 MiB: FAT16 when formatted automatically

    unsigned h, h2;
    size_t n;
    uint64_t off;
    mm_pico_fat_entry st;
    char buf[300];
    writes = 0;
    CHECK(mm_pico_fat_attach(&device, 0, &vol) == MM_PICO_FAT_CORRUPT,
          "an unformatted device is Corrupt");
    CHECK(writes == 0, "and nothing was written to it");
    CHECK(mm_pico_fat_format(&device, MM_PICO_FAT_FORMAT_AUTOMATIC) == 0, "format");
    CHECK(mm_pico_fat_attach(&device, 0, &vol) == 0, "a formatted device mounts");
    CHECK(mm_pico_fat_stat(vol, "", 0, &st) == 0 && st.directory, "the root has a stat");

    CHECK(mm_pico_fat_open(vol, P("a"), MM_PICO_FAT_WRITE, MM_PICO_FAT_CREATE_NEW, &h) == 0 &&
              mm_pico_fat_close(h) == 0,
          "CreateNew creates");
    CHECK(mm_pico_fat_stat(vol, P("a"), &st) == 0 && !st.directory && st.size == 0 &&
              st.modified == clock_value,
          "created empty, stamped to the clock");
    CHECK(write_file("p", "x", MM_PICO_FAT_CREATE_OR_TRUNCATE) == 0, "write p");
    CHECK(mm_pico_fat_open(vol, P("p"), MM_PICO_FAT_WRITE, MM_PICO_FAT_CREATE_NEW, &h) ==
              MM_PICO_FAT_EXISTS,
          "CreateNew on present is Exists");
    CHECK(mm_pico_fat_open(vol, P("absent"), MM_PICO_FAT_READ, MM_PICO_FAT_OPEN_EXISTING, &h) ==
              MM_PICO_FAT_NOT_FOUND,
          "OpenExisting absent");
    CHECK(mm_pico_fat_open(vol, P("nodir/f"), MM_PICO_FAT_WRITE, MM_PICO_FAT_CREATE_NEW, &h) ==
              MM_PICO_FAT_NOT_FOUND,
          "missing parent");

    CHECK(mm_pico_fat_open(vol, P("rt"), MM_PICO_FAT_READ_WRITE, MM_PICO_FAT_CREATE_NEW, &h) == 0,
          "rw open");
    CHECK(mm_pico_fat_write(h, "hello", 5, &n) == 0 && n == 5, "write hello");
    CHECK(mm_pico_fat_tell(h, &off) == 0 && off == 5, "tell 5");
    CHECK(mm_pico_fat_seek(h, 0) == 0 && mm_pico_fat_read(h, buf, 5, &n) == 0 && n == 5 &&
              !memcmp(buf, "hello", 5),
          "read back");
    CHECK(mm_pico_fat_read(h, buf, 5, &n) == 0 && n == 0, "eof read is zero");
    CHECK(mm_pico_fat_close(h) == 0, "close");

    CHECK(write_file("ap", "hello", MM_PICO_FAT_CREATE_OR_TRUNCATE) == 0, "ap");
    CHECK(mm_pico_fat_open(vol, P("ap"), MM_PICO_FAT_APPEND, MM_PICO_FAT_OPEN_EXISTING, &h) == 0 &&
              mm_pico_fat_seek(h, 0) == 0 && mm_pico_fat_write(h, " wor", 4, &n) == 0 &&
              mm_pico_fat_seek(h, 1) == 0 && mm_pico_fat_write(h, "ld", 2, &n) == 0 &&
              mm_pico_fat_close(h) == 0,
          "appends after seeks");
    CHECK(holds("ap", "hello world"), "every append lands at the end");

    CHECK(write_file("ex", "ab", MM_PICO_FAT_CREATE_OR_TRUNCATE) == 0, "ex");
    CHECK(mm_pico_fat_open(vol, P("ex"), MM_PICO_FAT_READ_WRITE, MM_PICO_FAT_OPEN_EXISTING, &h) ==
                  0 &&
              mm_pico_fat_seek(h, 5) == 0 && mm_pico_fat_tell(h, &off) == 0 && off == 5,
          "seek past the end is remembered");
    CHECK(mm_pico_fat_read(h, buf, 4, &n) == 0 && n == 0, "reading past the end gives nothing");
    CHECK(mm_pico_fat_write(h, "z", 1, &n) == 0, "write past end");
    CHECK(mm_pico_fat_file_stat(h, &st) == 0 && st.size == 6, "extended to 6");
    CHECK(mm_pico_fat_seek(h, 0) == 0 && mm_pico_fat_read(h, buf, 6, &n) == 0 && n == 6 &&
              buf[2] == 0 && buf[3] == 0 && buf[4] == 0 && buf[5] == 'z',
          "the gap reads as zeros");
    CHECK(mm_pico_fat_seek(h, 2) == 0 && mm_pico_fat_truncate(h) == 0 &&
              mm_pico_fat_file_stat(h, &st) == 0 && st.size == 2,
          "truncate at offset");
    CHECK(mm_pico_fat_close(h) == 0 && holds("ex", "ab"), "truncated content");

    CHECK(write_file("em", "something", MM_PICO_FAT_CREATE_OR_TRUNCATE) == 0, "em");
    CHECK(mm_pico_fat_open(vol, P("em"), MM_PICO_FAT_READ, MM_PICO_FAT_CREATE_OR_TRUNCATE, &h) ==
                  0 &&
              mm_pico_fat_file_stat(h, &st) == 0 && st.size == 0 && mm_pico_fat_close(h) == 0,
          "read + CreateOrTruncate empties");
    CHECK(mm_pico_fat_open(vol, P("rc"), MM_PICO_FAT_READ, MM_PICO_FAT_OPEN_OR_CREATE, &h) == 0 &&
              mm_pico_fat_close(h) == 0 && mm_pico_fat_stat(vol, P("rc"), &st) == 0,
          "read + OpenOrCreate creates");

    clock_value += 100;
    CHECK(write_file("p", "y", MM_PICO_FAT_OPEN_EXISTING) == 0 &&
              mm_pico_fat_stat(vol, P("p"), &st) == 0 && st.modified == clock_value,
          "a write restamps the file");

    CHECK(mm_pico_fat_make_directory(vol, P("d")) == 0, "mkdir");
    CHECK(mm_pico_fat_make_directory(vol, P("d")) == MM_PICO_FAT_EXISTS, "mkdir exists");
    CHECK(mm_pico_fat_make_directory(vol, P("x/y")) == MM_PICO_FAT_NOT_FOUND, "mkdir missing parent");
    CHECK(mm_pico_fat_open(vol, P("d"), MM_PICO_FAT_READ, MM_PICO_FAT_OPEN_EXISTING, &h) ==
              MM_PICO_FAT_IS_DIRECTORY,
          "open dir");
    CHECK(mm_pico_fat_open_directory(vol, P("p"), &h) == MM_PICO_FAT_NOT_DIRECTORY, "opendir file");
    CHECK(write_file("d/first", "1", MM_PICO_FAT_CREATE_NEW) == 0 &&
              write_file("d/second", "22", MM_PICO_FAT_CREATE_NEW) == 0 &&
              mm_pico_fat_make_directory(vol, P("d/inner")) == 0,
          "populate d");
    CHECK(mm_pico_fat_open_directory(vol, P("d"), &h) == 0, "opendir d");
    int done = 0, entries = 0, f1 = 0, f2 = 0, f3 = 0;
    char small[4];
    size_t len;
    CHECK(mm_pico_fat_next(h, small, sizeof small, &len, &st, &done) == MM_PICO_FAT_NAME_TOO_LONG,
          "small buffer");
    for (;;) {
        int s = mm_pico_fat_next(h, buf, 255, &len, &st, &done);
        if (s || done) {
            CHECK(s == 0, "next ok");
            break;
        }
        ++entries;
        buf[len] = 0;
        if (!strcmp(buf, "first")) f1 = !st.directory && st.size == 1;
        else if (!strcmp(buf, "second")) f2 = !st.directory && st.size == 2;
        else if (!strcmp(buf, "inner")) f3 = st.directory;
        else {
            printf("unexpected entry %s\n", buf);
            CHECK(0, "unexpected entry");
        }
    }
    CHECK(entries == 3 && f1 && f2 && f3, "listing without . and .., retry kept the entry");
    CHECK(mm_pico_fat_close_directory(h) == 0, "closedir");
    CHECK(mm_pico_fat_open_directory(vol, "", 0, &h) == 0 && mm_pico_fat_close_directory(h) == 0,
          "root listing opens");
    CHECK(write_file("a-much-longer-name.txt", "long", MM_PICO_FAT_CREATE_NEW) == 0 &&
              holds("a-much-longer-name.txt", "long"),
          "long file names");

    CHECK(mm_pico_fat_remove(vol, P("d")) == MM_PICO_FAT_NOT_EMPTY, "remove non-empty");
    CHECK(mm_pico_fat_remove(vol, P("never")) == MM_PICO_FAT_NOT_FOUND, "remove missing");
    CHECK(mm_pico_fat_remove(vol, P("d/inner")) == 0, "remove empty dir");

    CHECK(write_file("rf", "moved", MM_PICO_FAT_CREATE_NEW) == 0 &&
              write_file("rt2", "stays", MM_PICO_FAT_CREATE_NEW) == 0,
          "rename fixtures");
    CHECK(mm_pico_fat_rename(vol, P("rf"), P("rt2")) == MM_PICO_FAT_EXISTS && holds("rt2", "stays"),
          "rename onto existing refused");
    CHECK(mm_pico_fat_rename(vol, P("rf"), P("rto")) == 0 && holds("rto", "moved") &&
              mm_pico_fat_stat(vol, P("rf"), &st) == MM_PICO_FAT_NOT_FOUND,
          "rename moves");
    CHECK(mm_pico_fat_rename(vol, P("rto"), P("d/rto")) == 0 && holds("d/rto", "moved"),
          "rename into a directory");
    CHECK(mm_pico_fat_rename(vol, P("d"), P("d/sub")) == MM_PICO_FAT_BAD_ARGUMENT,
          "rename into itself");

    CHECK(mm_pico_fat_open(vol, P("ex"), MM_PICO_FAT_WRITE, MM_PICO_FAT_OPEN_EXISTING, &h) == 0,
          "writer open");
    CHECK(mm_pico_fat_open(vol, P("ex"), MM_PICO_FAT_READ, MM_PICO_FAT_OPEN_EXISTING, &h2) ==
              MM_PICO_FAT_BUSY,
          "second open busy");
    CHECK(mm_pico_fat_open(vol, P("EX"), MM_PICO_FAT_READ, MM_PICO_FAT_OPEN_EXISTING, &h2) ==
              MM_PICO_FAT_BUSY,
          "the same file under another case is busy too, through FatFs's lock");
    CHECK(mm_pico_fat_remove(vol, P("ex")) == MM_PICO_FAT_BUSY &&
              mm_pico_fat_rename(vol, P("ex"), P("ey")) == MM_PICO_FAT_BUSY,
          "remove/rename busy");
    CHECK(mm_pico_fat_close(h) == 0, "writer closed");
    CHECK(mm_pico_fat_open(vol, P("ex"), MM_PICO_FAT_READ, MM_PICO_FAT_OPEN_EXISTING, &h) == 0 &&
              mm_pico_fat_open(vol, P("ex"), MM_PICO_FAT_READ, MM_PICO_FAT_OPEN_EXISTING, &h2) == 0,
          "two readers");
    unsigned h3;
    CHECK(mm_pico_fat_open(vol, P("ex"), MM_PICO_FAT_WRITE, MM_PICO_FAT_OPEN_EXISTING, &h3) ==
              MM_PICO_FAT_BUSY,
          "writer while readers busy");
    CHECK(mm_pico_fat_close(h) == 0 && mm_pico_fat_close(h2) == 0, "readers closed");
    CHECK(mm_pico_fat_make_directory(vol, P("e")) == 0 && mm_pico_fat_open_directory(vol, P("e"), &h) == 0 &&
              mm_pico_fat_remove(vol, P("e")) == MM_PICO_FAT_BUSY &&
              mm_pico_fat_rename(vol, P("e"), P("e2")) == MM_PICO_FAT_BUSY,
          "an open directory cannot be removed or renamed");
    CHECK(mm_pico_fat_close_directory(h) == 0 && mm_pico_fat_remove(vol, P("e")) == 0,
          "once closed it can");

    uint64_t total, free_bytes;
    CHECK(mm_pico_fat_space(vol, &total, &free_bytes) == 0 && total > 7u * 1024 * 1024 &&
              free_bytes < total,
          "space");

    unsigned pool[9];
    int opened = 0;
    for (int i = 0; i < 9; ++i) {
        char name[8];
        snprintf(name, sizeof name, "f%d", i);
        if (mm_pico_fat_open(vol, P(name), MM_PICO_FAT_WRITE, MM_PICO_FAT_OPEN_OR_CREATE, &pool[i]) == 0)
            ++opened;
        else
            CHECK(i == 8, "only the ninth fails");
    }
    CHECK(opened == 8, "eight files open, the ninth is TooMany");
    for (int i = 0; i < 8; ++i) mm_pico_fat_close(pool[i]);

    CHECK(mm_pico_fat_open(vol, P("big"), MM_PICO_FAT_WRITE, MM_PICO_FAT_CREATE_NEW, &h) == 0,
          "big open");
    static char chunk[4096];
    memset(chunk, 'b', sizeof chunk);
    int s = 0;
    for (int i = 0; i < 4000 && s == 0; ++i) s = mm_pico_fat_write(h, chunk, sizeof chunk, &n);
    CHECK(s == MM_PICO_FAT_NO_SPACE && n < sizeof chunk, "filling the volume is NoSpace with a short count");
    mm_pico_fat_close(h);
    CHECK(mm_pico_fat_remove(vol, P("big")) == 0, "the big file goes");

    CHECK(mm_pico_fat_detach(vol) == 0, "detach");
    CHECK(mm_pico_fat_attach(&device, 0, &vol) == 0, "remount");
    CHECK(holds("d/rto", "moved") && holds("ap", "hello world"), "data survives a remount");
    CHECK(mm_pico_fat_stat(vol, P("p"), &st) == 0 && st.modified == clock_value,
          "timestamps survive");
    CHECK(mm_pico_fat_detach(vol) == 0, "detach");

    CHECK(mm_pico_fat_attach(&device, 1, &vol) == 0, "read-only mount");
    CHECK(mm_pico_fat_open(vol, P("ap"), MM_PICO_FAT_WRITE, MM_PICO_FAT_OPEN_EXISTING, &h) ==
                  MM_PICO_FAT_READ_ONLY &&
              mm_pico_fat_make_directory(vol, P("z")) == MM_PICO_FAT_READ_ONLY &&
              mm_pico_fat_remove(vol, P("ap")) == MM_PICO_FAT_READ_ONLY,
          "read-only refusals");
    CHECK(holds("ap", "hello world"), "read-only reads");
    CHECK(mm_pico_fat_detach(vol) == 0, "detach");

    fail_reads = 1;
    CHECK(mm_pico_fat_attach(&device, 0, &vol) == MM_PICO_FAT_TIMEOUT,
          "a device timeout is Timeout");
    fail_reads = 0;

    use_disk(131072);    // 64 MiB
    CHECK(mm_pico_fat_format(&device, MM_PICO_FAT_FORMAT_FAT32) == 0 &&
              mm_pico_fat_attach(&device, 0, &vol) == 0,
          "a 64 MiB device formats as FAT32 and mounts");
    CHECK(mm_pico_fat_space(vol, &total, &free_bytes) == 0 && total > 60u * 1024 * 1024,
          "with its space");
    CHECK(write_file("x", "on fat32", MM_PICO_FAT_CREATE_NEW) == 0 && holds("x", "on fat32"),
          "and holds files");
    CHECK(mm_pico_fat_detach(vol) == 0, "detach");
    CHECK(mm_pico_fat_format(&device, 99) == MM_PICO_FAT_BAD_ARGUMENT, "an unknown format is refused");

    printf("%d checks, %d failures\n", checks, failures);
    free(disk);
    return failures != 0;
}
