// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.rtc.pcf85063;

import mm.mcu;
import mm.rtc;

export namespace mm::rtc::pcf85063 {

struct Wiring {
    mm::mcu::I2cConfiguration i2c;
    unsigned int address = 0x51;
};

class Clock : public mm::rtc::Clock {
public:
    explicit Clock(const Wiring& wiring) : wiring_(wiring) {}

    [[nodiscard]] mm::rtc::Status initialize() override;
    [[nodiscard]] mm::rtc::Status read(mm::rtc::DateTime& time, bool& trusted) override;
    [[nodiscard]] mm::rtc::Status write(const mm::rtc::DateTime& time) override;

private:
    [[nodiscard]] mm::rtc::Status from_mcu(mm::mcu::Status status) const;

    Wiring wiring_;
    bool ready_ = false;
};

}
