#!/usr/bin/env python3
"""
IMA/TPM PCR10 helper (provided by Prof. Suzaki, IISEC; used unchanged)

- Default (no --verify, no --ima_log):
    Read IMA ascii_runtime_measurements and **recompute PCR[10]** from the top
    by chaining SHA-256 extensions of each entry's template-hash.
    Prints the simulated PCR[10] and line counts.

- --verify:
    In addition to the above simulation, read the **actual TPM PCR[10]** via
    `tpm2_pcrread sha256:10`, and search for the first line where the simulated
    PCR matches the TPM value. Prints [OK]/[NG] and the matching line if any.

- --ima_log:
    Specify the path to the IMA ascii_runtime_measurements file.
    Default is /sys/kernel/security/ima/ascii_runtime_measurements.
    Use this option if you want to test with a saved log or a different location.

Notes:
- Assumes IMA template compatible with `ima-ng` (d-ng + n-ng) and SHA-256.
- Lines not using sha256 (e.g., sha1) are skipped.
- Paths with spaces are handled (everything after the 4th token).
"""

import argparse
import hashlib
import struct
import subprocess
from typing import Optional, Tuple

IMA_MEASUREMENTS_PATH = "/sys/kernel/security/ima/ascii_runtime_measurements"


def compute_template_hash(file_hash_hex: str, file_path: str) -> bytes:
    """Build ima-ng template bytes and return SHA-256(template_data).

    d-ng := <u32 len><"sha256:\x00" + file_hash>
    n-ng := <u32 len><file_path_utf8 + "\x00">

    Special-case: if file_hash_hex is 64 hex zeros, return 0xff * 32.
    """
    if file_hash_hex == "0" * 64:
        return b"\xff" * 32

    hash_algo = b"sha256:\x00"
    file_hash_bin = bytes.fromhex(file_hash_hex)

    d_ng_base = hash_algo + file_hash_bin
    d_ng = struct.pack("<I", len(d_ng_base)) + d_ng_base

    file_path_encoded = file_path.encode("utf-8") + b"\x00"
    n_ng = struct.pack("<I", len(file_path_encoded)) + file_path_encoded

    template_data = d_ng + n_ng
    return hashlib.sha256(template_data).digest()


def get_tpm_pcr10() -> Optional[str]:
    """Return TPM PCR[10] (hex string without spaces), or None on failure."""
    try:
        result = subprocess.run(
            ["tpm2_pcrread", "sha256:10"], capture_output=True, text=True, check=True
        )
        for line in result.stdout.splitlines():
            line = line.strip()
            if line.startswith("10:"):
                return line.split(":", 1)[1].strip().replace(" ", "")
    except subprocess.CalledProcessError as e:
        print(f"ERROR: Failed to run tpm2_pcrread: {e}")
    return None


def iterate_ima_entries(ima_path: str):
    """Yield (file_hash_hex, file_path) for each sha256-based IMA line.

    Skips any line with fewer than 5 tokens or non-sha256 entries.
    Handles paths containing spaces by joining tokens [4:].
    """
    with open(ima_path, "r") as f:
        for line in f:
            parts = line.rstrip("\n").split()
            if len(parts) < 5:
                continue
            algo_and_hash = parts[3]
            if not algo_and_hash.startswith("sha256:"):
                continue
            file_hash_hex = algo_and_hash.split(":", 1)[1]
            file_path = " ".join(parts[4:])
            yield file_hash_hex, file_path


def recompute_pcr10(ima_path: str) -> Tuple[str, int]:
    """Recompute PCR10 by chaining SHA-256(prev||template_hash) over log.

    Returns (pcr10_hex, count_of_processed_lines).
    """
    pcr10 = b"\x00" * 32  # start from zeroed PCR value
    count = 0

    for file_hash_hex, file_path in iterate_ima_entries(ima_path):
        template_hash = compute_template_hash(file_hash_hex, file_path)
        pcr10 = hashlib.sha256(pcr10 + template_hash).digest()
        count += 1

    return pcr10.hex(), count


def simulate_pcr10_until_match(ima_path: str, expected_pcr10_hex: str) -> Tuple[Optional[int], int]:
    """Return (first_matching_line_index, total_processed_lines).

    The index is 1-based over *processed* lines (i.e., only sha256-handled lines).
    """
    pcr10 = b"\x00" * 32
    expected = bytes.fromhex(expected_pcr10_hex.replace("0x", ""))

    line_index = 0
    first_match: Optional[int] = None

    for file_hash_hex, file_path in iterate_ima_entries(ima_path):
        line_index += 1
        template_hash = compute_template_hash(file_hash_hex, file_path)
        pcr10 = hashlib.sha256(pcr10 + template_hash).digest()
        if first_match is None and pcr10 == expected:
            first_match = line_index

    return first_match, line_index


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Recompute or verify TPM PCR[10] from IMA ascii_runtime_measurements.\n"
            "By default (no --verify, no --ima_log), only recomputes and prints the simulated PCR[10].\n"
            "Use --verify to compare against TPM and find the first matching line.\n"
            "Use --ima_log to specify an alternate IMA log file."
        )
    )
    parser.add_argument(
        "--ima_log",
        default=IMA_MEASUREMENTS_PATH,
        help="Path to IMA ascii_runtime_measurements (default: %(default)s)",
    )
    parser.add_argument(
        "--verify",
        action="store_true",
        help="Verify against current TPM PCR[10] and report first matching line",
    )
    args = parser.parse_args()

    # Always recompute once; it's useful for both modes.
    sim_pcr_hex, processed = recompute_pcr10(args.ima_log)

    print(f"Simulated PCR[10]     : 0x{sim_pcr_hex}")
    print(f"Processed IMA entries : {processed}")

    if not args.verify:
        return

    # --verify mode: compare against TPM and search for first matching line
    tpm_pcr10 = get_tpm_pcr10()
    if not tpm_pcr10:
        print("ERROR: Could not retrieve PCR[10] from TPM (tpm2-tools available? perms?).")
        return

    print(f"TPM PCR[10]           : 0x{tpm_pcr10}")

    match_line, total = simulate_pcr10_until_match(args.ima_log, tpm_pcr10)
    print(f"Search scope (lines)  : {total}")

    if match_line:
        print(f"[OK] Match found at processed-line : {match_line}")
    else:
        print("[NG] No matching PCR[10] value found during IMA log simulation.")


if __name__ == "__main__":
    main()
