#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

usage() {
  cat <<'USAGE'
Usage: ./secure-boot-sign-imx-boot.sh [options]

Options:
  --yocto-dir PATH   Yocto directory (default: ./yocto)
  --cst-tar PATH     CST tarball path (e.g., cst-3.1.0.tgz)
  --cst-dir PATH     CST directory (already extracted)
  --out-dir PATH     Output directory (default: ./secure-boot-out)
  --pass PASS        CST key passphrase (default: test)
  -h, --help         Show this help

Notes:
  - This signs imx-boot (SPL + FIT) only.
  - Output: imx-boot-imx8mpevk-sd.bin-flash_evk-signed
USAGE
}

YOCTO_DIR="${YOCTO_DIR:-${SCRIPT_DIR}/yocto}"
CST_TARBALL=""
CST_DIR=""
OUT_DIR="${SCRIPT_DIR}/secure-boot-out"
CST_PASS="test"

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

IMX_BOOT_DIR="$YOCTO_DIR/build/tmp/deploy/images/imx8mpevk"
IMX_BOOT_BIN="$IMX_BOOT_DIR/imx-boot-imx8mpevk-sd.bin-flash_evk"
IMX_MKIMG_DIR="$YOCTO_DIR/build/tmp/work/imx8mpevk-poky-linux/imx-boot/1.0/git"
IMX_MKIMG_SOC_DIR="$IMX_MKIMG_DIR/iMX8M"

if [ ! -f "$IMX_BOOT_BIN" ]; then
  echo "Error: imx-boot not found at $IMX_BOOT_BIN" >&2
  echo "Run: YOCTO_DIR=$YOCTO_DIR ./yocto.sh" >&2
  exit 1
fi

if [ ! -d "$IMX_MKIMG_SOC_DIR" ]; then
  echo "Error: imx-mkimage directory not found at $IMX_MKIMG_SOC_DIR" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

# Build Docker image if needed
if ! docker image inspect veraison-yocto-builder >/dev/null 2>&1; then
  echo "Yocto Docker image not found; building it first..."
  docker build -f "$SCRIPT_DIR/Dockerfile.yocto" -t veraison-yocto-builder "$SCRIPT_DIR"
fi

CST_MOUNT_ARGS=""
if [ -n "$CST_TARBALL" ]; then
  CST_TARBALL_ABS="$(readlink -f "$CST_TARBALL")"
  CST_MOUNT_ARGS="-v $CST_TARBALL_ABS:/opt/cst.tar.gz:ro"
fi
if [ -n "$CST_DIR" ]; then
  CST_DIR_ABS="$(readlink -f "$CST_DIR")"
  CST_MOUNT_ARGS="$CST_MOUNT_ARGS -v $CST_DIR_ABS:/opt/cst:ro"
fi

OUT_DIR_ABS="$(readlink -f "$OUT_DIR")"
YOCTO_DIR_ABS="$(readlink -f "$YOCTO_DIR")"

# Run signing inside Docker
set -x

docker run --rm \
  --user 0:0 \
  -v "$YOCTO_DIR_ABS:/yocto" \
  -v "$YOCTO_DIR_ABS:$YOCTO_DIR_ABS" \
  -v "$OUT_DIR_ABS:/out" \
  $CST_MOUNT_ARGS \
  -e CST_PASS="$CST_PASS" \
  -w /out \
  veraison-yocto-builder \
  /bin/bash -c '
set -euo pipefail

if [ -x /opt/cst/linux64/bin/cst ]; then
  CST_ROOT=/opt/cst
elif [ -f /opt/cst.tar.gz ]; then
  if [ -x /out/cst/linux64/bin/cst ]; then
    CST_ROOT=/out/cst
  else
    mkdir -p /out/cst
    tar -xf /opt/cst.tar.gz -C /out/cst --strip-components=1
    CST_ROOT=/out/cst
  fi
else
  CST_ROOT=""
fi

if [ -z "$CST_ROOT" ] || [ ! -x "$CST_ROOT/linux64/bin/cst" ]; then
  echo "CST not found. Provide --cst-tar or --cst-dir with linux64/bin/cst" >&2
  exit 1
fi

CST_BIN="$CST_ROOT/linux64/bin/cst"
PKI_SCRIPT="$CST_ROOT/keys/hab4_pki_tree.sh"

if [ ! -x "$PKI_SCRIPT" ]; then
  echo "hab4_pki_tree.sh not found in CST package" >&2
  exit 1
fi

CSF_DIR=/out/csf
IMG_DIR=/out/img
LOG_DIR=/out/logs
mkdir -p "$CSF_DIR" "$IMG_DIR" "$LOG_DIR"

KEY_DIR="$CST_ROOT/keys"
CRT_DIR="$CST_ROOT/crts"
mkdir -p "$KEY_DIR" "$CRT_DIR"

# Generate PKI tree (test keys) non-interactively if missing
if [ ! -f "$KEY_DIR/SRK_1_2_3_4_table.bin" ]; then
  PKI_WORK="$CST_ROOT/keys"
  printf "n\nn\n2048\n10\n4\ny\n" | (cd "$PKI_WORK" && /bin/sh "$PKI_SCRIPT")

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
fi

IMX_MKIMG_DIR=/yocto/build/tmp/work/imx8mpevk-poky-linux/imx-boot/1.0/git
IMX_MKIMG_SOC_DIR="$IMX_MKIMG_DIR/iMX8M"

# Rebuild unsigned flash.bin to capture HAB blocks
( cd "$IMX_MKIMG_DIR" && make SOC=iMX8MP flash_spl_uboot ) >"$LOG_DIR/mkimage.log" 2>&1
cp "$IMX_MKIMG_SOC_DIR/flash.bin" "$IMG_DIR/flash.bin"

# Patch FIT hash into SPL (handles both no-FDT and FDT offsets)
SPL_MAP="$(find /yocto/build/tmp/work/imx8mpevk-poky-linux/u-boot-imx -name u-boot-spl.map | head -n1)"
if [ -z "$SPL_MAP" ] || [ ! -f "$SPL_MAP" ]; then
  echo "Failed to locate u-boot-spl.map for hash patch" >&2
  exit 1
fi

image_off_hex=$(awk "/^[[:space:]]*image_off[[:space:]]/ {print \$NF; exit}" "$LOG_DIR/mkimage.log")
if [ -z "$image_off_hex" ]; then
  echo "Failed to parse image_off from mkimage.log" >&2
  exit 1
fi
image_off=$((image_off_hex))

start_hex=$(awk "/__image_copy_start/ {print \$1; exit}" "$SPL_MAP")
if [ -z "$start_hex" ]; then
  start_hex=$(awk "/\\<_start\\>/ {print \$1; exit}" "$SPL_MAP")
fi
end_hex=$(awk "/ _end = \\./ {print \$1; exit}" "$SPL_MAP")
if [ -z "$start_hex" ] || [ -z "$end_hex" ]; then
  echo "Failed to parse _start/_end from $SPL_MAP" >&2
  exit 1
fi

start_addr=$((start_hex))
end_addr=$((end_hex))
end_off=$((end_addr - start_addr))

spl_size=$(stat -c %s "$IMX_MKIMG_SOC_DIR/u-boot-spl-ddr.bin")
hash_off=$((image_off + spl_size - 32))
nofdt_off=$((image_off + end_off + 0x18000))

spl_dtb_dir="$(dirname "$SPL_MAP")"
spl_dtb="$spl_dtb_dir/u-boot-spl.dtb"
if [ ! -f "$spl_dtb" ]; then
  echo "Failed to locate u-boot-spl.dtb for FDT size" >&2
  exit 1
fi
dtb_size_hex=$(od -An -tx1 -N4 -j4 "$spl_dtb" | tr -d " \n")
dtb_size=$((16#$dtb_size_hex))
dtb_size=$(((dtb_size + 3) & ~3))
fdt_off=$((image_off + end_off + dtb_size + 0x18000))

dd if="$IMG_DIR/flash.bin" of="$IMG_DIR/fit-hash.bin" bs=1 skip=$hash_off count=32 status=none

dd if="$IMG_DIR/fit-hash.bin" of="$IMG_DIR/flash.bin" bs=1 seek=$nofdt_off conv=notrunc status=none

dd if="$IMG_DIR/fit-hash.bin" of="$IMG_DIR/flash.bin" bs=1 seek=$fdt_off conv=notrunc status=none

# Extract HAB blocks and CSF offsets
spl_block=$(awk -F: "/spl hab block/{print \$2}" "$LOG_DIR/mkimage.log" | head -n1 | xargs)
sld_block=$(awk -F: "/sld hab block/{print \$2}" "$LOG_DIR/mkimage.log" | head -n1 | xargs)

csf_off=$(awk "/Loader IMAGE:/ {seen=1; next} seen && /csf_off/ {print \$NF; exit}" "$LOG_DIR/mkimage.log")
sld_csf_off=$(awk "/sld_csf_off/ {print \$NF; exit}" "$LOG_DIR/mkimage.log")

if [ -z "$spl_block" ] || [ -z "$sld_block" ] || [ -z "$csf_off" ] || [ -z "$sld_csf_off" ]; then
  echo "Failed to parse HAB blocks or CSF offsets. Check $LOG_DIR/mkimage.log" >&2
  exit 1
fi

# Generate FIT HAB blocks using print_fit_hab target
( cd "$IMX_MKIMG_DIR" && make SOC=iMX8MP print_fit_hab ) >"$LOG_DIR/fit_hab.log" 2>&1
fit_blocks=$(awk "/^0x/ {print \$1, \$2, \$3}" "$LOG_DIR/fit_hab.log")
if [ -z "$fit_blocks" ]; then
  echo "Failed to parse FIT HAB blocks. Check $LOG_DIR/fit_hab.log" >&2
  exit 1
fi

cat > "$CSF_DIR/csf_spl.txt" <<CSF_EOF
[Header]
Version = 4.3
Hash Algorithm = sha256
Engine = CAAM
Engine Configuration = 0
Certificate Format = X509
Signature Format = CMS

[Install SRK]
File = "$KEY_DIR/SRK_1_2_3_4_table.bin"
Source index = 0

[Install CSFK]
File = "$CRT_DIR/CSF1_1_sha256_2048_65537_v3_usr_crt.pem"

[Authenticate CSF]

[Unlock]
Engine = CAAM
Features = MID

[Install Key]
File = "$CRT_DIR/IMG1_1_sha256_2048_65537_v3_usr_crt.pem"
Verification index = 0
Target Index = 2

[Authenticate Data]
Verification index = 2
Engine = CAAM
Engine Configuration = 0
Blocks = $spl_block "$IMG_DIR/flash.bin"
CSF_EOF

cat > "$CSF_DIR/csf_fit.txt" <<CSF_EOF
[Header]
Version = 4.3
Hash Algorithm = sha256
Engine = CAAM
Engine Configuration = 0
Certificate Format = X509
Signature Format = CMS

[Install SRK]
File = "$KEY_DIR/SRK_1_2_3_4_table.bin"
Source index = 0

[Install CSFK]
File = "$CRT_DIR/CSF1_1_sha256_2048_65537_v3_usr_crt.pem"

[Authenticate CSF]

[Install Key]
File = "$CRT_DIR/IMG1_1_sha256_2048_65537_v3_usr_crt.pem"
Verification index = 0
Target Index = 2

[Authenticate Data]
Verification index = 2
Engine = CAAM
Engine Configuration = 0
Blocks = $sld_block "$IMG_DIR/flash.bin", \\
CSF_EOF

fit_count=$(echo "$fit_blocks" | wc -l)
fit_idx=0
while read -r addr off size; do
  fit_idx=$((fit_idx + 1))
  if [ "$fit_idx" -lt "$fit_count" ]; then
    echo "         $addr $off $size \"$IMG_DIR/flash.bin\", \\" >> "$CSF_DIR/csf_fit.txt"
  else
    echo "         $addr $off $size \"$IMG_DIR/flash.bin\"" >> "$CSF_DIR/csf_fit.txt"
  fi
done <<< "$fit_blocks"

"$CST_BIN" -i "$CSF_DIR/csf_spl.txt" -o "$CSF_DIR/csf_spl.bin"
"$CST_BIN" -i "$CSF_DIR/csf_fit.txt" -o "$CSF_DIR/csf_fit.bin"

cp "$IMG_DIR/flash.bin" "$IMG_DIR/signed-flash.bin"
dd if="$CSF_DIR/csf_spl.bin" of="$IMG_DIR/signed-flash.bin" bs=1 seek=$((csf_off)) conv=notrunc status=none

dd if="$CSF_DIR/csf_fit.bin" of="$IMG_DIR/signed-flash.bin" bs=1 seek=$((sld_csf_off)) conv=notrunc status=none

cp "$IMG_DIR/signed-flash.bin" "/out/imx-boot-imx8mpevk-sd.bin-flash_evk-signed"

echo "Signed image: /out/imx-boot-imx8mpevk-sd.bin-flash_evk-signed"
'

set +x

SIGNED_IMX_BOOT="$OUT_DIR/imx-boot-imx8mpevk-sd.bin-flash_evk-signed"
if [ ! -f "$SIGNED_IMX_BOOT" ]; then
  echo "Error: signed imx-boot not generated" >&2
  exit 1
fi

echo "Signed imx-boot generated at: $SIGNED_IMX_BOOT"
