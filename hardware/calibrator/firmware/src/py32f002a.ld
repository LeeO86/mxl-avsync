/* Linker script for PY32F002Ax5 (20 KB flash, 3 KB RAM). Written for this project. */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 20K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 3K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_min_stack = 0x200;

SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH

  .text :
  {
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
    _etext = .;
  } > FLASH

  .data : AT(_etext)
  {
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM

  .bss (NOLOAD) :
  {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM

  ASSERT(_ebss + _min_stack <= _estack, "RAM overflow: no room for the stack")

  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) }
}
