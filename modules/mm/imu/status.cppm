// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.imu:status;

export namespace mm::imu {

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
