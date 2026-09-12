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
  math(EXPR MM_UF2_OFFSET "${MM_UF2_OFFSET} + 512")
endwhile()
