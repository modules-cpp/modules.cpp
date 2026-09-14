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
target_compile_definitions(mm_external PRIVATE MM_SELECTED_BOARD="${MM_BOARD}")
target_link_libraries(mm_external pico_stdlib hardware_spi hardware_i2c hardware_uart)
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
