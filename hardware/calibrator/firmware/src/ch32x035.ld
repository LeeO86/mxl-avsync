/* CH32X035G8R6: 62 KB code flash at 0x00000000, 20 KB SRAM at 0x20000000
 * (same map as WCH's EVT Ld/Link.ld for the CH32X035). */
ENTRY(_start)

__stack_size = 1024;

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 62K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 20K
}

SECTIONS
{
    .init : {
        KEEP(*(.init))          /* reset jump + vector table (startup.c) */
        . = ALIGN(4);
    } >FLASH

    .text : {
        . = ALIGN(4);
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        . = ALIGN(4);
    } >FLASH

    .data : {
        . = ALIGN(4);
        _data_vma = .;
        *(.data .data.*)
        . = ALIGN(8);
        PROVIDE(__global_pointer$ = . + 0x800);
        *(.sdata .sdata.*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT>FLASH
    _data_lma = LOADADDR(.data);

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _sbss = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } >RAM

    _eusrstack = ORIGIN(RAM) + LENGTH(RAM);
    ASSERT(_ebss + __stack_size <= _eusrstack, "RAM full: no room for the stack")

    /DISCARD/ : { *(.comment) *(.riscv.attributes) *(.note*) }
}
