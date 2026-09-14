// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.touch:status;

export namespace mm::touch {

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
