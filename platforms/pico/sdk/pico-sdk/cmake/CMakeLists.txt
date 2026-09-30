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
  MM_ADC_REFERENCE_MV=${MM_ADC_REFERENCE_MV}
  MM_BOARD_HAS_LED=${MM_BOARD_HAS_LED}
  MM_BOARD_HAS_SECOND_I2C=${MM_BOARD_HAS_SECOND_I2C}
  MM_BOARD_HAS_SECOND_UART=${MM_BOARD_HAS_SECOND_UART}
  MM_BOARD_HAS_USB_HOST=${MM_BOARD_HAS_USB_HOST}
  MM_BOARD_USB_HOST_DP_PIN=${MM_BOARD_USB_HOST_DP_PIN}
  ${MM_BOARD_DEFINITIONS}
  PICO_STDIO_USB_CONNECTION_WITHOUT_DTR=1)
target_link_libraries(mm_external
  pico_stdlib
  pico_stdio_usb
  hardware_adc
  hardware_dma
  hardware_pio
  hardware_pwm
  hardware_spi
  hardware_i2c
  hardware_uart)
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
pico_enable_stdio_usb(mm_external 1)
pico_enable_stdio_semihosting(mm_external 1)
pico_add_extra_outputs(mm_external)
add_custom_command(TARGET mm_external POST_BUILD
  COMMAND ${CMAKE_COMMAND}
    "-DMM_UF2=$<TARGET_FILE_DIR:mm_external>/${MM_OUTPUT_NAME}.uf2"
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
