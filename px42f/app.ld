/* A PX-42F app: everything in one block at PX_APP_BASE (px_app.h), the
 * header first, then the zeroed bss, then the heap up to the end of the
 * app's 128 KB. */
MEMORY {
    RAM (rwx) : ORIGIN = 0x20060000, LENGTH = 128K
}

ENTRY(px_app_header)

SECTIONS {
    .image : {
        KEEP(*(.px_header))
        *(.text .text.*)
        *(.glue_7 .glue_7t .vfp11_veneer .v4_bx .iplt .rel.iplt .igot.plt) /* the linker's stubs */
        *(.rodata .rodata.*)
        . = ALIGN(4);
        *(.data .data.*)
        . = ALIGN(4);
        KEEP(*(.init_array .init_array.* .fini_array .fini_array.*))
        . = ALIGN(4);
    } > RAM

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > RAM

    __heap_start = .;

    /* No unwind tables (C, no exceptions: discarded below) -- but parts of
     * the C library refer to the unwinder, which wants to know where
     * they'd be: nowhere. */
    __exidx_start = .;
    __exidx_end = .;

    /* Not loaded: kept in the .elf for the debugger and tools. Anything
     * else the linker finds is an error (--orphan-handling): it would
     * land outside the image. */
    .ARM.attributes 0 : { KEEP(*(.ARM.attributes)) }
    .comment 0 : { *(.comment) }
    .debug_frame 0 : { *(.debug_frame) }
    .debug_info 0 : { *(.debug_info) }
    .debug_abbrev 0 : { *(.debug_abbrev) }
    .debug_line 0 : { *(.debug_line) }
    .debug_str 0 : { *(.debug_str) }
    .debug_line_str 0 : { *(.debug_line_str) }
    .debug_aranges 0 : { *(.debug_aranges) }
    .debug_rnglists 0 : { *(.debug_rnglists) }
    .debug_loclists 0 : { *(.debug_loclists) }

    /DISCARD/ : {
        *(.ARM.exidx* .ARM.extab* .init .fini .eh_frame)
    }
}
