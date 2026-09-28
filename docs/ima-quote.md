# Attesting the IMA measurement list with the fTPM

With the fTPM started before Linux (see `ftpm-imx8mp.md`), IMA extends every measured file into PCR 10 of the fTPM. This document describes how to prove to another machine that a given measurement list is the one the TPM attested, using nothing but `tpm2-tools` on both sides. It is the manual procedure agreed with IISEC; the verifier-side check runs on any host.

## What is checked

| Step | Where | What it establishes |
|------|-------|---------------------|
| Quote of PCR 10 signed by the AK, with a nonce | board (`ima-quote`) | The value of PCR 10 at that moment, signed inside the TPM |
| `tpm2_checkquote` with the AK public key, the nonce and the PCR values | verifier (`host-verify.sh`) | The quote is from the holder of the AK, is fresh (nonce), and `quote.pcr` matches the signed digest |
| Replay of the measurement list into a zeroed PCR until it reproduces the quoted value | verifier (`pcr10_recompute3.py`) | The leading entries of the list, up to the match, are the ones the TPM attested: no entry was altered, removed or reordered |

Entries after the matching line were recorded after the quote and are not attested. What the entries mean (whether the measured files are the expected ones) is a separate policy step and is not covered here.

The AK is created inside the fTPM as a child of the EK and is a restricted signing key, so it can only sign structures the TPM generated itself. The fTPM has no manufacturer EK certificate: the AK public key is handed to the verifier out of band.

## Board side

Add the package to the image (with the `optee-ftpm` feature):

```
IMAGE_INSTALL:append = " ima-quote"
```

Then, as root:

```sh
ima-quote                    # initialise, create EK/AK, quote with a random nonce
ima-quote <nonce-hex>        # same, with the verifier's nonce
ima-quote -k <nonce-hex>     # reuse the persistent EK/AK
```

The script runs, in this order: `tpm2_clear`; EK (RSA) to persistent handle `0x81010001`; AK (RSA, SHA-256, RSASSA) from the EK to `0x81000002`; a 16-byte nonce; `tpm2_quote --pcr-list=sha256:10`; and copies `ak.pub`, `quote.msg`, `quote.sig`, `service_provider_nonce`, `quote.pcr` and the measurement list into `/root/ima-quote/verification/`. `tpm2_clear` wipes any earlier AK (the EK comes back identical, it is derived from the endorsement seed). RSA key generation takes tens of seconds on the fTPM.

Because the EVK has no network, the script ends by printing `verification/` as a hex-encoded tarball between `-----BEGIN VERIFICATION HEX-----` and `-----END VERIFICATION HEX-----`; capture the console to a log file.

## Verifier side

The tools live in `attester/ima-quote/`. They need `tpm2-tools` and `python3`; `host-verify-docker.sh` runs the check in a container (built from the `Dockerfile` there) for hosts that lack `tpm2-tools`.

```sh
cd attester/ima-quote
sh host-unpack.sh /path/to/console.log /tmp/v      # -> /tmp/v/verification
sh host-verify.sh /tmp/v/verification               # or host-verify-docker.sh
```

`host-verify.sh` runs `tpm2_checkquote`, prints the PCR 10 value the quote attests, then trims the measurement list from the end one line at a time until `pcr10_recompute3.py` reproduces that value:

```
== PCR 10 from the quote: 5eb8370412d98afd...
[OK] match after removing 2 trailing line(s): the first 490 entries are covered by the quote
```

`[NG]` means no prefix of the list reproduces the quoted value: the list is not the one the quote attests.

`sh lab-test-docker.sh` exercises both sides against a software TPM (swtpm), including a tampered list, without a board.

## Notes

* `boot_aggregate`, the first entry, is computed over PCRs 0 to 7 (and 8, 9 for SHA-256), which nothing extends on this platform: its value is constant. Boot firmware is verified by HAB secure boot and reported by the existing remote attestation, not by the TPM.
* `tpm2_quote` needs no authorisation for the AK as created here; the owner hierarchy has no password, so root can also remove the persistent handles.
