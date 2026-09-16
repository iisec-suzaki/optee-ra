# fTPM (firmware TPM) on i.MX8MP EVK

This document describes how to build the Yocto image with the Microsoft firmware TPM (`ms-tpm-20-ref`) running as an OP-TEE trusted application, and how to exercise it from Linux with `tpm2-tools`.

## Overview

* The fTPM is a TPM 2.0 reference implementation packaged as an OP-TEE TA (UUID `bc50d971-d4c9-42c4-82cb-343fb7f37896`). `meta-arm` ships the recipe (`optee-ftpm`) and, when the `optee-ftpm` machine feature is set, links the TA into the OP-TEE OS image as an **early TA** (`CFG_EARLY_TA=y`), so it is available without loading anything from the normal world.
* On the Linux side the `tpm_ftpm_tee` driver exposes it as a standard `/dev/tpm0` character device.

`meta-arm` gates the recipe to QEMU/`genericarm64` machines. This layer adds the following bbappends and patches:

| File | Purpose |
|------|---------|
| `recipes-security/optee-ftpm/optee-ftpm_%.bbappend` | Allow `imx8mpevk` (`COMPATIBLE_MACHINE`) and build the TA as AArch64 |
| `recipes-kernel/linux/linux-imx_%.bbappend` + `linux-imx/ftpm.cfg` | Build the `tpm_ftpm_tee` driver into the i.MX kernel (`CONFIG_TCG_FTPM_TEE=y`) and keep Linux off the on-board eMMC (only when the `optee-ftpm` machine feature is set) |
| `recipes-security/optee/optee-os_%.imx.bbappend` | Build the OP-TEE core uSDHC driver and the native RPMB backend (`attester/optee-patches/`) into OP-TEE, and make RPMB the only private storage (`CFG_REE_FS=n`) |
| `recipes-bsp/u-boot/`, `recipes-bsp/imx-atf/` | Keep U-Boot off the on-board eMMC; make uSDHC3 a secure bus master in BL31 |

## Enabling the fTPM

Add to `conf/local.conf` (on top of the normal attestation configuration — the file lives at `${YOCTO_DIR}/build/conf/local.conf` on the host):

```
MACHINE_FEATURES:append = " optee-ftpm"
IMAGE_INSTALL:append = " optee-ftpm tpm2-tools libtss2-tcti-device"
```

Then rebuild. The `bitbake` commands below run inside the `yocto.sh` build container; if you have built before, editing `local.conf` and re-running `YOCTO_DIR=... ./yocto.sh full` performs the same sequence (its own config appends are grep-guarded and will not clobber these edits). Because the fTPM is embedded into the OP-TEE OS binary, `imx-boot` must be regenerated after `optee-os` changes, and the image repacked (the same sequence `yocto.sh full` uses):

```
bitbake core-image-minimal
bitbake -c cleansstate imx-boot && bitbake imx-boot
bitbake -f -c image core-image-minimal
```

For a HAB-signed board, sign the resulting image as usual with `secure-boot-imx8mp.sh` (see the secure-boot guide).

## How the fTPM starts before Linux

The fTPM keeps its persistent state in the RPMB partition of the on-board eMMC. In stock OP-TEE that traffic is relayed by `tee-supplicant`, so the TA could only start once user space was up, long after IMA had looked for a TPM and given up. With the `optee-ftpm` feature this layer instead:

* builds an eMMC controller driver for uSDHC3 into the OP-TEE core (`attester/optee-patches/imx_usdhc.c`) and routes `tee_rpmb_fs.c` to it (`rpmb-native-backend.py`), so RPMB is reachable with no help from Linux;
* makes RPMB the only private storage (`CFG_REE_FS=n`) and lets the TA enumerate at OP-TEE driver probe instead of waiting for the supplicant;
* gives the on-board eMMC to the TEE: uSDHC3 is disabled in the U-Boot and Linux device trees, BL31 configures it as a secure bus master with secure-only registers, and the shared `nand_usdhc_bus` clock is kept running by the kernel.

The result is that `/dev/tpm0` exists during kernel init and IMA anchors its measurement log in the fTPM (PCR 10). Nothing needs to be loaded after boot.

**First boot on a device.** With `CFG_RPMB_WRITE_KEY=y`, OP-TEE programs the RPMB authentication key the first time it finds none. This happens once per eMMC and cannot be undone (the key is derived from the CAAM master key and the eMMC CID, so it never has to be stored). The fTPM then creates its NV storage, which takes a few seconds more than a normal boot.

**CID layout.** The key derivation uses the 16 CID bytes in the JEDEC layout (manufacturer ID first, a zero in the CRC position), the same bytes the Linux kernel and tee-supplicant use, so a key programmed through one path is valid on the others. The first revision of the driver placed the bytes one position later; the lab EVK was provisioned with that layout and needs `CFG_IMX_USDHC_CID_COMPAT=y` (set in its `local.conf`) to keep deriving the same key. Never set it for a new device.

## Using the fTPM on the device

```sh
dmesg | grep -iE "tpm|ima:"      # no "No TPM chip found"; no tpm0 errors
ls /dev/tpm0
tpm2_getcap properties-fixed   # manufacturer/firmware info
tpm2_pcrread sha256:10         # non-zero once IMA has measured
```

## Caveats

* **The eMMC belongs to the TEE.** This arrangement assumes U-Boot and Linux never use the on-board eMMC, which holds on the EVK because it boots from SD. A product that boots from its only eMMC, or uses it as Linux storage, needs a different design (a separate TEE storage device, or a volatile fTPM seeded from CAAM).
* **RPMB key programming is irreversible.** See "First boot on a device".
* **Boot firmware is not measured.** IMA measures from kernel start; U-Boot and the kernel are not extended into the PCRs (HAB secure boot and the RA PRoT measurement cover them).
* With the feature enabled, the `meta-arm` bbappend pins `CFG_CORE_HEAP_SIZE` to 128 KiB — the fTPM needs more TEE core heap than OP-TEE's generic 64 KiB default. On i.MX this is a no-op: the NXP tree already defaults all i.MX platforms to 128 KiB.
