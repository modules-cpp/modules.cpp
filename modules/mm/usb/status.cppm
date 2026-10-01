// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.usb:status;

export namespace mm::usb {

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
