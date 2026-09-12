cmake_minimum_required(VERSION 3.13)
include(${CMAKE_CURRENT_BINARY_DIR}/mm-inputs.cmake)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_TOOLCHAIN_FILE ${MM_TOOLCHAIN_FILE} CACHE FILEPATH "" FORCE)
project(cmake_demo C CXX)

set(MM_DEMO_SOURCE ${MM_LIBRARY_SOURCE}/demo.c)
set_source_files_properties(${MM_OBJECTS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
add_executable(mm_external ${MM_OBJECTS} ${MM_DEMO_SOURCE})
set_target_properties(mm_external PROPERTIES
  LINKER_LANGUAGE CXX
  OUTPUT_NAME ${MM_OUTPUT_NAME})

file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/mm-abi-probe.txt CONTENT "${MM_DEMO_SOURCE}\n")
file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/mm-result.txt CONTENT
"$<TARGET_FILE:mm_external>
")
