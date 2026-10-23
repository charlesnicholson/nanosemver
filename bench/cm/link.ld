/* A bare Cortex-M image for bench/cm/cmcount.py, which calls the API functions
   directly: no vector table, no startup code, nothing but nanosemver.c and the
   libgcc and newlib-nano routines it pulls in. */
MEMORY { FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 256K
         RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 64K }
SECTIONS {
  .text : { *(.text*) *(.rodata*) } > FLASH
  .data : { *(.data*) } > RAM AT > FLASH
  .bss  : { *(.bss*) *(COMMON) } > RAM
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
}
