#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: ./secure-boot-patch-wic.sh [options]

Options:
  --wic PATH            Input WIC (optionally .zst)
  --signed-imx-boot PATH  Signed imx-boot binary
  --signed-image PATH     Signed kernel Image (optional)
  --out-dir PATH        Output directory (default: current)
  -h, --help            Show this help

Notes:
  - imx-boot is written at 32KB (seek=32)
  - Signed Image is copied into the boot partition (FAT)
USAGE
}

WIC_PATH=""
SIGNED_IMX_BOOT=""
SIGNED_IMAGE=""
OUT_DIR="."

while [ $# -gt 0 ]; do
  case "$1" in
    --wic)
      WIC_PATH="$2"; shift 2;;
    --signed-imx-boot)
      SIGNED_IMX_BOOT="$2"; shift 2;;
    --signed-image)
      SIGNED_IMAGE="$2"; shift 2;;
    --out-dir)
      OUT_DIR="$2"; shift 2;;
    -h|--help)
      usage; exit 0;;
    *)
      echo "Unknown option: $1" >&2
      usage; exit 1;;
  esac
  done

if [ -z "$WIC_PATH" ] || [ -z "$SIGNED_IMX_BOOT" ]; then
  echo "Error: --wic and --signed-imx-boot are required." >&2
  exit 1
fi

if [ ! -f "$WIC_PATH" ]; then
  echo "Error: WIC not found: $WIC_PATH" >&2
  exit 1
fi

if [ ! -f "$SIGNED_IMX_BOOT" ]; then
  echo "Error: signed imx-boot not found: $SIGNED_IMX_BOOT" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

WIC_BASE="$OUT_DIR/$(basename "$WIC_PATH")"
cp "$WIC_PATH" "$WIC_BASE"

if [[ "$WIC_BASE" == *.zst ]]; then
  zstd -df --rm "$WIC_BASE"
  WIC_RAW="${WIC_BASE%.zst}"
else
  WIC_RAW="$WIC_BASE"
fi

# Patch imx-boot at 32KB

dd if="$SIGNED_IMX_BOOT" of="$WIC_RAW" bs=1024 seek=32 conv=notrunc status=progress

# Patch kernel Image into boot partition
if [ -n "$SIGNED_IMAGE" ] && [ -f "$SIGNED_IMAGE" ]; then
  BOOT_OFFSET=$(parted -sm "$WIC_RAW" unit B print | awk -F: '$1==1 {gsub("B","",$2); print $2; exit}')
  if [ -z "$BOOT_OFFSET" ]; then
    echo "Warning: failed to detect boot partition offset; kernel not patched" >&2
  else
    MNT_DIR=$(mktemp -d)
    sudo mount -o loop,offset="$BOOT_OFFSET" "$WIC_RAW" "$MNT_DIR"
    sudo cp "$SIGNED_IMAGE" "$MNT_DIR/Image"
    sync
    sudo umount "$MNT_DIR"
    rmdir "$MNT_DIR"
    echo "Patched kernel Image in boot partition"
  fi
fi

if [[ "$WIC_PATH" == *.zst ]]; then
  zstd -f "$WIC_RAW"
  echo "Patched WIC: ${WIC_RAW}.zst"
else
  echo "Patched WIC: $WIC_RAW"
fi
