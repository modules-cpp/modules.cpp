// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.mcu:status;

export namespace mm::mcu {

// Unsupported is a first-class answer rather than a failure: a platform that has
// the facility class but not the instance, or the pin, returns it, and portable
// code branches on it. Without that, an interface shared by silicon that
// genuinely differs would have to promise what some platform cannot keep.
enum class Status { Ok, BadArgument, Unsupported, Busy, Timeout };

enum class Direction { In, Out };
enum class Pull { None, Up, Down };

}
