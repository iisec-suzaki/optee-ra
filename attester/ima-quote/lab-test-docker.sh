#!/bin/sh
# Run lab-test.sh in a container with tpm2-tools and swtpm.
#   sh lab-test-docker.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
docker image inspect tpmlab >/dev/null 2>&1 || docker build -q -t tpmlab "$HERE"
docker run --rm -v "$HERE/..:/lab:ro" tpmlab bash /lab/ima-quote/lab-test.sh
