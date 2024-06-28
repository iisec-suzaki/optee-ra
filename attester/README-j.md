
<!--
-------------------------------------------------------------------------------------------------------------------
-->
# 日本語解説　OP-TEE Attester

Attester は Relying Party (Verifier) に対して、リモート認証のリクエストを送るクライアントです。このクライアントはTEE(Trusted Execution Environment, 具体的にはArm TrustZone)を持ち、 OP-TEE を実行します。現在のAttestaerは QEMU の上で構築されます。

![](../OPTEE-RA.png)

## 実行方法

Attester はコンテナ上の QEMU の上で起動されます。以下の 5 つの手順に従い、コンテナの起動し、 Verifier "Varaison"との通信プログラムを実行してください。

### 1. コンテナの起動

以下のコマンドを実行すると、Docker コンテナ上に入ります。
```sh
./container/start.sh
```

### 2. normal world のターミナルを開く

手順 1. でコンテナを起動したターミナルとは別のターミナルを開き、以下を実行してください。QEMU 上で実行する Attester の normal world に接続する用のターミナルが準備されます。
```sh
./container/launch_soc_term.sh normal
```

### 3. secure world のターミナルを開く

手順 1. と 2. で使用したターミナルとは別のターミナルを開き、以下を実行してください。QEMU 上で実行する Attester の secure world に接続する用のターミナルが準備されます。
```sh
./container/launch_soc_term.sh secure
```

### 4. ユーザが追加した CA/TA/PTA のビルドと、QEMU の起動とログイン

手順 1. で起動したターミナルで以下コマンドをを実行してください。以下のコマンでは `/optee/optee_os/core/pta/sub.mk` を編集し、`subdirs-y += remote_attestation` の行を追加し、追加したアプリケーションを再ビルドし、QEMUを立ち上げます。
```sh
echo "subdirs-y += remote_attestation" >> /optee/optee_os/core/pta/sub.mk
make -C ${OPTEE_DIR}/build run CFG_REMOTE_ATTESTATION_PTA=y -j
```

QEMU が立ち上がったら、c を入力します。
```sh
(qemu) c
```

次に、normal world のターミナル（2. で起動したターミナル）上で、`test` ユーザでログインします。
```sh
buildroot login: test
```

### 5. プログラムの実行

プログラムは normal world のターミナル上で起動します。
開発した evidence 生成プログラムを実行する場合は、以下のコマンドで実行できます。
```sh
optee_remote_attestation
```

## QCBOR ライブラリの改変

PTA アプリケーションの中には CBOR を扱うためのライブラリである [QCBOR](https://github.com/laurencelundblade/QCBOR) を一部改変したものを使用している。改変元のコミット ID は [`83dbf5c`](https://github.com/laurencelundblade/QCBOR/tree/8320e8db0433f80da5bc5ad7aa8b3a0754d77764) で、それに以下の改変を加えている：

* `UsefulBuf.h`: `USEFULBUF_DISABLE_ALL_FLOAT` 定義の追加
* `UsefulBuf.c`: なし
* `ieee754.h`: なし
* `ieee754.c`: `USEFULBUF_DISABLE_ALL_FLOAT` 定義の追加、同マクロによる浮動小数点処理関数の除外
* `qcbor_common.h`: なし
* `qcbor_decode.h`: なし
* `qcbor_encode.c`: `qcbor_encode.h` の `include` パス変更（`qcbor/qcbor_encode.h` -> `qcbor_encode.h`）、`USEFULBUF_DISABLE_ALL_FLOAT` マクロの追加
* `qcbor_encode.h`: `qcbor_common.h` 及び `qcbor_private.h` の `include` パス変更（上記と同じように先頭の `qcbor/` を除去）
* `qcbor_private.h`: `qcbor_common.h` の `include` パス変更（上記と同じように先頭の `qcbor/` を除去）
* `qcbor_spiffy_decode.h`: なし

## フォーマット方法

```sh
clang-format -i -style=file pta_remote_attestation/*.h pta_remote_attestation/remote_attestation/*.c pta_remote_attestation/remote_attestation/*.h
clang-format -i -style=file remote_attestation/host/*.c remote_attestation/host/*.h remote_attestation/ta/*.c remote_attestation/ta/*.h remote_attestation/ta/include/*.h
```
