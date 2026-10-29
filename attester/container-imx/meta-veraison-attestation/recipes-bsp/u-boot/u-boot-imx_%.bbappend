FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += "file://hab.cfg"

# The on-board eMMC belongs to OP-TEE (RPMB secure storage): keep U-Boot off it.
SRC_URI += "${@bb.utils.contains('MACHINE_FEATURES', 'optee-ftpm', 'file://0001-imx8mp-evk-leave-usdhc3-to-the-TEE.patch', '', d)}"

# With the fTPM reachable from kernel init, let IMA measure the running
# system (tcb policy, ima-ng template, SHA-256) into PCR 10.
SRC_URI += "${@bb.utils.contains('MACHINE_FEATURES', 'optee-ftpm', 'file://0002-imx8mp-evk-ima-kernel-options.patch', '', d)}"
