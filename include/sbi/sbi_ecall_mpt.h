/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef __SBI_ECALL_MPT_H__
#define __SBI_ECALL_MPT_H__

/* Private, experimental ABI for the NEMU MPT demo; not a ratified SBI ABI. */
#define SBI_EXT_MPT_DEMO		0x084d5054
#define SBI_EXT_MPT_RESET	0 /* a0: default RWX permissions */
#define SBI_EXT_MPT_SET_PAGE	1 /* a0: page PA, a1: RWX permissions */
#define SBI_EXT_MPT_GET_PAGE	2 /* a0: page PA; value: RWX permissions */
#define SBI_EXT_MPT_ENABLE	3
#define SBI_EXT_MPT_DISABLE	4
#define SBI_EXT_MPT_STATUS	5 /* value: bit0 initialized, bit1 enabled */

#define SBI_MPT_R		1
#define SBI_MPT_W		2
#define SBI_MPT_X		4
#define SBI_MPT_RWX		7

#endif
