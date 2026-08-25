/* SPDX-License-Identifier: GPL-2.0-or-later */

ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0;
    .vectors : {
        KEEP(*(.vectors))
    } :text

    . = 0x10000;
    .text : {
        *(.text.boot)
        *(.text*)
    } :text
    .rodata : {
        *(.rodata*)
    } :text
    .data : {
        *(.data*)
    } :data
    .bss (NOLOAD) : {
        *(.bss*)
        *(COMMON)
    } :data

    ASSERT(. < 0x100000, "test image does not fit in identity mapping")

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.comment)
        *(.note*)
    }
}
