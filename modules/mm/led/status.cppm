// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.led:status;

export namespace mm::led {

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
