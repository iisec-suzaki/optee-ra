#!/bin/sh
# Restore the verification/ directory from a console log that contains the
# hex block printed by ima-quote on the board.
#   sh host-unpack.sh <console-log> [<dest-dir>]
set -e
LOG="${1:?usage: $0 <console-log> [dest-dir]}"; DEST="${2:-.}"
mkdir -p "$DEST"
# Take the block between the markers, then keep only the hex digits: the
# terminal adds timestamps and CRs, and kernel or OP-TEE messages printed
# while the board dumps the block land inside its lines.
sed 's/^\[[^]]*\] //; s/\r$//' "$LOG" \
  | sed -n '/^-----BEGIN VERIFICATION HEX-----/,/^-----END VERIFICATION HEX-----/p' \
  | sed '1d;$d; s/\[ *[0-9.]*\].*$//; /\/TC/d' | tr -cd '0-9a-f' \
  | xxd -r -p | tar xzf - -C "$DEST"
ls -d "$DEST/verification"
