# i.MX8MP EVK での fTPM(ファームウェア TPM)

Microsoft のファームウェア TPM(`ms-tpm-20-ref`)を OP-TEE の Trusted Application として動かす Yocto イメージのビルド方法と、Linux から `tpm2-tools` で動作確認する手順を説明します。

## 概要

* fTPM は TPM 2.0 リファレンス実装を OP-TEE TA(UUID `bc50d971-d4c9-42c4-82cb-343fb7f37896`)としてパッケージしたものです。 `meta-arm` がレシピ(`optee-ftpm`)を提供しており、マシンフィーチャ `optee-ftpm` を設定すると TA が OP-TEE OS バイナリに **early TA** (`CFG_EARLY_TA=y`)として組み込まれ、Normal World から何もロードせずに利用可能になります。
* Linux 側では `tpm_ftpm_tee` ドライバが標準の `/dev/tpm0` キャラクタデバイスとして公開します。

`meta-arm` はこのレシピを QEMU / `genericarm64` マシンに限定しています。本レイヤでは次の bbappend とパッチを追加しています:

| ファイル | 目的 |
|----------|------|
| `recipes-security/optee-ftpm/optee-ftpm_%.bbappend` | `imx8mpevk` を許可(`COMPATIBLE_MACHINE`)し、TA を AArch64 でビルド |
| `recipes-kernel/linux/linux-imx_%.bbappend` + `linux-imx/ftpm.cfg` | i.MX カーネルに `tpm_ftpm_tee` ドライバを組み込み(`CONFIG_TCG_FTPM_TEE=y`)、Linux が基板上 eMMC に触れないようにする(マシンフィーチャ `optee-ftpm` 設定時のみ) |
| `recipes-security/optee/optee-os_%.imx.bbappend` | OP-TEE コア内 uSDHC ドライバとネイティブ RPMB バックエンド(`attester/optee-patches/`)を OP-TEE に組み込み、私有ストレージを RPMB のみにする(`CFG_REE_FS=n`) |
| `recipes-bsp/u-boot/`, `recipes-bsp/imx-atf/` | U-Boot が基板上 eMMC に触れないようにする。BL31 で uSDHC3 をセキュアバスマスタにする |

## fTPM の有効化

通常のアテステーション設定に加えて `conf/local.conf`(ホスト上のパスは `${YOCTO_DIR}/build/conf/local.conf`)に以下を追加します:

```
MACHINE_FEATURES:append = " optee-ftpm"
IMAGE_INSTALL:append = " optee-ftpm tpm2-tools libtss2-tcti-device"
```

その後リビルドします。以下の `bitbake` コマンドは `yocto.sh` のビルドコンテナ内で実行されるものです。一度ビルド済みであれば、`local.conf` を編集して `YOCTO_DIR=... ./yocto.sh full` を再実行すれば同じ流れが走ります (`yocto.sh` 自身の設定追記は grep ガード付きのため、この編集を上書きしません)。fTPM は OP-TEE OS バイナリに組み込まれるため、 `optee-os` 変更後は `imx-boot` の再生成とイメージの再パックが必要です (`yocto.sh full` と同じ流れ):

```
bitbake core-image-minimal
bitbake -c cleansstate imx-boot && bitbake imx-boot
bitbake -f -c image core-image-minimal
```

HAB 署名済みボードの場合は、生成されたイメージを通常どおり `secure-boot-imx8mp.sh` で署名してください(セキュアブートガイド参照)。

## fTPM が Linux より先に起動する仕組み

fTPM は永続状態を基板上 eMMC の RPMB 領域に保存します。素の OP-TEE ではその通信を `tee-supplicant` が仲介するため、TA はユーザ空間が起動してからしか動けず、IMA が TPM を探し終えた後になっていました。`optee-ftpm` フィーチャでは本レイヤが次を行います:

* uSDHC3 用の eMMC コントローラドライバを OP-TEE コアに組み込み (`attester/optee-patches/imx_usdhc.c`)、`tee_rpmb_fs.c` の通信をそこへ振り向ける(`rpmb-native-backend.py`)。Linux の助けなしに RPMB へ到達できる
* 私有ストレージを RPMB のみにし(`CFG_REE_FS=n`)、TA が supplicant を待たずに OP-TEE ドライバのプローブ時に Linux へ現れるようにする
* 基板上 eMMC を TEE に渡す。U-Boot と Linux のデバイスツリーで uSDHC3 を無効化し、BL31 でセキュアバスマスタかつレジスタをセキュア専用にし、共有バスクロック `nand_usdhc_bus` をカーネルが止めないようにする

結果として、カーネル初期化中に `/dev/tpm0` が存在し、IMA は計測ログを fTPM(PCR 10)に紐付けます。起動後に読み込むものはありません。

**デバイスでの初回起動。** `CFG_RPMB_WRITE_KEY=y` により、OP-TEE は鍵が未設定であれば RPMB 認証鍵を書き込みます。これは eMMC ごとに 1 回だけで、取り消せません(鍵は CAAM のマスター鍵と eMMC の CID から導出するため、どこにも保存しません)。続いて fTPM が NV 領域を作成するため、初回のみ通常の起動より数秒長くかかります。

**CID の配置。** 鍵の導出には CID の 16 バイトを JEDEC の配置(先頭がメーカー ID、CRC の位置は 0)で使います。Linux カーネルと tee-supplicant が使うのと同じバイト列なので、どの経路で書き込んだ鍵も他の経路で有効です。ドライバの最初の版はこのバイト列を 1 つ後ろにずらして扱っており、研究室の EVK はその配置で鍵を書き込んであるため、同じ鍵を導き続けるには `CFG_IMX_USDHC_CID_COMPAT=y`(その EVK の `local.conf` に設定)が必要です。新しいデバイスには決して設定しないでください。

## 実機での使用

```sh
dmesg | grep -iE "tpm|ima:"      # "No TPM chip found" が出ないこと、tpm0 のエラーがないこと
ls /dev/tpm0
tpm2_getcap properties-fixed   # メーカー / ファームウェア情報
tpm2_pcrread sha256:10         # IMA が計測すれば非ゼロ
```

## 注意事項

* **eMMC は TEE のもの**: この構成は U-Boot と Linux が基板上 eMMC を使わないことが前提で、SD からブートする EVK では成立します。唯一の eMMC からブートする製品や、eMMC を Linux のストレージに使う製品では別の設計(TEE 専用の記憶デバイス、または CAAM 由来のシードで動く揮発 fTPM)が必要です。
* **RPMB 鍵の書き込みは取り消せない**: 「デバイスでの初回起動」を参照。
* **ブートファームウェアは計測されない**: IMA はカーネル起動後から計測します。U-Boot とカーネルは PCR に extend されません(HAB のセキュアブートと RA の PRoT 計測が担います)。
* フィーチャ有効時、`meta-arm` の bbappend が `CFG_CORE_HEAP_SIZE` を 128 KiB に固定します(fTPM は OP-TEE 汎用デフォルトの 64 KiB より多くの TEE コアヒープを必要とするため)。i.MX では NXP ツリーが元々全 i.MX プラットフォームを 128 KiB にしているため、実質的な変化はありません。
