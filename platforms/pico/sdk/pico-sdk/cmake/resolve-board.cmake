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
