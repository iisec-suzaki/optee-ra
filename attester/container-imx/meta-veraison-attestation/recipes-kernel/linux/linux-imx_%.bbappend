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

# With the fTPM the on-board eMMC belongs to OP-TEE (RPMB secure storage):
# keep Linux off the controller entirely, and keep the shared NAND/uSDHC bus
# clock, which also feeds that controller, running.
SRC_URI += "${@bb.utils.contains('MACHINE_FEATURES', 'optee-ftpm', 'file://0001-imx8mp-evk-leave-usdhc3-to-the-TEE.patch', '', d)}"
SRC_URI += "${@bb.utils.contains('MACHINE_FEATURES', 'optee-ftpm', 'file://0002-clk-imx8mp-keep-nand_usdhc_bus-running.patch', '', d)}"
