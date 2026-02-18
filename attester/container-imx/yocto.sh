#!/bin/bash
#
# Unified Yocto build helper for i.MX 8M Plus
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

usage() {
    cat <<'EOF'
Usage: ./yocto.sh [full|optee-os|veraison-attestation]

Commands:
  full                  Build the full Yocto image (default)
  optee-os              Rebuild optee-os only
  veraison-attestation  Rebuild veraison-attestation only

Environment:
  YOCTO_DIR  Yocto directory location (default: ./yocto)
EOF
}

COMMAND="${1:-full}"
case "$COMMAND" in
    full|optee-os|veraison-attestation) ;;
    -h|--help|help)
        usage
        exit 0
        ;;
    *)
        echo "Error: Unknown command '$COMMAND'."
        usage
        exit 1
        ;;
esac

YOCTO_DIR_DEFAULT="$SCRIPT_DIR/yocto"
YOCTO_DIR="${YOCTO_DIR:-$YOCTO_DIR_DEFAULT}"
mkdir -p "$YOCTO_DIR"
if [ "$YOCTO_DIR" != "$YOCTO_DIR_DEFAULT" ]; then
    ln -sfn "$YOCTO_DIR" "$YOCTO_DIR_DEFAULT"
fi

if [ "$COMMAND" = "full" ]; then
    echo "========================================="
    echo "Yocto Image Build in Docker Container"
    echo "with Veraison Attestation Application"
    echo "========================================="
    echo ""
    echo "This will:"
    echo "  1. Build a Docker image with Ubuntu 22.04 (Python 3.10+)"
    echo "  2. Download i.MX Yocto BSP to \${YOCTO_DIR:-./yocto}"
    echo "  3. Build complete system image (4-8 hours)"
    echo "  4. Output: \${YOCTO_DIR}/build/tmp/deploy/images/imx8mpevk/*.wic.zst"
    echo ""
    echo "Required: ~100GB disk space"
    echo ""
    echo "Environment variables:"
    echo "  YOCTO_DIR - Yocto directory location (default: ./yocto)"
    echo "              For faster builds: export YOCTO_DIR=/dev/shm/yocto"
    echo ""
fi

if [ "$COMMAND" = "full" ]; then
    echo "Building Yocto Docker environment..."
    docker build -f Dockerfile.yocto -t veraison-yocto-builder .
elif ! docker image inspect veraison-yocto-builder >/dev/null 2>&1; then
    echo "Yocto Docker image not found; building it first..."
    docker build -f Dockerfile.yocto -t veraison-yocto-builder .
fi

HOST_UID=$(id -u)
HOST_GID=$(id -g)
ATTESTER_DIR="$(dirname "$SCRIPT_DIR")"

docker run --rm \
  --user ${HOST_UID}:${HOST_GID} \
  -v "$SCRIPT_DIR:/workspace" \
  -v "$ATTESTER_DIR:/attester" \
  -v "$YOCTO_DIR:/yocto" \
  -v /etc/passwd:/etc/passwd:ro \
  -v /etc/group:/etc/group:ro \
  -e HOME=/tmp \
  -e COMMAND="$COMMAND" \
  -e IMX_RELEASE="${IMX_RELEASE:-imx-6.12.20-2.0.0}" \
  -e MACHINE="${MACHINE:-imx8mpevk}" \
  -e DISTRO="${DISTRO:-fsl-imx-xwayland}" \
  -w /workspace \
  veraison-yocto-builder \
  /bin/bash -c '
set -e

cd /yocto

if [ "$COMMAND" = "full" ]; then
    echo "========================================="
    echo "Step 1: Download i.MX Yocto BSP"
    echo "========================================="

    if [ ! -d "sources" ]; then
        echo "Downloading i.MX BSP ${IMX_RELEASE}..."
        repo init -u https://github.com/nxp-imx/imx-manifest -b imx-linux-walnascar -m ${IMX_RELEASE}.xml
        repo sync -j$(nproc)
    else
        echo "Using existing Yocto sources"
    fi
fi

echo ""
echo "========================================="
echo "Step 2: Setup Build Environment"
echo "========================================="

EULA=1 MACHINE=${MACHINE} DISTRO=${DISTRO} source ./imx-setup-release.sh -b build

for bblayers in conf/bblayers.conf conf/bblayers.conf.org; do
    if [ -f "$bblayers" ] && ! grep -q "meta-veraison-attestation" "$bblayers"; then
        echo "" >> "$bblayers"
        echo "# Veraison attestation layer" >> "$bblayers"
        echo "BBLAYERS += \"/workspace/meta-veraison-attestation\"" >> "$bblayers"
    fi
done

case "$COMMAND" in
    full)
        for localconf in conf/local.conf conf/local.conf.org; do
            if [ -f "$localconf" ] && ! grep -q "veraison-attestation" "$localconf"; then
                cat >> "$localconf" <<EOF

# OP-TEE configuration
MACHINE_FEATURES:append = " optee"
DISTRO_FEATURES:append = " optee"

# Enable CAAM (Cryptographic Acceleration and Assurance Module)
EXTRA_OEMAKE:append:pn-optee-os = " CFG_NXP_CAAM=y CFG_NXP_CAAM_ECC_DRV=y"

# Install Veraison attestation application
IMAGE_INSTALL:append = " veraison-attestation"
IMAGE_INSTALL:append = " optee-os optee-client optee-test"
EOF
            fi
        done

        echo ""
        echo "========================================="
        echo "Step 3: Building Image (4-8 hours)"
        echo "========================================="

        bitbake core-image-minimal

        echo ""
        echo "========================================="
        echo "Step 4: Rebuild imx-boot (embed updated tee.bin)"
        echo "========================================="

        bitbake -c cleansstate imx-boot
        bitbake imx-boot

        echo ""
        echo "========================================="
        echo "Step 5: Repack WIC image with updated imx-boot"
        echo "========================================="

        bitbake -f -c image core-image-minimal

        echo ""
        echo "========================================="
        echo "Build Complete!"
        echo "========================================="
        echo ""
        echo "Output image:"
        ls -lh tmp/deploy/images/${MACHINE}/core-image-minimal*.wic.zst | tail -1
        echo ""
        echo "Image location: /yocto/build/tmp/deploy/images/${MACHINE}/"
        echo ""
        ;;
    optee-os)
        for localconf in conf/local.conf conf/local.conf.org; do
            if [ -f "$localconf" ] && ! grep -q "CFG_NXP_CAAM" "$localconf"; then
                echo "" >> "$localconf"
                echo "# Enable CAAM (Cryptographic Acceleration and Assurance Module)" >> "$localconf"
                echo "EXTRA_OEMAKE:append:pn-optee-os = \" CFG_NXP_CAAM=y CFG_NXP_CAAM_ECC_DRV=y\"" >> "$localconf"
            fi
        done

        echo "Cleaning optee-os..."
        bitbake -c cleansstate optee-os

        echo "Rebuilding optee-os..."
        bitbake optee-os

        echo "Rebuilding imx-boot to embed updated tee.bin..."
        bitbake -c cleansstate imx-boot
        bitbake imx-boot
        ;;
    veraison-attestation)
        for localconf in conf/local.conf conf/local.conf.org; do
            if [ -f "$localconf" ] && ! grep -q "veraison-attestation" "$localconf"; then
                cat >> "$localconf" <<EOF

# OP-TEE configuration
MACHINE_FEATURES:append = " optee"
DISTRO_FEATURES:append = " optee"

# Enable CAAM (Cryptographic Acceleration and Assurance Module)
EXTRA_OEMAKE:append:pn-optee-os = " CFG_NXP_CAAM=y CFG_NXP_CAAM_ECC_DRV=y"

# Install Veraison attestation application
IMAGE_INSTALL:append = " veraison-attestation"
IMAGE_INSTALL:append = " optee-os optee-client optee-test"
EOF
            fi
        done

        echo "Cleaning veraison-attestation..."
        bitbake -c cleansstate veraison-attestation

        echo "Rebuilding veraison-attestation..."
        bitbake veraison-attestation
        ;;
esac
'

if [ "$COMMAND" = "full" ]; then
    echo ""
    echo "========================================="
    echo "Build Finished!"
    echo "========================================="
    echo ""
    echo "Image location: $YOCTO_DIR/build/tmp/deploy/images/imx8mpevk/"
    ls -lh "$YOCTO_DIR/build/tmp/deploy/images/imx8mpevk/"core-image-minimal*.wic.zst 2>/dev/null || \
        echo "No image found - check build log"
    echo ""
    echo "To flash to SD card:"
    echo "  cd $YOCTO_DIR/build/tmp/deploy/images/imx8mpevk/"
    echo "  zstd -d core-image-minimal-imx8mpevk.rootfs.wic.zst"
    echo "  sudo dd if=core-image-minimal-imx8mpevk.rootfs.wic of=/dev/sdX bs=4M status=progress && sync"
    echo ""
fi
