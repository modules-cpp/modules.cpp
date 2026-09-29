# Waveshare RP2350-LCD-1.54, and its -EN battery option. Sourced by
# mm_load_board.
board_interfaces="display imu audio"
provider_display=platform.rp2350_touch_lcd_154.display
driver_display=mm::lcd::st7789
provider_imu=platform.rp2350_touch_lcd_154.imu
driver_imu=mm::imu::qmi8658
provider_audio=platform.rp2350_touch_lcd_154.audio
driver_audio=mm::audio::es8311
