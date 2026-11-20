/* stage2.ld - ステージ2を LOAD_ADDR (Makefile で --defsym) に配置する */
OUTPUT_FORMAT("elf32-m68k")
ENTRY(_start)

SECTIONS
{
    . = LOAD_ADDR;

    .text : {
        KEEP(*(.text.start))
        *(.text .text.*)
        __text_end = .;
    }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }

    . = ALIGN(4);
    __bss_start = .;
    .bss : { *(.bss .bss.*) *(COMMON) }
    . = ALIGN(4);
    __bss_end = .;

    /* スタック (16KB)。ROM が用意したスタックは場所がわからず、ステージ2が大きくなると
       重なるおそれがあるので、自分の領域の中に用意する (crt0.S で切り替える) */
    . = ALIGN(16);
    __stack_bottom = .;
    . = . + 0x4000;
    __stack_top = .;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
