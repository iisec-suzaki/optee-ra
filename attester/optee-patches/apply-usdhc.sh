#!/bin/bash
# Wire the i.MX uSDHC driver into an optee_os source tree.
#
# Usage: ./apply-usdhc.sh <path-to-optee_os>
#
# Idempotent: re-running on an already patched tree is a no-op.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="${1:?usage: apply-usdhc.sh <path-to-optee_os>}"

[ -f "$SRC/core/arch/arm/plat-imx/conf.mk" ] || {
    echo "Error: $SRC does not look like an optee_os tree." >&2
    exit 1
}

install -m 644 "$HERE/imx_usdhc.c" "$SRC/core/drivers/imx_usdhc.c"
install -m 644 "$HERE/imx_usdhc.h" "$SRC/core/include/drivers/imx_usdhc.h"

# core/drivers/sub.mk
if ! grep -q imx_usdhc "$SRC/core/drivers/sub.mk"; then
    cat >> "$SRC/core/drivers/sub.mk" <<'EOF'
srcs-$(CFG_IMX_USDHC) += imx_usdhc.c
EOF
fi

# plat-imx/conf.mk: default the options off, enable on mx8mpevk
if ! grep -q CFG_IMX_USDHC "$SRC/core/arch/arm/plat-imx/conf.mk"; then
    python3 - "$SRC/core/arch/arm/plat-imx/conf.mk" <<'EOF'
import sys

path = sys.argv[1]
text = open(path).read()

anchor = """ifneq (,$(filter $(PLATFORM_FLAVOR),mx8mpevk))
CFG_DDR_SIZE ?= UL(0x180000000)"""

addition = """ifneq (,$(filter $(PLATFORM_FLAVOR),mx8mpevk))
CFG_DDR_SIZE ?= UL(0x180000000)
# Route OP-TEE secure storage RPMB traffic through the core driver rather
# than tee-supplicant. Implies the driver below, so it comes first.
CFG_IMX_RPMB_NATIVE ?= n
ifeq ($(CFG_IMX_RPMB_NATIVE),y)
$(call force,CFG_IMX_USDHC,y)
endif
# On-board eMMC (uSDHC3)
CFG_IMX_USDHC ?= n
CFG_IMX_USDHC_BASE ?= 0x30b60000
# CCM clock gate index and root clock register for uSDHC3
CFG_IMX_USDHC_CCM_CCGR ?= 94
CFG_IMX_USDHC_CCM_TARGET ?= 0xbc80
# Key layout of the first driver revision: only for eMMCs provisioned by it
CFG_IMX_USDHC_CID_COMPAT ?= n"""

if anchor not in text:
    sys.exit("anchor not found in conf.mk")

open(path, "w").write(text.replace(anchor, addition, 1))
EOF
fi

python3 "$HERE/rpmb-native-backend.py" "$SRC/core/tee/tee_rpmb_fs.c"

echo "uSDHC driver wired into $SRC"
