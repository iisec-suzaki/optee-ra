# Add CAAM-enabled Remote Attestation PTA to OP-TEE OS
# Note: veraison_attestation (non-CAAM) is already included in OP-TEE 4.6.0
# This adds remote_attestation (CAAM-enabled version we're developing)

# PTA source path (override in local.conf if needed)
PTA_EXTERNAL_SRC ?= "/attester/pta_remote_attestation"

# Enable the custom remote_attestation PTA
EXTRA_OEMAKE:append = " CFG_REMOTE_ATTESTATION_PTA=y"

# Performance measurement logging. Enable from local.conf with:
#   CFG_REMOTE_ATTESTATION_PERF:pn-optee-os = "y"
#   CFG_REMOTE_ATTESTATION_PERF:pn-veraison-attestation = "y"
# CFG_TEE_CORE_LOG_LEVEL must be raised together with the flag because
# meta-freescale's optee-os-common-fslc-imx.inc forces it to 0 (all core
# trace compiled out, RA_PERF lines included); this append is parsed after
# the .inc, so the later assignment wins on the make command line.
CFG_REMOTE_ATTESTATION_PERF ?= "n"
EXTRA_OEMAKE:append = "${@' CFG_REMOTE_ATTESTATION_PERF=y CFG_TEE_CORE_LOG_LEVEL=2' if d.getVar('CFG_REMOTE_ATTESTATION_PERF') == 'y' else ''}"

# Copy PTA source to OP-TEE OS source tree before compile
do_configure:append() {
    # Create PTA directory in OP-TEE OS source
    install -d ${S}/core/pta/remote_attestation
    install -d ${S}/core/pta/remote_attestation/qcbor

    # Source location (shared attester tree mounted in the build container)
    PTA_BASE="${PTA_EXTERNAL_SRC}"
    if [ ! -d "${PTA_BASE}" ]; then
        bbfatal "PTA source not found at ${PTA_BASE}. Set PTA_EXTERNAL_SRC or mount /attester."
    fi
    PTA_SRC="${PTA_BASE}/remote_attestation"

    # Copy PTA header to include directory
    cp ${PTA_BASE}/pta_remote_attestation.h ${S}/core/include/

    # Copy PTA source files
    cp ${PTA_SRC}/remote_attestation.c ${S}/core/pta/remote_attestation/
    cp ${PTA_SRC}/cbor.c ${S}/core/pta/remote_attestation/
    cp ${PTA_SRC}/cbor.h ${S}/core/pta/remote_attestation/
    cp ${PTA_SRC}/hash.c ${S}/core/pta/remote_attestation/
    cp ${PTA_SRC}/hash.h ${S}/core/pta/remote_attestation/
    cp ${PTA_SRC}/sign.c ${S}/core/pta/remote_attestation/
    cp ${PTA_SRC}/sign.h ${S}/core/pta/remote_attestation/
    cp ${PTA_SRC}/perf.c ${S}/core/pta/remote_attestation/
    cp ${PTA_SRC}/perf.h ${S}/core/pta/remote_attestation/
    cp ${PTA_SRC}/base64.c ${S}/core/pta/remote_attestation/
    cp ${PTA_SRC}/base64.h ${S}/core/pta/remote_attestation/
    cp ${PTA_SRC}/ocotp.c ${S}/core/pta/remote_attestation/
    cp ${PTA_SRC}/ocotp.h ${S}/core/pta/remote_attestation/
    cp ${PTA_SRC}/sub.mk ${S}/core/pta/remote_attestation/

    # Copy qcbor files
    cp ${PTA_SRC}/qcbor/*.c ${S}/core/pta/remote_attestation/qcbor/
    cp ${PTA_SRC}/qcbor/*.h ${S}/core/pta/remote_attestation/qcbor/

    # Add remote_attestation PTA to OP-TEE OS build
    if ! grep -q "^subdirs-y += remote_attestation" ${S}/core/pta/sub.mk; then
        echo "" >> ${S}/core/pta/sub.mk
        echo "subdirs-y += remote_attestation" >> ${S}/core/pta/sub.mk
    fi
}
