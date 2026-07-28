# fTPM (firmware TPM) on i.MX8MP EVK

This document describes how to build the Yocto image with the Microsoft
firmware TPM (`ms-tpm-20-ref`) running as an OP-TEE trusted application, and
how to exercise it from Linux with `tpm2-tools`.

## Overview

* The fTPM is a TPM 2.0 reference implementation packaged as an OP-TEE TA
  (UUID `bc50d971-d4c9-42c4-82cb-343fb7f37896`). `meta-arm` ships the recipe
  (`optee-ftpm`) and, when the `optee-ftpm` machine feature is set, links the
  TA into the OP-TEE OS image as an **early TA** (`CFG_EARLY_TA=y`), so it is
  available without loading anything from the normal world.
* On the Linux side the `tpm_ftpm_tee` driver exposes it as a standard
  `/dev/tpm0` character device.

`meta-arm` gates the recipe to QEMU/`genericarm64` machines, so this layer
adds two bbappends:

| File | Purpose |
|------|---------|
| `recipes-security/optee-ftpm/optee-ftpm_%.bbappend` | Allow `imx8mpevk` (`COMPATIBLE_MACHINE`) and build the TA as AArch64 |
| `recipes-kernel/linux/linux-imx_%.bbappend` + `linux-imx/ftpm.cfg` | Enable `CONFIG_TCG_FTPM_TEE=m` in the i.MX kernel (only when the `optee-ftpm` machine feature is set) |

## Enabling the fTPM

Add to `conf/local.conf` (on top of the normal attestation configuration —
the file lives at `${YOCTO_DIR}/build/conf/local.conf` on the host):

```
MACHINE_FEATURES:append = " optee-ftpm"
IMAGE_INSTALL:append = " optee-ftpm tpm2-tools kernel-module-tpm-ftpm-tee"
```

Then rebuild. The `bitbake` commands below run inside the `yocto.sh` build
container; if you have built before, editing `local.conf` and re-running
`YOCTO_DIR=... ./yocto.sh full` performs the same sequence (its own config
appends are grep-guarded and will not clobber these edits). Because the fTPM is embedded into the OP-TEE OS binary,
`imx-boot` must be regenerated after `optee-os` changes, and the image
repacked (the same sequence `yocto.sh full` uses):

```
bitbake core-image-minimal
bitbake -c cleansstate imx-boot && bitbake imx-boot
bitbake -f -c image core-image-minimal
```

For a HAB-signed board, sign the resulting image as usual with
`secure-boot-imx8mp.sh` (see the secure-boot guide).

## Using the fTPM on the device

The driver is built as a module on purpose: the fTPM stores its persistent
state through `tee-supplicant` (REE filesystem — RPMB is not provisioned on
the EVK), so the TA can only initialize once the supplicant is running.
After boot:

```sh
modprobe tpm_ftpm_tee
ls /dev/tpm0
tpm2_getcap properties-fixed   # manufacturer/firmware info
tpm2_pcrread                   # PCR banks
```

## Caveats

* **State storage**: fTPM persistent state lives on the REE filesystem via
  `tee-supplicant`. It is encrypted by OP-TEE, but without RPMB there is no
  rollback protection for the TPM state.
* **Not a measured-boot root**: nothing measures into the fTPM PCRs during
  boot on this platform; PCRs start at their reset values. Using the fTPM
  with IMA or as a boot-measurement root requires additional integration.
* With the feature enabled, the `meta-arm` bbappend pins `CFG_CORE_HEAP_SIZE`
  to 128 KiB — the fTPM needs more TEE core heap than OP-TEE's generic
  64 KiB default. On i.MX this is a no-op: the NXP tree already defaults all
  i.MX platforms to 128 KiB.
