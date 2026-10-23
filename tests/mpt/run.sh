#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Build isolated copies; never reconfigure or run make in the original NEMU tree.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
mode=${1:-enabled}
case "$mode" in
    enabled) option=y; absent=(); nemu_variant=mpt ;;
    disabled) option=n; absent=(-DEXPECT_ABSENT); nemu_variant=mpt ;;
    no-csr) option=y; absent=(-DEXPECT_ABSENT); nemu_variant=no-mpt ;;
    *) echo "usage: $0 [enabled|disabled|no-csr]" >&2; exit 2 ;;
esac
cross=${CROSS_COMPILE:-riscv64-unknown-linux-gnu-}
jobs=${JOBS:-8}
nemu=$(realpath "${NEMU_SRC:-$root/../NEMU}")
image=$(realpath "${LINUX_IMAGE:-$root/../linux-riscv-gate/arch/riscv/boot/Image}")
base="$root/build/mpt-demo"
out="$base/$mode"
copy="$base/nemu-$nemu_variant"
mkdir -p "$out" "$copy"
rsync -a --exclude=.git --exclude=/build --exclude=/include/config \
    --exclude=/include/generated --exclude=/.config --exclude=/.config.old \
    "$nemu/" "$copy/"
python3 - "$copy" "$nemu_variant" <<'PY'
import pathlib
import sys
root = pathlib.Path(sys.argv[1])
text = (root / 'configs/riscv64-xs_mpt_defconfig').read_text()
# Keep 64-bit paddr_t in this NEMU (selected by MSIZE), with sparse mmap backing.
text = text.replace('CONFIG_MSIZE=0x7ff80000000', 'CONFIG_MSIZE=0x100000000')
# PERF_OPT disables NEMU PMP/PMA enforcement: do not use it for protection tests.
text = text.replace('CONFIG_PERF_OPT=y', '# CONFIG_PERF_OPT is not set\n'
                    'CONFIG_RV_PMP_CHECK=y\nCONFIG_RV_PMA_CHECK=y')
if sys.argv[2] == 'no-mpt':
    text = text.replace('CONFIG_RV_MPT_CHECK=y', '# CONFIG_RV_MPT_CHECK is not set')
(root / '.config').write_text(text)
PY
(
    cd "$copy"
    export NEMU_HOME="$copy"
    # Disable upstream's automatic git-add/commit recipes.
    make git_commit= "$copy/tools/kconfig/build/conf" "$copy/tools/fixdep/build/fixdep"
    tools/kconfig/build/conf --olddefconfig Kconfig
    tools/kconfig/build/conf --syncconfig Kconfig
    make git_commit= -j"$jobs"
) > "$out/nemu-build.log" 2>&1
cp "$copy/.config" "$out/nemu.config"

dtc -I dts -O dtb -o "$out/nemu.dtb" tests/mpt/nemu.dts
flags=(-march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany -mno-relax
       -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -static -no-pie
       -O2 -Wall -Wextra -Werror -Iinclude -Wl,--build-id=none)
"${cross}gcc" "${flags[@]}" tests/mpt/init.S -o "$out/init"
python3 tests/mpt/mkinitrd.py "$out/init" "$out/initrd.cpio"
"${cross}gcc" "${flags[@]}" "${absent[@]}" \
    "-DLINUX_IMAGE=\"$image\"" "-DINITRD_IMAGE=\"$out/initrd.cpio\"" \
    -Wl,-T,tests/mpt/payload.lds tests/mpt/payload.S tests/mpt/payload.c \
    -o "$out/payload.elf"
"${cross}objcopy" -O binary "$out/payload.elf" "$out/payload.bin"
make PLATFORM=generic O="$out/opensbi" CROSS_COMPILE="$cross" \
    CONFIG_SBI_ECALL_MPT_DEMO="$option" FW_FDT_PATH="$out/nemu.dtb" \
    FW_PAYLOAD_PATH="$out/payload.bin" FW_PAYLOAD_FDT_OFFSET=0x8000000 \
    -j"$jobs" > "$out/opensbi-build.log" 2>&1
sha256sum "$image" > "$out/linux-image.sha256"
timeout "${TIMEOUT:-120}s" "$copy/build/riscv64-nemu-interpreter" -b \
    -I 2000000000 -l "$out/nemu-runtime.log" \
    "$out/opensbi/platform/generic/firmware/fw_payload.bin" > "$out/console.log" 2>&1
# NEMU can return zero when its instruction budget expires: check guest markers too.
if [[ "$mode" == enabled ]]; then
    grep -q 'MPT-DEMO PASS checks=' "$out/console.log"
else
    grep -q 'MPT-DEMO: absent-extension regression PASS' "$out/console.log"
fi
grep -q 'MPT-DEMO: Linux userspace /init PASS' "$out/console.log"
grep -q 'HIT GOOD TRAP' "$out/nemu-runtime.log"
if grep -Eq 'MPT-DEMO FAIL|Kernel panic|Oops:' "$out/console.log"; then
    echo "FAIL: see $out/console.log" >&2
    exit 1
fi
echo "PASS ($mode): $out/console.log"
