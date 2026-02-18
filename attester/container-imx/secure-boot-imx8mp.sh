#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

usage() {
  cat <<'USAGE'
Usage: ./secure-boot-imx8mp.sh [options]

Options:
  --yocto-dir PATH   Yocto directory (default: ./yocto)
  --cst-tar PATH     CST tarball path (e.g., cst-3.1.0.tgz)
  --cst-dir PATH     CST directory (already extracted)
  --out-dir PATH     Output directory (default: ./secure-boot-out)
  --pass PASS        CST key passphrase (default: test)
  --patch-wic PATH   Patch WIC image with signed imx-boot + Image (optional)
  -h, --help         Show this help

Notes:
  - This script orchestrates:
      1) sign imx-boot
      2) sign kernel Image
      3) patch WIC (optional)
USAGE
}

YOCTO_DIR="${YOCTO_DIR:-${SCRIPT_DIR}/yocto}"
CST_TARBALL=""
CST_DIR=""
OUT_DIR="${SCRIPT_DIR}/secure-boot-out"
CST_PASS="test"
PATCH_WIC=""

while [ $# -gt 0 ]; do
  case "$1" in
    --yocto-dir)
      YOCTO_DIR="$2"; shift 2;;
    --cst-tar)
      CST_TARBALL="$2"; shift 2;;
    --cst-dir)
      CST_DIR="$2"; shift 2;;
    --out-dir)
      OUT_DIR="$2"; shift 2;;
    --pass)
      CST_PASS="$2"; shift 2;;
    --patch-wic)
      PATCH_WIC="$2"; shift 2;;
    -h|--help)
      usage; exit 0;;
    *)
      echo "Unknown option: $1" >&2
      usage; exit 1;;
  esac
  done

if [ -z "$CST_TARBALL" ] && [ -z "$CST_DIR" ]; then
  echo "Error: --cst-tar or --cst-dir is required." >&2
  exit 1
fi

SIGN_IMX_BOOT="$SCRIPT_DIR/secure-boot-sign-imx-boot.sh"
SIGN_KERNEL="$SCRIPT_DIR/secure-boot-sign-kernel.sh"
PATCH_WIC_SCRIPT="$SCRIPT_DIR/secure-boot-patch-wic.sh"

"$SIGN_IMX_BOOT" \
  --yocto-dir "$YOCTO_DIR" \
  ${CST_TARBALL:+--cst-tar "$CST_TARBALL"} \
  ${CST_DIR:+--cst-dir "$CST_DIR"} \
  --out-dir "$OUT_DIR" \
  --pass "$CST_PASS"

"$SIGN_KERNEL" \
  --yocto-dir "$YOCTO_DIR" \
  ${CST_TARBALL:+--cst-tar "$CST_TARBALL"} \
  ${CST_DIR:+--cst-dir "$CST_DIR"} \
  --out-dir "$OUT_DIR" \
  --pass "$CST_PASS"

if [ -n "$PATCH_WIC" ]; then
  "$PATCH_WIC_SCRIPT" \
    --wic "$PATCH_WIC" \
    --signed-imx-boot "$OUT_DIR/imx-boot-imx8mpevk-sd.bin-flash_evk-signed" \
    --signed-image "$OUT_DIR/img/Image-signed" \
    --out-dir "$OUT_DIR"
fi
