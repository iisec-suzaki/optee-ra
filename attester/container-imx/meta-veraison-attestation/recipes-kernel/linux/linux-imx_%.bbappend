# Enable the fTPM TEE driver (tpm_ftpm_tee) for the OP-TEE firmware TPM.
FILESEXTRAPATHS:prepend := "${THISDIR}/linux-imx:"
SRC_URI += "file://ftpm.cfg"
do_configure:append() {
    cat ${UNPACKDIR}/ftpm.cfg >> ${B}/.config
    oe_runmake -C ${S} O=${B} olddefconfig
}
