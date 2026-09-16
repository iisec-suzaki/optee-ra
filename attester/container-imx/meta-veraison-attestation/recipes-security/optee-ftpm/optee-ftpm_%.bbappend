# Enable the Microsoft fTPM TA on i.MX8MPEVK (meta-arm gates it to qemu/genericarm64).
COMPATIBLE_MACHINE:imx8mpevk = "imx8mpevk"

# Build the TA as AArch64 (same as our RA TA; the meta-arm recipe only sets
# this for qemu machines).
EXTRA_OEMAKE:append = " CFG_ARM64_ta_arm64=y"
