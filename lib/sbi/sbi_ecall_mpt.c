/* SPDX-License-Identifier: BSD-2-Clause */
/* Single-hart, opt-in demo of the custom NEMU MPT format. */
#include <sbi/riscv_asm.h>
#include <sbi/riscv_barrier.h>
#include <sbi/sbi_csr_detect.h>
#include <sbi/sbi_domain.h>
#include <sbi/sbi_ecall.h>
#include <sbi/sbi_ecall_mpt.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_trap.h>

#if __riscv_xlen == 64
#define CSR_NEMU_MMPT	0x382
#define MPT_PAGES	32
#define MPT_ENTRIES	512
#define MPT_LEAF	3UL
#define MPT_MODE	(1UL << 60)
#define MPT_LIMIT	(1UL << 43)

/* In firmware BSS, hence covered by OpenSBI's existing M-mode protection. */
static unsigned long tables[MPT_PAGES][MPT_ENTRIES]
				__attribute__((aligned(4096)));
static unsigned int used;
static bool enabled;

static unsigned long leaf(unsigned long perm)
{
	unsigned long entry = MPT_LEAF;
	unsigned int i;

	for (i = 0; i < 16; i++)
		entry |= perm << (10 + 3 * i);
	return entry;
}

static void fill(unsigned long *table, unsigned long entry)
{
	unsigned int i;

	for (i = 0; i < MPT_ENTRIES; i++)
		table[i] = entry;
}

/* NEMU flushes its translation cache on every mmpt write. */
static void activate(unsigned long value)
{
	mb();
	csr_write(CSR_NEMU_MMPT, value);
	mb();
}

static bool valid_page(unsigned long addr)
{
	return addr < MPT_LIMIT && !(addr & 4095UL);
}

static unsigned long get_page(unsigned long addr)
{
	unsigned long *table = tables[0], entry;
	int level;

	for (level = 2; level >= 0; level--) {
		entry = table[(addr >> (16 + 9 * level)) & 511];
		if (entry & 2)
			return (entry >> (10 + 3 *
				((addr >> (12 + 9 * level)) & 15))) & 7;
		table = (unsigned long *)((entry >> 10) << 12);
	}
	return 0; /* All tables are constructed locally; unreachable. */
}

static int set_page(unsigned long addr, unsigned long perm)
{
	unsigned long *table = tables[0], *child, *slot, entry;
	unsigned int i, shift;
	int level;

	/* Reserve enough pages before changing the tree (at most two splits). */
	for (level = 2; level > 0; level--) {
		entry = table[(addr >> (16 + 9 * level)) & 511];
		if (entry & 2) {
			if (used + level > MPT_PAGES)
				return SBI_ERR_FAILED;
			break;
		}
		table = (unsigned long *)((entry >> 10) << 12);
	}

	table = tables[0];
	for (level = 2; level > 0; level--) {
		slot = &table[(addr >> (16 + 9 * level)) & 511];
		entry = *slot;
		if (entry & 2) {
			child = tables[used++];
			/* Each parent permission covers 32 child entries. */
			for (i = 0; i < MPT_ENTRIES; i++)
				child[i] = leaf((entry >> (10 + 3 * (i / 32))) & 7);
			*slot = (((unsigned long)child >> 12) << 10) | 1;
		} else {
			child = (unsigned long *)((entry >> 10) << 12);
		}
		table = child;
	}
	slot = &table[(addr >> 16) & 511];
	shift = 10 + 3 * ((addr >> 12) & 15);
	*slot = (*slot & ~(7UL << shift)) | (perm << shift);
	return 0;
}

static int mpt_handler(unsigned long extid, unsigned long funcid,
		       struct sbi_trap_regs *regs,
		       struct sbi_ecall_return *out)
{
	/* No guest/domain policy or cross-hart synchronization in this demo. */
	if (sbi_domain_thishart_ptr() != &root ||
	    (regs->mstatus & MSTATUS_MPV))
		return SBI_ERR_DENIED;

	switch (funcid) {
	case SBI_EXT_MPT_STATUS:
		out->value = (used ? 1 : 0) | (enabled ? 2 : 0);
		return 0;
	case SBI_EXT_MPT_DISABLE:
		activate(0);
		enabled = false;
		return 0;
	case SBI_EXT_MPT_RESET:
		if (regs->a0 > SBI_MPT_RWX)
			return SBI_ERR_INVALID_PARAM;
		if (enabled)
			return SBI_ERR_DENIED;
		fill(tables[0], leaf(regs->a0));
		used = 1;
		return 0;
	case SBI_EXT_MPT_SET_PAGE:
	case SBI_EXT_MPT_GET_PAGE:
		if (!valid_page(regs->a0) ||
		    (funcid == SBI_EXT_MPT_SET_PAGE && regs->a1 > SBI_MPT_RWX))
			return SBI_ERR_INVALID_PARAM;
		if (!used)
			return SBI_ERR_INVALID_STATE;
		if (funcid == SBI_EXT_MPT_GET_PAGE) {
			out->value = get_page(regs->a0);
			return 0;
		}
		if (enabled)
			return SBI_ERR_DENIED;
		if (!sbi_domain_check_addr_range(&root, regs->a0, 4096,
						PRV_S, SBI_DOMAIN_READ))
			return SBI_ERR_INVALID_ADDRESS;
		return set_page(regs->a0, regs->a1);
	case SBI_EXT_MPT_ENABLE:
		if (!used)
			return SBI_ERR_INVALID_STATE;
		activate(MPT_MODE | ((unsigned long)tables[0] >> 12));
		enabled = true;
		return 0;
	default:
		return SBI_ERR_NOT_SUPPORTED;
	}
}
#endif

struct sbi_ecall_extension ecall_mpt;

static int mpt_register(void)
{
#if __riscv_xlen == 64
	struct sbi_trap_info trap = { 0 };
	unsigned long value;

	if (sbi_hart_count() != 1)
		return 0;
	value = csr_read_allowed(CSR_NEMU_MMPT, &trap);
	/* Do not take ownership of an already configured table. */
	if (trap.cause || value)
		return 0;
	return sbi_ecall_register_extension(&ecall_mpt);
#else
	return 0;
#endif
}

struct sbi_ecall_extension ecall_mpt = {
	.name = "mptdemo",
	.extid_start = SBI_EXT_MPT_DEMO,
	.extid_end = SBI_EXT_MPT_DEMO,
	.experimental = true,
	.register_extensions = mpt_register,
#if __riscv_xlen == 64
	.handle = mpt_handler,
#endif
};
