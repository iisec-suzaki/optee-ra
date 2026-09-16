FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# With the fTPM the on-board eMMC (uSDHC3) belongs to OP-TEE: make its DMA a
# secure master and its registers secure-only.
SRC_URI += "${@bb.utils.contains('MACHINE_FEATURES', 'optee-ftpm', 'file://0001-imx8mp-usdhc3-secure-master.patch', '', d)}"
