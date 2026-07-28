# Enable the fTPM TEE driver (tpm_ftpm_tee) for the OP-TEE firmware TPM.
# Gated on the optee-ftpm machine feature so that default builds keep a
# byte-identical kernel configuration.
FILESEXTRAPATHS:prepend := "${THISDIR}/linux-imx:"
SRC_URI += "${@bb.utils.contains('MACHINE_FEATURES', 'optee-ftpm', 'file://ftpm.cfg', '', d)}"
do_configure:append() {
    if [ -f ${UNPACKDIR}/ftpm.cfg ]; then
        cat ${UNPACKDIR}/ftpm.cfg >> ${B}/.config
        oe_runmake -C ${S} O=${B} olddefconfig
    fi
}
