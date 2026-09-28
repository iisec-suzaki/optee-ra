# fTPM による IMA 計測リストの検証

fTPM を Linux より先に起動する構成(`ftpm-imx8mp-j.md`)では、IMA は計測したファイルごとに fTPM の PCR 10 を extend します。本書は、ある計測リストが TPM の保証するものであることを、双方 `tpm2-tools` だけで別マシンに示す手順を説明します。情セ大と合意した手動の手順で、検証側はどのホストでも実行できます。

## 何を確かめるか

| 手順 | 場所 | 分かること |
|------|------|-----------|
| nonce 付きで PCR 10 の quote を AK で署名 | 基板(`ima-quote`) | その時点の PCR 10 の値が TPM 内で署名されたこと |
| AK 公開鍵・nonce・PCR 値で `tpm2_checkquote` | 検証者(`host-verify.sh`) | quote が AK の持ち主によるもの、再送でない(nonce)、`quote.pcr` が署名されたダイジェストと一致すること |
| 計測リストをゼロの PCR に先頭から extend し直し、quote の値を再現する行まで採用 | 検証者(`pcr10_recompute3.py`) | 一致した行までのエントリが TPM の保証するもので、改ざん・削除・並べ替えがないこと |

一致した行より後のエントリは quote の後に記録されたもので、保証の外です。エントリの意味(計測されたファイルが想定どおりか)は別のポリシー判定で、本書の範囲外です。

AK は fTPM 内で EK の子として作る制限付き署名鍵で、TPM が内部で生成した構造体にしか署名できません。fTPM にはメーカーの EK 証明書がないため、AK の公開鍵は帯域外で検証者に渡します。

## 基板側

`optee-ftpm` フィーチャとあわせてイメージにパッケージを加えます。

```
IMAGE_INSTALL:append = " ima-quote"
```

root で次を実行します。

```sh
ima-quote                    # 初期化、EK/AK 生成、乱数 nonce で quote
ima-quote <nonce-hex>        # 検証者の nonce を使う
ima-quote -k <nonce-hex>     # 永続化済みの EK/AK を使い回す
```

スクリプトは次の順に実行します。`tpm2_clear`、EK(RSA)を永続ハンドル `0x81010001` に、EK の子として AK(RSA、SHA-256、RSASSA)を `0x81000002` に、 16 バイトの nonce、`tpm2_quote --pcr-list=sha256:10`、そして `ak.pub`、 `quote.msg`、`quote.sig`、`service_provider_nonce`、`quote.pcr`、計測リストを `/root/ima-quote/verification/` にコピー。`tpm2_clear` は以前の AK を消します(EK はシードから導出されるので同じ鍵に戻ります)。RSA の鍵生成は fTPM では数十秒かかります。

EVK にはネットワークがないため、最後に `verification/` を 16 進の tar として `-----BEGIN VERIFICATION HEX-----` と `-----END VERIFICATION HEX-----` の間に出力します。コンソールをログファイルに記録してください。

## 検証者側

ツールは `attester/ima-quote/` にあります。`tpm2-tools` と `python3` が必要です。`host-verify-docker.sh` は、`tpm2-tools` のないホスト向けに同じ確認をコンテナ(同ディレクトリの `Dockerfile`)内で行います。

```sh
cd attester/ima-quote
sh host-unpack.sh /path/to/console.log /tmp/v      # -> /tmp/v/verification
sh host-verify.sh /tmp/v/verification               # または host-verify-docker.sh
```

`host-verify.sh` は `tpm2_checkquote` を実行して quote が保証する PCR 10 の値を表示し、計測リストを後ろから 1 行ずつ削りながら `pcr10_recompute3.py` がその値を再現するまで繰り返します。

```
== PCR 10 from the quote: 5eb8370412d98afd...
[OK] match after removing 2 trailing line(s): the first 490 entries are covered by the quote
```

`[NG]` は、リストのどの先頭部分も quote の値を再現しないこと、つまりリストが quote の保証するものではないことを意味します。

`sh lab-test-docker.sh` は、基板なしにソフトウェア TPM(swtpm)上で両側を通します(改ざんしたリストの拒否を含む)。

## 補足

* 先頭の `boot_aggregate` は PCR 0〜7(SHA-256 では 8、9 も)から計算されますが、このプラットフォームではどれも extend されないため一定値です。ブートファームウェアは HAB のセキュアブートで検証され、既存のリモートアテステーションで報告されます。TPM の役割ではありません。
* ここで作る AK の `tpm2_quote` に認可は不要です。owner 階層にパスワードがないため、root は永続ハンドルを削除することもできます。
