# i.MX8MP EVK での fTPM(ファームウェア TPM)

Microsoft のファームウェア TPM(`ms-tpm-20-ref`)を OP-TEE の Trusted
Application として動かす Yocto イメージのビルド方法と、Linux から
`tpm2-tools` で動作確認する手順を説明します。

## 概要

* fTPM は TPM 2.0 リファレンス実装を OP-TEE TA(UUID
  `bc50d971-d4c9-42c4-82cb-343fb7f37896`)としてパッケージしたものです。
  `meta-arm` がレシピ(`optee-ftpm`)を提供しており、マシンフィーチャ
  `optee-ftpm` を設定すると TA が OP-TEE OS バイナリに **early TA**
  (`CFG_EARLY_TA=y`)として組み込まれ、Normal World から何もロードせずに
  利用可能になります。
* Linux 側では `tpm_ftpm_tee` ドライバが標準の `/dev/tpm0` キャラクタ
  デバイスとして公開します。

`meta-arm` はこのレシピを QEMU / `genericarm64` マシンに限定しているため、
本レイヤでは 2 つの bbappend を追加しています:

| ファイル | 目的 |
|----------|------|
| `recipes-security/optee-ftpm/optee-ftpm_%.bbappend` | `imx8mpevk` を許可(`COMPATIBLE_MACHINE`)し、TA を AArch64 でビルド |
| `recipes-kernel/linux/linux-imx_%.bbappend` + `linux-imx/ftpm.cfg` | i.MX カーネルで `CONFIG_TCG_FTPM_TEE=m` を有効化(マシンフィーチャ `optee-ftpm` 設定時のみ) |

## fTPM の有効化

通常のアテステーション設定に加えて `conf/local.conf`(ホスト上のパスは
`${YOCTO_DIR}/build/conf/local.conf`)に以下を追加します:

```
MACHINE_FEATURES:append = " optee-ftpm"
IMAGE_INSTALL:append = " optee-ftpm tpm2-tools kernel-module-tpm-ftpm-tee"
```

その後リビルドします。以下の `bitbake` コマンドは `yocto.sh` のビルド
コンテナ内で実行されるものです。一度ビルド済みであれば、`local.conf` を
編集して `YOCTO_DIR=... ./yocto.sh full` を再実行すれば同じ流れが走ります
(`yocto.sh` 自身の設定追記は grep ガード付きのため、この編集を上書き
しません)。fTPM は OP-TEE OS バイナリに組み込まれるため、
`optee-os` 変更後は `imx-boot` の再生成とイメージの再パックが必要です
(`yocto.sh full` と同じ流れ):

```
bitbake core-image-minimal
bitbake -c cleansstate imx-boot && bitbake imx-boot
bitbake -f -c image core-image-minimal
```

HAB 署名済みボードの場合は、生成されたイメージを通常どおり
`secure-boot-imx8mp.sh` で署名してください(セキュアブートガイド参照)。

## 実機での使用

ドライバは意図的にモジュールとしてビルドしています。fTPM は永続状態を
`tee-supplicant` 経由(REE ファイルシステム。EVK では RPMB 未プロビジョン)
で保存するため、supplicant 起動後でないと TA を初期化できないためです。
起動後:

```sh
modprobe tpm_ftpm_tee
ls /dev/tpm0
tpm2_getcap properties-fixed   # メーカー / ファームウェア情報
tpm2_pcrread                   # PCR バンク
```

## 注意事項

* **状態の保存先**: fTPM の永続状態は `tee-supplicant` 経由で REE ファイル
  システムに置かれます。OP-TEE により暗号化されますが、RPMB がないため
  TPM 状態のロールバック保護はありません。
* **measured boot のルートではない**: このプラットフォームではブート中に
  fTPM の PCR へ計測値を extend する仕組みはなく、PCR はリセット値のまま
  始まります。IMA やブート計測のルートとして使うには追加の統合が必要です。
* フィーチャ有効時、`meta-arm` の bbappend が `CFG_CORE_HEAP_SIZE` を
  128 KiB に固定します(fTPM は OP-TEE 汎用デフォルトの 64 KiB より多くの
  TEE コアヒープを必要とするため)。i.MX では NXP ツリーが元々全 i.MX
  プラットフォームを 128 KiB にしているため、実質的な変化はありません。
