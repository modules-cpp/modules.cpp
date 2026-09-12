ENTRY(_start)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .text : { *(.text*) *(.rodata*) KEEP(*(.init)) KEEP(*(.fini)) } > FLASH
    .ARM.extab : { *(.ARM.extab*) } > FLASH
    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH
    .preinit_array : { KEEP(*(.preinit_array*)) } > FLASH
    .init_array : { KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) } > FLASH
    .fini_array : { KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) } > FLASH
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
