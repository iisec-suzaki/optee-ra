#!/usr/bin/env python3
"""Compute the ARoT reference measurement-value from a TA binary.

This is the offline equivalent of the remote attestation PTA's
get_hash_ta_memory(): SHA-256 over the calling TA's read-only memory
regions as mapped by ldelf, which are

  - the first page of the TA ELF (ldelf maps file offset 0, 4096 bytes,
    read-only: ELF header + program headers, see ldelf init_elf()), and
  - every read-only PT_LOAD segment: file content zero-padded to the
    page-rounded memory size.

The regions are hashed smallest-first (ties broken by content), matching
the runtime cmp_regions() ordering, so the result is independent of TA
ASLR. Verified against the runtime measurement on QEMU.

Accepts either the raw TA ELF (*.elf / *.stripped.elf) or the signed
*.ta file (the signed header is skipped automatically).

Usage:
    ./compute-arot-refval.py path/to/<uuid>.ta

Requires: pyelftools (pip install pyelftools)
"""
import base64
import hashlib
import io
import sys

from elftools.elf.elffile import ELFFile

PAGE = 4096


def elf_bytes(path):
    """Return the ELF image contained in path (skip a .ta signed header)."""
    data = open(path, "rb").read()
    if data[:4] == b"\x7fELF":
        return data
    off = data.find(b"\x7fELF")
    if off < 0:
        raise SystemExit(f"error: no ELF image found in {path}")
    return data[off:]


def roundup(n, align=PAGE):
    return (n + align - 1) & ~(align - 1)


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 1

    data = elf_bytes(sys.argv[1])
    elf = ELFFile(io.BytesIO(data))

    # Region 1: the ELF header page mapped read-only by ldelf (file page 0)
    regions = [data[:PAGE].ljust(PAGE, b"\0")]

    # Read-only PT_LOAD segments, zero-padded to the mapped (page-rounded
    # memsz) size. Writable segments are not part of the measurement.
    ro_ranges = []
    for seg in elf.iter_segments():
        if seg["p_type"] != "PT_LOAD" or seg["p_flags"] & 0x2:  # PF_W
            continue
        content = data[seg["p_offset"]:seg["p_offset"] + seg["p_filesz"]]
        regions.append(content.ljust(roundup(seg["p_memsz"]), b"\0"))
        ro_ranges.append((seg["p_vaddr"], seg["p_vaddr"] + seg["p_memsz"]))

    # The measurement is only load-address-independent if no relocation
    # targets a read-only segment; warn if one does.
    for sec in elf.iter_sections():
        if not sec.name.startswith(".rela"):
            continue
        for rel in sec.iter_relocations():
            off = rel["r_offset"]
            if any(lo <= off < hi for lo, hi in ro_ranges):
                print(f"warning: relocation targets read-only segment "
                      f"(offset 0x{off:x}); the runtime measurement may "
                      f"not match this offline value", file=sys.stderr)
                break

    # Hash smallest-first, ties by content (matches runtime cmp_regions())
    regions.sort(key=lambda r: (len(r), r))

    h = hashlib.sha256()
    for r in regions:
        h.update(r)

    digest = h.digest()
    print(f"ARoT measurement-value (b64): {base64.b64encode(digest).decode()}")
    print(f"ARoT measurement-value (hex): {digest.hex()}")
    print(f'comid digests entry: "sha-256;{base64.b64encode(digest).decode()}"')
    return 0


if __name__ == "__main__":
    sys.exit(main())
