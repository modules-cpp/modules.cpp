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

# The flash region: data storage at the top of the flash the build assumes,
# PICO_FLASH_SIZE_BYTES, which mm.mcu's flash-region calls address and
# littlefs uses for a board's own files. A whole number of 4 KiB erase
# sectors, zero for none. Keyed on the vendor board, because the build's flash
# size is the vendor header's: 2 MB for pico and pico-w, 4 MB for the pico2
# family, whatever a board's chip has; rp2350_pizero, which selects its own
# 16 MB header, has a larger region. MM_BOARD_FLASH_REGION_BYTES given to
# CMake or in the environment takes precedence, which is how the bridge's
# overlap check is exercised.
if(MM_VENDOR_BOARD STREQUAL "pico" OR MM_VENDOR_BOARD STREQUAL "pico-w")
  set(MM_BOARD_FLASH_REGION_DEFAULT 262144)
else()
  set(MM_BOARD_FLASH_REGION_DEFAULT 524288)
endif()
if(MM_BOARD STREQUAL "rp2350_pizero" OR
   MM_BOARD STREQUAL "rp2350_pizero_usb_host")
  set(MM_BOARD_FLASH_REGION_DEFAULT 4194304)
endif()
if(NOT DEFINED MM_BOARD_FLASH_REGION_BYTES)
  if(DEFINED ENV{MM_BOARD_FLASH_REGION_BYTES})
    set(MM_BOARD_FLASH_REGION_BYTES $ENV{MM_BOARD_FLASH_REGION_BYTES})
  else()
    set(MM_BOARD_FLASH_REGION_BYTES ${MM_BOARD_FLASH_REGION_DEFAULT})
  endif()
endif()
if(NOT MM_BOARD_FLASH_REGION_BYTES MATCHES "^[0-9]+$")
  message(FATAL_ERROR "MM_BOARD_FLASH_REGION_BYTES must be a non-negative number of "
    "bytes, not '${MM_BOARD_FLASH_REGION_BYTES}'")
endif()
math(EXPR MM_BOARD_FLASH_REGION_REMAINDER "${MM_BOARD_FLASH_REGION_BYTES} % 4096")
if(NOT MM_BOARD_FLASH_REGION_REMAINDER EQUAL 0)
  message(FATAL_ERROR "MM_BOARD_FLASH_REGION_BYTES must be a whole number of 4096-byte "
    "erase sectors, not ${MM_BOARD_FLASH_REGION_BYTES}")
endif()

# littlefs's pools: volumes attached at once, and files and directories open
# at once across them. Every pool entry is static RAM in every program that
# links littlefs -- about 770 bytes a volume, 650 a file, and 590 a directory
# -- so the default suits an RP2040's 264 KB: one volume, the board's own
# storage, four files, two directories. A board with RAM to spare, or more
# volumes to mount, says so in its row. MM_BOARD_LFS_VOLUMES,
# MM_BOARD_LFS_FILES, and MM_BOARD_LFS_DIRECTORIES given to CMake or in the
# environment take precedence over the table.
set(MM_BOARD_LFS_VOLUMES_DEFAULT 1)
set(MM_BOARD_LFS_FILES_DEFAULT 4)
set(MM_BOARD_LFS_DIRECTORIES_DEFAULT 2)
if(MM_BOARD STREQUAL "rp2350_pizero" OR
   MM_BOARD STREQUAL "rp2350_pizero_usb_host")
  # A 4 MiB region on an RP2350B: room for the pools littlefs had before.
  set(MM_BOARD_LFS_VOLUMES_DEFAULT 2)
  set(MM_BOARD_LFS_FILES_DEFAULT 8)
  set(MM_BOARD_LFS_DIRECTORIES_DEFAULT 4)
endif()
foreach(MM_POOL IN ITEMS VOLUMES:4 FILES:32 DIRECTORIES:16)
  string(REPLACE ":" ";" MM_POOL_PARTS ${MM_POOL})
  list(GET MM_POOL_PARTS 0 MM_POOL_NAME)
  list(GET MM_POOL_PARTS 1 MM_POOL_MOST)
  set(MM_POOL_VARIABLE MM_BOARD_LFS_${MM_POOL_NAME})
  if(NOT DEFINED ${MM_POOL_VARIABLE})
    if(DEFINED ENV{${MM_POOL_VARIABLE}})
      set(${MM_POOL_VARIABLE} $ENV{${MM_POOL_VARIABLE}})
    else()
      set(${MM_POOL_VARIABLE} ${${MM_POOL_VARIABLE}_DEFAULT})
    endif()
  endif()
  if(NOT ${MM_POOL_VARIABLE} MATCHES "^[0-9]+$" OR ${MM_POOL_VARIABLE} LESS 1 OR
     ${MM_POOL_VARIABLE} GREATER ${MM_POOL_MOST})
    message(FATAL_ERROR "${MM_POOL_VARIABLE} must be a whole number from 1 to "
      "${MM_POOL_MOST}, not '${${MM_POOL_VARIABLE}}'")
  endif()
endforeach()

# A PIO USB host port, two GPIOs Pico-PIO-USB drives as a second USB port, D+
# on MM_BOARD_USB_HOST_DP_PIN and D- on the next; the native port stays the
# USB console. Only a board that says so has one: the pico_usb_host and
# pico2_usb_host composite boards, on GP2 and GP3, which the vendor boards'
# default wiring leaves free, and rp2350_pizero_usb_host, on the GP28 and GP29
# the Waveshare RP2350-PiZero wires to its PIO-USB socket. The clock becomes
# 120 MHz on such a board.
set(MM_BOARD_HAS_USB_HOST 0)
set(MM_BOARD_USB_HOST_DP_PIN 0)
if(MM_BOARD STREQUAL "pico_usb_host" OR
   MM_BOARD STREQUAL "pico2_usb_host")
  set(MM_BOARD_HAS_USB_HOST 1)
  set(MM_BOARD_USB_HOST_DP_PIN 2)
endif()
if(MM_BOARD STREQUAL "rp2350_pizero_usb_host")
  set(MM_BOARD_HAS_USB_HOST 1)
  set(MM_BOARD_USB_HOST_DP_PIN 28)
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

# The Waveshare RP2350-PiZero is an RP2350B, and pico2.h defines PICO_RP2350A
# as one without a guard, so no definition here can make a pico2 build see the
# B package's forty-eight GPIOs. Its row selects the SDK's own header instead,
# MM_PICO_BOARD_HEADER, which the bridge uses in place of the vendor board's;
# that header sets the B package, 16MB of flash, UART1 on GP4 and GP5, and no
# LED. ADC_AVDD is the 3V3 rail. No second I2C or UART: the header's defaults
# are already instance one on the 40-pin header's pins. rp2350_pizero_usb_host
# is the same board, so the same row.
set(MM_PICO_BOARD_HEADER "")
if(MM_BOARD STREQUAL "rp2350_pizero" OR
   MM_BOARD STREQUAL "rp2350_pizero_usb_host")
  set(MM_PICO_BOARD_HEADER waveshare_rp2350_pizero)
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
