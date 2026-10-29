#!/bin/sh
# host-verify.sh run inside a container with tpm2-tools (for hosts that lack
# them).   sh host-verify-docker.sh <verification-dir>
set -e
D=$(cd "${1:?usage: $0 <verification-dir>}" && pwd); HERE=$(cd "$(dirname "$0")" && pwd)
docker image inspect tpmlab >/dev/null 2>&1 || docker build -q -t tpmlab "$HERE"
docker run --rm -v "$HERE:/lab:ro" -v "$D:/v" tpmlab sh /lab/host-verify.sh /v
