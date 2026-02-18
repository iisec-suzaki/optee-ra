# Provisioning


Provisioning is a script to register the Trust Anchor and Reference Value to the Verifier. It wraps the commands provided by Veraison.

![](../OPTEE-RA.png)

## Execution Method

Use the following command to generate an endorsement in CBOR format.

```sh
cocli comid create --template data/comid-psa-ta-qemu.json \
                   --template data/comid-psa-refval-qemu.json \
                   --output-dir data
cocli corim create --template data/corim-psa.json \
                   --comid data/comid-psa-refval-qemu.cbor \
                   --comid data/comid-psa-ta-qemu.cbor \
                   --output data/psa-endorsements.cbor
```

For i.MX 8M Plus, replace all `-qemu` suffixes with `-imx`.

Use the following command to register the Trust Anchor and Reference Value with the verification server.
```sh
cocli corim submit --corim-file=data/psa-endorsements.cbor \
                   --api-server="https://provisioning-service:8888/endorsement-provisioning/v1/submit" \
                   --media-type="'application/corim-unsigned+cbor; profile=http://arm.com/psa/iot/1'" \
                   --ca-cert /tmp/veraison/certs/rootCA.crt
```

The provisioned data can be checked with the following command, and you will get output similar to the following.
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

If you update reference values or see `ear.status` as `warning` with `executables not recognized`, clear the stores and re-run provisioning in a shell where `env.bash` is sourced.

```sh
veraison clear-stores
./run.sh qemu
```

Use `imx` instead of `qemu` for i.MX 8M Plus.

## Easier Execution Method

You can automatically execute the above steps by running the following command.
```sh
# QEMU (default)
./run.sh qemu
# i.MX 8M Plus
./run.sh imx
```

If you omit the argument, `qemu` is used.
