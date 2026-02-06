# Secure Boot (i.MX8MP EVK, HABv4, SD boot)

This guide describes a reproducible secure-boot flow for i.MX8MP EVK using
HABv4. It follows the NXP HABv4 secure boot procedure (SRK/CSF/Authenticate
Data) and the i.MX8M boot image format. The repo helper scripts build and sign
`imx-boot` and the kernel `Image`, then patch the WIC image.

Secure boot is **irreversible** once the fuses are burned. Always validate in
Open mode before closing the device.

## Prerequisites

- Run commands from the repo root
- Yocto build directory: `/dev/shm/yocto`
- CST tarball placed at: `./cst-3.1.0.tgz`
- Output directory (relative): `../optee-ra-artifacts/secure-boot`
- SD card device path (example: `/dev/sdX`)

## 1) Build Yocto image (first time only)

```bash
cd ./attester/container-imx
YOCTO_DIR=/dev/shm/yocto ./yocto.sh
```

## 2) Generate signed imx-boot and patched WIC

```bash
OUT_DIR=../optee-ra-artifacts/secure-boot
OUT_WIC="$YOCTO_DIR/build/tmp/deploy/images/imx8mpevk/core-image-minimal-imx8mpevk.rootfs.wic.zst"

./attester/container-imx/secure-boot-imx8mp.sh \
  --yocto-dir /dev/shm/yocto \
  --cst-tar ./cst-3.1.0.tgz \
  --out-dir "$OUT_DIR" \
  --patch-wic "$OUT_WIC"
```

Outputs:
- Signed imx-boot
  - `$OUT_DIR/imx-boot-imx8mpevk-sd.bin-flash_evk-signed`
- Signed kernel Image
  - `$OUT_DIR/img/Image-signed`
- WIC with signed imx-boot injected at 32KB and signed `Image` in the boot partition
  - `$OUT_DIR/core-image-minimal-imx8mpevk.rootfs.wic.zst`

You can run the individual steps directly:

```bash
./attester/container-imx/secure-boot-sign-imx-boot.sh \
  --yocto-dir /dev/shm/yocto \
  --cst-tar ./cst-3.1.0.tgz \
  --out-dir "$OUT_DIR"

./attester/container-imx/secure-boot-sign-kernel.sh \
  --yocto-dir /dev/shm/yocto \
  --cst-tar ./cst-3.1.0.tgz \
  --out-dir "$OUT_DIR"

./attester/container-imx/secure-boot-patch-wic.sh \
  --wic "$OUT_WIC" \
  --signed-imx-boot "$OUT_DIR/imx-boot-imx8mpevk-sd.bin-flash_evk-signed" \
  --signed-image "$OUT_DIR/img/Image-signed" \
  --out-dir "$OUT_DIR"
```

## 3) Backup the signing keys (mandatory)

The CST helper generates a PKI tree and SRK materials under:
`$OUT_DIR/cst`

Archive it before burning fuses:

```bash
sudo tar -czf "$OUT_DIR/secure-boot-keys.tar.gz" \
  -C "$OUT_DIR" cst
```

## 4) Extract SRK fuse values

```bash
hexdump -e '/4 "0x"' -e '/4 "%X""\n"' \
  "$OUT_DIR/cst/keys/SRK_1_2_3_4_fuses.bin"
```

This prints 8 lines. You will use them in `fuse prog` below.

## 5) Write SD card

```bash
cd "$OUT_DIR"
zstd -d core-image-minimal-imx8mpevk.rootfs.wic.zst
sudo dd if=core-image-minimal-imx8mpevk.rootfs.wic of=/dev/sdX bs=4M status=progress conv=fsync
```

Replace `/dev/sdX` with the actual SD device.

## 6) Open mode validation (U-Boot)

```text
=> hab_status
```

Expected: `Secure boot disabled` (Open). This is correct before closing.

## 7) Program SRK fuses (U-Boot)

**Before writing**:

```text
=> fuse read 6 0 4
=> fuse read 7 0 4
```

**Write SRK hash** (replace values with the 8 lines from Step 4):

```text
=> fuse prog 6 0 0x........
=> fuse prog 6 1 0x........
=> fuse prog 6 2 0x........
=> fuse prog 6 3 0x........

=> fuse prog 7 0 0x........
=> fuse prog 7 1 0x........
=> fuse prog 7 2 0x........
=> fuse prog 7 3 0x........
```

**After writing**:

```text
=> fuse read 6 0 4
=> fuse read 7 0 4
```

Reboot:

```text
=> reset
```

## 8) Close the device (irreversible)

Only after Open validation is complete:

```text
=> fuse prog 1 3 0x2000000
```

Then reboot and check:

```text
=> hab_status
```

Expected: `Secure boot enabled`.

## 9) Remote Attestation checks

If `relying-party-service` is not resolvable, add it:

```bash
echo "192.168.8.244 relying-party-service" >> /etc/hosts
```

Run attestation:

```bash
optee_remote_attestation --key-hex <FullKey>
```

Expected: `ear.status: "affirming"`.

If you reuse a BlackKey generated before closing the device, attestation should fail
(e.g. `Failed to sign payload` / `TEEC_InvokeCommand failed`). Secure Boot changes
the CAAM key context (MPMR/BKEK), so old BlackKeys are no longer valid.

## 10) BlackKey non-reuse test (two devices)

Use the same SRK for both devices.

Device A:

```bash
optee_remote_attestation --generate-blackkey
# Save FullKey(hex)
```

Device B:

```bash
optee_remote_attestation --key-hex <FullKey-from-A>
```

Expected: **failure** (e.g. `Failed to sign payload`), proving BlackKey is device-bound.

## Notes

- Close is irreversible. Do not proceed without key backups.
- The signed imx-boot is injected at 32KB in the WIC by the script.
- Use the same SRK values on both devices for the non-reuse test.
