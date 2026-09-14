// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.rtc:status;

export namespace mm::rtc {

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
