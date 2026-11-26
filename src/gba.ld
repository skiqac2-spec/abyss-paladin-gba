OUTPUT_ARCH(arm)
ENTRY(_start)
MEMORY {
  rom   : ORIGIN = 0x08000000, LENGTH = 32M
  ewram : ORIGIN = 0x02000000, LENGTH = 256K
  iwram : ORIGIN = 0x03000000, LENGTH = 31K
}
SECTIONS {
  .text : {
    KEEP(*(.crt0))
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(4);
  } > rom
  .ARM.exidx : { *(.ARM.exidx*) } > rom
  __data_lma = ALIGN(4);
  .data : AT(__data_lma) {
    __data_start = .;
    *(.data .data.*)
    . = ALIGN(4);
    __data_end = .;
  } > iwram
  __iwram_lma = __data_lma + SIZEOF(.data);
  .iwram : AT(__iwram_lma) {
    __iwram_start = .;
    *(.iwram .iwram.*)
    . = ALIGN(4);
    __iwram_end = .;
  } > iwram
  .bss (NOLOAD) : {
    __bss_start = .;
    *(.bss .bss.* COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > iwram
  .ewram_bss (NOLOAD) : {
    __ewram_bss_start = .;
    *(.ewram_bss .ewram_bss.*)
    . = ALIGN(4);
    __ewram_bss_end = .;
  } > ewram
  /DISCARD/ : { *(.comment) *(.ARM.attributes) }
}
