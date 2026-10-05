// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A native harness for the littlefs adapter: adapter.c and littlefs compiled
// for the build machine against a RAM NOR flash, which refuses to program a
// byte that is not erased, and mm.fs's Volume contract checked through the
// private ABI. scripts/test-littlefs.sh builds and runs it; the module build
// compiles no C, so test.sh cannot.
#include "lfs-c.h"
#include <stdio.h>
#include <string.h>

#define ERASE 4096u
#define BLOCKS 64u
static unsigned char flash[ERASE * BLOCKS];
static unsigned long programs_over_unerased;
static int fail_reads;

static int geometry(void* c, uint32_t* r, uint32_t* p, uint32_t* e, uint32_t* n) {
    (void)c; *r = 1; *p = 256; *e = ERASE; *n = BLOCKS; return MM_LINUX_LFS_OK;
}
static int rd(void* c, uint64_t off, void* d, size_t s) {
    (void)c; if (fail_reads) return MM_LINUX_LFS_TIMEOUT;
    if (off + s > sizeof flash) return MM_LINUX_LFS_BAD_ARGUMENT;
    memcpy(d, flash + off, s); return MM_LINUX_LFS_OK;
}
static int pr(void* c, uint64_t off, const void* d, size_t s) {
    (void)c; if (off % 256 || s % 256 || off + s > sizeof flash) return MM_LINUX_LFS_BAD_ARGUMENT;
    for (size_t i = 0; i < s; ++i) if (flash[off + i] != 0xff) { ++programs_over_unerased; return MM_LINUX_LFS_BAD_ARGUMENT; }
    memcpy(flash + off, d, s); return MM_LINUX_LFS_OK;
}
static int er(void* c, uint64_t off, uint64_t s) {
    (void)c; if (off % ERASE || s % ERASE || off + s > sizeof flash) return MM_LINUX_LFS_BAD_ARGUMENT;
    memset(flash + off, 0xff, s); return MM_LINUX_LFS_OK;
}
static int sy(void* c) { (void)c; return MM_LINUX_LFS_OK; }
static const mm_linux_lfs_device device = {0, geometry, rd, pr, er, sy};
static uint64_t clock_value = 1780000000u;
static uint64_t now(void) { return clock_value; }

static int failures, checks;
#define CHECK(cond, what) do { ++checks; if (!(cond)) { ++failures; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what); } } while (0)
#define P(s) s, strlen(s)

static unsigned vol;
static int write_file(const char* path, const char* text, int disposition) {
    unsigned h;
    int s = mm_linux_lfs_open(vol, P(path), MM_LINUX_LFS_WRITE, disposition, &h);
    if (s) return s;
    size_t n = 0;
    s = mm_linux_lfs_write(h, text, strlen(text), &n);
    if (s || n != strlen(text)) return 99;
    return mm_linux_lfs_close(h);
}
static int holds(const char* path, const char* text) {
    unsigned h; char buf[128]; size_t n = 0;
    if (mm_linux_lfs_open(vol, P(path), MM_LINUX_LFS_READ, MM_LINUX_LFS_OPEN_EXISTING, &h)) return 0;
    if (mm_linux_lfs_read(h, buf, sizeof buf, &n)) return 0;
    mm_linux_lfs_close(h);
    return n == strlen(text) && memcmp(buf, text, n) == 0;
}

int main(void) {
    memset(flash, 0xff, sizeof flash);
    mm_linux_lfs_set_clock(now);
    unsigned v2;
    CHECK(mm_linux_lfs_attach(&device, 0, 0, 500, &v2) == MM_LINUX_LFS_CORRUPT, "a blank device without format_if_blank is Corrupt");
    CHECK(mm_linux_lfs_attach(&device, 1, 1, 500, &v2) == MM_LINUX_LFS_READ_ONLY, "a read-only blank device cannot be formatted");
    CHECK(mm_linux_lfs_attach(&device, 0, 1, 500, &vol) == MM_LINUX_LFS_OK, "a blank device is formatted and mounted");

    unsigned h, h2; size_t n; uint64_t off; mm_linux_lfs_entry st; char buf[300];
    CHECK(mm_linux_lfs_open(vol, P("a"), MM_LINUX_LFS_WRITE, MM_LINUX_LFS_CREATE_NEW, &h) == 0 && mm_linux_lfs_close(h) == 0, "CreateNew creates");
    CHECK(mm_linux_lfs_stat(vol, P("a"), &st) == 0 && !st.directory && st.size == 0 && st.modified == clock_value, "created empty, stamped");
    CHECK(write_file("p", "x", MM_LINUX_LFS_CREATE_OR_TRUNCATE) == 0, "write p");
    CHECK(mm_linux_lfs_open(vol, P("p"), MM_LINUX_LFS_WRITE, MM_LINUX_LFS_CREATE_NEW, &h) == MM_LINUX_LFS_EXISTS, "CreateNew on present is Exists");
    CHECK(mm_linux_lfs_open(vol, P("absent"), MM_LINUX_LFS_READ, MM_LINUX_LFS_OPEN_EXISTING, &h) == MM_LINUX_LFS_NOT_FOUND, "OpenExisting absent");
    CHECK(mm_linux_lfs_open(vol, P("nodir/f"), MM_LINUX_LFS_WRITE, MM_LINUX_LFS_CREATE_NEW, &h) == MM_LINUX_LFS_NOT_FOUND, "missing parent");

    CHECK(mm_linux_lfs_open(vol, P("rt"), MM_LINUX_LFS_READ_WRITE, MM_LINUX_LFS_CREATE_NEW, &h) == 0, "rw open");
    CHECK(mm_linux_lfs_write(h, "hello", 5, &n) == 0 && n == 5, "write hello");
    CHECK(mm_linux_lfs_tell(h, &off) == 0 && off == 5, "tell 5");
    CHECK(mm_linux_lfs_seek(h, 0) == 0 && mm_linux_lfs_read(h, buf, 5, &n) == 0 && n == 5 && !memcmp(buf, "hello", 5), "read back");
    CHECK(mm_linux_lfs_read(h, buf, 5, &n) == 0 && n == 0, "eof read is zero");
    CHECK(mm_linux_lfs_close(h) == 0, "close");

    CHECK(write_file("ap", "hello", MM_LINUX_LFS_CREATE_OR_TRUNCATE) == 0, "ap");
    CHECK(mm_linux_lfs_open(vol, P("ap"), MM_LINUX_LFS_APPEND, MM_LINUX_LFS_OPEN_EXISTING, &h) == 0 && mm_linux_lfs_seek(h, 0) == 0 && mm_linux_lfs_write(h, " world", 6, &n) == 0 && mm_linux_lfs_close(h) == 0, "append after seek");
    CHECK(holds("ap", "hello world"), "append at end");

    CHECK(write_file("ex", "ab", MM_LINUX_LFS_CREATE_OR_TRUNCATE) == 0, "ex");
    CHECK(mm_linux_lfs_open(vol, P("ex"), MM_LINUX_LFS_READ_WRITE, MM_LINUX_LFS_OPEN_EXISTING, &h) == 0 && mm_linux_lfs_seek(h, 5) == 0 && mm_linux_lfs_write(h, "z", 1, &n) == 0, "write past end");
    CHECK(mm_linux_lfs_file_stat(h, &st) == 0 && st.size == 6, "extended to 6");
    CHECK(mm_linux_lfs_seek(h, 0) == 0 && mm_linux_lfs_read(h, buf, 6, &n) == 0 && n == 6 && buf[2] == 0 && buf[4] == 0 && buf[5] == 'z', "zero filled");
    CHECK(mm_linux_lfs_seek(h, 2) == 0 && mm_linux_lfs_truncate(h) == 0 && mm_linux_lfs_file_stat(h, &st) == 0 && st.size == 2, "truncate at offset");
    CHECK(mm_linux_lfs_close(h) == 0 && holds("ex", "ab"), "truncated content");

    CHECK(write_file("em", "something", MM_LINUX_LFS_CREATE_OR_TRUNCATE) == 0, "em");
    CHECK(mm_linux_lfs_open(vol, P("em"), MM_LINUX_LFS_READ, MM_LINUX_LFS_CREATE_OR_TRUNCATE, &h) == 0 && mm_linux_lfs_file_stat(h, &st) == 0 && st.size == 0 && mm_linux_lfs_close(h) == 0, "read + CreateOrTruncate empties");
    CHECK(mm_linux_lfs_open(vol, P("rc"), MM_LINUX_LFS_READ, MM_LINUX_LFS_OPEN_OR_CREATE, &h) == 0 && mm_linux_lfs_close(h) == 0 && mm_linux_lfs_stat(vol, P("rc"), &st) == 0, "read + OpenOrCreate creates");

    clock_value += 100;
    CHECK(mm_linux_lfs_open(vol, P("p"), MM_LINUX_LFS_WRITE, MM_LINUX_LFS_OPEN_EXISTING, &h) == 0 && mm_linux_lfs_close(h) == 0, "open p for write, close unchanged");
    CHECK(mm_linux_lfs_stat(vol, P("p"), &st) == 0 && st.modified == clock_value - 100, "an unchanged write open keeps the timestamp");
    CHECK(write_file("p", "y", MM_LINUX_LFS_OPEN_EXISTING) == 0 && mm_linux_lfs_stat(vol, P("p"), &st) == 0 && st.modified == clock_value, "a write updates the timestamp");

    CHECK(mm_linux_lfs_make_directory(vol, P("d")) == 0, "mkdir");
    CHECK(mm_linux_lfs_make_directory(vol, P("d")) == MM_LINUX_LFS_EXISTS, "mkdir exists");
    CHECK(mm_linux_lfs_make_directory(vol, P("x/y")) == MM_LINUX_LFS_NOT_FOUND, "mkdir missing parent");
    CHECK(mm_linux_lfs_open(vol, P("d"), MM_LINUX_LFS_READ, MM_LINUX_LFS_OPEN_EXISTING, &h) == MM_LINUX_LFS_IS_DIRECTORY, "open dir");
    CHECK(mm_linux_lfs_open_directory(vol, P("p"), &h) == MM_LINUX_LFS_NOT_DIRECTORY, "opendir file");
    CHECK(write_file("d/first", "1", MM_LINUX_LFS_CREATE_NEW) == 0 && write_file("d/second", "22", MM_LINUX_LFS_CREATE_NEW) == 0 && mm_linux_lfs_make_directory(vol, P("d/inner")) == 0, "populate d");
    CHECK(mm_linux_lfs_open_directory(vol, P("d"), &h) == 0, "opendir d");
    int done = 0, entries = 0, f1 = 0, f2 = 0, f3 = 0; char small[4]; size_t len;
    CHECK(mm_linux_lfs_next(h, small, sizeof small, &len, &st, &done) == MM_LINUX_LFS_NAME_TOO_LONG, "small buffer");
    for (;;) {
        int s = mm_linux_lfs_next(h, buf, 255, &len, &st, &done);
        if (s || done) { CHECK(s == 0, "next ok"); break; }
        ++entries; buf[len] = 0;
        if (!strcmp(buf, "first")) f1 = !st.directory && st.size == 1;
        else if (!strcmp(buf, "second")) f2 = !st.directory && st.size == 2;
        else if (!strcmp(buf, "inner")) f3 = st.directory;
        else { printf("unexpected entry %s\n", buf); CHECK(0, "unexpected entry"); }
    }
    CHECK(entries == 3 && f1 && f2 && f3, "listing without . and .., retry kept the entry");
    CHECK(mm_linux_lfs_close_directory(h) == 0, "closedir");
    CHECK(mm_linux_lfs_open_directory(vol, "", 0, &h) == 0 && mm_linux_lfs_close_directory(h) == 0, "root listing opens");

    CHECK(mm_linux_lfs_remove(vol, P("d")) == MM_LINUX_LFS_NOT_EMPTY, "remove non-empty");
    CHECK(mm_linux_lfs_remove(vol, P("never")) == MM_LINUX_LFS_NOT_FOUND, "remove missing");
    CHECK(mm_linux_lfs_remove(vol, P("d/inner")) == 0, "remove empty dir");

    CHECK(write_file("rf", "moved", MM_LINUX_LFS_CREATE_NEW) == 0 && write_file("rt2", "stays", MM_LINUX_LFS_CREATE_NEW) == 0, "rename fixtures");
    CHECK(mm_linux_lfs_rename(vol, P("rf"), P("rt2")) == MM_LINUX_LFS_EXISTS && holds("rt2", "stays"), "rename onto existing refused");
    CHECK(mm_linux_lfs_rename(vol, P("rf"), P("rto")) == 0 && holds("rto", "moved") && mm_linux_lfs_stat(vol, P("rf"), &st) == MM_LINUX_LFS_NOT_FOUND, "rename moves");
    CHECK(mm_linux_lfs_rename(vol, P("d"), P("d/sub")) == MM_LINUX_LFS_BAD_ARGUMENT, "rename into itself");

    CHECK(mm_linux_lfs_open(vol, P("ex"), MM_LINUX_LFS_WRITE, MM_LINUX_LFS_OPEN_EXISTING, &h) == 0, "writer open");
    CHECK(mm_linux_lfs_open(vol, P("ex"), MM_LINUX_LFS_READ, MM_LINUX_LFS_OPEN_EXISTING, &h2) == MM_LINUX_LFS_BUSY, "second open busy");
    CHECK(mm_linux_lfs_remove(vol, P("ex")) == MM_LINUX_LFS_BUSY && mm_linux_lfs_rename(vol, P("ex"), P("ey")) == MM_LINUX_LFS_BUSY, "remove/rename busy");
    CHECK(mm_linux_lfs_close(h) == 0, "writer closed");
    CHECK(mm_linux_lfs_open(vol, P("ex"), MM_LINUX_LFS_READ, MM_LINUX_LFS_OPEN_EXISTING, &h) == 0 && mm_linux_lfs_open(vol, P("ex"), MM_LINUX_LFS_READ, MM_LINUX_LFS_OPEN_EXISTING, &h2) == 0, "two readers");
    unsigned h3; CHECK(mm_linux_lfs_open(vol, P("ex"), MM_LINUX_LFS_WRITE, MM_LINUX_LFS_OPEN_EXISTING, &h3) == MM_LINUX_LFS_BUSY, "writer while readers busy");
    CHECK(mm_linux_lfs_close(h) == 0 && mm_linux_lfs_close(h2) == 0, "readers closed");
    CHECK(mm_linux_lfs_make_directory(vol, P("e")) == 0 && mm_linux_lfs_open_directory(vol, P("e"), &h) == 0 && mm_linux_lfs_remove(vol, P("e")) == MM_LINUX_LFS_BUSY, "an open directory cannot be removed");
    CHECK(mm_linux_lfs_rename(vol, P("e"), P("e2")) == MM_LINUX_LFS_BUSY, "or renamed");
    CHECK(mm_linux_lfs_close_directory(h) == 0 && mm_linux_lfs_remove(vol, P("e")) == 0, "once closed it can");

    uint64_t total, free_bytes;
    CHECK(mm_linux_lfs_space(vol, &total, &free_bytes) == 0 && total == ERASE * BLOCKS && free_bytes < total, "space");

    unsigned pool[9]; int opened = 0;
    for (int i = 0; i < 9; ++i) { char name[8]; snprintf(name, sizeof name, "f%d", i); if (mm_linux_lfs_open(vol, P(name), MM_LINUX_LFS_WRITE, MM_LINUX_LFS_OPEN_OR_CREATE, &pool[i]) == 0) ++opened; else CHECK(i == 8, "only the ninth fails"); }
    CHECK(opened == 8, "eight files open, the ninth is TooMany");
    for (int i = 0; i < 8; ++i) mm_linux_lfs_close(pool[i]);

    // Fill the volume.
    CHECK(mm_linux_lfs_open(vol, P("big"), MM_LINUX_LFS_WRITE, MM_LINUX_LFS_CREATE_NEW, &h) == 0, "big open");
    static char chunk[4096]; memset(chunk, 'b', sizeof chunk); int s = 0; size_t written = 0;
    for (int i = 0; i < 200 && s == 0; ++i) { s = mm_linux_lfs_write(h, chunk, sizeof chunk, &n); written += n; }
    if (s == 0) s = mm_linux_lfs_sync(h);
    CHECK(s == MM_LINUX_LFS_NO_SPACE, "filling the volume is NoSpace");
    mm_linux_lfs_close(h);
    mm_linux_lfs_remove(vol, P("big"));

    // Remount: data survives.
    CHECK(mm_linux_lfs_detach(vol) == 0, "detach");
    CHECK(mm_linux_lfs_attach(&device, 0, 0, 500, &vol) == 0, "remount without formatting");
    CHECK(holds("rto", "moved") && holds("ap", "hello world"), "data survives a remount");
    CHECK(mm_linux_lfs_stat(vol, P("p"), &st) == 0 && st.modified == clock_value, "timestamps survive");
    CHECK(mm_linux_lfs_detach(vol) == 0, "detach");

    // Read-only mount.
    CHECK(mm_linux_lfs_attach(&device, 1, 0, 500, &vol) == 0, "read-only mount");
    CHECK(mm_linux_lfs_open(vol, P("rto"), MM_LINUX_LFS_WRITE, MM_LINUX_LFS_OPEN_EXISTING, &h) == MM_LINUX_LFS_READ_ONLY && mm_linux_lfs_make_directory(vol, P("z")) == MM_LINUX_LFS_READ_ONLY && mm_linux_lfs_remove(vol, P("rto")) == MM_LINUX_LFS_READ_ONLY, "read-only refusals");
    CHECK(holds("rto", "moved"), "read-only reads");
    CHECK(mm_linux_lfs_detach(vol) == 0, "detach");

    // A device fault surfaces as the device's own answer.
    fail_reads = 1;
    CHECK(mm_linux_lfs_attach(&device, 0, 1, 500, &vol) == MM_LINUX_LFS_TIMEOUT, "a device timeout is Timeout, and nothing is formatted");
    fail_reads = 0;

    // A damaged volume is Corrupt and never erased.
    memset(flash, 0x00, 2 * ERASE);
    CHECK(mm_linux_lfs_attach(&device, 0, 1, 500, &vol) == MM_LINUX_LFS_CORRUPT, "a non-blank unmountable device is Corrupt");
    CHECK(flash[0] == 0x00 && flash[ERASE + 5] == 0x00, "and is not erased");

    CHECK(programs_over_unerased == 0, "littlefs never programmed unerased flash through the adapter");
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
