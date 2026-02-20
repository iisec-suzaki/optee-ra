#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

usage() {
  cat <<'USAGE'
Usage: ./secure-boot-gen-keys.sh [options]

Options:
  --cst-tar PATH   CST tarball path (e.g., cst-3.1.0.tgz)
  --out-dir PATH   Output directory (default: ./secure-boot-out)
  --pass PASS      CST key passphrase (default: test)
  -h, --help       Show this help

Notes:
  - Extracts CST, generates HABv4 PKI tree (RSA 2048, SHA-256, 4 SRK CAs)
  - Output: $OUT_DIR/cst/ with keys/, crts/, and SRK_1_2_3_4_fuses.bin
  - If keys already exist in $OUT_DIR/cst/, the script aborts to prevent
    accidental overwrite. Delete $OUT_DIR/cst/ manually to regenerate.
USAGE
}

CST_TARBALL=""
OUT_DIR="${SCRIPT_DIR}/secure-boot-out"
CST_PASS="test"

while [ $# -gt 0 ]; do
  case "$1" in
    --cst-tar)
      CST_TARBALL="$2"; shift 2;;
    --out-dir)
      OUT_DIR="$2"; shift 2;;
    --pass)
      CST_PASS="$2"; shift 2;;
    -h|--help)
      usage; exit 0;;
    *)
      echo "Unknown option: $1" >&2
      usage; exit 1;;
  esac
done

if [ -z "$CST_TARBALL" ]; then
  echo "Error: --cst-tar is required." >&2
  exit 1
fi

if [ ! -f "$CST_TARBALL" ]; then
  echo "Error: CST tarball not found: $CST_TARBALL" >&2
  exit 1
fi

if [ -f "$OUT_DIR/cst/keys/SRK_1_2_3_4_table.bin" ]; then
  echo "Error: keys already exist in $OUT_DIR/cst/" >&2
  echo "Delete $OUT_DIR/cst/ first if you want to regenerate." >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

# Build Docker image if needed
if ! docker image inspect veraison-yocto-builder >/dev/null 2>&1; then
  echo "Yocto Docker image not found; building it first..."
  docker build -f "$SCRIPT_DIR/Dockerfile.yocto" -t veraison-yocto-builder "$SCRIPT_DIR"
fi

CST_TARBALL_ABS="$(readlink -f "$CST_TARBALL")"
OUT_DIR_ABS="$(readlink -f "$OUT_DIR")"

docker run --rm \
  --user 0:0 \
  -v "$CST_TARBALL_ABS:/opt/cst.tar.gz:ro" \
  -v "$OUT_DIR_ABS:/out" \
  -e CST_PASS="$CST_PASS" \
  -w /out \
  veraison-yocto-builder \
  /bin/bash -c '
set -euo pipefail

mkdir -p /out/cst
tar -xf /opt/cst.tar.gz -C /out/cst --strip-components=1

CST_ROOT=/out/cst
KEY_DIR="$CST_ROOT/keys"
CRT_DIR="$CST_ROOT/crts"
PKI_SCRIPT="$KEY_DIR/hab4_pki_tree.sh"

if [ ! -x "$PKI_SCRIPT" ]; then
  echo "hab4_pki_tree.sh not found in CST package" >&2
  exit 1
fi

# Generate HABv4 PKI tree non-interactively
# Answers: no existing CA, no using HABv3, RSA 2048, 10yr validity, 4 SRK CAs, yes
printf "n\nn\n2048\n10\n4\ny\n" | (cd "$KEY_DIR" && /bin/sh "$PKI_SCRIPT")

SRK_TOOL="$CST_ROOT/linux64/bin/srktool"
if [ ! -x "$SRK_TOOL" ]; then
  echo "srktool not found in CST package" >&2
  exit 1
fi

"$SRK_TOOL" --hab_ver 4 \
  --table "$KEY_DIR/SRK_1_2_3_4_table.bin" \
  --efuses "$KEY_DIR/SRK_1_2_3_4_fuses.bin" \
  --digest sha256 \
  --certs "$CRT_DIR/SRK1_sha256_2048_65537_v3_ca_crt.pem,$CRT_DIR/SRK2_sha256_2048_65537_v3_ca_crt.pem,$CRT_DIR/SRK3_sha256_2048_65537_v3_ca_crt.pem,$CRT_DIR/SRK4_sha256_2048_65537_v3_ca_crt.pem"

echo ""
echo "=== SRK fuse values ==="
hexdump -e '"  " /4 "0x"' -e '/4 "%X""\n"' "$KEY_DIR/SRK_1_2_3_4_fuses.bin"
echo ""
echo "Keys generated in /out/cst/"
'

echo ""
echo "CST keys generated at: $OUT_DIR/cst/"
echo ""
echo "Back up these keys before burning fuses:"
echo "  tar -czf $OUT_DIR/secure-boot-keys.tar.gz -C $OUT_DIR cst"
