# 日本語解説 Provisioning

Provisioning は Verifier に対して、アテステーションで使うTrust AnchorとReference Valueを登録するスクリプトです。Veraison が提供するコマンドをラッパーしています。

![](../OPTEE-RA.png)

## 実行方法

以下のコマンドで、CBOR フォーマットの endorsment を生成します。
```sh
cocli comid create --template data/comid-psa-ta-qemu.json \
                   --template data/comid-psa-refval-qemu.json \
                   --output-dir data
cocli corim create --template data/corim-psa.json \
                   --comid data/comid-psa-refval-qemu.cbor \
                   --comid data/comid-psa-ta-qemu.cbor \
                   --output data/psa-endorsements.cbor
```

実機の場合はすべての `-qemu` サフィックスを `-imx` に置き換えてください。

以下のコマンドで、検証サーバーに対して、trust anchor と reference value を登録します。
```sh
cocli corim submit --corim-file=data/psa-endorsements.cbor \
                   --api-server="https://provisioning-service:8888/endorsement-provisioning/v1/submit" \
                   --media-type="'application/corim-unsigned+cbor; profile=http://arm.com/psa/iot/1'" \
                   --ca-cert /tmp/veraison/certs/rootCA.crt
```

provison されたデータは以下のコマンドで確認でき、以下のような出力が得られます。
```sh
veraison stores
```

```txt
TRUST ANCHORS:
--------------
{
  "scheme": "PSA_IOT",
  "type": "trust anchor",
  "subType": "",
  "attributes": {
    "PSA_IOT.hw-model": "RoadRunner",
    "PSA_IOT.hw-vendor": "ACME",
    "PSA_IOT.iak-pub": "-----BEGIN PUBLIC KEY-----\nMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEMKBCTNIcKUSDii11ySs3526iDZ8A\niTo7Tu6KPAqv7D7gS2XpJFbZiItSs3m9+9Ue6GnvHw/GW2ZZaVtszggXIw==\n-----END PUBLIC KEY-----",
    "PSA_IOT.impl-id": "cWVtdS1vcHRlZS1yYS0wMDAwMDAwMDAwMDAwMDAwMDE=",
    "PSA_IOT.inst-id": "AZDHHoAwT5jWVWpALAWTszqArL0I5K/5xAKfbhfhA5lR"
  }
}

ENDORSEMENTS:
-------------
{
  "scheme": "PSA_IOT",
  "type": "reference value",
  "subType": "PSA_IOT.sw-component",
  "attributes": {
    "PSA_IOT.hw-model": "RoadRunner",
    "PSA_IOT.hw-vendor": "ACME",
    "PSA_IOT.impl-id": "cWVtdS1vcHRlZS1yYS0wMDAwMDAwMDAwMDAwMDAwMDE=",
    "PSA_IOT.measurement-desc": "sha-256",
    "PSA_IOT.measurement-type": "ARoT",
    "PSA_IOT.measurement-value": "Qjf7I3AQkjFoBQBbhrKrYPX/toHhnmfZeingk5oE6jA=",
    "PSA_IOT.signer-id": "rLsRx+TaIXIFUjzkzhokWuGiOa48a/2eeHH35di66Gs="
  }
}
```

`ear.status` が `warning` で `executables not recognized` のようなログが出る場合は、古いエンドースメントが残っています。`env.bash` を source したシェルでストアをクリアし、provisioning をやり直してください。

```sh
veraison clear-stores
./run.sh qemu
```

実機の場合は `imx` を指定します。

## より簡単な実行方法

下のコマンドを実行することで、上の手順を自動で実行することができます。
```sh
# QEMU (default)
./run.sh qemu
# i.MX 8M Plus
./run.sh imx
```

引数を省略した場合は `qemu` が使われます。

## 参照値のオフライン計算

`data/comid-psa-refval-*.json` に登録する `measurement-value` のダイジェストは、
ビルド成果物から直接計算できます。イメージの起動やデバッグログの取得は不要です
(i.MX の本番ビルドはコアログレベルが 0 のため、オフライン計算が唯一の取得手段です):

```sh
# ARoT(TA の測定値): 署名済み .ta または TA の ELF を渡す
./compute-arot-refval.py path/to/<uuid>.ta

# PRoT(OP-TEE OS の測定値): コアの ELF を渡す
./compute-prot-refval.py path/to/tee.elf
```

それぞれ、対応する measurement に記載する `sha-256;<base64>` ダイジェストを
出力します。どちらも QEMU 上で、PTA が出力する実行時測定値と一致することを
検証済みです。PRoT の値はビルド固有(OP-TEE がビルド日時をハッシュ対象の
`.rodata` に埋め込むため)なので、リリースビルドごとに再計算・再登録して
ください。ARoT の値は TA バイナリが変わったときだけ変わります。
`pyelftools` が必要です(`pip install pyelftools`)。
