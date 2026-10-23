/* SPDX-License-Identifier: BSD-2-Clause */
#include <sbi/sbi_ecall_mpt.h>

typedef unsigned long ulong;
struct result { long error; ulong value; };
struct boot_args { ulong hart, fdt; };
volatile ulong fault_cause, fault_addr, fault_resume;
#ifndef EXPECT_ABSENT
static ulong data_pages[3 * 4096 / sizeof(ulong)] __attribute__((aligned(4096)));
#endif
extern char exec_page[];
extern ulong probe_load(ulong addr);
extern void probe_store(ulong addr, ulong value);
extern ulong probe_exec(ulong addr);
static unsigned int checks;

static struct result call(ulong eid, ulong fid, ulong arg0, ulong arg1)
{
	register ulong a0 asm("a0") = arg0;
	register ulong a1 asm("a1") = arg1;
	register ulong a6 asm("a6") = fid;
	register ulong a7 asm("a7") = eid;

	asm volatile ("ecall" : "+r"(a0), "+r"(a1) : "r"(a6), "r"(a7) : "memory");
	return (struct result) { (long)a0, a1 };
}

static struct result mpt(ulong fid, ulong a0, ulong a1)
{
	return call(SBI_EXT_MPT_DEMO, fid, a0, a1);
}

static void puts(const char *str)
{
	while (*str)
		call(1, 0, *str++, 0);
}

static void hex(ulong value)
{
	int shift;
	for (shift = 60; shift >= 0; shift -= 4)
		call(1, 0, "0123456789abcdef"[(value >> shift) & 15], 0);
}

static void check(int ok, const char *name)
{
	checks++;
	if (ok)
		return;
	puts("\nMPT-DEMO FAIL: ");
	puts(name);
	puts(" check="); hex(checks);
	puts(" scause="); hex(fault_cause);
	puts(" stval="); hex(fault_addr);
	puts("\n");
	register ulong a0 asm("a0") = 1;
	asm volatile (".word 0x0000006b" : : "r"(a0) : "memory");
	for (;;) asm volatile ("wfi");
}

#ifndef EXPECT_ABSENT
static void success(ulong fid, ulong a0, ulong a1)
{
	check(mpt(fid, a0, a1).error == 0, "SBI operation");
}

static void query(ulong addr, ulong perm)
{
	struct result r = mpt(SBI_EXT_MPT_GET_PAGE, addr, 0);
	check(!r.error && r.value == perm, "page permission");
}

static void status(ulong bits)
{
	struct result r = mpt(SBI_EXT_MPT_STATUS, 0, 0);
	check(!r.error && r.value == bits, "status");
}
#endif

struct boot_args demo_main(ulong hart, ulong fdt)
{
	struct result r = call(0x10, 3, SBI_EXT_MPT_DEMO, 0);
#ifndef EXPECT_ABSENT
	ulong page = (ulong)data_pages + 4096;
	ulong code = (ulong)exec_page;
	unsigned int i;

	puts("\nMPT-DEMO: S-mode tests starting\n");
	check(!r.error && r.value == 1, "base probe");
	status(0);
	check(mpt(SBI_EXT_MPT_ENABLE, 0, 0).error == -10, "enable before reset");
	check(mpt(SBI_EXT_MPT_GET_PAGE, page, 0).error == -10, "get before reset");
	check(mpt(SBI_EXT_MPT_SET_PAGE, page, 1).error == -10, "set before reset");
	check(mpt(99, 0, 0).error == -2, "unknown FID");
	check(mpt(SBI_EXT_MPT_RESET, 8, 0).error == -3, "invalid reset");
	success(SBI_EXT_MPT_RESET, 0, 0);
	success(SBI_EXT_MPT_SET_PAGE, page, 7);
	query(page, 7);
	query(page - 4096, 0);
	query(page + 4096, 0);
	query((1UL << 43) - 4096, 0);
	success(SBI_EXT_MPT_RESET, 7, 0);
	status(1);
	probe_load(0x80000000);
	check(fault_cause == 5 && fault_addr == 0x80000000,
	      "PMP protects firmware even with MPT disabled");
	check(mpt(SBI_EXT_MPT_SET_PAGE, page + 1, 1).error == -3, "unaligned");
	check(mpt(SBI_EXT_MPT_SET_PAGE, 1UL << 43, 1).error == -3, "out of range");
	check(mpt(SBI_EXT_MPT_GET_PAGE, 1UL << 43, 0).error == -3, "get range");
	check(mpt(SBI_EXT_MPT_SET_PAGE, page, 8).error == -3, "invalid permissions");
	check(mpt(SBI_EXT_MPT_SET_PAGE, 0x80000000, 1).error == -5, "firmware protected");
	/* Split paths around each indexing boundary; never access these addresses. */
	const ulong boundaries[] = { 1UL << 16, 1UL << 25, 1UL << 34 };
	for (i = 0; i < 3; i++) {
		ulong addr = boundaries[i];
		success(SBI_EXT_MPT_SET_PAGE, addr - 4096, 2);
		success(SBI_EXT_MPT_SET_PAGE, addr, 1);
		query(addr - 8192, 7);
		query(addr - 4096, 2);
		query(addr, 1);
		query(addr + 4096, 7);
	}
	/* Sixteen distinct root entries each need two child tables; only 31 free. */
	success(SBI_EXT_MPT_RESET, 7, 0);
	for (i = 1; i <= 15; i++)
		success(SBI_EXT_MPT_SET_PAGE, (ulong)i << 34, 1);
	check(mpt(SBI_EXT_MPT_SET_PAGE, 16UL << 34, 1).error == -1, "pool exhaustion");
	query(16UL << 34, 7);
	query(15UL << 34, 1);
	success(SBI_EXT_MPT_RESET, 7, 0);
	query(15UL << 34, 7);
	puts("MPT-DEMO: ABI, splitting, exhaustion PASS\n");

	/* Exercise all eight permission combinations with real S-mode accesses. */
	for (i = 0; i < 8; i++) {
		success(SBI_EXT_MPT_DISABLE, 0, 0);
		*(volatile ulong *)page = 123;
		success(SBI_EXT_MPT_SET_PAGE, page, i);
		success(SBI_EXT_MPT_SET_PAGE, code, i);
		query(page, i);
		query(page - 4096, 7);
		query(page + 4096, 7);
		success(SBI_EXT_MPT_ENABLE, 0, 0);
		status(3);
		check(mpt(SBI_EXT_MPT_RESET, 7, 0).error == -4, "reset while enabled");
		check(mpt(SBI_EXT_MPT_SET_PAGE, page, 7).error == -4, "set while enabled");
		ulong value = probe_load(page);
		check(i & 1 ? (!fault_cause && value == 123) :
			(fault_cause == 5 && fault_addr == page), "load enforcement");
		probe_store(page, 456);
		check(i & 2 ? !fault_cause :
			(fault_cause == 7 && fault_addr == page), "store enforcement");
		value = probe_exec(code);
		check(i & 4 ? (!fault_cause && value == 42) :
			(fault_cause == 1 && fault_addr == code), "execute enforcement");
		success(SBI_EXT_MPT_DISABLE, 0, 0);
		check(probe_load(page) == (i & 2 ? 456 : 123) && !fault_cause,
		      "disable restores access and denied stores do not commit");
	}
	puts("MPT-DEMO: all RWX combinations and access faults PASS\n");
	/* Enforce MPT during Linux boot without restricting its RAM/MMIO. */
	success(SBI_EXT_MPT_RESET, 7, 0);
	success(SBI_EXT_MPT_ENABLE, 0, 0);
	status(3);
	puts("MPT-DEMO PASS checks=0x"); hex(checks);
	puts("; Linux @0x80400000, MPT enabled\n");
#else
	check(!r.error && !r.value, "extension should be absent");
	check(mpt(SBI_EXT_MPT_STATUS, 0, 0).error == -2, "absent extension call");
	puts("MPT-DEMO: absent-extension regression PASS; entering Linux\n");
#endif
	return (struct boot_args) { hart, fdt };
}
