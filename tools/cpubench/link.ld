ENTRY(_start)
SECTIONS { . = 0; .text : { *(.text.start) *(.text*) } .rodata : { *(.rodata*) *(.srodata*) } .data : { *(.data*) *(.sdata*) } .bss : { *(.bss*) *(.sbss*) *(COMMON) } . = ALIGN(16); . += 0x10000; _stack_top = .; }
