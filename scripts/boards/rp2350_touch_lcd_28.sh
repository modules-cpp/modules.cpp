# Waveshare RP2350-Touch-LCD-2.8. Sourced by mm_load_board.
board_interfaces="display touch imu rtc audio"
provider_display=platform.rp2350_touch_lcd_28.display
driver_display=mm::lcd::st7789
provider_touch=platform.rp2350_touch_lcd_28.touch
driver_touch=mm::touch::cst328
provider_imu=platform.rp2350_touch_lcd_28.imu
driver_imu=mm::imu::qmi8658
provider_rtc=platform.rp2350_touch_lcd_28.rtc
driver_rtc=mm::rtc::pcf85063
provider_audio=platform.rp2350_touch_lcd_28.audio
driver_audio=
