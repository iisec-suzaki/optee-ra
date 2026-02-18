# Secure Boot（i.MX8MP EVK / HABv4 / SD起動）

このドキュメントは i.MX8MP EVK の HABv4 Secure Boot を、
NXPのHABv4手順（SRK/CSF/Authenticate Data）と i.MX8M の
ブートイメージ形式に沿って再現可能に行う手順です。
署名済み `imx-boot` とカーネル `Image` を作り、WIC に反映します。

**ヒューズ書き込みは不可逆**です。Open での検証が完了するまで Close にしないでください。

## 前提

- コマンドはリポジトリのルートから実行
- Yocto: `/dev/shm/yocto`
- CST tarball: `./cst-3.1.0.tgz`
- 出力先（相対）: `../optee-ra-artifacts/secure-boot`
- SD デバイス: `/dev/sdX`（環境に合わせて置換）

## 1) Yocto ビルド（初回のみ）

```bash
cd ./attester/container-imx
YOCTO_DIR=/dev/shm/yocto ./yocto.sh
```

## 2) 署名済み imx-boot と WIC を作成

```bash
OUT_DIR=../optee-ra-artifacts/secure-boot
OUT_WIC="$YOCTO_DIR/build/tmp/deploy/images/imx8mpevk/core-image-minimal-imx8mpevk.rootfs.wic.zst"

./attester/container-imx/secure-boot-imx8mp.sh \
  --yocto-dir /dev/shm/yocto \
  --cst-tar ./cst-3.1.0.tgz \
  --out-dir "$OUT_DIR" \
  --patch-wic "$OUT_WIC"
```

生成物:
- 署名済み imx‑boot
  - `$OUT_DIR/imx-boot-imx8mpevk-sd.bin-flash_evk-signed`
- 署名済み `Image`
  - `$OUT_DIR/img/Image-signed`
- 署名済み imx‑boot（32KB）と署名済み `Image` を反映した WIC
  - `$OUT_DIR/core-image-minimal-imx8mpevk.rootfs.wic.zst`

各ステップを個別に実行することもできます:

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

## 3) 鍵のバックアップ（必須）

CST が生成した鍵は以下にあります:
`$OUT_DIR/cst`

必ずアーカイブしてください:

```bash
sudo tar -czf "$OUT_DIR/secure-boot-keys.tar.gz" \
  -C "$OUT_DIR" cst
```

## 4) SRK Fuse 値の抽出

```bash
hexdump -e '/4 "0x"' -e '/4 "%X""\n"' \
  "$OUT_DIR/cst/keys/SRK_1_2_3_4_fuses.bin"
```

出力された 8 行を `fuse prog` で使います。

## 5) SD へ書き込み

```bash
cd "$OUT_DIR"
zstd -d core-image-minimal-imx8mpevk.rootfs.wic.zst
sudo dd if=core-image-minimal-imx8mpevk.rootfs.wic of=/dev/sdX bs=4M status=progress conv=fsync
```

`/dev/sdX` は実際のデバイスに置換してください。

## 6) Open での確認（U-Boot）

```text
=> hab_status
```

Open では `Secure boot disabled` のままで OK。

## 7) SRK Fuse 書き込み（U-Boot）

**書き込み前**:

```text
=> fuse read 6 0 4
=> fuse read 7 0 4
```

**SRK 書き込み**（Step 4 の値に置換）:

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

**書き込み後**:

```text
=> fuse read 6 0 4
=> fuse read 7 0 4
```

再起動:

```text
=> reset
```

## 8) Close（不可逆）

Open での確認が終わってから実行:

```text
=> fuse prog 1 3 0x2000000
```

再起動後:

```text
=> hab_status
```

`Secure boot enabled` が出れば Close 完了。

## 9) Remote Attestation 確認

`relying-party-service` が引けない場合:

```bash
echo "192.168.8.244 relying-party-service" >> /etc/hosts
```

実行:

```bash
optee_remote_attestation --key-hex <FullKey>
```

期待値: `ear.status: "affirming"`。

Secure Boot 前に生成した BlackKey を Close 後に使うと、
`Failed to sign payload` / `TEEC_InvokeCommand failed` となり失敗するのが正常です。
Secure Boot により CAAM の鍵文脈（MPMR/BKEK）が変わるため、古い BlackKey は無効になります。

## 10) BlackKey 使い回し不可の検証（2台）

CAAM BlackKey がデバイス固有であることを検証します。同じ SRK fuse 値を持つ
2台のデバイスでも、各デバイスの JDKEK（ハードウェア固有のエントロピーから
導出）が異なるため、BlackKey は別デバイスでは使用できません。

両方の実機を **同じ SRK** で Close しておきます（手順 8-9）。

### 10.1 Device A の BlackKey を Device B で使用

**Device A** で BlackKey を生成:

```bash
optee_remote_attestation --generate-blackkey
# FullKey を控える
```

**Device B** で Device A の FullKey を使用:

```bash
optee_remote_attestation --key-hex <FullKey-from-A>
```

期待: `Failed to sign payload` — Device B の CAAM は Device A の BlackKey
blob を復号できないため、署名に失敗します。

### 10.2 Device B で新規 BlackKey を生成

```bash
optee_remote_attestation --generate-blackkey
# FullKey を控える
optee_remote_attestation --key-hex <FullKey-from-B>
```

期待: `ear.status: "contraindicated"`、`"no trust anchor for evidence"`。

署名は成功しますが、Device B の公開鍵（`psa-instance-id`）が Verifier に
登録されていないため、アテステーションは失敗します。

### 10.3 Device B を Verifier に登録して検証

Device B のアテステーションを成功させるには、Verifier に trust anchor を
追加登録します。`--generate-blackkey` で出力された PubX/PubY から
instance-id と PEM 公開鍵を計算し（手順 9 のヘルパー参照）、Device B 用の
provisioning JSON を作成して submit します:

```bash
# Device B の trust anchor をビルド・登録
veraison -- cocli comid create \
    --template provisoning/data/comid-psa-ta-deviceB.json \
    --template provisoning/data/comid-psa-refval-imx.json \
    --output-dir provisoning/data

veraison -- cocli corim create \
    --template provisoning/data/corim-psa.json \
    --comid provisoning/data/comid-psa-refval-imx.cbor \
    --comid provisoning/data/comid-psa-ta-deviceB.cbor \
    --output provisoning/data/psa-endorsements-deviceB.cbor

veraison -- cocli corim submit \
    --corim-file provisoning/data/psa-endorsements-deviceB.cbor \
    --api-server "https://provisioning-service:9443/endorsement-provisioning/v1/submit" \
    --media-type 'application/corim-unsigned+cbor; profile="http://arm.com/psa/iot/1"'
```

**Device B** で再度アテステーションを実行:

```bash
optee_remote_attestation --key-hex <FullKey-from-B>
```

期待: `ear.status: "affirming"`。

### まとめ

| テスト | 結果 | 意味 |
|--------|------|------|
| A の BlackKey を B で使用 | `Failed to sign` | BlackKey はデバイス固有（JDKEK が異なる） |
| B の新規 BlackKey（未登録） | `no trust anchor` | 鍵ペアが異なり instance-id が Verifier に未登録 |
| B の BlackKey を登録後 | `affirming` | trust anchor 追加後、アテステーション成功 |

## 注意事項

- Close は不可逆です。鍵のバックアップは必須。
- 署名済み imx‑boot は WIC の 32KB に埋め込まれます。
- 2台検証では SRK 値を必ず揃える必要があります。
- 各デバイスは固有の CAAM 鍵ペアを生成するため、Verifier にはデバイスごとに
  trust anchor を個別登録する必要があります。
