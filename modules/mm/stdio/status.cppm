// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.stdio:status;

export namespace mm::stdio {

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
