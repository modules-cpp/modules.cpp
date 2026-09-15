// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>
#include <span>

import mm.display;
import mm.imu;
import mm.mcu;
import mm.rtc;
import mm.stdio;
import mm.touch;

namespace {

void report(mm::stdio::Console& console, const char* text) {
    std::size_t length=0;while(text[length]!='\0')++length;
    std::size_t written=0;
    console.write(std::as_bytes(std::span{text,length}),written);
}

}

int main() {
    auto& console=mm::stdio::selected_console();
    if(console.initialize()!=mm::stdio::Status::Ok)return 1;
    report(console,"linux-smoke: stdio working\n");

    unsigned long before=0;
    if(mm::mcu::ticks_ms(before)!=mm::mcu::Status::Ok||
       mm::mcu::delay_ms(1)!=mm::mcu::Status::Ok)return 2;
    report(console,"linux-smoke: monotonic time working\n");

    auto& clock=mm::rtc::selected_clock();
    const auto rtc_init=clock.initialize();
    if(rtc_init==mm::rtc::Status::Ok){mm::rtc::DateTime time{};bool trusted=false;if(clock.read(time,trusted)!=mm::rtc::Status::Ok)return 3;report(console,"linux-smoke: rtc working\n");}
    else if(rtc_init!=mm::rtc::Status::Unsupported&&rtc_init!=mm::rtc::Status::BadArgument)return 4;

    auto& display=mm::display::selected_display();const auto display_init=display.initialize();
    if(display_init==mm::display::Status::Ok){if(display.clear(mm::display::Color::Black)!=mm::display::Status::Ok||display.refresh(mm::display::Refresh::Full)!=mm::display::Status::Ok)return 5;display.sleep();report(console,"linux-smoke: display working\n");}
    else if(display_init!=mm::display::Status::Unsupported&&display_init!=mm::display::Status::Busy&&display_init!=mm::display::Status::BadArgument)return 6;

    auto& touch=mm::touch::selected_touch();const auto touch_init=touch.initialize();
    if(touch_init==mm::touch::Status::Ok){std::array<mm::touch::Point,10> points{};std::size_t count=0;if(touch.read(points,count)!=mm::touch::Status::Ok)return 7;touch.sleep();report(console,"linux-smoke: touch working\n");}
    else if(touch_init!=mm::touch::Status::Unsupported&&touch_init!=mm::touch::Status::BadArgument)return 8;

    auto& imu=mm::imu::selected_imu();const auto imu_init=imu.initialize();
    if(imu_init==mm::imu::Status::Ok){mm::imu::Axes acceleration{},rotation{};if(imu.read(acceleration,rotation)!=mm::imu::Status::Ok)return 9;imu.sleep();report(console,"linux-smoke: imu working\n");}
    else if(imu_init!=mm::imu::Status::Unsupported&&imu_init!=mm::imu::Status::BadArgument)return 10;
    return 0;
}
