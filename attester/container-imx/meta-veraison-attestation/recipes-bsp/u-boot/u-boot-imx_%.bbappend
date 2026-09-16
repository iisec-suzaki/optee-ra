FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += "file://hab.cfg"

# The on-board eMMC belongs to OP-TEE (RPMB secure storage): keep U-Boot off it.
SRC_URI += "${@bb.utils.contains('MACHINE_FEATURES', 'optee-ftpm', 'file://0001-imx8mp-evk-leave-usdhc3-to-the-TEE.patch', '', d)}"
