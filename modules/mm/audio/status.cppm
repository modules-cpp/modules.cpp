// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.audio:status;

export namespace mm::audio {

// mm.imu's answers, unchanged, so a caller that handles one device family
// handles this one.
enum class Status {
    Ok,
    BadArgument,
    Unsupported,
    NotInitialized,
    Busy,
    Timeout,
    TransportError
};

}
