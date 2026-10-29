#!/bin/sh
# Verifier side of the IMA attestation check. $1 = the verification/
# directory produced by ima-quote on the board.
# 1. tpm2_checkquote (signature, nonce, PCR values), 2. trim the IMA list
# from the end one line at a time until pcr10_recompute3.py reproduces the
# PCR 10 shown by checkquote: the remaining lines are what the quote attests.
set -e
D=$(cd "${1:?usage: $0 <verification-dir>}" && pwd); HERE=$(cd "$(dirname "$0")" && pwd)
cd "$D"
echo "== tpm2_checkquote"
OUT=$(tpm2_checkquote --public ak.pub --message quote.msg --signature quote.sig \
        --qualification service_provider_nonce --pcr quote.pcr)
echo "$OUT"
PCR=$(echo "$OUT" | sed -n 's/^ *10: *0x\([0-9A-Fa-f]*\).*/\1/p' | head -1 | tr 'A-F' 'a-f')
[ -n "$PCR" ] || { echo "PCR 10 not found in checkquote output"; exit 1; }
echo "== PCR 10 from the quote: $PCR"
echo "== trimming ascii_runtime_measurements from the end until the recomputation matches"
TOTAL=$(wc -l < ascii_runtime_measurements)
k=0
while [ $k -lt $TOTAL ]; do
    if [ $k -eq 0 ]; then cp ascii_runtime_measurements measurements.log; else head -n -$k ascii_runtime_measurements > measurements.log; fi
    SIM=$(python3 "$HERE/pcr10_recompute3.py" --ima_log measurements.log | sed -n 's/^Simulated PCR\[10\] *: *0x//p')
    if [ "$SIM" = "$PCR" ]; then
        echo "[OK] match after removing $k trailing line(s): the first $((TOTAL-k)) entries are covered by the quote"
        exit 0
    fi
    k=$((k+1))
done
echo "[NG] no prefix of the log reproduces the quoted PCR 10"; exit 1
