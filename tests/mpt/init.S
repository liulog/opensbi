/* SPDX-License-Identifier: BSD-2-Clause */
/* Tiny initramfs /init: prove the unchanged Linux Image reaches U-mode. */
	.section .text
	.globl _start
_start:
	/* /dev/kmsg uses the polling kernel console, not IRQ-driven tty TX. */
	li a0, -100 /* AT_FDCWD */
	la a1, device
	li a2, 1 /* O_WRONLY */
	li a3, 0
	li a7, 56 /* openat */
	ecall
	bltz a0, fail
	la a1, message
	la a2, message_end
	sub a2, a2, a1
	mv s0, a2
	li a7, 64 /* write */
	ecall
	bne a0, s0, fail
	li a0, 0
	/* NEMU's simulation-exit instruction, not a standard RISC-V opcode. */
	.word 0x0000006b
fail:
	li a0, 1
	.word 0x0000006b
	j fail
	.section .rodata
device:
	.asciz "/dev/kmsg"
message:
	.ascii "MPT-DEMO: Linux userspace /init PASS\n"
message_end:
