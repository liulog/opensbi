/* SPDX-License-Identifier: BSD-2-Clause */
	.option norelax
	.option norvc
	.section .text.entry, "ax"
	.globl _start
_start:
	csrw sie, zero
	csrw satp, zero
	la t0, __bss_start
	la t1, __bss_end
1:
	sd zero, 0(t0)
	addi t0, t0, 8
	bltu t0, t1, 1b
	la sp, __stack_top
	la t0, trap_entry
	csrw stvec, t0
	call demo_main
	/* demo_main returns the original hart ID and DTB as a two-word struct. */
	csrw sie, zero
	csrw sscratch, zero
	csrw satp, zero
	sfence.vma
	fence.i
	li t0, 0x80400000
	jr t0

	.balign 4
trap_entry:
	addi sp, sp, -32
	sd t0, 0(sp)
	sd t1, 8(sp)
	la t0, fault_cause
	csrr t1, scause
	sd t1, 0(t0)
	la t0, fault_addr
	csrr t1, stval
	sd t1, 0(t0)
	la t0, fault_resume
	ld t1, 0(t0)
	beqz t1, unexpected_trap
	csrw sepc, t1
	ld t0, 0(sp)
	ld t1, 8(sp)
	addi sp, sp, 32
	sret
unexpected_trap:
	li a0, 1
	.word 0x0000006b
	j unexpected_trap

	/* These access probes install an exact recovery PC (including fetch faults). */
	.macro begin_probe
	la t0, fault_cause
	sd zero, 0(t0)
	la t0, fault_resume
	la t1, 1f
	sd t1, 0(t0)
	.endm
	.macro end_probe
1:
	la t0, fault_resume
	sd zero, 0(t0)
	ret
	.endm
	.globl probe_load
probe_load:
	begin_probe
	ld a0, 0(a0)
	end_probe
	.globl probe_store
probe_store:
	begin_probe
	sd a1, 0(a0)
	end_probe
	.globl probe_exec
probe_exec:
	addi sp, sp, -16
	sd ra, 0(sp)
	begin_probe
	jalr ra, a0, 0
1:
	la t0, fault_resume
	sd zero, 0(t0)
	ld ra, 0(sp)
	addi sp, sp, 16
	ret

	.section .test_exec, "ax"
	.balign 4096
	.globl exec_page
exec_page:
	li a0, 42
	ret
	.balign 4096

	.section .kernel_image, "ax"
	.incbin LINUX_IMAGE

	.section .initrd, "a"
	.incbin INITRD_IMAGE
