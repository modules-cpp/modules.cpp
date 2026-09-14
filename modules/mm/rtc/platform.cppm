// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.rtc:platform;

import :status;
import :types;

export namespace mm::rtc {

class Clock {
public:
    virtual ~Clock() = default;

    [[nodiscard]] virtual Status initialize() { return Status::Unsupported; }

    // trusted is false when the clock lost its oscillator since it was last
    // set, which means the fields are well formed and meaningless. It is an
    // out-parameter rather than a field of DateTime so that a caller has to
    // name it, and rather than a Status so that a caller is not pushed into
    // treating a readable clock as a failure.
    [[nodiscard]] virtual Status read(DateTime&, bool& trusted) {
        return Status::Unsupported;
    }

    // Setting the time is what makes a later reading trusted.
    [[nodiscard]] virtual Status write(const DateTime&) { return Status::Unsupported; }
};

void set_clock(Clock& clock);
[[nodiscard]] Clock& selected_clock();

}
