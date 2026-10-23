/* SPDX-License-Identifier: BSD-2-Clause */
OUTPUT_ARCH(riscv)
ENTRY(_start)
PHDRS
{
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
	kernel PT_LOAD FLAGS(5);
	initrd PT_LOAD FLAGS(4);
}
SECTIONS
{
	. = 0x80200000;
	.text : { *(.text.entry) *(.text .text.*) } :text
	.rodata : { *(.rodata .rodata.*) } :text
	. = ALIGN(4096);
	.test_exec : { *(.test_exec) } :text
	. = ALIGN(4096);
	.data : { *(.data .data.*) *(.sdata .sdata.*) } :data
	.bss (NOLOAD) : {
		__bss_start = .;
		*(.bss .bss.*) *(.sbss .sbss.*) *(COMMON)
		. = ALIGN(16);
		. += 16384;
		__stack_top = .;
		__bss_end = .;
	} :data
	ASSERT(. < 0x80400000, "MPT shim overlaps Linux")
	. = 0x80400000;
	.kernel_image : { *(.kernel_image) } :kernel
	ASSERT(. <= 0x84000000, "Linux overlaps initramfs")
	. = 0x84000000;
	.initrd : { *(.initrd) } :initrd
}
