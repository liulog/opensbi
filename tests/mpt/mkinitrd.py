#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Create a minimal newc archive without root privileges or host cpio."""
import pathlib
import sys

archive = bytearray()


def entry(name, mode, data=b"", device=(0, 0)):
    fields = (1, mode, 0, 0, 1, 0, len(data), 0, 0, *device, len(name) + 1, 0)
    archive.extend(b"070701" + b"".join(f"{v:08x}".encode() for v in fields))
    archive.extend(name.encode() + b"\0")
    archive.extend(b"\0" * (-len(archive) % 4))
    archive.extend(data)
    archive.extend(b"\0" * (-len(archive) % 4))


entry("dev", 0o040755)
entry("dev/kmsg", 0o020600, device=(1, 11))
entry("init", 0o100755, pathlib.Path(sys.argv[1]).read_bytes())
entry("TRAILER!!!", 0)
if len(archive) > 65536:
    raise SystemExit("initramfs exceeds the reserved 64 KiB")
archive.extend(b"\0" * (65536 - len(archive)))
pathlib.Path(sys.argv[2]).write_bytes(archive)
