#!/usr/bin/env python3
"""Compute the PRoT reference measurement-value from a tee.elf build artifact.

This is the offline equivalent of the remote attestation PTA's
get_hash_tee_memory(): SHA-256 over the OP-TEE core's immutable memory,
hashed in the same order as the runtime measurement:

    [__text_start,      __text_data_start)
    [__text_data_end,   __text_end)
    (pager text sections, if present)
    [__rodata_start,    __rodata_end)
    (pager rodata sections, if present)

Because the PRoT measurement-value is build-specific (core_v_str embeds the
build timestamp), the reference value must be recomputed and re-provisioned
for every released build. This script makes that possible without booting
the image or raising the core log level.

Usage:
    ./compute-prot-refval.py path/to/tee.elf

Output: the base64 measurement-value to put in the comid reference value
(digests entry: "sha-256;<base64>").

Requires: pyelftools (pip install pyelftools)
"""
import base64
import hashlib
import sys

from elftools.elf.elffile import ELFFile


def load_symbols(elf):
    syms = {}
    for secname in (".symtab", ".dynsym"):
        sec = elf.get_section_by_name(secname)
        if sec is None:
            continue
        for sym in sec.iter_symbols():
            syms[sym.name] = sym["st_value"]
    return syms


def read_va_range(elf, start, end):
    """Read [start, end) virtual addresses from the ELF LOAD segments."""
    if end <= start:
        return b""
    out = bytearray(end - start)
    covered = 0
    for seg in elf.iter_segments():
        if seg["p_type"] != "PT_LOAD":
            continue
        seg_va = seg["p_vaddr"]
        seg_filesz = seg["p_filesz"]
        lo = max(start, seg_va)
        hi = min(end, seg_va + seg_filesz)
        if lo >= hi:
            continue
        data = seg.data()
        out[lo - start:hi - start] = data[lo - seg_va:hi - seg_va]
        covered += hi - lo
    if covered != end - start:
        raise SystemExit(
            f"error: VA range 0x{start:x}..0x{end:x} not fully covered by "
            f"LOAD segments ({covered}/{end - start} bytes)")
    return bytes(out)


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 1
    path = sys.argv[1]

    with open(path, "rb") as f:
        elf = ELFFile(f)
        syms = load_symbols(elf)

        def addr(name):
            if name not in syms:
                raise SystemExit(f"error: symbol {name} not found in {path}")
            return syms[name]

        # Same span order as get_hash_tee_memory()
        spans = [
            (addr("__text_start"), addr("__text_data_start")),
            (addr("__text_data_end"), addr("__text_end")),
        ]
        if "__text_init_start" in syms:  # CFG_WITH_PAGER=y builds only
            spans += [
                (addr("__text_init_start"), addr("__text_init_end")),
                (addr("__text_pageable_start"), addr("__text_pageable_end")),
            ]
        spans.append((addr("__rodata_start"), addr("__rodata_end")))
        if "__rodata_init_start" in syms:  # CFG_WITH_PAGER=y builds only
            spans += [
                (addr("__rodata_init_start"), addr("__rodata_init_end")),
                (addr("__rodata_pageable_start"),
                 addr("__rodata_pageable_end")),
            ]

        h = hashlib.sha256()
        for start, end in spans:
            h.update(read_va_range(elf, start, end))

    digest = h.digest()
    print(f"PRoT measurement-value (b64): {base64.b64encode(digest).decode()}")
    print(f"PRoT measurement-value (hex): {digest.hex()}")
    print(f'comid digests entry: "sha-256;{base64.b64encode(digest).decode()}"')
    return 0


if __name__ == "__main__":
    sys.exit(main())
