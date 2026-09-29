# Waveshare RP2350-Touch-LCD-1.54, and its -EN battery option: rp2350_lcd_154
# with a touch controller. Sourced by mm_load_board.
. "$script_dir/scripts/boards/rp2350_lcd_154.sh"
board_interfaces="display touch imu audio"
provider_touch=platform.rp2350_touch_lcd_154.touch
driver_touch=mm::touch::cst816
