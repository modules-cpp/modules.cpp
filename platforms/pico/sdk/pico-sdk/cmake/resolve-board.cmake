set(MM_VENDOR_BOARD "")
foreach(MM_CANDIDATE IN LISTS MM_BOARD_CHAIN)
  if(MM_CANDIDATE STREQUAL "pico" OR
     MM_CANDIDATE STREQUAL "pico-w" OR
     MM_CANDIDATE STREQUAL "pico2-arm" OR
     MM_CANDIDATE STREQUAL "pico2-riscv" OR
     MM_CANDIDATE STREQUAL "pico2-w-arm" OR
     MM_CANDIDATE STREQUAL "pico2-w-riscv")
    set(MM_VENDOR_BOARD ${MM_CANDIDATE})
    break()
  endif()
endforeach()
if(NOT MM_VENDOR_BOARD)
  message(FATAL_ERROR "pico-sdk bridge recognises no board in chain: ${MM_BOARD_CHAIN}")
endif()

# The ADC reference, in millivolts, keyed on the board the lane selected and
# not on the vendor ancestor above: a child board's wiring is its own, so it
# has a row of its own or reports zero. A row is a value read from the board's
# schematic or measured on it. The six vendor boards feed ADC_VREF from the
# 3V3 rail through a filter, per their published schematics.
set(MM_ADC_REFERENCE_MV 0)
if(MM_BOARD STREQUAL "pico" OR
   MM_BOARD STREQUAL "pico-w" OR
   MM_BOARD STREQUAL "pico2-arm" OR
   MM_BOARD STREQUAL "pico2-riscv" OR
   MM_BOARD STREQUAL "pico2-w-arm" OR
   MM_BOARD STREQUAL "pico2-w-riscv" OR
   MM_BOARD STREQUAL "pico_usb_device" OR
   MM_BOARD STREQUAL "pico2_usb_device")
  set(MM_ADC_REFERENCE_MV 3300)
endif()

# The Waveshare RP2350 LCD 1.54 family, per its schematic: ADC_AVDD is the 3V3
# rail; GP25, pico2's LED, is the volume button, so there is no LED; and pico2's
# default UART on GP0 and GP1 would drive the amplifier enable and the codec's
# playback data, so the default UART is UART1 on the exposed GP26 and GP27.
set(MM_BOARD_HAS_LED 1)
set(MM_BOARD_DEFINITIONS "")

# A second I2C wiring, instance 1 on GP26 and GP27, the pins the Arduino cores
# for RP2040 and RP2350 give Wire1. The six vendor boards leave both free; a
# composite board has one only once its schematic says so, because those pins
# are often something else on it -- the LCD 1.54 family's UART, below.
set(MM_BOARD_HAS_SECOND_I2C 0)
if(MM_BOARD STREQUAL "pico" OR
   MM_BOARD STREQUAL "pico-w" OR
   MM_BOARD STREQUAL "pico2-arm" OR
   MM_BOARD STREQUAL "pico2-riscv" OR
   MM_BOARD STREQUAL "pico2-w-arm" OR
   MM_BOARD STREQUAL "pico2-w-riscv" OR
   MM_BOARD STREQUAL "pico_usb_device" OR
   MM_BOARD STREQUAL "pico2_usb_device" OR
   MM_BOARD STREQUAL "rp2040_zero" OR
   MM_BOARD STREQUAL "rp2350_zero")
  set(MM_BOARD_HAS_SECOND_I2C 1)
endif()

# Native USB port ownership: console (every board today) vs application.
set(MM_BOARD_USB_PORT "console")
if(MM_BOARD STREQUAL "pico_usb_device" OR
   MM_BOARD STREQUAL "pico2_usb_device")
  set(MM_BOARD_USB_PORT "application")
endif()

# USB CDC connect delay in milliseconds (default 500 ms).
# Slower hosts (such as Raspberry Pi acting as test host) need time after
# USB enumeration for the cdc_acm driver to bind and serial terminals to open.
# An explicit CMake -DMM_PICO_STDIO_USB_CONNECT_DELAY_MS takes precedence,
# followed by environment variable MM_PICO_STDIO_USB_CONNECT_DELAY_MS.
if(NOT DEFINED MM_PICO_STDIO_USB_CONNECT_DELAY_MS)
  if(DEFINED ENV{MM_PICO_STDIO_USB_CONNECT_DELAY_MS})
    set(MM_PICO_STDIO_USB_CONNECT_DELAY_MS $ENV{MM_PICO_STDIO_USB_CONNECT_DELAY_MS})
  else()
    set(MM_PICO_STDIO_USB_CONNECT_DELAY_MS 500)
  endif()
endif()
if(NOT MM_PICO_STDIO_USB_CONNECT_DELAY_MS MATCHES "^[0-9]+$")
  message(FATAL_ERROR "MM_PICO_STDIO_USB_CONNECT_DELAY_MS must be a non-negative "
    "integer number of milliseconds, not '${MM_PICO_STDIO_USB_CONNECT_DELAY_MS}'")
endif()

# A PIO USB host port, two GPIOs Pico-PIO-USB drives as a second USB port, D+
# on MM_BOARD_USB_HOST_DP_PIN and D- on the next; the native port stays the
# USB console. Only a board that says so has one: the pico_usb_host and
# pico2_usb_host composite boards, on GP2 and GP3, which the vendor boards'
# default wiring leaves free. The clock becomes 120 MHz on such a board.
set(MM_BOARD_HAS_USB_HOST 0)
set(MM_BOARD_USB_HOST_DP_PIN 0)
if(MM_BOARD STREQUAL "pico_usb_host" OR
   MM_BOARD STREQUAL "pico2_usb_host")
  set(MM_BOARD_HAS_USB_HOST 1)
  set(MM_BOARD_USB_HOST_DP_PIN 2)
endif()

# A second UART, instance 1 on GP8 and GP9, the pins the Arduino cores for
# RP2040 and RP2350 give Serial2. The same six vendor boards, for the same
# reason: a composite board's default UART may already be UART1, as the LCD
# 1.54 family's is.
set(MM_BOARD_HAS_SECOND_UART ${MM_BOARD_HAS_SECOND_I2C})
if(MM_BOARD STREQUAL "rp2350_lcd_154" OR
   MM_BOARD STREQUAL "rp2350_touch_lcd_154")
  set(MM_ADC_REFERENCE_MV 3300)
  set(MM_BOARD_HAS_LED 0)
  list(APPEND MM_BOARD_DEFINITIONS
    PICO_DEFAULT_UART=1
    PICO_DEFAULT_UART_TX_PIN=26
    PICO_DEFAULT_UART_RX_PIN=27)
endif()

# The Waveshare RP2040-GEEK and RP2350-GEEK, per their schematics: ADC_AVDD is
# the 3V3 rail; GP25, the vendor board's LED, is the panel backlight, so there
# is no LED; and GP0 and GP1 go nowhere, so the default UART is UART1 on the
# header labelled UART, GP4 and GP5, as the SDK's own GEEK headers have it.
if(MM_BOARD STREQUAL "rp2040_geek" OR
   MM_BOARD STREQUAL "rp2350_geek")
  set(MM_ADC_REFERENCE_MV 3300)
  set(MM_BOARD_HAS_LED 0)
  list(APPEND MM_BOARD_DEFINITIONS
    PICO_DEFAULT_UART=1
    PICO_DEFAULT_UART_TX_PIN=4
    PICO_DEFAULT_UART_RX_PIN=5)
endif()

# The Waveshare RP2040-Zero and RP2350-Zero, per their schematics: ADC_VREF,
# and on the RP2350 ADC_AVDD, is the 3V3 rail; GP25, the vendor board's LED, is
# a free pad and the only LED is a WS2812B on GP16, so there is no LED. Their
# GP0 and GP1 are on the header, so the vendor board's default UART stands, and
# GP8, GP9, GP26, and GP27 are free header pins, so the second I2C and UART
# rows above include them.
if(MM_BOARD STREQUAL "rp2040_zero" OR
   MM_BOARD STREQUAL "rp2350_zero")
  set(MM_ADC_REFERENCE_MV 3300)
  set(MM_BOARD_HAS_LED 0)
endif()

# The Waveshare RP2350-Touch-LCD-2.8, per its schematic: ADC_AVDD is the 3V3
# rail, and GP25, pico2's LED, is the battery key, so there is no LED. Its
# GP0 and GP1 are the exposed UART, so pico2's default UART stands.
if(MM_BOARD STREQUAL "rp2350_touch_lcd_28")
  set(MM_ADC_REFERENCE_MV 3300)
  set(MM_BOARD_HAS_LED 0)
endif()
