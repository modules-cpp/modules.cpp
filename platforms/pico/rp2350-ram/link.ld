ENTRY(reset_handler)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 512K
}

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(256);
        KEEP(*(.isr_vector))
    } > RAM
    ASSERT(ADDR(.isr_vector) == ORIGIN(RAM), ".isr_vector must start at SRAM base")
    ASSERT(ADDR(.isr_vector) % 256 == 0, ".isr_vector must be 256-byte aligned")

    .text : { *(.text*) *(.rodata*) KEEP(*(.init)) KEEP(*(.fini)) } > RAM
    .ARM.extab : { *(.ARM.extab*) } > RAM
    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > RAM
    .preinit_array : { KEEP(*(.preinit_array*)) } > RAM
    .init_array : { KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) } > RAM
    .fini_array : { KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) } > RAM
    .data : { *(.data*) } > RAM
    .bss (NOLOAD) :
    {
        __bss_start__ = .;
        *(.bss*) *(COMMON)
        __bss_end__ = .;
    } > RAM
    . = ALIGN(8);
    __end__ = .;
    end = .;
    __heap_limit = ORIGIN(RAM) + LENGTH(RAM) - 0x1000;
    __stack = ORIGIN(RAM) + LENGTH(RAM);
}
