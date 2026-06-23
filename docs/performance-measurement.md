# Performance Measurement Logging (`RA_PERF`)

This repository can emit timing logs for the whole remote attestation flow so
that the total processing time and the duration of each major event (hash
computation, CBOR/COSE encoding, CAAM signing, black-key processing, …) can be
collected for performance evaluation.

The Japanese version of this document is
[performance-measurement-j.md](performance-measurement-j.md).

## Log format

Every measurement is a single line:

```
RA_PERF|<layer>|<event>|<duration_us>|key=<keymode>
```

| Field         | Meaning                                                                                  |
| ------------- | ---------------------------------------------------------------------------------------- |
| `layer`       | `host` (Linux client), `ta` (user TA), `pta` (PTA inside the OP-TEE core)                 |
| `event`       | See the log point tables below                                                            |
| `duration_us` | Duration in microseconds (integer)                                                        |
| `keymode`     | Key path of the run: `embedded` (built-in test key), `plain` (external plain key), `black` (CAAM black key), `-` (not applicable) |

Clock sources and where the lines appear:

| Layer  | Clock                                          | Resolution | Output                                          |
| ------ | ---------------------------------------------- | ---------- | ----------------------------------------------- |
| `host` | `clock_gettime(CLOCK_MONOTONIC)`               | ~1 µs      | stdout of `optee_remote_attestation`            |
| `ta`   | `TEE_GetSystemTime`                            | ~1 ms      | secure console (`I/TA:` prefix)                 |
| `pta`  | ARM generic timer (`CNTPCT`), frequency `CNTFRQ` | sub-µs   | secure console (`I/TC:` prefix)                 |

On QEMU the secure console is `serial1.log` (with `make check`) or the
soc_term window; on the i.MX 8M Plus EVK it is the debug UART. `dmesg` is not
involved. The PTA reports the counter frequency once per run as the
`cntfrq` pseudo-event so the tick-to-µs conversion can be verified
(i.MX8MP: 8 MHz, QEMU virt: ~62.5 MHz).

## Log points

### `host` (stdout)

| Event                   | Measures                                                              |
| ----------------------- | --------------------------------------------------------------------- |
| `teec_init`             | `TEEC_InitializeContext` (open `/dev/tee0`)                            |
| `teec_open_session`     | `TEEC_OpenSession` — includes TA load/authentication on every run      |
| `veraison_new_session`  | HTTP round trip to the relying party (`newSession`, nonce receipt)     |
| `evidence_get`          | `TEEC_InvokeCommand` — entire secure-side evidence generation as seen from the REE |
| `print_evidence`        | Console hex dump of the evidence (so it can be subtracted from `total`)|
| `veraison_post_evidence`| HTTP round trip: evidence POST → attestation result (EAR JWT)          |
| `total`                 | One whole attestation transaction (session open → result received)    |
| `blackkey_generate`     | `--generate-blackkey` total (two PTA invocations: size probe + fetch)  |
| `blackkey_convert`      | `--convert-key` invocation                                             |

### `ta` (secure console, ~1 ms resolution)

| Event        | Measures                                            |
| ------------ | ---------------------------------------------------- |
| `pta_invoke` | `TEE_InvokeTACommand` round trip into the PTA        |
| `cmd_total`  | Whole TA command incl. parameter staging and copy-back |

### `pta` (secure console, µs resolution)

| Event              | Measures                                                                    |
| ------------------ | ---------------------------------------------------------------------------- |
| `cntfrq`           | (pseudo-event) generic timer frequency in Hz, not a duration                  |
| `ocotp_srk`        | OCOTP SRK-hash fuse read for signer-id (i.MX only)                            |
| `ocotp_lifecycle`  | OCOTP lifecycle read (i.MX only; re-reads the SRK words internally)           |
| `instance_id`      | PSA instance-id derivation (SHA-256 of the public key)                        |
| `hash_ta`          | ARoT measurement: SHA-256 over the calling TA's read-only memory              |
| `hash_tee`         | PRoT measurement: SHA-256 over the OP-TEE core `.text` + `.rodata`            |
| `cbor_encode`      | QCBOR encoding of the PSA claims                                              |
| `sign_key_setup`   | Signing key allocation and import (black-key blob or embedded key + pubkey)   |
| `sign_tbs_hash`    | SHA-256 of the COSE to-be-signed structure                                    |
| `sign_ecdsa`       | `crypto_acipher_ecc_sign` — **the black-key comparison bracket** (see below)  |
| `sign_verify`      | Self-check `crypto_acipher_ecc_verify` (embedded-key path only)               |
| `cose_sign1`       | Whole COSE_Sign1 generation (includes the four `sign_*` events)               |
| `cmd_total`        | Whole `GET_CBOR_EVIDENCE` PTA command                                         |
| `perf_flush`       | Cost of printing the buffered RA_PERF lines themselves (see Caveats)          |
| `keypair_generate` | `crypto_acipher_gen_ecc_key` (CAAM keygen on i.MX); logged twice per `--generate-blackkey` (size probe + fetch) |
| `blackkey_encap`   | `caam_key_black_encapsulation` (CCM) in `--convert-key`                       |
| `convert_total`    | Whole `CONVERT_TO_BLACKKEY` PTA command                                       |

## Enabling the logs

The firmware-side instrumentation is compiled in only with
`CFG_REMOTE_ATTESTATION_PERF=y`; release builds are unaffected by default.

**QEMU** (inside the attester container):

```bash
make check CFG_REMOTE_ATTESTATION_PTA=y CFG_REMOTE_ATTESTATION_PERF=y
```

**i.MX 8M Plus (Yocto)** — add to `local.conf` (or pass through your build
wrapper):

```
CFG_REMOTE_ATTESTATION_PERF:pn-optee-os = "y"
CFG_REMOTE_ATTESTATION_PERF:pn-veraison-attestation = "y"
```

then rebuild `optee-os` (cleansstate), `veraison-attestation`, the image, and
re-sign. Note: NXP's `optee-os-common-fslc-imx.inc` forces
`CFG_TEE_CORE_LOG_LEVEL=0` (all core trace compiled out), so the bbappend in
this repository automatically raises the core log level to 2 together with the
perf flag — without it no `pta` line would ever reach the UART.

**Host client** — runtime opt-in, independent of the firmware flag:

```bash
optee_remote_attestation --perf          # or RA_PERF=1 optee_remote_attestation
```

Additional client options for data collection:

| Option        | Purpose                                                                  |
| ------------- | ------------------------------------------------------------------------ |
| `--loop N`    | Repeat the whole attestation N times (per-iteration `total` lines)        |
| `--no-server` | Skip the verifier round trips and attest a local random nonce — useful on devices that cannot reach a relying party, and for isolating TEE-side numbers from network jitter |

## Recommended measurement protocol

1. Fix the environment and record it: board (i.MX8MPEVK), build config,
   `cntfrq` line, software versions, N. Pin the CPU governor and record the
   actual frequency — the sub-ms events (signing, hashing) swing with DVFS:

   ```bash
   for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
     echo performance > "$g" 2>/dev/null
   done
   cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
   cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq
   ```

   If pinning is not possible, at least read and record `scaling_governor`
   and `scaling_cur_freq` so the DVFS-induced variance can be bounded.
2. Collect ≥ 30 iterations per configuration and discard the first iteration
   (cold caches, first TA load):

```bash
# with verifier (end-to-end):
optee_remote_attestation --perf --loop 31 | tee host-embedded.log

# TEE-side only (no network):
optee_remote_attestation --perf --no-server --loop 31 | tee host-offline.log

# CAAM black key (i.MX only): generate once, then measure
optee_remote_attestation --generate-blackkey --perf      # records blackkey_generate
optee_remote_attestation --perf --loop 31 --key-hex <FullKey> | tee host-black.log
```

3. Capture the secure console (UART/`serial1.log`) for the `pta`/`ta` lines.
4. Aggregate (mean/min/max per event and key path):

```bash
grep -h -o 'RA_PERF|.*' serial1.log host-*.log | awk -F'|' '
  { split($5, k, "="); id = $2 "|" $3 "|" k[2];
    n[id]++; s[id] += $4;
    if (min[id] == "" || $4 < min[id]) min[id] = $4;
    if ($4 > max[id]) max[id] = $4 }
  END { for (i in n) printf "%-44s n=%-3d mean=%10.1f min=%8d max=%8d\n",
        i, n[i], s[i]/n[i], min[i], max[i] }' | sort
```

This one-liner aggregates **all** iterations, so its `n` includes the cold
first iteration that step 2 says to discard. Either treat its output as
"untrimmed (n=N)" or drop the first occurrence per `layer|event|keymode`
before aggregating (the raw log preserves iteration order, so `tail -n +2`
per event works). Note `teec_init`/`teec_open_session` are measured once
before the loop, so they are already outside the per-iteration aggregates.

## Comparing CAAM signing with and without the black key

The black-key usage is a **runtime** choice on a single i.MX build:

* **with black key**: pass `--key-hex <FullKey>` (FullKey =
  `PubX(32) || PubY(32) || serialized CAAM black-key blob`, produced by
  `--generate-blackkey` or `--convert-key`). Log lines carry `key=black`.
* **without black key**: pass no key arguments — the embedded test key is
  used. Log lines carry `key=embedded`.

Compare the `pta|sign_ecdsa` event only:

* On i.MX with `CFG_NXP_CAAM_ECC_DRV=y` **both** paths execute the ECDSA sign
  on the CAAM hardware; the black-key path additionally pays the key-blob
  decapsulation and in-engine CCM key import, which is exactly the delta the
  comparison isolates. (A QEMU run provides a third, software-only datapoint.)
* `sign_verify` runs **only** on the embedded-key path (signature self-check);
  it is reported separately precisely so that it does not bias the comparison.
  Do not use `cose_sign1` or `cmd_total` for the black-key delta.

> **Security note.** Running without the black key means the signing key is
> handled as plaintext inside OP-TEE (and, for the embedded test key, is part
> of the binary). It is **not** protected by the CAAM and the
> secure-boot-rooted key-protection guarantee does not hold. The
> non-black-key numbers are for performance comparison only and must not be
> used in a production configuration.

## Caveats for interpreting the numbers

* All PTA timestamps are taken **before** any RA_PERF line is printed; the
  lines are flushed in one batch at the end of the PTA command. Console
  writes are synchronous, so this flush *is* visible in the brackets of the
  layers above (`ta|pta_invoke`, `host|evidence_get`, `host|total`). The
  flush reports its own cost as `pta|perf_flush` — subtract it when relating
  layers (only the final `perf_flush` line itself remains unaccounted).
* Use `pta|cmd_total` as the pure evidence-generation time;
  `host|evidence_get − ta|cmd_total` ≈ REE↔TEE transition + libteec overhead,
  `ta|pta_invoke − pta|cmd_total − pta|perf_flush` ≈ secure-world dispatch
  overhead.
* `host|total` includes all verbose client prints; `print_evidence` is
  measured so the console dump can be subtracted.
* The `ta` layer has only ~1 ms resolution (`TEE_GetSystemTime`); user TAs
  have no direct access to the cycle counter.
* `hash_ta` includes a `qsort` with `memcmp` of the TA's read-only regions
  (ASLR-stable ordering), not just the SHA-256 itself.
* `ocotp_lifecycle` internally re-reads the SRK fuse words already read by
  `ocotp_srk`; the duplication is intentionally measured as-is.
