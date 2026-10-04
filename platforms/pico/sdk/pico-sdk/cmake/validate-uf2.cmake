if(NOT DEFINED MM_UF2 OR MM_UF2 STREQUAL "")
  message(FATAL_ERROR "UF2 validation requires MM_UF2")
endif()

if(NOT EXISTS "${MM_UF2}")
  message(FATAL_ERROR "picotool did not produce the expected UF2: ${MM_UF2}")
endif()

file(SIZE "${MM_UF2}" MM_UF2_SIZE)
math(EXPR MM_UF2_REMAINDER "${MM_UF2_SIZE} % 512")
if(MM_UF2_SIZE EQUAL 0 OR NOT MM_UF2_REMAINDER EQUAL 0)
  file(REMOVE "${MM_UF2}")
  message(FATAL_ERROR
    "picotool produced an invalid UF2 size (${MM_UF2_SIZE} bytes): ${MM_UF2}")
endif()

# A little-endian 32-bit field of the block at offset, as a number.
function(mm_uf2_word offset field result)
  math(EXPR position "${offset} + ${field}")
  file(READ "${MM_UF2}" bytes OFFSET ${position} LIMIT 4 HEX)
  string(SUBSTRING "${bytes}" 0 2 b0)
  string(SUBSTRING "${bytes}" 2 2 b1)
  string(SUBSTRING "${bytes}" 4 2 b2)
  string(SUBSTRING "${bytes}" 6 2 b3)
  math(EXPR value "0x${b3}${b2}${b1}${b0}")
  set(${result} ${value} PARENT_SCOPE)
endfunction()

# With MM_UF2_LIMIT, no block written to flash may reach that address: it is
# where the board's flash region starts. Blocks flagged not-main-flash, and
# blocks of the absolute family -- picotool's RP2350-E10 workaround block,
# which sits at the top of a 16 MB part whatever the image -- are not part of
# the image and are not checked.
set(MM_UF2_HIGHEST 0)
set(MM_UF2_OFFSET 0)
while(MM_UF2_OFFSET LESS MM_UF2_SIZE)
  file(READ "${MM_UF2}" MM_UF2_HEADER
    OFFSET ${MM_UF2_OFFSET} LIMIT 8 HEX)
  math(EXPR MM_UF2_END_OFFSET "${MM_UF2_OFFSET} + 508")
  file(READ "${MM_UF2}" MM_UF2_END_MAGIC
    OFFSET ${MM_UF2_END_OFFSET} LIMIT 4 HEX)
  if(NOT MM_UF2_HEADER STREQUAL "5546320a57515d9e" OR
     NOT MM_UF2_END_MAGIC STREQUAL "306fb10a")
    math(EXPR MM_UF2_BLOCK "${MM_UF2_OFFSET} / 512")
    file(REMOVE "${MM_UF2}")
    message(FATAL_ERROR
      "picotool produced invalid UF2 framing in block ${MM_UF2_BLOCK}: ${MM_UF2}")
  endif()
  if(DEFINED MM_UF2_LIMIT AND NOT MM_UF2_LIMIT STREQUAL "")
    mm_uf2_word(${MM_UF2_OFFSET} 8 MM_UF2_FLAGS)
    mm_uf2_word(${MM_UF2_OFFSET} 12 MM_UF2_ADDRESS)
    mm_uf2_word(${MM_UF2_OFFSET} 16 MM_UF2_PAYLOAD)
    mm_uf2_word(${MM_UF2_OFFSET} 28 MM_UF2_FAMILY)
    math(EXPR MM_UF2_NOT_MAIN "${MM_UF2_FLAGS} & 1")
    math(EXPR MM_UF2_HAS_FAMILY "${MM_UF2_FLAGS} & 0x2000")
    set(MM_UF2_SKIP FALSE)
    if(NOT MM_UF2_NOT_MAIN EQUAL 0)
      set(MM_UF2_SKIP TRUE)
    endif()
    if(NOT MM_UF2_HAS_FAMILY EQUAL 0 AND MM_UF2_FAMILY EQUAL 3834380119)  # 0xe48bff57
      set(MM_UF2_SKIP TRUE)
    endif()
    if(NOT MM_UF2_SKIP AND MM_UF2_ADDRESS GREATER_EQUAL 268435456 AND
       MM_UF2_ADDRESS LESS 536870912)
      math(EXPR MM_UF2_END "${MM_UF2_ADDRESS} + ${MM_UF2_PAYLOAD}")
      if(MM_UF2_END GREATER MM_UF2_HIGHEST)
        set(MM_UF2_HIGHEST ${MM_UF2_END})
      endif()
    endif()
  endif()
  math(EXPR MM_UF2_OFFSET "${MM_UF2_OFFSET} + 512")
endwhile()

if(DEFINED MM_UF2_LIMIT AND NOT MM_UF2_LIMIT STREQUAL "" AND
   MM_UF2_HIGHEST GREATER MM_UF2_LIMIT)
  math(EXPR MM_UF2_HIGHEST_HEX "${MM_UF2_HIGHEST}" OUTPUT_FORMAT HEXADECIMAL)
  math(EXPR MM_UF2_LIMIT_HEX "${MM_UF2_LIMIT}" OUTPUT_FORMAT HEXADECIMAL)
  file(REMOVE "${MM_UF2}")
  message(FATAL_ERROR
    "the image ends at ${MM_UF2_HIGHEST_HEX}, past the flash region's start at "
    "${MM_UF2_LIMIT_HEX}: shrink the program or MM_BOARD_FLASH_REGION_BYTES in "
    "platforms/pico/sdk/pico-sdk/cmake/resolve-board.cmake")
endif()
