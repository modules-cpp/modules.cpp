// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.fs:status;

export namespace mm::fs {

// One vocabulary for every volume, so a program written against a FAT card
// handles a littlefs flash region or a Linux directory without a second table.
// A driver maps its own errors onto these and nothing else.
enum class Status {
    Ok,
    BadArgument,     // a malformed path, a call on a closed File, an access mismatch
    Unsupported,     // the volume or the platform does not do this
    NotFound,        // no such file or directory, or a missing parent
    Exists,          // CreateNew, make_directory, or rename onto an existing name
    NotDirectory,    // a path component, or open_directory's target, is a file
    IsDirectory,     // open of a directory, remove of a directory as a file
    NotEmpty,        // remove of a directory with entries
    NoSpace,         // the volume, or its root directory, is full
    ReadOnly,        // the volume, or the file, cannot be written
    NameTooLong,     // a component over max_name, a path over max_path
    TooMany,         // no free handle, no free mount slot
    Busy,            // a file open elsewhere for writing; remove, rename, or
                     // unmount of something open
    CrossVolume,     // rename between two mounts
    Corrupt,         // the volume fails its own consistency checks
    Timeout,         // a peer that did not answer in time
    TransportError,  // the device, the host OS, or a link failed beneath
};

}
