#!/bin/bash
set -e
export TPM2TOOLS_TCTI="swtpm:port=2321"
mkdir -p /tmp/swtpm && swtpm socket --tpm2 --tpmstate dir=/tmp/swtpm --ctrl type=tcp,port=2322 --server type=tcp,port=2321 --flags not-need-init,startup-clear -d; sleep 1
L=/tmp/ima.log
echo "10 x ima-ng sha256:7b6436b0c98f62380866d9432c2af0ee08ce16a171bda6951aecd95ee1307d61 boot_aggregate" > $L
echo "10 x ima-ng sha256:ffd332cb89c56a4ef35f612a4bf6b712e480e7ec8ce718ca8a6aed4af54aad8a /usr/lib/systemd/systemd" >> $L
python3 - <<'PY' | while read h; do tpm2_pcrextend 10:sha256=$h; done
import hashlib, struct
for line in open("/tmp/ima.log"):
    p = line.split(); d = b"sha256:\x00" + bytes.fromhex(p[3][7:]); n = " ".join(p[4:]).encode() + b"\x00"
    print(hashlib.sha256(struct.pack("<I", len(d)) + d + struct.pack("<I", len(n)) + n).hexdigest())
PY
# device script: sysfs -> fake log; the copy into verification/ gets two extra lines appended (entries after the quote)
sed -e "s#^cp /sys/kernel/security/ima/ascii_runtime_measurements verification/#cp $L verification/ascii_runtime_measurements#" -e "s#/sys/kernel/security/ima/ascii_runtime_measurements#$L#g" -e 's#^W=/root/ima-quote#W=/tmp/ima-quote#' /lab/container-imx/meta-veraison-attestation/recipes-security/ima-quote/files/ima-quote > /tmp/dev.sh
# entries that arrive after the quote: append them to the copy the verifier gets
# swtpm keeps fewer objects loaded than the fTPM does: it needs the transient
# objects flushed after each evictcontrol, which the fTPM does not (verified on
# the EVK with the plain procedure). Insert that only for this dry run.
sed -i '/^    tpm2_evictcontrol -C o -c ek.ctx/a\    tpm2_flushcontext -t' /tmp/dev.sh
sed -i '/^    tpm2_evictcontrol -C o -c ak.ctx/a\    tpm2_flushcontext -t' /tmp/dev.sh
sed -i 's#^ls -l verification$#echo "10 x ima-ng sha256:d93a3aac3ff92069fbe65186c27e7862958c52bd05766759730c49f57b49da65 /usr/lib/ld-linux-aarch64.so.1" >> verification/ascii_runtime_measurements; echo "10 x ima-ng sha256:a86b4aad77977d352c2fae7d190aa97264d61715cd6b31da0c9e78b5132e0bd3 /x" >> verification/ascii_runtime_measurements; ls -l verification#' /tmp/dev.sh
sh /tmp/dev.sh 2>&1 | sed 's/^/[2026-09-18 10:00:00.000] /; s/$/\r/' > /tmp/term.log
grep -c "VERIFICATION HEX" /tmp/term.log
mkdir -p /tmp/v && sed 's/^\[[^]]*\] //; s/\r$//' /tmp/term.log | sed -n '/^-----BEGIN VERIFICATION HEX/,/^-----END VERIFICATION HEX/p' | sed '1d;$d' | tr -d ' \n' | python3 -c 'import sys,binascii;sys.stdout.buffer.write(binascii.unhexlify(sys.stdin.read().strip()))' | tar xzf - -C /tmp/v
sh /lab/ima-quote/host-verify.sh /tmp/v/verification
echo "== tampered log must fail"; sed -i '1s/7b6436b0/00000000/' /tmp/v/verification/ascii_runtime_measurements; sh /lab/ima-quote/host-verify.sh /tmp/v/verification && echo UNEXPECTED || echo "   rejected as expected"
echo "== LAB DONE"
