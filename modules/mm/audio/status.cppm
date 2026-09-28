// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.audio:status;

export namespace mm::audio {

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
