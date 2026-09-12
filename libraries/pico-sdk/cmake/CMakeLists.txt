cmake_minimum_required(VERSION 3.13)
include(${CMAKE_CURRENT_BINARY_DIR}/mm-inputs.cmake)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MM_BOARD STREQUAL "pico")
  set(PICO_BOARD pico CACHE STRING "" FORCE)
  set(PICO_PLATFORM rp2040 CACHE STRING "" FORCE)
elseif(MM_BOARD STREQUAL "pico2")
  set(PICO_BOARD pico2 CACHE STRING "" FORCE)
  set(PICO_PLATFORM rp2350-arm-s CACHE STRING "" FORCE)
else()
  message(FATAL_ERROR "pico-sdk bridge requires a selected board")
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
  COMPILE_OPTIONS "-std=c11;-Wall;-Wextra;-Werror")
target_link_libraries(mm_external pico_stdlib)
pico_enable_stdio_semihosting(mm_external 1)
pico_add_extra_outputs(mm_external)

file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/mm-abi-probe.txt CONTENT "${MM_ADAPTER}\n")
file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/mm-result.txt CONTENT
"$<TARGET_FILE:mm_external>
$<TARGET_FILE_DIR:mm_external>/${MM_OUTPUT_NAME}.bin
$<TARGET_FILE_DIR:mm_external>/${MM_OUTPUT_NAME}.hex
$<TARGET_FILE_DIR:mm_external>/${MM_OUTPUT_NAME}.uf2
$<TARGET_FILE_DIR:mm_external>/${MM_OUTPUT_NAME}.elf.map
")
