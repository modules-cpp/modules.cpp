cmake_minimum_required(VERSION 3.13)
include(${CMAKE_CURRENT_BINARY_DIR}/mm-inputs.cmake)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(${CMAKE_CURRENT_LIST_DIR}/resolve-board.cmake)

if(MM_VENDOR_BOARD STREQUAL "pico")
  set(PICO_BOARD pico CACHE STRING "" FORCE)
  set(PICO_PLATFORM rp2040 CACHE STRING "" FORCE)
elseif(MM_VENDOR_BOARD STREQUAL "pico-w")
  set(PICO_BOARD pico_w CACHE STRING "" FORCE)
  set(PICO_PLATFORM rp2040 CACHE STRING "" FORCE)
elseif(MM_VENDOR_BOARD STREQUAL "pico2-arm")
  set(PICO_BOARD pico2 CACHE STRING "" FORCE)
  set(PICO_PLATFORM rp2350-arm-s CACHE STRING "" FORCE)
elseif(MM_VENDOR_BOARD STREQUAL "pico2-riscv")
  # Same hardware as pico2-arm, so PICO_BOARD is the same; PICO_PLATFORM is what
  # selects the Hazard3 cores.
  set(PICO_BOARD pico2 CACHE STRING "" FORCE)
  set(PICO_PLATFORM rp2350-riscv CACHE STRING "" FORCE)
elseif(MM_VENDOR_BOARD STREQUAL "pico2-w-arm")
  set(PICO_BOARD pico2_w CACHE STRING "" FORCE)
  set(PICO_PLATFORM rp2350-arm-s CACHE STRING "" FORCE)
elseif(MM_VENDOR_BOARD STREQUAL "pico2-w-riscv")
  set(PICO_BOARD pico2_w CACHE STRING "" FORCE)
  set(PICO_PLATFORM rp2350-riscv CACHE STRING "" FORCE)
endif()

# A board whose chip differs from its vendor ancestor's in what only an SDK
# board header can say -- the RP2350B package -- names that header in the
# board table. PICO_PLATFORM stays the vendor board's: the cores are the same.
if(MM_PICO_BOARD_HEADER)
  set(PICO_BOARD ${MM_PICO_BOARD_HEADER} CACHE STRING "" FORCE)
endif()

get_filename_component(MM_COMPILER_DIRECTORY ${MM_C_COMPILER} DIRECTORY)
set(PICO_TOOLCHAIN_PATH ${MM_COMPILER_DIRECTORY} CACHE PATH "" FORCE)
set(CMAKE_C_COMPILER ${MM_C_COMPILER} CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER ${MM_CXX_COMPILER} CACHE FILEPATH "" FORCE)

# A board with a PIO USB host port builds TinyUSB's host over Pico-PIO-USB,
# which pio-usb/vendor.sh provisions; TinyUSB's rp2040 family makes the
# tinyusb_pico_pio_usb target from this path.
if(MM_BOARD_HAS_USB_HOST)
  set(PICO_PIO_USB_PATH ${CMAKE_CURRENT_LIST_DIR}/../pio-usb/upstream)
  if(NOT EXISTS ${PICO_PIO_USB_PATH}/src/pio_usb.c)
    message(FATAL_ERROR "board ${MM_BOARD} has a PIO USB host port; Pico-PIO-USB is absent: "
      "run platforms/pico/sdk/pico-sdk/pio-usb/vendor.sh")
  endif()
endif()

include(${MM_LIBRARY_SOURCE}/pico_sdk_init.cmake)
project(modules_cpp_pico C CXX ASM)
pico_sdk_init()

# Compile camera support only for applications reaching the camera provider.
set(MM_PICO_CAM_CAMERA 0)
if(MM_OBJECTS MATCHES "/boards/pico_cam_a/camera/")
  if(NOT MM_BOARD STREQUAL "pico_cam_a")
    message(FATAL_ERROR "PICO-Cam-A camera provider requires board pico_cam_a")
  endif()
  set(MM_PICO_CAM_CAMERA 1)
endif()
set(MM_ADAPTER ${CMAKE_CURRENT_LIST_DIR}/../ext/adapter.c)
set_source_files_properties(${MM_OBJECTS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
add_executable(mm_external ${MM_OBJECTS} ${MM_ADAPTER})
set_target_properties(mm_external PROPERTIES
  LINKER_LANGUAGE CXX
  OUTPUT_NAME ${MM_OUTPUT_NAME})
set_source_files_properties(${MM_ADAPTER} PROPERTIES
  COMPILE_OPTIONS "-std=gnu11;-Wall;-Wextra;-Werror")
target_compile_definitions(mm_external PRIVATE
  MM_SELECTED_BOARD="${MM_BOARD}"
  MM_PICO_CAM_CAMERA=${MM_PICO_CAM_CAMERA}
  MM_ADC_REFERENCE_MV=${MM_ADC_REFERENCE_MV}
  MM_BOARD_HAS_LED=${MM_BOARD_HAS_LED}
  MM_BOARD_HAS_SECOND_I2C=${MM_BOARD_HAS_SECOND_I2C}
  MM_BOARD_HAS_SECOND_UART=${MM_BOARD_HAS_SECOND_UART}
  MM_BOARD_HAS_USB_HOST=${MM_BOARD_HAS_USB_HOST}
  MM_BOARD_USB_HOST_DP_PIN=${MM_BOARD_USB_HOST_DP_PIN}
  MM_PICO_STDIO_USB_CONNECT_DELAY_MS=${MM_PICO_STDIO_USB_CONNECT_DELAY_MS}
  MM_BOARD_FLASH_REGION_BYTES=${MM_BOARD_FLASH_REGION_BYTES}
  ${MM_BOARD_DEFINITIONS}
  PICO_STDIO_USB_CONNECTION_WITHOUT_DTR=1)
if(MM_BOARD_USB_PORT STREQUAL "application")
  target_compile_definitions(mm_external PRIVATE MM_BOARD_USB_PORT_APPLICATION=1)
  target_include_directories(mm_external PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../ext/usb-device)
  target_link_libraries(mm_external
    pico_stdlib
    pico_stdio_uart
    tinyusb_device
    hardware_adc
    hardware_dma
    hardware_flash
    hardware_pio
    hardware_pwm
    hardware_spi
    hardware_i2c
    hardware_uart
    pico_flash)
  pico_enable_stdio_uart(mm_external 1)
  pico_enable_stdio_usb(mm_external 0)
else()
  target_link_libraries(mm_external
    pico_stdlib
    pico_stdio_usb
    hardware_adc
    hardware_dma
    hardware_flash
    hardware_pio
    hardware_pwm
    hardware_spi
    hardware_i2c
    hardware_uart
    pico_flash)
  pico_enable_stdio_usb(mm_external 1)
endif()
if(MM_BOARD_HAS_USB_HOST)
  if(NOT TARGET tinyusb_pico_pio_usb)
    message(FATAL_ERROR "TinyUSB did not make tinyusb_pico_pio_usb from ${PICO_PIO_USB_PATH}")
  endif()
  # This directory's tusb_config.h is TinyUSB's configuration once the host is
  # linked; it keeps the native port the console pico_stdio_usb expects.
  target_include_directories(mm_external PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../ext/usb-host)
  # pico_stdio_usb stops supplying its descriptors once the application links
  # TinyUSB; the console is still its device, so it still supplies them.
  target_compile_definitions(mm_external PRIVATE PICO_STDIO_USB_USE_DEFAULT_DESCRIPTORS=1)
  target_link_libraries(mm_external tinyusb_device tinyusb_host tinyusb_pico_pio_usb)
endif()
# littlefs, for a program that linked its provider: object paths mirror
# source paths, so the provider's object is recognisable among MM_OBJECTS. A
# static library, so a member nothing references is not linked; built
# without malloc, since the adapter supplies every buffer.
if(MM_OBJECTS MATCHES "/platforms/pico/sdk/pico-sdk/littlefs/provider/")
  set(MM_LITTLEFS ${CMAKE_CURRENT_LIST_DIR}/../littlefs)
  if(NOT EXISTS ${MM_LITTLEFS}/upstream/lfs.c)
    message(FATAL_ERROR "this program uses littlefs; the littlefs checkout is absent: "
      "run platforms/pico/sdk/pico-sdk/littlefs/vendor.sh")
  endif()
  add_library(mm_littlefs STATIC
    ${MM_LITTLEFS}/upstream/lfs.c
    ${MM_LITTLEFS}/upstream/lfs_util.c
    ${MM_LITTLEFS}/adapter.c)
  target_include_directories(mm_littlefs PRIVATE ${MM_LITTLEFS} ${MM_LITTLEFS}/upstream)
  target_compile_definitions(mm_littlefs PRIVATE
    LFS_NO_MALLOC LFS_NO_DEBUG LFS_NO_WARN LFS_NO_ERROR)
  set_source_files_properties(${MM_LITTLEFS}/adapter.c PROPERTIES
    COMPILE_OPTIONS "-std=gnu11;-Wall;-Wextra;-Werror")
  target_link_libraries(mm_littlefs PRIVATE pico_platform_headers)
  target_link_libraries(mm_external mm_littlefs)
endif()
# FatFs, for a program that linked its provider, recognised the same way.
# Every FatFs translation unit is compiled with the project's ffconf.h
# included first, so FatFs never reads upstream's configuration; the sample
# diskio.c is replaced by adapter.c, and ffsystem.c serves options left off.
if(MM_OBJECTS MATCHES "/platforms/pico/sdk/pico-sdk/fatfs/provider/")
  set(MM_FATFS ${CMAKE_CURRENT_LIST_DIR}/../fatfs)
  if(NOT EXISTS ${MM_FATFS}/upstream/source/ff.c)
    message(FATAL_ERROR "this program uses FAT; the FatFs checkout is absent: "
      "run platforms/pico/sdk/pico-sdk/fatfs/vendor.sh")
  endif()
  add_library(mm_fatfs STATIC
    ${MM_FATFS}/upstream/source/ff.c
    ${MM_FATFS}/upstream/source/ffunicode.c
    ${MM_FATFS}/adapter.c)
  target_include_directories(mm_fatfs PRIVATE ${MM_FATFS} ${MM_FATFS}/upstream/source)
  target_compile_options(mm_fatfs PRIVATE -include ${MM_FATFS}/ffconf.h)
  set_source_files_properties(${MM_FATFS}/adapter.c PROPERTIES
    COMPILE_OPTIONS "-std=gnu11;-Wall;-Wextra;-Werror")
  target_link_libraries(mm_fatfs PRIVATE pico_platform_headers)
  target_link_libraries(mm_external mm_fatfs)
endif()
pico_enable_stdio_semihosting(mm_external 1)
# Every image links TinyUSB's RP2040 device driver; ext/adapter.c wraps one of
# its functions to tolerate a stale buffer notification instead of panicking.
target_link_options(mm_external PRIVATE
  "LINKER:--wrap=hw_endpoint_xfer_continue")
pico_add_extra_outputs(mm_external)
# Where the flash region starts, as an address in the execute-in-place window:
# no block of the image may reach it. PICO_FLASH_SIZE_BYTES is the board
# header's expression, set by pico_sdk_init; the SDK's own default applies
# when no header sets it.
set(MM_FLASH_SIZE_EXPRESSION "${PICO_FLASH_SIZE_BYTES}")
if(MM_FLASH_SIZE_EXPRESSION STREQUAL "")
  set(MM_FLASH_SIZE_EXPRESSION "2 * 1024 * 1024")
endif()
math(EXPR MM_FLASH_SIZE "${MM_FLASH_SIZE_EXPRESSION}")
if(MM_BOARD_FLASH_REGION_BYTES GREATER_EQUAL MM_FLASH_SIZE)
  message(FATAL_ERROR "the flash region, ${MM_BOARD_FLASH_REGION_BYTES} bytes, does not fit "
    "the ${MM_FLASH_SIZE}-byte flash board ${MM_BOARD} builds for")
endif()
math(EXPR MM_UF2_LIMIT "0x10000000 + ${MM_FLASH_SIZE} - ${MM_BOARD_FLASH_REGION_BYTES}")
add_custom_command(TARGET mm_external POST_BUILD
  COMMAND ${CMAKE_COMMAND}
    "-DMM_UF2=$<TARGET_FILE_DIR:mm_external>/${MM_OUTPUT_NAME}.uf2"
    "-DMM_UF2_LIMIT=${MM_UF2_LIMIT}"
    -P ${CMAKE_CURRENT_LIST_DIR}/validate-uf2.cmake
  COMMENT "Validate UF2 output"
  VERBATIM)

file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/mm-abi-probe.txt CONTENT "${MM_ADAPTER}\n")
file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/mm-result.txt CONTENT
"$<TARGET_FILE:mm_external>
$<TARGET_FILE_DIR:mm_external>/${MM_OUTPUT_NAME}.bin
$<TARGET_FILE_DIR:mm_external>/${MM_OUTPUT_NAME}.hex
$<TARGET_FILE_DIR:mm_external>/${MM_OUTPUT_NAME}.uf2
$<TARGET_FILE_DIR:mm_external>/${MM_OUTPUT_NAME}.elf.map
")
