# Veraison Remote Attestation for i.MX 8M Plus

i.MX 8M Plus向けVeraison Remote AttestationアプリケーションのYoctoビルド環境。

CAAM (Cryptographic Acceleration and Assurance Module) を使用したハードウェアベースの鍵保護をサポート。

## ディレクトリ構成

```
container-imx/
├── yocto.sh                    # Yoctoビルドスクリプト (full/optee-os/veraison-attestation)
├── Dockerfile.yocto            # Yoctoビルド用Dockerイメージ
├── meta-veraison-attestation/  # Yoctoレイヤー
│   ├── conf/
│   │   └── layer.conf
│   └── recipes-security/
│       ├── optee/              # OP-TEE CAAM PTA拡張
│       │   └── optee-os_%.imx.bbappend
│       └── veraison-attestation/
│           └── veraison-attestation_1.0.bb
```

アプリケーションのソースは `../remote_attestation` を参照します（Docker 内では `/attester/remote_attestation`）。

## 必要環境

- Docker
- 約100GB のディスク空き容量
- 4-8時間のビルド時間（初回）

## ビルド方法

### 基本的な使用方法

```bash
./yocto.sh
```

引数なしの場合は `full`（フルイメージビルド）を実行します。

### 環境変数

| 変数 | デフォルト | 説明 |
|------|-----------|------|
| `YOCTO_DIR` | `./yocto` | Yoctoソースとビルドディレクトリの場所 |

PTA のソースは `../pta_remote_attestation` を参照します（Docker 内では `/attester/pta_remote_attestation`）。
必要に応じて `conf/local.conf` に `PTA_EXTERNAL_SRC = "/path/to/pta_remote_attestation"` を設定してください。

Attester アプリケーションのソースは `../remote_attestation` を参照します（Docker 内では `/attester/remote_attestation`）。
必要に応じて `conf/local.conf` に `VERAISON_ATTESTATION_EXTERNAL_SRC = "/path/to/remote_attestation"` を設定してください。

### 高速ビルド（tmpfs使用）

RAMディスク（/dev/shm）を使用することでビルドを高速化できます：

```bash
YOCTO_DIR=/dev/shm/yocto ./yocto.sh
```

**注意**: tmpfsでは一部のパッケージ（gdk-pixbuf-native等）のビルドに失敗する場合があります。その場合は通常のファイルシステムを使用してください。

### カスタムディレクトリ

```bash
YOCTO_DIR=/path/to/yocto ./yocto.sh
```

### 部分的な再ビルド

```bash
# OP-TEE OSのみ再ビルド
./yocto.sh optee-os

# veraison-attestationのみ再ビルド
./yocto.sh veraison-attestation
```

**注意**: i.MX 8M Plus は SD 先頭の `imx-boot` に埋め込まれた `tee.bin` を使用します。`optee-os` を更新した場合は `imx-boot` の再ビルドが必要です。`./yocto.sh optee-os` は `imx-boot` の再ビルドまで実行しますが、SD 書き込み用の `.wic.zst` を更新したい場合は `./yocto.sh`（full）で再パッケージしてください。

## 出力

ビルド成功後、以下の場所にイメージが生成されます：

```
${YOCTO_DIR}/build/tmp/deploy/images/imx8mpevk/core-image-minimal-imx8mpevk.rootfs.wic.zst
```

## SDカードへの書き込み

```bash
cd ${YOCTO_DIR}/build/tmp/deploy/images/imx8mpevk/
zstd -d core-image-minimal-imx8mpevk.rootfs.wic.zst
sudo dd if=core-image-minimal-imx8mpevk.rootfs.wic of=/dev/sdX bs=4M status=progress && sync
```

**注意**: `/dev/sdX` は実際のSDカードデバイスに置き換えてください。

## コンポーネント

### PTA (Pseudo Trusted Application)

OP-TEE OS内で動作するCAAM対応のRemote Attestation PTA：
- CAAM Black Keyによる秘密鍵の保護
- ECDSA P-256署名
- Evidence生成（CBOR/COSE形式）

### TA (Trusted Application)

セキュアワールドで動作するTrusted Application：
- PTAとのインターフェース
- ホストアプリケーションからの呼び出し処理

### Host Application

ノーマルワールドで動作するホストアプリケーション：
- Rust FFIライブラリ（Veraisonクライアント、CBOR処理）
- TAとの通信
- Veraisonサービスへのリモートアテステーション実行

## CAAM設定

ビルドでは以下のCAAM設定が有効化されます：

- `CFG_NXP_CAAM=y` - CAAM有効化
- `CFG_NXP_CAAM_ECC_DRV=y` - ECC暗号ドライバ
- `CFG_NXP_CAAM_BLOB_DRV=y` - Blobドライバ（Black Key用）

## 使用方法（ターゲットデバイス上）

```bash
# Remote Attestationの実行
optee_remote_attestation --verifier <verifier-url> --nonce <base64-nonce>
```
