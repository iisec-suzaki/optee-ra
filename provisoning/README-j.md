<!--
-------------------------------------------------------------------------------------------------------------------
-->
# 日本語解説 Provisioning

Provisioning は Verifier に対して、アテステーションで使うTrust AnchorとReference Valueを登録するスクリプトです。Veraison が提供するコマンドをラッパーしています。

![](../OPTEE-RA.png)

## 実行方法

以下のコマンドで、CBOR フォーマットの endorsment を生成します。
```sh
cocli comid create --template data/comid-psa-ta.json \
                   --template data/comid-psa-refval.json \
                   --output-dir data
cocli corim create --template data/corim-psa.json \
                   --comid data/comid-psa-refval.cbor \
                   --comid data/comid-psa-ta.cbor \
                   --output data/psa-endorsements.cbor
```

以下のコマンドで、検証サーバーに対して、trust anchor と reference value を登録します。
```sh
cocli corim submit --corim-file=data/psa-endorsements.cbor \
                   --api-server="http://provisioning-service:8888/endorsement-provisioning/v1/submit" \
                   --media-type="'application/corim-unsigned+cbor; profile=http://arm.com/psa/iot/1'"
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
    "PSA_IOT.impl-id": "YWNtZS1pbXBsZW1lbnRhdGlvbi1pZC0wMDAwMDAwMDE=",
    "PSA_IOT.inst-id": "Ac7rrnuJJ6MiflMDz14PH3s0u1Qq1yUKwD+83jbsLxUI"
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
    "PSA_IOT.impl-id": "YWNtZS1pbXBsZW1lbnRhdGlvbi1pZC0wMDAwMDAwMDE=",
    "PSA_IOT.measurement-desc": "sha-256",
    "PSA_IOT.measurement-type": "PRoT",
    "PSA_IOT.measurement-value": "KAnwpOcg4BXhGp/0onvYCH3rnH3wR6xHj9hOrTropX4=",
    "PSA_IOT.signer-id": "rLsRx+TaIXIFUjzkzhokWuGiOa48a/2eeHH35di66Gs="
  }
}
```

## より簡単な実行方法

下のコマンドを実行することで、上の手順を自動で実行することができます。
```sh
./run.sh
```
