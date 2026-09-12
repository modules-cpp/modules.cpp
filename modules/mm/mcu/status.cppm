// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include "mcu-cxx.h"

export module mm.mcu:status;

export namespace mm::mcu {

// Unsupported is a first-class answer rather than a failure: a platform that has
// the facility class but not the instance, or the pin, returns it, and portable
// code branches on it. Without that, an interface shared by silicon that
// genuinely differs would have to promise what some platform cannot keep.
enum class Status { Ok, BadArgument, Unsupported, Busy, Timeout };

}

export namespace mm::mcu::detail {

// The codes come from mcu-c.h so the interface and every platform read one
// definition. An unrecognised answer is reported as BadArgument: at this layer a
// platform returning an undefined code is indistinguishable from a call the
// platform rejected, and inventing a sixth status to describe a platform bug
// would put it in every caller's switch.
[[nodiscard]] inline Status status_from(int code) {
    switch (code) {
        case MM_MCU_OK: return Status::Ok;
        case MM_MCU_BAD_ARGUMENT: return Status::BadArgument;
        case MM_MCU_UNSUPPORTED: return Status::Unsupported;
        case MM_MCU_BUSY: return Status::Busy;
        case MM_MCU_TIMEOUT: return Status::Timeout;
        default: return Status::BadArgument;
    }
}

}
