PHDRS
{
    headers PT_PHDR PHDRS;
    text    PT_LOAD FILEHDR PHDRS;
    rodata  PT_LOAD;
    data    PT_LOAD FLAGS(7);
    dynamic PT_DYNAMIC;
}

SECTIONS
{
    . = 0xffffffff80000000 + SIZEOF_HEADERS;
    __kernel_start = . - SIZEOF_HEADERS;

    .text : {
        *(.text .text.*)
    } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .rodata : {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .data : {
        *(.data .data.*)
        *(.sdata .sdata.*)
        KEEP(*(.limine_requests_start_marker))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end_marker))
    } :data

    .dynamic : {
        *(.dynamic)
    } :data :dynamic

    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        __bss_end = .;
    } :data

    .user_exec (NOLOAD) : ALIGN(CONSTANT(MAXPAGESIZE)) {
        __xsh_exec_base = .;
        . += 0x100000;
        __xsh_exec_end = .;

        . = ALIGN(CONSTANT(MAXPAGESIZE));
        __cmd_exec_base = .;
        . += 0x100000;
        __cmd_exec_end = .;
    } :data

    __kernel_end = .;

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
        *(.interp)
        *(.comment)
    }
}
