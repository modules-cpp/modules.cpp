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
   MM_BOARD STREQUAL "pico2-w-riscv")
  set(MM_ADC_REFERENCE_MV 3300)
endif()
