OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
    requests PT_LOAD;
    text PT_LOAD;
    rodata PT_LOAD;
    data PT_LOAD;
}

SECTIONS
{
    . = 0xffffffff80000000;

    .limine_requests : ALIGN(8) {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :requests

    .text : ALIGN(4K) { *(.text .text.*) } :text
    .rodata : ALIGN(4K) { *(.rodata .rodata.*) } :rodata
    .data : ALIGN(4K) { *(.data .data.*) } :data
    .bss : ALIGN(4K) { *(COMMON) *(.bss .bss.*) } :data

    /DISCARD/ : { *(.eh_frame*) *(.note .note.*) *(.comment) }
}
